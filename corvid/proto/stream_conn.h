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
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>
#include <fcntl.h>

#include "epoll_loop.h"
#include "net_endpoint.h"
#include "recv_buffer.h"
#include "../concurrency/relaxed_atomic.h"
#include "../strings/no_zero.h"

namespace corvid { inline namespace proto {

using namespace corvid::strings::no_zero_funcs;

// Forward declaration for `stream_conn_handlers`.
class stream_conn;

// User-supplied persistent callbacks for a `stream_conn`. All fields are
// optional; a null handler is silently skipped when its event fires.
//
//  `on_data(conn, view)` -- fired when data arrives. `conn` is a reference to
//                           the connection; the user may call `conn.send` or
//                           other methods on it. `view` is a
//                           `recv_buffer_view` token: call `active_view` (or
//                           implicit `std::string_view` conversion) to read
//                           the data, and `consume(n)` or
//                           `update_active_view(tail)` to advance the consume
//                           pointer. `EPOLLIN` is re-enabled and any remaining
//                           data is compacted when `view` destructs.
//  `on_drain(conn)`      -- fired when writes become available: when a `send`
//                           finishes with no outbound bytes left pending
//                           (covering both immediate writes and buffered
//                           `EPOLLOUT`-driven drains), or when an async
//                           `connect` succeeds.
//  `on_close(conn)`      -- fired once when the connection closes: peer EOF,
//                           an I/O error, or a failed async `connect`. If no
//                           handler is installed, a graceful close is
//                           initiated automatically after peer EOF so that
//                           connections without an external owner (e.g.,
//                           accepted or released) are fully torn down rather
//                           than left half-open indefinitely. To keep the
//                           write side open after peer EOF, install an
//                           `on_close` handler and call `close` or `hangup`
//                           only when done.
struct stream_conn_handlers {
  std::function<bool(stream_conn&, recv_buffer_view)> on_data = nullptr;
  std::function<bool(stream_conn&)> on_drain = nullptr;
  std::function<bool(stream_conn&)> on_close = nullptr;
};

// A `stream_conn` is a non-blocking stream socket driven by an `epoll_loop`.
// Instances are created, directly or indirectly, by `stream_conn_ptr_with`
// factories, and registered with the loop.
//
// Supports three creation paths:
//
// 1. Already-connected socket (whether from `connect` or `accept`): use the
//    `stream_conn_ptr_with` constructor directly. The socket must be
//    non-blocking and connected.
//
// 2. Async connect: use the static `stream_conn_ptr_with::connect` factory. It
//    creates the socket, optionally binds the local end, calls `connect(2)`,
//    and registers with the loop. When the kernel reports the outcome,
//    `on_writable` or `on_error` transitions either to connected state
//    (notifying any write waiter) or closes the connection.
//
// 3. Listening: use the static `stream_conn_ptr_with::listen` factory. It
//    creates the socket, sets `SO_REUSEADDR`, binds, and calls `listen(2)`.
//    `EPOLLIN` events call `accept4` in a drain loop; each accepted connection
//    is created as a self-owning `stream_conn` in the loop, with a copy of the
//    listener's handlers. `send` and other data-path operations are disabled
//    on a listening `stream_conn`.
//
// Thread safety: `send`, `close`, `hangup`, and the destructor are safe
// to call from any thread. They route work to the command queue via
// `epoll_loop::post`. All actual I/O and epoll-mask mutations run
// exclusively on the loop thread.
//
// Send path: `send(std::string&&)` takes ownership of the caller's string.
// If the send queue is empty, an immediate `::write` is attempted. If all
// bytes are written, the string is discarded and `on_writable` notifies any
// pending write waiter immediately because no outbound bytes remain pending.
// Otherwise the remainder of the string is added to the send queue and
// `EPOLLOUT` is armed so that subsequent `EPOLLOUT` events flush the send
// queue. When the queue empties, `EPOLLOUT` is disarmed and `on_writable`
// notifies any pending write waiter.
//
// Receive path: `EPOLLIN` is armed while `recv_buf_.reads_enabled` is true.
// When `EPOLLIN` fires, `on_readable` appends bytes to the persistent
// `recv_buffer` and delivers a `recv_buffer_view` token to the active
// `on_data` handler. The view holds `begin`/`end` semantics: the parser
// advances `begin` via `consume` and `EPOLLIN` is re-enabled when the view
// destructs. If the buffer fills up while a view is live, `EPOLLIN` is
// disabled until the view destructs and `resume_receive` compacts it.
//
// Close: `close` is graceful. If the send queue is non-empty, the socket
// closes only after it drains. `hangup` discards pending outbound data and
// closes immediately. The destructor of `stream_conn_ptr_with` uses `hangup`,
// so you should call `close` in the non-error path.
//
// Supports persistent callbacks via `stream_conn_handlers`, and `send`.
//
// Two additional per-call models are provided by `stream_async.h`:
// `stream_async_cb` (one-shot callbacks) and `stream_async_coro` (coroutines).
// Both temporarily redirect `active_handlers_` so `stream_conn` is unaware
// of them.
//
// `stream_conn` is designed to be inherited by `stream_conn_with_state`, but
// should otherwise be treated as `final` due to the delicacy of the
// mechanisms.
class stream_conn: public io_conn {
public:
  // Default receive-buffer capacity per connection, in bytes.
  static constexpr size_t default_recv_buf_size = 16384;

  // True if the connection has not yet been closed.
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // Change the per-connection receive-buffer target used for future reads.
  // `size` must be non-zero. The actual buffer string size used by a given
  // read may be somewhat larger if more capacity is available. Safe to call
  // from any thread. Returns false if `size == 0`.
  [[nodiscard]] bool set_recv_buf_size(size_t size) {
    if (size == 0) return false;
    recv_buf_.min_capacity =
        std::min(size, std::numeric_limits<std::size_t>::max() / 2);
    return true;
  }

  // The current per-connection receive-buffer target used for future reads.
  // The actual buffer string size used by a given read may be somewhat
  // larger if more capacity is available. Safe to call from any thread.
  [[nodiscard]] size_t recv_buf_size() const noexcept {
    return recv_buf_.min_capacity;
  }

