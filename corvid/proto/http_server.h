// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "epoll_loop.h"
#include "stream_conn.h"
#include "terminated_text_parser.h"
#include "../concurrency/timer_fuse.h"

using namespace std::chrono_literals;

namespace corvid { inline namespace proto {

// Minimal HTTP 0.9 server, based on
// https://datatracker.ietf.org/doc/html/rfc1945, where it covers
// Simple-Request and Simple-Response.
//
// Listens for TCP connections, parses each request line with
// `terminated_text_parser`, and sends a canned HTML response for any `GET
// /path` request.
//
// Construct via the `create` factory, which returns a
// `std::shared_ptr<http_server>`. Each accepted connection is owned by the
// loop and carries its own `http_conn_state` so that partial request lines
// arriving across multiple `on_data` calls are handled correctly, and so
// that any bytes arriving after the response is queued are silently ignored
// (avoiding the TCP RST that `shutdown_read` can trigger).
//
// If the `loop` argument to `create` is null, the server starts its own
// `epoll_loop_runner` (background thread) and owns it for its lifetime.
// Otherwise the caller supplies a shared loop and the server borrows it.
//
// If the `wheel` argument to `create` is null, the server starts its own
// `timing_wheel_runner` (background thread) and owns it for its lifetime.
// Otherwise the caller supplies a wheel and the server borrows it.
//
// `request_timeout` controls how long the server waits for a complete request
// line before forcefully closing an idle connection. After each successfully
// parsed request the timeout is re-armed, anticipating future keep-alive
// support. Defaults to 30 s.
class http_server: public std::enable_shared_from_this<http_server> {
  struct http_conn_state {
    terminated_text_parser::state parser_state;
    std::atomic_uint64_t read_seq;
    std::atomic_uint64_t write_seq; // for on_drain write timeout (future)
    bool done{};
  };
  using conn_t = stream_conn_with_state<http_conn_state>;

public:
  // Create an HTTP 0.9 server listening on `endpoint`.
  //
  // If `loop` is non-null, the server shares it; otherwise it constructs and
  // owns an `epoll_loop_runner`. If `wheel` is non-null, the server borrows
  // it; otherwise it constructs and owns a `timing_wheel_runner`.
  // Returns null if the listen socket cannot be created.
  [[nodiscard]] static std::shared_ptr<http_server> create(
      const net_endpoint& endpoint, std::shared_ptr<epoll_loop> loop = nullptr,
      std::shared_ptr<timing_wheel> wheel = nullptr,
      timing_wheel::duration_t request_timeout = 30s) {
    auto self = std::shared_ptr<http_server>(new http_server{});

    // Get an epoll loop.
    if (!loop) {
      self->runner_.emplace();
      self->loop_ = self->runner_->loop();
    } else
      self->loop_ = std::move(loop);

    // Get a timing wheel.
    if (!wheel) {
      self->wheel_runner_.emplace();
      self->wheel_ = self->wheel_runner_->wheel();
    } else
      self->wheel_ = std::move(wheel);

    self->request_timeout_ = request_timeout;

    // Start listening.
    self->listener_ = stream_conn_ptr_with<conn_t>::listen(*self->loop_,
        endpoint,
        {.on_data =
                [self](stream_conn& conn, recv_buffer_view view) {
                  return handle_data(conn, std::move(view), *self->wheel_,
                      self->request_timeout_);
                },
            .on_drain =
                [self](stream_conn& conn) {
                  return handle_drain(conn, *self->wheel_,
                      self->request_timeout_);
                }},
        /*mutual_close=*/true);
    if (!self->listener_) return nullptr;

    return self;
  }

  // Return the actual bound address (useful when `endpoint` used port 0).
  [[nodiscard]] net_endpoint local_endpoint() const {
    return listener_->local_endpoint();
  }

  // Return a `shared_ptr<http_server>` to `*this`.
  [[nodiscard]] std::shared_ptr<http_server> self() {
    return std::static_pointer_cast<http_server>(shared_from_this());
  }

private:
  http_server() = default;

