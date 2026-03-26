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
#include "http_header_block.h"
#include "stream_conn.h"
#include "terminated_text_parser.h"
#include "../strings/conversion.h"
#include "../concurrency/timer_fuse.h"

namespace corvid { inline namespace proto {

using namespace std::chrono_literals;

// Connection lifecycle phase for an accepted HTTP connection.
//
// `request_line` -- seeking `"\r\n"` for the request line. Empty blocks
//                   (leading bare CRLFs) are silently skipped per RFC 9112
//                   section 2.2. A non-empty block is passed to
//                   `request_header_block::extract`; HTTP/0.9 dispatches
//                   immediately, HTTP/1.x transitions to `header_lines`.
// `header_lines` -- seeking `"\r\n"` for each header-field line. A non-empty
//                   block is fed to `http_headers::extract_line`; an empty
//                   block (the blank-line terminator) dispatches the request.
// `body`         -- reading the request body; reserved for future use.
// `response`     -- a response has been queued (or the connection is closing);
//                   incoming bytes are silently ignored so the send queue can
//                   drain without triggering a TCP RST.
// `done`         -- terminal state; treated identically to `response` in
//                   `on_data` but set after `close()` is called to signal
//                   that no further processing should occur.
enum class http_phase : uint8_t { request_line, header_lines, body, response, done };

// HTTP/1.x server (including HTTP/0.9) built on `stream_conn`.
//
// Listens for TCP (or UDS/ANS) connections. Parses each request in two
// phases using `terminated_text_parser` (sentinel `"\r\n"`, max 8192 bytes
// per line): Phase 1 reads the request line, Phase 2 reads header-field
// lines until the blank-line terminator. HTTP/0.9 requests (no version
// token) are dispatched after Phase 1 with no Phase 2.
//
// Persistent connections (keep-alive) and pipelining are supported:
// `on_data` loops over all complete header blocks present in the receive
// buffer, queuing a response for each one. Because `stream_conn::send` is
// FIFO, responses are always delivered in request order.
//
// Construct via the `create` factory, which returns a
// `std::shared_ptr<http_server>`. If the `loop` argument is null, the
// server starts its own `epoll_loop_runner`; otherwise it shares the
// supplied loop. If the `wheel` argument is null, the server starts its
// own `timing_wheel_runner`; otherwise it borrows the supplied wheel.
//
// `request_timeout` controls how long the server waits for a complete
// request header block before forcefully closing an idle connection.
// Re-armed after each parsed request for keep-alive connections. Defaults
// to 30 s.
//
// `write_timeout` controls how long the server waits for the send queue to
// drain after queueing a response. Disarmed in `on_drain` when the queue
// empties normally. Defaults to 5 s.
class http_server: public std::enable_shared_from_this<http_server> {
  struct http_conn_state {
    terminated_text_parser::state parser_state; // falsy until initialized
    std::atomic_uint64_t read_seq;
    std::atomic_uint64_t write_seq;
    http_phase phase{http_phase::request_line};
    request_header_block req; // populated across request_line / header_lines
  };
  using conn_t = stream_conn_with_state<http_conn_state>;
  using conn_ptr_t = stream_conn_ptr_with<conn_t>;
  using timer_fuse_t = timer_fuse<stream_conn>;
  enum class allow : std::uint8_t { ctor };

public:
  using duration_t = timing_wheel::duration_t;
  using epoll_loop_ptr = std::shared_ptr<epoll_loop>;
  using timing_wheel_ptr = std::shared_ptr<timing_wheel>;

  // Create an HTTP/1.1 server listening on `endpoint`.
  //
  // If `loop` is non-null, the server shares it; otherwise it constructs
  // and owns an `epoll_loop_runner`. If `wheel` is non-null, the server
  // borrows it; otherwise it constructs and owns a `timing_wheel_runner`.
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
    // Use `weak_loop()` rather than `loop()` because this callback runs on
    // the timing-wheel thread. The connection may still be alive (since `c`
    // holds a `shared_ptr` to it) but the loop may have already been
    // destroyed, making `loop()` a dangling-reference dereference.
    auto loop = c->weak_loop().lock();
    if (!loop) return true;
    return loop->post([fuse]() -> bool {
      if (auto c = fuse.get_if_armed()) return c->hangup();
      return true;
    });
  }

  // Arm a read timeout on `conn`. Increments `read_seq` to stale any prior
  // fuse, then schedules a `hangup` after `read_timeout_` via the timing
  // wheel. Called on connection (via `handle_drain`) and again after each
  // parsed request so that keep-alive connections restart the clock.
  [[nodiscard]] bool arm_read_timeout(stream_conn& conn) const {
    return timer_fuse_t::set_timeout(*wheel_,
        conn_t::from(conn).state().read_seq, std::weak_ptr{conn.self()},
        read_timeout_, timeout_hangup);
  }