  // Whether the read side is still open. Safe to call from any thread. Returns
  // false if the connection is closed or the read side has been shut down.
  [[nodiscard]] bool can_read() const noexcept { return read_open_; }

  // Whether the write side is still open. Safe to call from any thread.
  // Returns false if the connection is closed or the write side has been shut
  // down.
  [[nodiscard]] bool can_write() const noexcept { return write_open_; }

  // The remote peer address supplied at construction. Safe to call from any
  // thread.
  [[nodiscard]] const net_endpoint& remote_endpoint() const noexcept {
    return remote_;
  }

  // The local address this socket is bound to. Useful after `listen` on
  // port 0 to discover the OS-assigned port. Safe to call from any thread.
  [[nodiscard]] net_endpoint local_endpoint() const noexcept {
    return net_endpoint{sock()};
  }

  // Take ownership of `buf` and start sending it. Safe to call from any
  // thread. Success does not mean that the buffer has been fully sent.
  // Instead, send completion is signaled via the `on_drain` callback.
  [[nodiscard]] bool send(std::string&& buf) {
    if (buf.empty()) return false;
    if (!open_) return false;
    if (!write_open_) return false;
    return execute_or_post([p = self(), b = std::move(buf)]() mutable {
      return p->enqueue_send(std::move(b));
    });
  }

  // Start a graceful close. Drains pending sends first, then shuts down the
  // socket. Safe to call from any thread. Once this is called, destructing the
  // owning `stream_conn_ptr_with` does not cause a forceful close.
  [[nodiscard]] bool close() {
    graceful_close_started_ = true;
    return loop_.post([p = self()] { return p->do_close(); });
  }

  // Start a forceful close. Pending sends are discarded and SO_LINGER is
  // disabled. Safe to call from any thread.
  [[nodiscard]] bool hangup() {
    return execute_or_post([p = self()] { return p->do_hangup(); });
  }

  // Shut down the local read side while keeping the write side available.
  // Safe to call from any thread.
  [[nodiscard]] bool shutdown_read(execution exec = execution::blocking) {
    if (!open_) return false;
    return exec_lambda(exec, [p = self()] { return p->do_shutdown_read(); });
  }

  // Shut down the local write side while keeping the read side available.
  // Safe to call from any thread.
  [[nodiscard]] bool shutdown_write(execution exec = execution::blocking) {
    if (!open_) return false;
    return exec_lambda(exec, [p = self()] { return p->do_shutdown_write(); });
  }

protected:
  enum class allow : bool { ctor };

  friend class stream_async_base;
  template<typename>
  friend class stream_conn_ptr_with;

public:
  // Constructor. Technically public to allow `std::make_shared<stream_conn>`
  // from `stream_conn_ptr_with` factories and `handle_listen`, but gated
  // behind a protected type. Use `stream_conn_ptr_with` to construct an
  // instance.
  explicit stream_conn(allow, epoll_loop& loop, net_socket&& sock,
      const net_endpoint& remote, stream_conn_handlers&& h, size_t rbs,
      bool connecting, bool listening) noexcept
      : io_conn{std::move(sock)}, loop_{loop}, remote_{remote},
        own_handlers_{std::move(h)}, active_handlers_{&own_handlers_},
        open_{true}, connecting_{connecting}, listening_{listening} {
    recv_buf_.min_capacity =
        std::min(rbs, std::numeric_limits<std::size_t>::max() / 2);
    // Listening sockets have no writable data path.
    if (listening) write_open_ = false;
  }

protected:
  // Event loop that drives this connection. Protected so derived classes can
  // reference it in `accept_clone` overrides.
  epoll_loop& loop_;

  // Produce a new connected `stream_conn` for an accepted socket. Called by
  // `handle_listen` instead of constructing `stream_conn` directly, so derived
  // classes can override this to supply a richer concrete type. Returning
  // `nullptr` causes `handle_listen` to skip registration for that socket
  // (connection-limiting hook). The `handlers` argument is a copy of the
  // listener's `own_handlers_`.
  [[nodiscard]] virtual std::shared_ptr<stream_conn>
  accept_clone(net_socket&& sock, const net_endpoint& remote,
      stream_conn_handlers handlers) const {
    return std::make_shared<stream_conn>(allow::ctor, loop_, std::move(sock),
        remote, std::move(handlers), recv_buf_size(),
        /*connecting=*/false, /*listening=*/false);
  }

private:
  net_endpoint remote_;

  // The connection's own persistent handlers. Never moved or destroyed while
  // the connection is alive. `active_handlers_` always points here unless an
  // `stream_async_base` has temporarily redirected it to its own handlers.
  stream_conn_handlers own_handlers_;

  // Atomic pointer to the currently active handlers. Normally points to
  // `own_handlers_`. `stream_async_base::install_handlers` atomically swaps
  // this to its own `handlers_` member; its destructor swaps it back.
  std::atomic<stream_conn_handlers*> active_handlers_;

  // Send queue.
  // TODO: Consider using an object pool owned by the loop to reduce the
  // overhead of empty deques.
  std::deque<std::string> send_queue_;

  // Unsent tail of `send_queue_.front`. Empty iff `send_queue_` is empty.
  std::string_view head_span_;

  // Persistent receive buffer. The framework appends bytes after `end`;
  // the parser consumes bytes from `begin`.
  recv_buffer recv_buf_;

  // Cleared atomically by `do_close_now`. Read from any thread via
  // `is_open`.
  relaxed_atomic_bool open_;

  //  Set by `close` to start a graceful close, preventing the destructor
  //  of `stream_conn_ptr_with` from closing forcefully.
  relaxed_atomic_bool graceful_close_started_{false};

  // Whether the read and write sides of the socket are open (assuming `open_`
  // is true). Cleared by `do_shutdown_read` and `do_shutdown_write`
  relaxed_atomic_bool read_open_{true};
  relaxed_atomic_bool write_open_{true};

  // Set to true by `register_with_loop` after successful epoll registration.
  // Before this point, `execute_or_post` defers inline execution even on the
  // loop thread, so that operations posted before registration (e.g., `send`
  // from the loop thread immediately after `adopt`) do not attempt epoll
  // mutations on an unregistered fd.
  relaxed_atomic_bool registered_{false};

