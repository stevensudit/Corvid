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
#include <compare>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "epoll_loop.h"
#include "http_head_codec.h"
#include "http_transaction.h"
#include "stream_conn.h"
#include "terminated_text_parser.h"
#include "../containers/opt_find.h"
#include "../concurrency/timer_fuse.h"
#include "../containers/scoped_value.h"
#include "../containers/hash_combiner.h"

namespace corvid { inline namespace proto {

using namespace std::chrono_literals;

// Connection lifecycle phase for an accepted HTTP connection.
//
// `request_line` -- seeking `"\r\n"` for the request line. Empty blocks
//                   (leading bare CRLFs) are silently skipped per RFC 9112
//                   section 2.2. A non-empty block is passed to
//                   `request_head::parse`; HTTP/0.9 dispatches
//                   immediately, HTTP/1.x transitions to `header_lines`.
// `header_lines` -- seeking `"\r\n\r\n"` for the entire header block. A
//                   non-empty block is fed to `http_headers::add_lines`; if
//                   the input starts with `"\r\n"` (blank line immediately
//                   after the request line, no headers), the terminator is
//                   consumed directly and the request is dispatched.
// `body`         -- reading the request body; active read transaction is
//                   called with each new `on_data` arrival via `handle_body`.
// `response`     -- writing the response; active write transaction is called
//                   with each `on_drain` via `handle_drain`.
// `done`         -- terminal state; a response has been queued and the write
//                   side of the socket will be closed once it completes. In
//                   the meantime, incoming bytes are silently ignored so the
//                   send queue can drain without triggering a TCP RST.
enum class http_phase : uint8_t {
  request_line,
  header_lines,
  body,
  response,
  done
};

struct host_path {
  std::string_view hostname;
  std::string_view base_path;
  auto operator<=>(const host_path&) const = default;
};

// Key for `http_server` route registration. `hostname` is matched against the
// `Host` request header; an empty `hostname` matches any host. `base_path` is
// the leading path component extracted from the request target (e.g., `"/api"`
// from `"/api/v2"`) and must be at least `"/"`.
struct host_path_key {
  std::string hostname;
  std::string base_path;
  auto operator<=>(const host_path_key&) const = default;
  [[nodiscard]] constexpr operator host_path() const noexcept {
    return {hostname, base_path};
  }
};

// Enable transparent hashing and equality for `host_path`.
struct transparent_hash_equal_host_path {
  using is_transparent = void;

  [[nodiscard]] size_t operator()(const auto& value) const noexcept {
    auto view = static_cast<host_path>(value);
    return corvid::hash_combiners::combined_hash(view.hostname,
        view.base_path);
  }

  [[nodiscard]] constexpr bool
  operator()(const auto& lhs, const auto& rhs) const noexcept {
    auto lhs_view = static_cast<host_path>(lhs);
    auto rhs_view = static_cast<host_path>(rhs);
    return lhs_view == rhs_view;
  }
};

// Ideally, this should be a trie to allow longest-prefix matching, but a
// single-level solution works for now.
using route_map_t = std::unordered_map<host_path_key, transaction_factory,
    transparent_hash_equal_host_path, transparent_hash_equal_host_path>;

// HTTP/1.x server (including HTTP/0.9) built on `stream_conn` and
// `epoll_loop`, with `timing_wheel`-driven timeouts.
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
// own `timing_wheel_runner`; otherwise it shares the supplied wheel. The
// server itself runs primarily within the `loop` thread, with timeouts
// originating in the `wheel` thread.
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
  // State associated with each accepted connection, stored in the
  // `stream_conn` via `stream_conn_with_state`. All fields are mutated on the
  // epoll loop thread; no locking is needed.
  struct http_conn_state {
    // Note: Ordering was chosen to improve packing, not based on logic.

    // Send callback bound for use with transaction `handle_drain`.
    http_transaction::send_fn send_fn;

    std::atomic_uint64_t read_seq;  // Identifies read timeout fuse.
    std::atomic_uint64_t write_seq; // Identifies write timeout fuse.

    terminated_text_parser::state parser_state; // falsy until initialized

    // Pipeline of transactions for this connection.
    transaction_queue pipeline;

    request_head req; // populated across request_line / header_lines

    http_phase phase{http_phase::request_line}; // state machine

    uint8_t leading_crlf_count{}; // bare CRLFs skipped before request line

    // Reentrancy guard for `drain_transaction`. Set while a drain is in
    // progress; a reentrant entry (triggered via `notify_drained` firing
    // from within `conn.send`) returns immediately so the outer call can
    // advance the queue pointer safely.
    bool draining_{};
  };