  // Arm a write timeout on `conn`. Increments `write_seq` to stale any
  // prior fuse, then schedules a `hangup` after `write_timeout_` via the
  // timing wheel. Called just before queueing a response. Disarmed by
  // `handle_drain` when the send queue empties before the deadline.
  [[nodiscard]] bool arm_write_timeout(stream_conn& conn) const {
    return timer_fuse_t::set_timeout(*wheel_,
        conn_t::from(conn).state().write_seq, std::weak_ptr{conn.self()},
        write_timeout_, timeout_hangup);
  }

  // Initialize the parser state and arm the initial read timeout for a
  // freshly accepted connection. Safe to call multiple times; subsequent
  // calls are no-ops. Called from `handle_drain` (first writable event,
  // before any data) and from `handle_data` as a fallback, because
  // `EPOLLIN` is dispatched before `EPOLLOUT` in the same wakeup.
  [[nodiscard]] bool
  ensure_initialized(stream_conn& conn, http_conn_state& state) const {
    if (state.parser_state) return true;
    state.parser_state = terminated_text_parser::state{"\r\n", 8192};
    return arm_read_timeout(conn);
  }

  // Initialize the connection on the first writable event after accept, or
  // disarm the write timeout when the send queue fully drains after a
  // response. `timer_fuse::disarm` is `++seq`, which is safe to call
  // unconditionally -- incrementing an unarmed fuse is a no-op.
  [[nodiscard]] bool handle_drain(stream_conn& conn) const {
    auto& state = conn_t::from(conn).state();
    if (!ensure_initialized(conn, state)) return false;
    timer_fuse_t::disarm(state.write_seq);
    return true;
  }

  // Build a minimal HTTP/1.1 error response with no body. Used when the
  // server needs to respond before a `request_header_block` is available
  // (e.g., parse failure).
  [[nodiscard]] static std::string make_error_response(int code,
      std::string_view phrase, bool keep_alive = false) {
    response_header_block resp;
    resp.status_code = code;
    resp.reason = std::string{phrase};
    if (!resp.headers.add_raw(
            "Connection", keep_alive ? "keep-alive" : "close"))
      return "HTTP/1.1 400 Bad Request\r\n\r\n";
    return resp.serialize();
  }

  // Process one parsed request and queue the appropriate response.
  // Returns true if the connection should remain open (keep-alive),
  // false if it should be closed after the response drains.
  [[nodiscard]] bool
  dispatch_request(stream_conn& conn, const request_header_block& req) const {
    const bool alive = req.headers.keep_alive(req.version);

    // HTTP/1.1 requires a `Host` header.
    if (req.version == http_version::http_11 && !req.headers.get("Host")) {
      if (!arm_write_timeout(conn)) return false;
      if (!conn.send(make_error_response(400, "Bad Request"))) return false;
      return false;
    }

    if (req.method != http_method::GET) {
      if (!arm_write_timeout(conn)) return false;
      if (!conn.send(make_error_response(405, "Method Not Allowed", alive)))
        return false;
      return alive;
    }

    // The path encodes the desired response body size.
    const std::string_view path =
        req.target.empty()
            ? std::string_view{"/"}
            : std::string_view{req.target};
    const auto length = strings::parse_num<size_t>(
        path.substr(path.starts_with('/') ? 1 : 0), size_t{0});
    if (length > 10ULL * 1024ULL * 1024ULL) {
      if (!arm_write_timeout(conn)) return false;
      if (!conn.send(make_error_response(400, "Bad Request", alive)))
        return false;
      return alive;
    }

    // Build a canned HTML response body with the requested padding.
    std::string html{"<html><body><p>I am definitely, totally the file \""};
    html += std::to_string(length);
    html += "\".</p></body>";
    html.append(length, ' ');
    html += "</html>";

    response_header_block resp;
    resp.version =
        (req.version == http_version::http_10)
            ? http_version::http_10
            : http_version::http_11;
    resp.status_code = 200;
    resp.reason = "OK";
    if (!resp.headers.add_raw(
            "Connection", alive ? "keep-alive" : "close")) {
      if (!arm_write_timeout(conn)) return false;
      if (!conn.send(make_error_response(400, "Bad Request"))) return false;
      return false;
    }

    if (!resp.headers.add_raw("Content-Type", "text/html; charset=utf-8") ||
        !resp.headers.add_raw("Content-Length", std::to_string(html.size()))) {
      if (!arm_write_timeout(conn)) return false;
      if (!conn.send(make_error_response(400, "Bad Request"))) return false;
      return false;
    }

    if (!arm_write_timeout(conn)) return false;
    if (!conn.send(resp.serialize())) return false;
    // Re-arm: if the headers drained synchronously, `handle_drain` will have
    // disarmed the timeout before the body was queued.
    if (!arm_write_timeout(conn)) return false;
    if (!conn.send(std::move(html))) return false;
    return alive;
  }