  // True while an async connect is in progress. Cleared by
  // `handle_connect` once `SO_ERROR` is checked. During this phase,
  // `on_readable`, `on_writable`, and `on_error` all route to
  // `handle_connect` instead of the normal data paths.
  bool connecting_ = false;

  // True for listening sockets created by `stream_conn_ptr_with::listen`. In
  // this phase, `on_readable` routes to `handle_listen` to drain accepted
  // connections, `on_writable` is a no-op, and the write data path is
  // disabled.
  bool listening_ = false;

  // Set by `do_close` to request a full close after pending writes drain.
  bool close_requested_ = false;

  // Set by `notify_close_once` to ensure `on_close` is delivered at most
  // once.
  bool close_notified_ = false;

  // Set by `handle_read_eof` when EOF arrives while `view_active` is true.
  // Cleared and acted on by `resume_receive` once the live view destructs,
  // preventing a second view from being created while the first is still live.
  bool eof_pending_ = false;

  // Return a `shared_ptr<stream_conn>` to `*this`.
  [[nodiscard]] std::shared_ptr<stream_conn> self() {
    return std::static_pointer_cast<stream_conn>(shared_from_this());
  }

  // Register `net_socket` with the loop. Stores a shared owner in the loop's
  // registration map, keeping the state alive as long as the fd is
  // registered, even if its `stream_conn_ptr_with` is destructed. (However,
  // its destruction will start a forceful close.)
  //
  // In connecting mode, arms `EPOLLOUT` (so the first `on_writable`,
  // `on_readable`, or `on_error` can call `handle_connect`) and
  // suppresses `EPOLLIN` until the connect resolves. In connected mode,
  // derives initial read/write interest from `wants_read_events` and
  // `send_queue_` so that handlers or queued sends registered before this call
  // are correctly armed.
  [[nodiscard]] bool register_with_loop() {
    if (!open_) return false;
    const bool want_write = connecting_ || !send_queue_.empty();
    // Suppress reads while connecting: `handle_connect` calls
    // `refresh_read_interest` once `SO_ERROR` confirms success.
    const bool want_read = !connecting_ && wants_read_events();
    if (loop_.register_socket(shared_from_this(), want_read, want_write)) {
      registered_ = true;
      return true;
    }
    return do_close_now(close_mode::forceful) && false;
  }

  // `io_conn` overrides: called on the loop thread by `dispatch_event`.
  [[nodiscard]] bool on_readable() override {
    assert(loop_.is_loop_thread());
    if (listening_) return handle_listen();
    if (connecting_) return handle_connect();
    return handle_readable();
  }

  [[nodiscard]] bool on_writable() override {
    assert(loop_.is_loop_thread());
    assert(!listening_); // Nobody should be writing to a listening socket.
    if (connecting_) return handle_connect();
    return flush_send_queue();
  }

  // Called by `epoll_loop` for `EPOLLERR | EPOLLHUP`: always an unrecoverable
  // TCP-level event (hard socket error, RST, or full teardown).
  //
  // Note: `EPOLLRDHUP` (normal peer half-close) is always armed but is routed
  // through `on_readable` by `dispatch_event`, so it never reaches here.
  //
  // When `EPOLLHUP | EPOLLIN` fires in the same wakeup, `dispatch_event`
  // calls `on_readable` first to drain any buffered data before calling
  // `on_error`. That single `recv` in `handle_readable` may not fully
  // empty the kernel receive buffer, so `peek_eof` can still return `false`
  // (data available) when we arrive here. In that case the connection has
  // already hung up, the remaining data has no handler to receive it, and
  // there is no path forward: so a forceful close is correct.
  [[nodiscard]] bool on_error() override {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    if (listening_) return do_close_now(close_mode::forceful);
    if (connecting_) return handle_connect();
    if (read_open_) {
      if (wants_read_events()) return handle_readable();
      const auto eof = sock().peek_eof();
      if (!eof.has_value() || !*eof)
        return do_close_now(close_mode::forceful) && false;
      return handle_read_eof();
    }

    return maybe_finish_after_side_close() || true;
  }

  // Run `fn` immediately if on the loop thread and already registered with the
  // loop, otherwise post it. Mirrors `epoll_loop::execute_or_post` but guards
  // against running inline before `register_with_loop` has executed, which
  // would cause silently failed attempts at epoll mutations (e.g.,
  // `enable_writes`) on an unregistered fd.
  template<typename FN>
  [[nodiscard]] bool execute_or_post(FN&& fn) {
    if (loop_.is_loop_thread() && registered_) return fn();
    return loop_.post(std::forward<FN>(fn));
  }

  // Execute the lambda `fn` on the loop thread.
  //
  // If we're already on the loop thread and registered, runs it inline.
  // Otherwise, posts it to the queue, and either returns immediately
  // (`execution::nonblocking`) or waits for completion
  // (`execution::blocking`). The blocking `post_and_wait` is only used when
  // off the loop thread AND already registered; calling it before registration
  // would defeat the pre-registration guard, since `post_and_wait` executes
  // inline on the loop thread. If we run synchronously, the return value of
  // the lambda is passed through; otherwise, it's always true.
  template<typename FN>
  [[nodiscard]] bool exec_lambda(execution exec, FN&& fn) {
    const auto is_loop_thread = loop_.is_loop_thread();
    const bool registered = registered_;
    if (is_loop_thread && registered) return fn();
    if (exec == execution::nonblocking || is_loop_thread || !registered)
      return loop_.post(std::forward<FN>(fn));
    return loop_.post_and_wait(std::forward<FN>(fn));
  }

  // Acquire-load `active_handlers_`, synchronizing with the release store in
  // `stream_async_base::install_handlers` and its destructor. Required
  // whenever the returned pointer is dereferenced to invoke a handler.
  [[nodiscard]] stream_conn_handlers*
  acquire_active_handlers() const noexcept {
    return active_handlers_.load(std::memory_order::acquire);
  }