  using conn_t = stream_conn_with_state<http_conn_state>;
  using conn_ptr_t = stream_conn_ptr_with<conn_t>;
  using timer_fuse_t = timer_fuse<stream_conn>;
  enum class allow : bool { ctor };

public:
  using duration_t = timing_wheel::duration_t;
  using http_server_ptr = std::shared_ptr<http_server>;
  using epoll_loop_ptr = std::shared_ptr<epoll_loop>;
  using timing_wheel_ptr = std::shared_ptr<timing_wheel>;

  // Create an HTTP/1.1 server listening on `endpoint`.
  //
  // If `loop` is non-null, the server shares it; otherwise it constructs
  // and owns an `epoll_loop_runner`. If `wheel` is non-null, the server shares
  // it; otherwise it constructs and owns a `timing_wheel_runner`. Returns null
  // if the listen socket cannot be created.
  [[nodiscard]] static http_server_ptr create(const net_endpoint& endpoint,
      epoll_loop_ptr loop = nullptr, timing_wheel_ptr wheel = nullptr,
      duration_t request_timeout = 30s, duration_t write_timeout = 5s) {
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

    // Start listening. Use `weak_ptr` to avoid a reference cycle:
    // `http_server` owns the loop, which owns the listener registration, which
    // holds a copy of these handlers; and accepted connections get copies,
    // too. A `shared_ptr` capture would prevent the `http_server` destructor
    // from ever firing, so the background threads would never stop. Ask me how
    // I know.
    std::weak_ptr<http_server> weak_self{self};
    self->listener_ = conn_ptr_t::listen(self->loop_, endpoint,
        {.on_data =
                [weak_self](stream_conn& conn, recv_buffer_view view) {
                  auto s = weak_self.lock();
                  if (!s) return false;
                  return s->handle_data(conn, std::move(view));
                },
            .on_drain =
                [weak_self](stream_conn& conn) {
                  auto s = weak_self.lock();
                  if (!s) return false;
                  return s->handle_drain(conn);
                }},
        coordination_policy::bilateral);
    if (!self->listener_) return nullptr;

    return self;
  }

  // Return the actual bound address (useful when `endpoint` used port 0).
  [[nodiscard]] net_endpoint local_endpoint() const {
    return listener_->local_endpoint();
  }

  // Return a `std::shared_ptr` to `*this`.
  [[nodiscard]] http_server_ptr self() {
    return std::static_pointer_cast<http_server>(shared_from_this());
  }

  // Register `factory` for the route identified by `key`. The `hostname`
  // field is matched against the `Host` request header (empty matches any
  // host). The `base_path` field is the leading path component (e.g.,
  // `"/api"` from `"/api/v2"`); `"/"` acts as a host-scoped catch-all for
  // unmatched paths. Replaces any existing registration for the same key.
  void add_route(host_path_key key, transaction_factory factory) {
    assert(key.base_path.starts_with("/"));
    assert(key.base_path.size() == 1 || !key.base_path.ends_with("/"));
    routes_[std::move(key)] = std::move(factory);
  }

