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
#include "http_head_codec.h"
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
//                   `request_head::parse`; HTTP/0.9 dispatches
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
enum class http_phase : uint8_t {
  request_line,
  header_lines,
  body,
  response,
  done
};

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
    request_head req; // populated across request_line / header_lines
    uint8_t leading_crlf_count{}; // bare CRLFs skipped before request line
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
  // For the purpose of testing, a zero duration means "no timeout" (i.e.,
  // don't arm a fuse at all).
  [[nodiscard]] bool arm_read_timeout(stream_conn& conn) const {
    if (read_timeout_ == 0s) return true;
    return timer_fuse_t::set_timeout(*wheel_,
        conn_t::from(conn).state().read_seq, std::weak_ptr{conn.self()},
        read_timeout_, timeout_hangup);
  }

  // Arm a write timeout on `conn`. Increments `write_seq` to stale any
  // prior fuse, then schedules a `hangup` after `write_timeout_` via the
  // timing wheel. Called just before queueing a response. Disarmed by
  // `handle_drain` when the send queue empties before the deadline.
  // For the purpose of testing, a zero duration means "no timeout" (i.e.,
  // don't arm a fuse at all).
  [[nodiscard]] bool arm_write_timeout(stream_conn& conn) const {
    if (write_timeout_ == 0s) return true;
    return timer_fuse_t::set_timeout(*wheel_,
        conn_t::from(conn).state().write_seq, std::weak_ptr{conn.self()},
        write_timeout_, timeout_hangup);
  }

  // Initialize the parser state and arm the initial read timeout for a
  // freshly accepted connection. Safe to call multiple times; subsequent
  // calls are no-ops.
  //
  // Called from `handle_drain` (first writable event, before any data) and
  // from `handle_data` as a fallback, because `EPOLLIN` is dispatched before
  // `EPOLLOUT` in the same wakeup.
  [[nodiscard]] bool
  ensure_initialized(stream_conn& conn, http_conn_state& state) const {
    if (state.parser_state) return true;
    state.parser_state = terminated_text_parser::state{"\r\n", 8192};
    return arm_read_timeout(conn);
  }

  // When the send queue fully drains after a response, disarm the write
  // timeout. Also, if called before the initial request is received,
  // initialize the parser state, arming the read timeout.
  [[nodiscard]] bool handle_drain(stream_conn& conn) const {
    auto& state = conn_t::from(conn).state();
    timer_fuse_t::disarm(state.write_seq);
    if (!ensure_initialized(conn, state)) {
      (void)hangup(conn);
      return false;
    }
    return true;
  }

  [[nodiscard]] static after_response hangup(stream_conn& conn) {
    (void)conn.hangup();
    return after_response::close;
  }

  [[nodiscard]] after_response send_error_response(stream_conn& conn,
      after_response keep_alive = after_response::close,
      http_version version = http_version::http_1_1,
      http_status_code code = http_status_code::BAD_REQUEST,
      std::string_view phrase = "Bad Request") const {
    if (version == http_version::http_0_9) return hangup(conn);
    if (!arm_write_timeout(conn)) return hangup(conn);
    if (!conn.send(response_head::make_error_response(keep_alive, version,
            code, phrase)))
      return hangup(conn);
    return keep_alive;
  }

  // Process one parsed request and queue the appropriate response.
  // Returns `after_response::keep_alive` or `after_response::close`. (On
  // severe error, the connection may already be hung up, turning the close
  // into a no-op.)
  [[nodiscard]] after_response
  dispatch_request(stream_conn& conn, const request_head& req) const {
    const after_response keep_alive = req.options.keep_alive(req.version);

    // HTTP/1.1 requires a `Host` header.
    if (req.version == http_version::http_1_1 && !req.headers.get("Host"))
      return send_error_response(conn, keep_alive, req.version);

    if (req.method != http_method::GET)
      return send_error_response(conn, keep_alive, req.version,
          http_status_code::METHOD_NOT_ALLOWED, "Method Not Allowed");

    // The path encodes the desired response body size.
    const std::string_view path =
        req.target.empty()
            ? std::string_view{"/"}
            : std::string_view{req.target};
    const auto length = strings::parse_num<size_t>(
        path.substr(path.starts_with('/') ? 1 : 0), size_t{0});
    if (length > 10ULL * 1024ULL * 1024ULL)
      return send_error_response(conn, keep_alive, req.version);

    // Build a canned HTML response body with the requested padding.
    std::string html{"<html><body><p>I am definitely, totally the file \""};
    html += std::to_string(length);
    html += "\".</p></body>";
    html.append(length, ' ');
    html += "</html>";

    if (req.version != http_version::http_0_9) {
      response_head resp;
      resp.version = req.version;
      resp.status_code = http_status_code::OK;
      resp.reason = "OK";
      resp.options.connection = keep_alive;
      resp.options.content_length = html.size();
      if (!resp.headers.add_raw("Content-Type", "text/html; charset=utf-8"))
        return send_error_response(conn, after_response::close, req.version);

      if (!conn.send(resp.serialize())) return hangup(conn);
    }

    if (!arm_write_timeout(conn)) return hangup(conn);
    if (!conn.send(std::move(html))) return hangup(conn);
    return keep_alive;
  }

  // State-machine phase handlers for `handle_data`. Each returns `nullopt`
  // to continue the loop, or a `bool` to return immediately from
  // `handle_data`.

  // Try to parse out just the request line, which ends at crlf. This lets
  // us check for HTTP/0.9 instead of blocking forever on headers that might
  // never come. Note that, while headers are technically optional for
  // HTTP/1.0, we can at least count on the crlfcrlf sentinel.
  [[nodiscard]] std::optional<bool> handle_request_line(stream_conn& conn,
      http_conn_state& state, std::string_view& input,
      recv_buffer_view& view) const {
    terminated_text_parser parser{state.parser_state};
    std::string_view block_view;
    const auto r = parser.parse(input, block_view);
    if (!r) return true; // incomplete; wait for more data

    // Request line exceeded 8192-byte limit. We have no idea what version
    // the client is trying to speak, so there's no point pretending to
    // send a reasonable error response.
    if (!*r) return conn.hangup() && false;

    // Skip leading bare CRLFs (RFC 9112 section 2.2), up to the limit.
    if (block_view.empty()) {
      if (state.leading_crlf_count >= max_leading_crls)
        return conn.hangup() && false;
      ++state.leading_crlf_count;
      view.update_active_view(input);
      parser.reset();
      return std::nullopt;
    }
    state.leading_crlf_count = 0;

    // Extract request line.
    const bool extracted = state.req.parse(block_view);
    view.update_active_view(input);
    parser.reset();

    if (!extracted) {
      // Best practice is to assume HTTP/1.1 in our error response when
      // something goes wrong during the version parsing, or even if it's
      // apparently HTTP/0.9 but still invalid.
      if (state.req.version != http_version::http_1_0 &&
          state.req.version != http_version::http_1_1)
        state.req.version = http_version::http_1_1;
      (void)send_error_response(conn, after_response::close,
          state.req.version);
      state.phase = http_phase::response;
      return conn.close() && false;
    }

    if (state.req.version == http_version::http_0_9) {
      // HTTP/0.9: request line only, no headers.
      const auto keep_alive = dispatch_request(conn, state.req);
      state.req.clear();
      if (keep_alive == after_response::close) {
        state.phase = http_phase::response;
        return conn.close();
      }
      if (!arm_read_timeout(conn)) return conn.hangup() && false;
      return std::nullopt;
    }

    // HTTP/1.x: proceed to parse header-field lines.
    state.parser_state = terminated_text_parser::state{"\r\n\r\n", 8192};
    state.phase = http_phase::header_lines;
    return std::nullopt;
  }

  // Send a 400 error response and close the connection after a header-block
  // parse failure (oversized block or malformed field lines).
  [[nodiscard]] std::optional<bool>
  reject_header_block(stream_conn& conn, http_conn_state& state) const {
    if (!arm_write_timeout(conn)) return conn.hangup() && false;
    if (!conn.send(response_head::make_error_response(after_response::close,
            state.req.version)))
      return conn.hangup() && false;
    state.phase = http_phase::response;
    return conn.close() && false;
  }

  // Parse or skip the header block for an incoming request. When the
  // request has no header-field lines (blank line follows the request line
  // immediately) the active data begins with "\r\n" rather than
  // "...\r\n\r\n". The "\r\n\r\n" sentinel cannot match in that case, so
  // detect it up front and skip the parser entirely.
  //
  // Returns `nullopt` when the block is complete and ready for dispatch,
  // `true` to wait for more data, or `false` after an error is sent.
  [[nodiscard]] std::optional<bool> parse_header_block(stream_conn& conn,
      http_conn_state& state, std::string_view& input,
      recv_buffer_view& view) const {
    if (!input.starts_with("\r\n")) {
      terminated_text_parser parser{state.parser_state};
      std::string_view block_view;
      const auto r = parser.parse(input, block_view);
      if (!r) return true; // incomplete; wait for more data
      if (!*r) return reject_header_block(conn, state); // oversized

      // Process before updating view (buffer may compact on update).
      const bool lines_ok = state.req.headers.add_lines(block_view);
      view.update_active_view(input);
      parser.reset();
      if (!lines_ok) return reject_header_block(conn, state); // malformed
      state.req.options.extract(state.req.headers);
    } else {
      // No headers: consume the blank-line terminator and fall through
      // to dispatch with an empty header set.
      input.remove_prefix(2);
      view.update_active_view(input);
    }
    return std::nullopt;
  }

  [[nodiscard]] std::optional<bool> handle_header_lines(stream_conn& conn,
      http_conn_state& state, std::string_view& input,
      recv_buffer_view& view) const {
    const auto r = parse_header_block(conn, state, input, view);
    if (r) return r;

    // End of header block; dispatch the request.
    const auto keep_alive = dispatch_request(conn, state.req);
    state.req.clear();
    // Reset sentinel for the next request line (keep-alive / pipelining).
    state.parser_state = terminated_text_parser::state{"\r\n", 8192};
    state.phase = http_phase::request_line;
    if (keep_alive == after_response::close) {
      state.phase = http_phase::response;
      return conn.close();
    }
    // Keep-alive: re-arm read timeout, then loop to process any additional
    // pipelined requests already in the receive buffer.
    if (!arm_read_timeout(conn)) return conn.hangup() && false;
    return std::nullopt;
  }

  // Future: accumulate request body bytes.
  [[nodiscard]] bool handle_body(stream_conn& conn, http_conn_state&,
      std::string_view&, recv_buffer_view&) const {
    if (!arm_read_timeout(conn)) return conn.hangup() && false;
    return true;
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
        const auto r = handle_request_line(conn, state, input, view);
        if (r) return *r;
        continue;
      }
      case http_phase::header_lines: {
        const auto r = handle_header_lines(conn, state, input, view);
        if (r) return *r;
        continue;
      }
      case http_phase::body: return handle_body(conn, state, input, view);
      case http_phase::response:
      case http_phase::done:
        // Ignore trailing bytes; let the send queue drain cleanly.
        return true;
      }
    }
  }

  // Maximum bare CRLFs to skip before the request line (RFC 9112 §2.2).
  static constexpr uint8_t max_leading_crls{8};

  std::optional<epoll_loop_runner> runner_;
  epoll_loop_ptr loop_;
  std::optional<timing_wheel_runner> wheel_runner_;
  timing_wheel_ptr wheel_;
  duration_t read_timeout_{30s};
  duration_t write_timeout_{5s};
  conn_ptr_t listener_;
};
}} // namespace corvid::proto