  // Read interest is needed while in listening mode (always) or while the
  // active `on_data` handler is installed AND `reads_enabled` is true.
  // `reads_enabled` allows reads to be suppressed even when a handler exists
  // (e.g., before the first `read` call in `stream_async`, or for
  // connections that should not start reading until explicitly armed).
  [[nodiscard]] bool wants_read_events() const noexcept {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    if (listening_) return true;
    if (!recv_buf_.reads_enabled) return false;
    if (!read_open_ || !acquire_active_handlers()->on_data) return false;
    return true;
  }

  // Set `reads_enabled` and propagate the change to the loop.
  // Loop-thread-only.
  [[nodiscard]] bool enable_reads(bool on = true) {
    assert(loop_.is_loop_thread());
    recv_buf_.reads_enabled = on;
    return refresh_read_interest();
  }

  // Returns true if `active_handlers_` has been redirected away from
  // `own_handlers_` by an `stream_async_base`.
  [[nodiscard]] bool are_handlers_external() const noexcept {
    return acquire_active_handlers() != &own_handlers_;
  }

  // Apply the current read-interest policy to the loop without disturbing
  // the always-armed error/hangup notifications.
  [[nodiscard]] bool refresh_read_interest() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    return loop_.enable_reads(*this, wants_read_events());
  }

  // Deliver `on_close` at most once for the lifetime of the connection.
  [[nodiscard]] bool notify_close_once() {
    assert(loop_.is_loop_thread());
    if (close_notified_) return false;
    close_notified_ = true;
    auto* h = acquire_active_handlers();
    if (h->on_close) return h->on_close(*this);
    return true;
  }

  // Deliver newly read data to the active `on_data` handler (which may be
  // `own_handlers_.on_data` or one installed by a `stream_async_base` child).
  // Sets `view_active` before dispatching; it is cleared by `resume_receive`
  // once the view destructs.
  [[nodiscard]] bool notify_read_ready() {
    assert(loop_.is_loop_thread());
    auto* h = acquire_active_handlers();
    if (h->on_data) {
      recv_buf_.view_active = true;
      return h->on_data(*this,
          recv_buffer_view{recv_buf_, [sp = self()](size_t n, size_t lse) {
                             sp->resume_receive(n, lse);
                           }});
    }
    return refresh_read_interest();
  }

  // Report read-side closure to any active `on_data` handler. Zeroes
  // `begin`/`end` before dispatching so that `active_view` returns an empty
  // view, signaling EOF to the parser. Must only be called when no
  // `recv_buffer_view` is live (`view_active` must be false).
  [[nodiscard]] bool notify_read_closed() {
    assert(loop_.is_loop_thread());
    assert(!recv_buf_.view_active); // must not be called while a view is live
    auto* h = acquire_active_handlers();
    if (h->on_data) {
      recv_buf_.begin.store(0, std::memory_order::relaxed);
      recv_buf_.end.store(0, std::memory_order::relaxed);
      recv_buf_.view_active = true;
      return h->on_data(*this,
          recv_buffer_view{recv_buf_, [sp = self()](size_t n, size_t lse) {
                             sp->resume_receive(n, lse);
                           }});
    }
    return refresh_read_interest();
  }

  // Notify that all queued outbound data has been fully drained.
  [[nodiscard]] bool notify_drained() {
    auto* h = acquire_active_handlers();
    if (h->on_drain) return h->on_drain(*this);
    return true;
  }

  // Shut down the local read side and notify any blocked read waiter.
  [[nodiscard]] bool do_shutdown_read() {
    assert(loop_.is_loop_thread());
    if (!open_ || !read_open_) return false;
    if (!sock().shutdown(SHUT_RD))
      return do_close_now(close_mode::forceful) && false;
    read_open_ = false;
    if (!loop_.enable_reads(*this, false)) return false;
    // Notify any `stream_async_base` that may have a pending read waiter.
    // `notify_read_closed` calls `active_handlers_->on_data` with an empty
    // buffer; persistent own handlers use `on_close` instead. Skip if a view
    // is live (`recv_buf_.view_active` is true), since `notify_read_closed`
    // must not be called while a view exists.
    if (are_handlers_external() && !recv_buf_.view_active)
      (void)notify_read_closed();
    return maybe_finish_after_side_close() || true;
  }

  // Shut down the local write side and discard any unsent outbound data.
  [[nodiscard]] bool do_shutdown_write() {
    assert(loop_.is_loop_thread());
    if (!open_ || !write_open_) return false;
    if (!sock().shutdown(SHUT_WR))
      return do_close_now(close_mode::forceful) && false;
    write_open_ = false;
    send_queue_.clear();
    head_span_ = {};
    if (!loop_.enable_writes(*this, false)) return false;
    return maybe_finish_after_side_close() || true;
  }

  // Fully close once both local directions have been shut down.
  [[nodiscard]] bool maybe_finish_after_side_close(
      close_mode mode = close_mode::graceful) {
    assert(loop_.is_loop_thread());
    if (!read_open_ && !write_open_) return do_close_now(mode) && false;
    return true;
  }

  // Dispatch the deferred EOF notifications: deliver `notify_read_closed` to
  // any external async handler, then fire `on_close`. Called by
  // `handle_read_eof` when no view is live, or by `resume_receive` after
  // a deferred EOF (when `eof_pending_` was set).
  [[nodiscard]] bool do_eof_notifications() {
    assert(loop_.is_loop_thread());
    // `read_open_`, `enable_reads`, and `enable_rdhup` were already handled in
    // `handle_read_eof`.
    if (are_handlers_external()) (void)notify_read_closed();
    (void)notify_close_once();
    if (close_requested_ && send_queue_.empty()) return do_close_now();
    // If no `on_close` handler is installed, initiate a graceful close so
    // that connections without an external owner are fully torn down rather
    // than left half-open indefinitely. Connections that need the write side
    // after peer EOF must install an `on_close` handler and call `close` or
    // `hangup` only when done.
    if (!acquire_active_handlers()->on_close) return do_close();
    return maybe_finish_after_side_close();
  }