  http_server(allow) {};

private:
  // Actual payload for timeouts.
  [[nodiscard]] static bool timeout_hangup(const timer_fuse_t& fuse) {
    auto c = fuse.get_if_armed();
    if (!c) return true;
    // Use `weak_loop()` rather than `loop()` because this callback runs on
    // the timing-wheel thread. The connection may still be alive (since `c`
    // holds a `std::shared_ptr` to it) but the loop may have already been
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
  // parsed request, and each read of the body, so that keep-alive connections
  // restart the clock. For the purpose of testing, a zero duration means "no
  // timeout" (i.e., don't arm a fuse at all).
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
  [[nodiscard]] bool ensure_initialized(stream_conn& conn) const {
    auto& state = conn_t::from(conn).state();
    if (state.parser_state) return true;
    state.parser_state = terminated_text_parser::state{"\r\n", 8192};
    state.send_fn = [this, &conn](std::string&& buf) -> bool {
      if (buf.empty() || !arm_write_timeout(conn) ||
          !conn.send(std::move(buf)))
      {
        (void)hangup(conn);
        return false;
      }
      return true;
    };
    return arm_read_timeout(conn);
  }

  // When the send queue fully drains, disarm the write timeout and notify
  // the active write transaction. Transitions to `done` when in `response`
  // phase and the transaction queue empties. Also handles initial parser
  // setup on the first drain event before any request has arrived.
  [[nodiscard]] bool handle_drain(stream_conn& conn) const {
    auto& state = conn_t::from(conn).state();
    timer_fuse_t::disarm(state.write_seq);
    if (!ensure_initialized(conn)) {
      (void)hangup(conn);
      return false;
    }
    return drain_transaction(conn);
  }

  // Hang up immediately.
  [[nodiscard]] static after_response hangup(stream_conn& conn) {
    (void)conn.hangup();
    return after_response::close;
  }

  // Send error response, hanging up if anything goes wrong.
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

  // Find the registered route for `key`. Extracts the `base_path` from
  // `key.base_path` directly and performs a prioritized lookup:
  //   1. `{hostname, base_path}` -- exact host + folder
  //   2. `{hostname, "/"}` -- host-specific catch-all
  //   3. `{"", base_path}` -- any-host folder match
  //   4. `{"", "/"}` -- any-host catch-all
  // Returns a pointer to the factory, or nullptr.
  [[nodiscard]] const transaction_factory* find_route(
      const host_path& key) const {
    // Try for exact host and folder match first.
    if (auto f = find_opt(routes_, key)) return f;
    // If not already trying for root path, try now.
    if (key.base_path != "/")
      if (auto f = find_opt(routes_, host_path{key.hostname, "/"})) return f;

    // If hostname not empty, perhaps paths under an empty host were
    // registered, as a catch-all.
    if (!key.hostname.empty()) {
      // First try folder match with catch-all host.
      if (auto f = find_opt(routes_, host_path{"", key.base_path})) return f;
      // If not already trying for root path, try now.
      if (key.base_path != "/")
        if (auto f = find_opt(routes_, host_path{"", "/"})) return f;
    }
    return nullptr;
  }

  // Dispatch the current request header to a new transaction, enqueuing it.
  [[nodiscard]] after_response
  dispatch_transaction(stream_conn& conn, recv_buffer_view& view) const {
    auto& state = conn_t::from(conn).state();
    const after_response keep_alive =
        state.req.options.keep_alive(state.req.version);

    // HTTP/1.1 requires a `Host` header.
    if (state.req.version == http_version::http_1_1 &&
        !state.req.headers.get("Host"))
      return send_error_response(conn, after_response::close,
          state.req.version);

    // Build the route key from the `Host` header and the leading path
    // component of the request target (e.g., "/api" from "/api/v2" or "/api/",
    // and "/" from "/" or "/api").
    auto host_opt = state.req.headers.get("Host");
    std::string_view hostname = host_opt ? *host_opt : std::string_view{};
    std::string_view base_path = state.req.target;
    auto sep = base_path.find('/', 1);
    if (sep == std::string_view::npos) sep = 1;
    base_path = base_path.substr(0, sep);

    const transaction_factory* factory = find_route({hostname, base_path});
    if (!factory)
      return send_error_response(conn, keep_alive, state.req.version,
          http_status_code::NOT_FOUND, "Not Found");

    // Call transaction factory with the request head.
    // Don't trust the factory to move the request head out of the state.
    auto tx = (*factory)(std::move(state.req));
    state.req.clear();

    // Enqueue transaction into pipeline.
    if (!tx)
      return send_error_response(conn, keep_alive, http_version::http_1_1,
          http_status_code::SERVICE_UNAVAILABLE, "Service Unavailable");

    // Propagate the connection-close policy from the request headers; factory
    // code or the transaction itself may override `close_after` later.
    tx->close_after = keep_alive;
    state.pipeline.add_transaction(tx);

    // First `handle_data` call: transaction sees headers via `req_` and
    // whatever body bytes are already in the receive buffer. `view` is
    // passed by reference so the outer parsing loop in `handle_data` can
    // continue after this call (allowing pipelining).
    const stream_claim sc = tx->handle_data(view);

    // Release means that the transaction no longer needs the input stream. The
    // reason is that there is no more body to consume (perhaps because there
    // was no body). In response, we advance the active reader and stay in
    // `request_line` phase so that we can parse the next request.
    //
    // Claim means that the transaction retains the input stream. It is still
    // consuming body bytes, therefore we stay on the current reader and enter
    // `body` phase so that future `on_data` arrivals are directed to the
    // transaction's `handle_data` until it releases.
    if (sc == stream_claim::release)
      state.pipeline.next_reader();
    else
      state.phase = http_phase::body;

    // If no previous transaction is writing, give this one a chance to
    // immediately respond.
    if (state.pipeline.get_writer() == tx)
      if (!drain_transaction(conn)) return after_response::close;

    // The transaction owns the close decision via `close_after`; always
    // report keep-alive here so callers do not prematurely enter close phase.
    return after_response::keep_alive;
  }

  // Call active write transaction to let it send the next part of the
  // response. Once it's done, it's removed.
  [[nodiscard]] static bool drain_transaction(stream_conn& conn) {
    // Avoid reentrancy when `conn.send` drains synchronously.
    auto& state = conn_t::from(conn).state();
    if (state.draining_) return true;
    scoped_value guard{state.draining_, true};

    // If no active writer, nothing to do.
    auto* writer = state.pipeline.get_writer().get();
    if (!writer) return true;

    // If active writer transaction claims the write stream, it is still
    // producing output.
    if (writer->handle_drain(state.send_fn) == stream_claim::claim)
      return true;

    // The transaction can decide to gently close the connection after its
    // response, so we ask its opinion before killing it.
    const auto disposition = writer->close_after;
    (void)state.pipeline.remove_transaction();

    // We can't stop any work started during the read phase of a transaction,
    // but at least we can stop it from responding. Ideally, the transaction
    // would delay work until it starts to respond.
    if (disposition == after_response::close) {
      while (state.pipeline.remove_transaction());
      return enter_close_phase(conn) && false;
    }

    return true;
  }

  // State-machine phase handlers for `handle_data`. Each returns `nullopt`
  // to continue the loop, or a `bool` to return immediately from
  // `handle_data`.

  // Try to parse out just the request line, which ends at crlf. This lets
  // us check for HTTP/0.9 instead of blocking forever on headers that might
  // never come. Note that, while headers are technically optional for
  // HTTP/1.0, we can at least count on the crlfcrlf sentinel.
  [[nodiscard]] std::optional<bool> handle_request_line(stream_conn& conn,
      std::string_view& input, recv_buffer_view& view) const {
    auto& state = conn_t::from(conn).state();
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
      if (++state.leading_crlf_count >= max_leading_crls)
        return conn.hangup() && false;

      view.update_active_view(input);
      parser.reset();
      return std::nullopt;
    }
    state.leading_crlf_count = 0;

    // Extract request line.
    const bool extracted = state.req.parse(block_view);
    view.update_active_view(input);
    parser.reset();

    // Best practice is to assume HTTP/1.1 in our error response when
    // something goes wrong during the version parsing, or even if it's
    // apparently HTTP/0.9 but still invalid.
    auto version = state.req.version;
    if (!extracted) {
      if (version != http_version::http_1_0) version = http_version::http_1_1;
      (void)send_error_response(conn, after_response::close, version);
      return enter_close_phase(conn) && false;
    }

    // HTTP/0.9: request line only, no headers. Dispatch, then enter close
    // phase so incoming bytes are ignored while the response drains.
    if (version == http_version::http_0_9) {
      (void)dispatch_transaction(conn, view);
      return enter_close_phase(conn);
    }

    // HTTP/1.x: proceed to parse header-field lines.
    state.parser_state = terminated_text_parser::state{"\r\n\r\n", 8192};
    state.phase = http_phase::header_lines;
    return std::nullopt;
  }