  // Handle incoming data for an accepted connection.
  //
  // Implements an explicit state machine driven by `state.phase`. The parser
  // always uses `"\r\n"` as its sentinel (max 8192 bytes per line). In the
  // `request_line` phase, empty blocks (leading bare CRLFs) are skipped; a
  // non-empty block is the request line. In the `header_lines` phase, each
  // non-empty block is a header-field line; an empty block is the
  // blank-line terminator that ends the header section. The loop continues
  // to process all complete lines present in the receive buffer, providing
  // pipelining: multiple queued requests are handled in a single `on_data`
  // call, and `stream_conn::send` FIFO ordering guarantees response order.
  [[nodiscard]] bool
  handle_data(stream_conn& conn, recv_buffer_view view) const {
    auto& state = conn_t::from(conn).state();
    if (!ensure_initialized(conn, state)) return false;

    auto input = view.active_view();

    while (true) {
      switch (state.phase) {
      case http_phase::request_line: {
        terminated_text_parser parser{state.parser_state};
        std::string_view block_view;
        const auto r = parser.parse(input, block_view);
        if (!r) return true; // incomplete; wait for more data

        if (!*r) {
          // Request line exceeded 8192-byte limit.
          if (!arm_write_timeout(conn)) return false;
          if (!conn.send(make_error_response(400, "Bad Request")))
            return false;
          state.phase = http_phase::response;
          return conn.close() && false;
        }

        // Skip leading bare CRLFs (RFC 9112 section 2.2).
        if (block_view.empty()) {
          view.update_active_view(input);
          parser.reset();
          continue;
        }

        // Extract before `recv_buffer_view` destructs (buffer may compact).
        const bool extracted = state.req.extract(block_view);
        view.update_active_view(input);
        parser.reset();

        if (!extracted) {
          if (!arm_write_timeout(conn)) return false;
          if (!conn.send(make_error_response(400, "Bad Request")))
            return false;
          state.phase = http_phase::response;
          return conn.close() && false;
        }

        if (state.req.version == http_version::http_09) {
          // HTTP/0.9: request line only, no headers.
          const bool alive = dispatch_request(conn, state.req);
          state.req.clear();
          if (!alive) {
            state.phase = http_phase::response;
            return conn.close();
          }
          if (!arm_read_timeout(conn)) return false;
          continue;
        }

        // HTTP/1.x: proceed to parse header-field lines.
        state.phase = http_phase::header_lines;
        continue;
      }

      case http_phase::header_lines: {
        terminated_text_parser parser{state.parser_state};
        std::string_view block_view;
        const auto r = parser.parse(input, block_view);
        if (!r) return true; // incomplete; wait for more data

        if (!*r) {
          // Header line exceeded 8192-byte limit.
          if (!arm_write_timeout(conn)) return false;
          if (!conn.send(make_error_response(400, "Bad Request")))
            return false;
          state.phase = http_phase::response;
          return conn.close() && false;
        }

        // Process before updating view (buffer may compact on update).
        const bool line_ok =
            block_view.empty() || state.req.headers.extract_line(block_view);
        view.update_active_view(input);
        parser.reset();

        if (!line_ok) {
          if (!arm_write_timeout(conn)) return false;
          if (!conn.send(make_error_response(400, "Bad Request")))
            return false;
          state.phase = http_phase::response;
          return conn.close() && false;
        }

        if (!block_view.empty()) continue; // more header lines

        // Blank line: end of headers; dispatch the request.
        const bool alive = dispatch_request(conn, state.req);
        state.req.clear();
        state.phase = http_phase::request_line;
        if (!alive) {
          state.phase = http_phase::response;
          return conn.close();
        }
        // Keep-alive: re-arm read timeout, then loop to process any
        // additional pipelined requests already in the receive buffer.
        if (!arm_read_timeout(conn)) return false;
        continue;
      }

      case http_phase::body:
        // Future: accumulate request body bytes.
        return true;

      case http_phase::response:
      case http_phase::done:
        // Ignore trailing bytes; let the send queue drain cleanly.
        return true;
      }
    }
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