  // Handle EOF by closing the read side and finishing any pending close.
  //
  // Disarms both `EPOLLIN` and `EPOLLRDHUP` immediately: after EOF, the peer
  // has done `SHUT_WR`, so `EPOLLRDHUP` would otherwise remain
  // level-triggered and repeatedly wake the loop while the write side is
  // still open.
  //
  // Sets `eof_pending_` and defers notifications if a `recv_buffer_view` is
  // live when EOF arrives, or becomes live as a result of dispatching buffered
  // data. In both cases `resume_receive` handles the EOF once the view
  // destructs, ensuring at most one live view at a time.
  [[nodiscard]] bool handle_read_eof() {
    assert(loop_.is_loop_thread());
    read_open_ = false;
    // TODO: We can do this in one step.
    (void)loop_.enable_reads(*this, false);
    (void)loop_.enable_rdhup(*this, false);
    // Defer notifications: deliver any remaining buffered data and the EOF
    // signal once the live view destructs (via `resume_receive`).
    if (recv_buf_.view_active) {
      eof_pending_ = true;
      return true;
    }
    // No view active. Deliver any buffered data first, then close.
    if (!recv_buf_.active().empty()) {
      if (!notify_read_ready()) return false;
      // An async parser may have kept the view alive past the return of
      // `on_data`. If so, defer EOF the same way as when EOF arrives while
      // a view is already live: `resume_receive` will handle it once the
      // view destructs.
      if (recv_buf_.view_active) {
        eof_pending_ = true;
        return true;
      }
    }

    return do_eof_notifications() && false;
  }

  // Handle a deferred EOF that arrived while a `recv_buffer_view` was live.
  // Called from `resume_receive` after the view destructs and compaction is
  // done. If buffered data remains, re-dispatches it first (keeping
  // `eof_pending_` set so the next `resume_receive` call finishes the job);
  // otherwise clears `eof_pending_` and fires `do_eof_notifications`.
  [[nodiscard]] bool handle_deferred_eof() {
    if (!recv_buf_.active().empty()) {
      return loop_.post([p = self()]() -> bool {
        if (!p->open_) return false;
        return p->notify_read_ready();
      });
    }
    eof_pending_ = false;
    return do_eof_notifications() && false;
  }

  // Handle write failure by aborting queued sends and closing as needed.
  [[nodiscard]] bool handle_write_failure() {
    assert(loop_.is_loop_thread());
    if (!write_open_->exchange(false, std::memory_order::relaxed))
      return false;
    send_queue_.clear();
    head_span_ = {};
    (void)loop_.enable_writes(*this, false);
    if (close_requested_ || !read_open_)
      return do_close_now(close_mode::forceful);
    return maybe_finish_after_side_close(close_mode::forceful);
  }

  // Resolve a pending async connect by checking `SO_ERROR`.
  //
  // On failure, closes the connection (notifying any pending read waiter and
  // firing `on_close`).
  //
  // On success, transitions to connected state: arms `EPOLLIN` if a read
  // waiter or `on_data` handler is active. If no sends are queued, disarms
  // `EPOLLOUT` and fires `notify_drained` immediately. This is how the
  // caller learns the connection is established. If sends were queued before
  // the connect resolved, `EPOLLOUT` stays armed; `notify_drained` fires
  // once the queue drains.
  //
  // Called from `on_readable`, `on_writable`, and `on_error` while
  // `connecting_` is set.
  [[nodiscard]] bool handle_connect() {
    assert(loop_.is_loop_thread());
    assert(connecting_);
    connecting_ = false;

    const auto err = sock().get_option<int>(SOL_SOCKET, SO_ERROR);
    if (!err || *err != 0) return do_close_now(close_mode::forceful) && false;

    // Arm reads if an `on_data` handler is installed.
    (void)refresh_read_interest();

    if (send_queue_.empty()) {
      // No pending sends: disarm `EPOLLOUT` and signal that writes are open.
      if (!loop_.enable_writes(*this, false))
        return do_close_now(close_mode::forceful) && false;
      return notify_drained();
    }
    // Otherwise `EPOLLOUT` is already armed; the next `on_writable` will
    // call `flush_send_queue`, which fires `notify_drained` when done.
    return true;
  }

  // Drain all pending connections. Each accepted connection gets a copy of
  // the listener's handlers and is immediately registered with the loop as a
  // self-owning connection.
  [[nodiscard]] bool handle_listen() {
    assert(loop_.is_loop_thread());
    while (true) {
      auto accepted = sock().accept();
      if (!accepted) break;
      auto peer = accept_clone(std::move(accepted->first),
          net_endpoint{accepted->second}, stream_conn_handlers{own_handlers_});
      if (!peer) continue; // accept_clone declined this connection
      if (!peer->register_with_loop()) return false;
    }
    return true;
  }

  // Append incoming bytes to `recv_buf_` and dispatch to the active `on_data`
  // handler via a `recv_buffer_view`. If a view is already live
  // (`view_active` is true), just extend `end` atomically; the in-flight
  // parser will observe the new bytes on its next `active_view` call. If the
  // buffer is full, disable reads until `resume_receive` compacts it.
  [[nodiscard]] bool handle_readable() {
    if (!read_open_) return false;

    // Initial allocation. Round `min_capacity` up to the actual allocated
    // capacity at that size, using a CAS so a concurrent `set_recv_buf_size`
    // that stored a larger value is not overwritten.
    if (recv_buf_.buffer.empty()) {
      const size_t configured = recv_buf_.min_capacity;
      const size_t actual =
          no_zero::enlarge_to(recv_buf_.buffer, configured).size();
      auto expected = configured;
      recv_buf_.min_capacity->compare_exchange_strong(expected, actual,
          std::memory_order::relaxed, std::memory_order::relaxed);
    }

    const size_t space = recv_buf_.write_space();
    if (space == 0) {
      // Buffer full. Suppress `EPOLLIN` directly (without touching
      // `reads_enabled`) so `resume_receive` can re-arm it via
      // `refresh_read_interest` once compaction frees space.
      if (!loop_.enable_reads(*this, false)) return false;
      return true;
    }

    const size_t old_end = recv_buf_.end.load(std::memory_order::relaxed);
    if (!sock().recv_at(recv_buf_.buffer, old_end)) {
      const bool hard_error = recv_buf_.buffer.size() == old_end;
      no_zero::enlarge_to_cap(recv_buf_.buffer);
      if (hard_error) return do_close_now(close_mode::forceful) && false;

      // EOF.
      // Arm EOF handling. If a view is active, defer until it destructs so
      // we never violate the "at most one live view" invariant.
      return handle_read_eof();
    }

    // `recv_at` trimmed `buffer` to `old_end + n`. Update `end` atomically so
    // any live async parser can see the new bytes, then restore the invariant.
    recv_buf_.end.store(recv_buf_.buffer.size(), std::memory_order::release);
    no_zero::enlarge_to_cap(recv_buf_.buffer);

    // If a view is already live, the in-flight parser will observe the new
    // bytes on its next `active_view` call; skip dispatching a second
    // `on_data`.
    if (recv_buf_.view_active) return true;
    if (!recv_buf_.active().empty()) return notify_read_ready();
    return true;
  }