  // Set `state.phase` to `response` when transactions are still draining,
  // or skip directly to `done` (disarming the read timeout) when the queue
  // is already empty (e.g., after an error response sent via `conn.send`
  // directly with no transaction in the queue).
  [[nodiscard]] static bool enter_close_phase(stream_conn& conn) {
    auto& state = conn_t::from(conn).state();
    if (state.phase == http_phase::done) return true;

    if (!state.pipeline.get_writer()) {
      timer_fuse_t::disarm(state.read_seq);
      state.phase = http_phase::done;
    } else
      state.phase = http_phase::response;

    return conn.close();
  }

  // Send a 400 error response and close the connection after a header-block
  // parse failure (oversized block or malformed field lines).
  [[nodiscard]] std::optional<bool> reject_header_block(
      stream_conn& conn) const {
    auto& state = conn_t::from(conn).state();
    if (!arm_write_timeout(conn)) return conn.hangup() && false;
    if (!conn.send(response_head::make_error_response(after_response::close,
            state.req.version)))
      return conn.hangup() && false;
    return enter_close_phase(conn) && false;
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
      std::string_view& input, recv_buffer_view& view) const {
    auto& state = conn_t::from(conn).state();
    if (!input.starts_with("\r\n")) {
      terminated_text_parser parser{state.parser_state};
      std::string_view block_view;
      const auto r = parser.parse(input, block_view);
      if (!r) return true; // incomplete; wait for more data
      if (!*r) return reject_header_block(conn); // oversized

      // Process before updating view (buffer may compact on update).
      const bool lines_ok = state.req.headers.add_lines(block_view);
      view.update_active_view(input);
      parser.reset();
      if (!lines_ok) return reject_header_block(conn); // malformed

      state.req.options.extract(state.req.headers);
    } else {
      // No headers: consume the blank-line terminator and fall through
      // to dispatch with an empty header set.
      input.remove_prefix(2);
      view.update_active_view(input);
    }
    return std::nullopt;
  }

