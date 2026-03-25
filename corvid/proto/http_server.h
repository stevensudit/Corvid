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
#include "../strings/conversion.h"
#include "../concurrency/timer_fuse.h"

namespace corvid { inline namespace proto {

using namespace std::chrono_literals;

// Minimal HTTP 0.9 server, based on the part of the HTTP/1.0 spec,
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
//
// `write_timeout` controls how long the server waits for the send queue to
// drain after queueing a response. If the queue does not empty in time (e.g.,
// because the client stops reading), the connection is forcefully closed via
// `hangup`. Disarmed in `on_drain` when the queue empties normally. Defaults
// to 5 s.
class http_server: public std::enable_shared_from_this<http_server> {
  struct http_conn_state {
    terminated_text_parser::state parser_state;
    std::atomic_uint64_t read_seq;
    std::atomic_uint64_t write_seq; // stales pending write-timeout fuses
    bool done{};
  };
  using conn_t = stream_conn_with_state<http_conn_state>;
  using conn_ptr_t = stream_conn_ptr_with<conn_t>;
  using timer_fuse_t = timer_fuse<stream_conn>;
  enum class allow : std::uint8_t { ctor };

public:
  using duration_t = timing_wheel::duration_t;
  using epoll_loop_ptr = std::shared_ptr<epoll_loop>;
  using timing_wheel_ptr = std::shared_ptr<timing_wheel>;

  // Create an HTTP 0.9 server listening on `endpoint`.
  //
  // If `loop` is non-null, the server shares it; otherwise it constructs and
  // owns an `epoll_loop_runner`. If `wheel` is non-null, the server borrows
  // it; otherwise it constructs and owns a `timing_wheel_runner`.
  // Returns null if the listen socket cannot be created.
  [[nodiscard]] static std::shared_ptr<http_server>
  create(const net_endpoint& endpoint, epoll_loop_ptr loop = nullptr,
      timing_wheel_ptr wheel = nullptr, duration_t request_timeout = 30s,
      duration_t write_timeout = 5s) {
    auto self = std::make_shared<http_server>(allow::ctor);

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

    self->read_timeout_ = request_timeout;
    self->write_timeout_ = write_timeout;

    // Start listening.
    self->listener_ = conn_ptr_t::listen(self->loop_, endpoint,
        {.on_data =
                [self](stream_conn& conn, recv_buffer_view view) {
                  return self->handle_data(conn, std::move(view));
                },
            .on_drain =
                [self](stream_conn& conn) {
                  return self->handle_drain(conn);
                }},
        coordination_policy::bilateral);
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

  http_server(allow) {};

private:
  // Actual payload for timeouts.
  [[nodiscard]] static bool timeout_hangup(const timer_fuse_t& fuse) {
    auto c = fuse.get_if_armed();
    if (!c) return true;
    // Use `weak_loop()` rather than `loop()` because this callback runs on the
    // timing-wheel thread. The connection is still be alive (since `c` holds a
    // `std::shared_ptr` to it) but the loop may have already been destroyed,
    // which would make `loop()` a dangling-reference dereference.
    auto loop = c->weak_loop().lock();
    if (!loop) return true;
    return loop->post([fuse]() -> bool {
      if (auto c = fuse.get_if_armed()) return c->hangup();
      return true;
    });
  }

  // Arm a read timeout on `conn`. Increments `read_seq` to stale any prior
  // fuse, then schedules a `hangup` after `timeout` via the timing wheel.
  // Called on connection (via `handle_drain`) and again after each parsed
  // request so that keep-alive connections restart the clock between commands.
  [[nodiscard]] bool arm_read_timeout(stream_conn& conn) const {
    return timer_fuse_t::set_timeout(*wheel_,
        conn_t::from(conn).state().read_seq, std::weak_ptr{conn.self()},
        read_timeout_, timeout_hangup);
  }

  // Arm a write timeout on `conn`. Increments `write_seq` to stale any prior
  // fuse, then schedules a `hangup` after `timeout` via the timing wheel.
  // Called just before queueing a response. Disarmed by `handle_drain` when
  // the send queue empties before the deadline.
  [[nodiscard]] bool arm_write_timeout(stream_conn& conn) const {
    return timer_fuse_t::set_timeout(*wheel_,
        conn_t::from(conn).state().write_seq, std::weak_ptr{conn.self()},
        write_timeout_, timeout_hangup);
  }

  // Initialize the parser state and arm the initial read timeout for a freshly
  // accepted connection. Safe to call multiple times; subsequent calls are
  // no-ops. Called from `handle_drain` (first writable event, before any data)
  // and from `handle_data` as a fallback, because `EPOLLIN` is dispatched
  // before `EPOLLOUT` in the same wakeup, so data can arrive before
  // `on_drain` fires.
  [[nodiscard]] bool
  ensure_initialized(stream_conn& conn, http_conn_state& state) const {
    if (state.parser_state) return true;
    state.parser_state = terminated_text_parser::state{"\r\n", 8192};
    return arm_read_timeout(conn);
  }

  // Initialize the connection on the first writable event after accept, or
  // disarm the write timeout when the send queue fully drains after a
  // response.
  [[nodiscard]] bool handle_drain(stream_conn& conn) const {
    auto& state = conn_t::from(conn).state();
    if (!ensure_initialized(conn, state)) return false;
    // Disarm any pending write timeout: the send queue emptied in time.
    if (state.done) timer_fuse_t::disarm(state.write_seq);
    return true;
  }

  // Handle incoming data for an accepted connection. Parses a single
  // `GET /path` line and sends back a canned HTML response, then closes.
  [[nodiscard]] bool
  handle_data(stream_conn& conn, recv_buffer_view view) const {
    auto& state = conn_t::from(conn).state();

    // If we already sent a response, ignore any trailing bytes so the send
    // queue can drain without triggering a TCP RST.
    if (state.done) return true;

    if (!ensure_initialized(conn, state)) return false;

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

    // The path is a length.
    const auto length = strings::parse_num<size_t>(path.substr(1), 0);
    if (length > 10 * 1024 * 1024) return conn.close() && false;
    const auto length_text = std::to_string(length);

    // "Look up" the file.
    std::string html{"<html><body><p>I am definitely, totally the file \""};
    html += length_text;
    html += "\".</p></body>";
    html.append(length, ' ');
    html += "</html>\r\n";

    // Arm the write timeout before queueing the response. `handle_drain`
    // disarms it when the send queue empties. If the client stops reading and
    // the queue stalls, the fuse fires and closes the connection via `hangup`.
    if (!arm_write_timeout(conn)) return false;

    // Send the response.
    if (!conn.send(std::move(html))) return conn.close() && false;

    // Re-arm the read timeout for the next request (handles keep-alive
    // connections; harmless for HTTP 0.9 since the connection closes
    // immediately, staling the new fuse naturally).
    if (!arm_read_timeout(conn)) return false;

    // Mark the connection done and queue a graceful close. Any bytes that
    // arrive before the close completes will be silently ignored above.
    state.done = true;
    return conn.close();
  }

  std::optional<epoll_loop_runner> runner_;
  epoll_loop_ptr loop_;
  std::optional<timing_wheel_runner> wheel_runner_;
  timing_wheel_ptr wheel_;
  duration_t read_timeout_{30s};
  duration_t write_timeout_{5s};
  conn_ptr_t listener_;
};
}} // namespace corvid::proto