  // Post all receive-buffer recovery work (compact, re-enable reads, optional
  // re-dispatch) back to the loop thread. Called by `recv_buffer_view`
  // destructor with the parser's requested buffer size (0 = no expansion) and
  // the `end` value last observed by the parser via `active_view`.
  //
  // Uses `execute_or_post` for compaction and `refresh_read_interest`, and may
  // call `notify_read_ready`.
  void resume_receive(size_t new_size = 0, size_t last_seen_end = 0) {
    (void)execute_or_post([p = self(), new_size, last_seen_end]() -> bool {
      if (!p->open_) return false;
      p->recv_buf_.view_active = false;

      // Evaluate before compaction: compaction may reset `end` to
      // `active_len`, making a post-compact comparison against `last_seen_end`
      // meaningless.
      const bool unseen_bytes =
          p->recv_buf_.end.load(std::memory_order::acquire) > last_seen_end;
      p->recv_buf_.compact(new_size);

      // If EOF arrived while a view was live, handle it now.
      if (p->eof_pending_) return p->handle_deferred_eof();

      if (p->recv_buf_.write_space() == 0) {
        // Compact created no free space: the parser consumed nothing and the
        // buffer is still full. Post `on_data` again so the parser gets
        // another chance; it should call `expand_to` or start consuming.
        return p->loop_.post([p]() -> bool {
          if (!p->open_) return false;
          return p->notify_read_ready();
        });
      }

      // Bytes can arrive while a view is live (`handle_readable` extends `end`
      // but skips the `on_data` dispatch). Re-dispatch now if the parser has
      // not yet seen all buffered bytes; otherwise just re-arm `EPOLLIN`.
      if (unseen_bytes && !p->recv_buf_.active().empty()) {
        return p->loop_.post([p]() -> bool {
          if (!p->open_) return false;
          return p->notify_read_ready();
        });
      }

      // Space is available and no unseen bytes remain. Re-evaluate `EPOLLIN`
      // based on current state (`reads_enabled` and active handler presence).
      // This re-arms `EPOLLIN` for persistent handlers and leaves it off for
      // `stream_async` when no read is pending.
      return p->refresh_read_interest();
    });
  }

  // Enqueue `their_buf` for sending. Attempts an immediate `::send` when the
  // queue is empty; if any bytes remain (partial write or EAGAIN) they are
  // pushed onto `send_queue_` and tracked by `head_span_`. `EPOLLOUT` is
  // armed when the queue becomes non-empty; `flush_send_queue` drains it
  // on subsequent `EPOLLOUT` events.
  [[nodiscard]] bool enqueue_send(std::string&& their_buf) {
    assert(loop_.is_loop_thread());
    // Their buf is our buf now.
    auto buf = std::move(their_buf);

    if (!open_ || !write_open_) return false;

    // If there are sends queued ahead of us, or if an async connect is still
    // in progress (sends would fail with `ENOTCONN`), just add to the back.
    // `EPOLLOUT` is already armed in both cases.
    if (const auto send_queue_empty = send_queue_.empty();
        !send_queue_empty || connecting_)
    {
      send_queue_.push_back(std::move(buf));
      if (send_queue_empty) head_span_ = send_queue_.front();
      return true;
    }

    // Send as much as we can of the new buffer.
    auto buf_view = std::string_view{buf};
    if (!sock().send(buf_view)) return handle_write_failure() && false;

    // If fully sent, nothing to queue.
    if (buf_view.empty()) return notify_drained();

    // If we couldn't send all of it, push to queue and arm `EPOLLOUT`.
    // Note: We don't reuse `buf_view` because, in principle, moving a string
    // could change the buffer location (such as with SSO).
    const size_t sent = buf.size() - buf_view.size();
    send_queue_.push_back(std::move(buf));
    head_span_ = send_queue_.front();
    head_span_.remove_prefix(sent);
    return loop_.enable_writes(*this);
  }

  // Drain `send_queue_` as far as `::write` allows, advancing `head_span_`.
  // When a string is fully sent, `pop_front` destroys it immediately.
  // Disarms `EPOLLOUT` and notifies any pending write waiter when the queue
  // empties (or calls `do_close_now` if a graceful close was requested).
  [[nodiscard]] bool flush_send_queue() {
    // Guard against being called after `do_close_now` (e.g., when both
    // `EPOLLIN` and `EPOLLOUT` fire in the same event and the readable
    // handler closes the connection before we get here).
    if (!open_) return false;

    // NOTE: Scatter/gather would be nice here.

    // Write until we're out of data, are blocked, or fail.
    while (!send_queue_.empty()) {
      // If we can't write at all, close immediately.
      if (!sock().send(head_span_)) return handle_write_failure() && false;

      // If we weren't able to write the whole buffer, try later; keep
      // `EPOLLOUT` armed.
      if (!head_span_.empty()) return true;

      // If all gone, move on to the next string in the queue.
      send_queue_.pop_front();
      if (!send_queue_.empty()) head_span_ = send_queue_.front();
    }

    // Queue fully drained, so no need to keep `EPOLLOUT` armed.
    if (!loop_.enable_writes(*this, false))
      return do_close_now(close_mode::forceful) && false;

    // If we were waiting to close, do so now. Notify any `stream_async_base`
    // write waiter of the successful drain before closing; bare persistent
    // handlers receive only `on_close`.
    if (close_requested_) {
      if (are_handlers_external()) (void)notify_drained();
      return do_close_now();
    }

    // Notify any pending write waiter that all outbound data has drained.
    return notify_drained();
  }