  // Handle the header lines phase, which ends when the full block is parsed or
  // skipped. On success, dispatches the transaction and transitions to `body`
  // phase if the transaction claims the input stream, or back to
  // `request_line` phase if it releases immediately (no body or fully
  // consumed). Returns `nullopt` to continue the loop, or a `bool` to return
  // immediately from `handle_data`.
  [[nodiscard]] std::optional<bool> handle_header_lines(stream_conn& conn,
      std::string_view& input, recv_buffer_view& view) const {
    auto& state = conn_t::from(conn).state();
    const auto r = parse_header_block(conn, input, view);
    if (r) return r;

    // End of header block; create and enqueue a transaction.
    const auto keep_alive = dispatch_transaction(conn, view);

    // If we're not in body phase, that means that the transaction released the
    // input stream immediately (no body or fully consumed). In that case, we
    // parse the next next request line. If we are in body phase, the
    // transaction is still consuming input, so we stay in the loop.
    input = {};
    if (state.phase != http_phase::body) {
      // Transaction released the input immediately (no body or fully
      // consumed); reset sentinel for the next request line.
      state.parser_state = terminated_text_parser::state{"\r\n", 8192};
      state.phase = http_phase::request_line;
      // Re-sync `input` from the view so the outer loop continues correctly.
      input = view.active_view();
    }

    // Posthumously honor the transaction's keep-alive decision.
    if (keep_alive == after_response::close) return enter_close_phase(conn);

    // If we're keeping alive, process additional pipelined requests.
    if (!arm_read_timeout(conn)) return conn.hangup() && false;
    return std::nullopt;
  }

  // Deliver incoming body bytes to the active read transaction. Transitions
  // back to `request_line` phase when the transaction releases the input
  // stream.
  [[nodiscard]] bool handle_body(stream_conn& conn, std::string_view& input,
      recv_buffer_view& view) const {
    auto& state = conn_t::from(conn).state();
    auto* reader = state.pipeline.get_reader().get();
    assert(reader);

    // Deliver the full available buffer to the transaction; it controls
    // how much it consumes via `recv_buffer_view`.
    const stream_claim sc = reader->handle_data(view);

    // If the transaction has relased the input stream, we transition back to
    // `request_line` phase to parse the next request.
    input = {};
    if (sc == stream_claim::release) {
      state.pipeline.next_reader();
      state.parser_state = terminated_text_parser::state{"\r\n", 8192};
      state.phase = http_phase::request_line;
      // Re-sync `input` from the view so the outer loop can continue
      // to process pipelined requests with any remaining bytes.
      input = view.active_view();
    }

    if (!arm_read_timeout(conn)) return conn.hangup() && false;
    return true;
  }

  // Handle incoming data for an accepted connection.
  //
  // Implements an explicit state machine driven by `state.phase`. In the
  // `request_line` phase the parser uses `"\r\n"` as its sentinel; empty
  // blocks (leading bare CRLFs) are skipped and a non-empty block is the
  // request line. In the `header_lines` phase the parser uses `"\r\n\r\n"`
  // as its sentinel so the entire header block is read at once; if the input
  // starts with `"\r\n"` (no headers) the terminator is consumed directly.
  // The loop continues to process all complete blocks present in the receive
  // buffer, providing pipelining: multiple queued requests are handled in a
  // single `on_data` call, and `stream_conn::send` FIFO ordering guarantees
  // response order.
  [[nodiscard]] bool
  handle_data(stream_conn& conn, recv_buffer_view view) const {
    auto& state = conn_t::from(conn).state();
    if (!ensure_initialized(conn)) return false;

    auto input = view.active_view();

    while (true) {
      switch (state.phase) {
      case http_phase::request_line: {
        const auto r = handle_request_line(conn, input, view);
        if (r) return *r;
        continue;
      }
      case http_phase::header_lines: {
        const auto r = handle_header_lines(conn, input, view);
        if (r) return *r;
        continue;
      }
      case http_phase::body: return handle_body(conn, input, view);
      case http_phase::response:
      // Both phases arrive here only after `enter_close_phase` has called
      // `conn.close()`. The write side drains via `on_drain`/`handle_drain`,
      // which is independent of this read path. Nothing useful can be done
      // with incoming bytes, so we drop them and let the queue finish.
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
  route_map_t routes_;
};
}} // namespace corvid::proto