  // Arm a read timeout on `conn`. Increments `read_seq` to stale any prior
  // fuse, then schedules a `hangup` after `timeout` via the timing wheel.
  // Called on connection (via `handle_drain`) and again after each parsed
  // request so that keep-alive connections restart the clock between commands.
  [[nodiscard]] static bool arm_read_timeout(stream_conn& conn,
      timing_wheel& wheel, std::atomic_uint64_t& read_seq,
      timing_wheel::duration_t timeout) {
    return timer_fuse<stream_conn>::set_timeout(wheel, read_seq,
        std::weak_ptr(conn.self()), timeout,
        [](const timer_fuse<stream_conn>& fuse) -> bool {
          auto c = fuse.get_if_armed();
          if (!c) return true;
          return c->loop().post([fuse]() -> bool {
            if (auto c = fuse.get_if_armed()) return c->hangup();
            return true;
          });
        });
  }

  // Initialize the parser state and arm the initial read timeout for a freshly
  // accepted connection. Safe to call multiple times; subsequent calls are
  // no-ops. Called from `handle_drain` (first writable event, before any data)
  // and from `handle_data` as a fallback, because `EPOLLIN` is dispatched
  // before `EPOLLOUT` in the same wakeup, so data can arrive before
  // `on_drain` fires.
  [[nodiscard]] static bool ensure_initialized(stream_conn& conn,
      http_conn_state& state, timing_wheel& wheel,
      timing_wheel::duration_t request_timeout) {
    if (state.parser_state) return true;
    state.parser_state = terminated_text_parser::state{"\r\n", 8192};
    return arm_read_timeout(conn, wheel, state.read_seq, request_timeout);
  }

  // Initialize the connection on the first writable event after accept.
  [[nodiscard]] static bool handle_drain(stream_conn& conn,
      timing_wheel& wheel, timing_wheel::duration_t request_timeout) {
    auto& state = conn_t::from(conn).state();
    return ensure_initialized(conn, state, wheel, request_timeout);
  }

  // Handle incoming data for an accepted connection. Parses a single
  // `GET /path` line and sends back a canned HTML response, then closes.
  [[nodiscard]] static bool handle_data(stream_conn& conn,
      recv_buffer_view view, timing_wheel& wheel,
      timing_wheel::duration_t request_timeout) {
    auto& state = conn_t::from(conn).state();

    // If we already sent a response, ignore any trailing bytes so the send
    // queue can drain without triggering a TCP RST.
    if (state.done) return true;

    if (!ensure_initialized(conn, state, wheel, request_timeout)) return false;

    terminated_text_parser parser{state.parser_state};

    auto input = view.active_view();
    std::string_view simple_request;
    const auto r = parser.parse(input, simple_request);

    // If incomplete, wait for more.
    if (!r) return true;

    // If invalid, close the connection. Essentially, we need to find a "\r\n"
    // sentinel before reaching the 8k line-length limit.
    if (!*r) return conn.close() && false;

    // The syntax is trivial.
    static constexpr std::string_view prefix{"GET /"};
    if (!simple_request.starts_with(prefix)) return conn.close() && false;

    // Keep the slash and anything after it as the path.
    const std::string_view path{simple_request.substr(prefix.size() - 1)};
    // TODO: Validate the path to prevent directory traversal.

    // "Look up" the file.
    std::string html{"<html><body><p>I am definitely, totally the file \""};
    // TODO: This needs to be HTML-escaped, for security and such.
    html += path;
    html += "\".</p></body></html>\r\n";

    // Send the response.
    if (!conn.send(std::move(html))) return conn.close() && false;

    // Re-arm the read timeout for the next request (handles keep-alive
    // connections; harmless for HTTP 0.9 since the connection closes
    // immediately, staling the new fuse naturally).
    if (!arm_read_timeout(conn, wheel, state.read_seq, request_timeout))
      return false;

    // Mark the connection done and queue a graceful close. Any bytes that
    // arrive before the close completes will be silently ignored above.
    state.done = true;
    return conn.close();
  }

  std::optional<epoll_loop_runner> runner_;
  std::shared_ptr<epoll_loop> loop_;
  std::optional<timing_wheel_runner> wheel_runner_;
  std::shared_ptr<timing_wheel> wheel_;
  timing_wheel::duration_t request_timeout_{30s};
  stream_conn_ptr_with<conn_t> listener_;
};
}} // namespace corvid::proto