  // Graceful close: if data is pending let `flush_send_queue` finish
  // first.
  [[nodiscard]] bool do_close() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    close_requested_ = true;
    if (!send_queue_.empty()) return false;
    return do_close_now();
  }

  // Forceful close: discard pending sends and close immediately.
  [[nodiscard]] bool do_hangup() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    // TODO: Deal with redundancy between this and do_close_now().
    send_queue_.clear();
    head_span_ = {};
    close_requested_ = false;

    return do_close_now(close_mode::forceful);
  }

  // Unconditional close. Idempotent via `open_.exchange(false)`. Returns false
  // if already closed, true if this call performed the close.
  [[nodiscard]] bool do_close_now(close_mode mode = close_mode::graceful) {
    assert(loop_.is_loop_thread());
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;

    read_open_ = false;
    write_open_ = false;

    (void)loop_.unregister_socket(sock());
    (void)sock().close(mode);

    send_queue_.clear();
    head_span_ = {};
    close_requested_ = false;

    // `on_close` notifies any pending `stream_async_base` waiters (coro or
    // cb). The handler is always posted-resume, never inline, so any
    // in-progress `await_suspend` on the call stack has returned before the
    // coroutine continues -- preventing use-after-free when `do_close_now`
    // fires from within `enqueue_send` -> `await_suspend`.
    return notify_close_once();
  }
};

// A move-only smart pointer that owns a `stream_conn` (or a class derived from
// it). Despite being implemented with a `shared_ptr`, a `stream_conn_ptr_with`
// fully owns the connection and removes it from the `epoll_loop` on
// destruction.
//
// `T` defaults to `stream_conn`. Use `stream_conn_ptr_with<MyConn>` (where
// `MyConn` derives from `stream_conn`) to get a typed handle whose
// `operator->` returns `MyConn*`. A `stream_conn_ptr_with<Derived>` is
// implicitly convertible to `stream_conn_ptr_with<Base>`, mirroring
// `shared_ptr` covariance.
//
// The public methods are factory creators (`adopt`, `connect`, `listen`),
// `close`, `release`, `pointer`, and `operator->`.
template<typename T = stream_conn>
class stream_conn_ptr_with {
  static_assert(std::derived_from<T, stream_conn>,
      "stream_conn_ptr_with<T>: T must derive from stream_conn");

public:
  // Default constructor -- creates an empty (invalid) handle. `operator bool`
  // returns false.
  stream_conn_ptr_with() noexcept = default;

  stream_conn_ptr_with(stream_conn_ptr_with&&) noexcept = default;
  stream_conn_ptr_with(const stream_conn_ptr_with&) = delete;

  stream_conn_ptr_with& operator=(stream_conn_ptr_with&&) = default;
  stream_conn_ptr_with& operator=(const stream_conn_ptr_with&) = delete;

  // Implicit upcast: `stream_conn_ptr_with<Derived>` ->
  // `stream_conn_ptr_with<Base>`. Mirrors the covariance of `shared_ptr`.
  template<typename U>
  requires std::derived_from<U, T>
  stream_conn_ptr_with(stream_conn_ptr_with<U>&& other) noexcept
      : conn_(std::move(other.conn_)) {}

  // Performs `hangup` on destruction. If you want to close cleanly, you must
  // call `close` before the instance is destructed.
  // NOLINTBEGIN(bugprone-exception-escape)
  ~stream_conn_ptr_with() {
    if (!conn_ || conn_->graceful_close_started_) return;
    (void)conn_->loop_.execute_or_post([p = std::move(conn_)] {
      return p->do_hangup();
    });
  }
  // NOLINTEND(bugprone-exception-escape)

  // Return a const reference to the underlying `shared_ptr` without releasing
  // ownership. The caller may copy it to extend the connection's lifetime
  // beyond that of this `stream_conn_ptr_with`.
  [[nodiscard]] const std::shared_ptr<T>& pointer() const { return conn_; }

  // Release ownership of the connection. The connection remains registered
  // with the loop and is responsible for itself.
  [[nodiscard]] std::shared_ptr<T> release() { return std::move(conn_); }

  // Adopt an already-connected, non-blocking `sock` and register it with
  // `loop`. `remote` records the peer address for diagnostics. May be called
  // from any thread. Socket must be nonblocking.
  [[nodiscard]] static stream_conn_ptr_with adopt(epoll_loop& loop,
      net_socket&& sock, const net_endpoint& remote,
      stream_conn_handlers&& h = {},
      size_t recv_buf_size = stream_conn::default_recv_buf_size) {
    assert((sock.get_flags().value_or(0) & O_NONBLOCK) != 0);
    return stream_conn_ptr_with{loop, std::move(sock), remote, std::move(h),
        recv_buf_size, /*connecting=*/false, /*listening=*/false};
  }

  // Initiate an async connection to `remote`. Creates a non-blocking socket
  // matching `remote`'s address family, optionally binds the local end to
  // `local` (if non-empty), then calls `connect(2)`. The socket is registered
  // with `EPOLLOUT` if `connect(2)` yields `EINPROGRESS`; the first
  // `on_writable`, `on_readable`, or `on_error` then calls
  // `handle_connect`, which either transitions to connected state (notifying
  // any pending write waiter) or closes the connection (notifying any pending
  // read waiter and firing `on_close`). If `connect(2)` succeeds immediately
  // (e.g., loopback or Unix-domain sockets), a posted task fires
  // `notify_drained` directly after registration.
  //
  // Returns an invalid (empty) handle if the socket cannot be created, `local`
  // cannot be bound, or `connect(2)` fails synchronously; `errno` is set by
  // the failing syscall. Async failure (e.g., `ECONNREFUSED` delivered later)
  // goes through the normal close path.
  [[nodiscard]] static stream_conn_ptr_with connect(epoll_loop& loop,
      const net_endpoint& remote, stream_conn_handlers&& h = {},
      const net_endpoint& local = net_endpoint::invalid,
      size_t recv_buf_size = stream_conn::default_recv_buf_size) {
    auto sock = net_socket::create_for(remote);
    if (!sock.is_open()) return {};

    if (local && !sock.bind(local)) return {};

    const auto result = sock.connect(remote);
    if (result && !*result) return {};
    const auto still_connecting = !result;

    auto ptr = stream_conn_ptr_with{loop, std::move(sock), remote,
        std::move(h), recv_buf_size, /*connecting=*/still_connecting,
        /*listening=*/false};

    // When `connect(2)` succeeds immediately, `handle_connect` never fires
    // via `EPOLLOUT`. Post a follow-up task to mirror its success path:
    // `register_with_loop` already armed `EPOLLIN` correctly; all that
    // remains is to signal readiness via `notify_drained` if no sends are
    // pending (otherwise `flush_send_queue` will call it when the queue
    // drains, as in the normal data path).
    if (!still_connecting) {
      if (!loop.post([p = ptr.conn_] {
            if (p->open_ && p->send_queue_.empty()) return p->notify_drained();
            return false;
          }))
        ptr.conn_.reset();
    }

    return ptr;
  }

  // Create a non-blocking listening socket bound to `local`, register it with
  // `loop`, and return a handle. `EPOLLIN` events drain all pending
  // connections via `accept4`; each creates a self-owning connection (via
  // `accept_clone`) with a copy of `h`. Returns an invalid handle if socket
  // creation, `SO_REUSEADDR`, `bind`, or `listen(2)` fails; `errno` is set by
  // the failing syscall. `reuse_port` enables `SO_REUSEPORT` for multi-process
  // load balancing.
  [[nodiscard]] static stream_conn_ptr_with listen(epoll_loop& loop,
      const net_endpoint& local, stream_conn_handlers&& h = {},
      bool reuse_port = false) {
    auto sock = net_socket::create_for(local);
    if (!sock.is_open()) return {};

    if (!sock.set_reuse_addr()) return {};
    if (reuse_port && !sock.set_reuse_port()) return {};
    if (!sock.bind(local)) return {};
    if (!sock.listen()) return {};

    return stream_conn_ptr_with{loop, std::move(sock), net_endpoint::invalid,
        std::move(h), stream_conn::default_recv_buf_size,
        /*connecting=*/false, /*listening=*/true};
  }

  // Start a graceful close. Drains pending sends first, then shuts down the
  // socket. Safe to call from any thread. Once this is called, destructing
  // this object does not cause a forceful close.
  [[nodiscard]] bool close() {
    if (!conn_) return false;
    return conn_->close();
  }

  // Access the underlying connection directly.
  [[nodiscard]] T* operator->() noexcept { return conn_.get(); }
  [[nodiscard]] const T* operator->() const noexcept { return conn_.get(); }

  // True if this handle is non-empty (i.e., holds a connection).
  [[nodiscard]] explicit operator bool() const noexcept {
    return conn_ != nullptr;
  }

private:
  template<typename>
  friend class stream_conn_ptr_with;

  // Private constructor used by `connect`, `listen`, and `adopt`.
  explicit stream_conn_ptr_with(epoll_loop& loop, net_socket&& sock,
      const net_endpoint& remote, stream_conn_handlers&& h,
      size_t recv_buf_size, bool connecting, bool listening) {
    assert((sock.get_flags().value_or(0) & O_NONBLOCK) != 0);
    if (recv_buf_size == 0) recv_buf_size = stream_conn::default_recv_buf_size;
    conn_ = std::make_shared<T>(stream_conn::allow::ctor, loop,
        std::move(sock), remote, std::move(h), recv_buf_size, connecting,
        listening);
    if (!loop.post([p = conn_] { return p->register_with_loop(); }))
      conn_.reset();
  }

  std::shared_ptr<T> conn_;
};

// Untyped alias: the common case where no per-connection state is needed.
// Use `stream_conn_ptr_with<MyConn>` for a typed handle to a derived class.
using stream_conn_ptr = stream_conn_ptr_with<>;

// Extends `stream_conn` with a typed per-connection state value. `STATE` must
// be default-constructible; it is value-initialized when the connection is
// created.
//
// Use `stream_conn_ptr_with<stream_conn_with_state<STATE>>` to hold an
// instance. In callbacks (which receive `stream_conn&`), recover the concrete
// type and state via `stream_conn_with_state<STATE>::from(conn)`.
//
// The `accept_clone` override ensures that connections accepted by a listening
// `stream_conn_ptr_with<stream_conn_with_state<STATE>>` also have type
// `stream_conn_with_state<STATE>` with a fresh default-constructed `STATE`.
template<typename STATE>
class stream_conn_with_state final: public stream_conn {
public:
  using state_t = STATE;

  // Construct via `stream_conn_ptr_with` factories only. Technically public
  // because `make_shared` requires it, but gated by the protected `allow`
  // token.
  explicit stream_conn_with_state(allow a, epoll_loop& loop, net_socket&& sock,
      const net_endpoint& remote, stream_conn_handlers&& h, size_t rbs,
      bool connecting, bool listening) noexcept
      : stream_conn(a, loop, std::move(sock), remote, std::move(h), rbs,
            connecting, listening) {}

  // Access per-connection state.
  [[nodiscard]] state_t& state() noexcept { return state_; }
  [[nodiscard]] const state_t& state() const noexcept { return state_; }

  // Debug-safe downcast from `stream_conn&` to `stream_conn_with_state&`.
  // In debug builds, asserts that `c` is actually a
  // `stream_conn_with_state<STATE>`; uses `static_cast` in release builds.
  [[nodiscard]] static stream_conn_with_state& from(stream_conn& c) noexcept {
    assert(dynamic_cast<stream_conn_with_state*>(&c) != nullptr);
    return static_cast<stream_conn_with_state&>(c);
  }

protected:
  // Produce a `stream_conn_with_state<STATE>` for each accepted connection,
  // with a fresh default-constructed `STATE`. Returning `nullptr` skips
  // registration (connection-limiting hook).
  [[nodiscard]] std::shared_ptr<stream_conn> accept_clone(net_socket&& sock,
      const net_endpoint& remote,
      stream_conn_handlers handlers) const override {
    return std::make_shared<stream_conn_with_state<state_t>>(allow::ctor,
        loop_, std::move(sock), remote, std::move(handlers), recv_buf_size(),
        /*connecting=*/false, /*listening=*/false);
  }

private:
  state_t state_{};
};

}} // namespace corvid::proto
