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
#include "../strings/no_zero.h"

namespace corvid { inline namespace proto {

using namespace corvid::strings::no_zero_funcs;

// Forward declaration for `stream_conn_handlers`.
class stream_conn;

// User-supplied persistent callbacks for a `stream_conn`. All fields are
// optional; a null handler is silently skipped when its event fires.
//
//  `on_data(conn, str)` -- fired when data arrives. `conn` is a reference to
//                          the connection; the user may call `conn.send()` or
//                          other methods on it. `str` is the internal receive
//                          buffer resized to the number of bytes read; the
//                          user may `std::move` from it to steal the
//                          allocation without an extra copy. Both references
//                          are valid only during the call.
//  `on_drain(conn)`     -- fired when writes become available: when a `send()`
//                          finishes with no outbound bytes left pending
//                          (covering both immediate writes and buffered
//                          `EPOLLOUT`-driven drains), or when an async
//                          `connect()` succeeds.
//  `on_close(conn)`     -- fired once when the connection closes: peer EOF, an
//                          I/O error, or a failed async `connect()`. If no
//                          handler is installed, a graceful close is initiated
//                          automatically after peer EOF so that connections
//                          without an external owner (e.g., accepted or
//                          released) are fully torn down rather than left
//                          half-open indefinitely. To keep the write side open
//                          after peer EOF, install an `on_close` handler and
//                          call `close()` or `hangup()` only when done.
struct stream_conn_handlers {
  std::function<void(stream_conn&, std::string&)> on_data = nullptr;
  std::function<void(stream_conn&)> on_drain = nullptr;
  std::function<void(stream_conn&)> on_close = nullptr;
};

// A `stream_conn` is a non-blocking stream socket driven by an `epoll_loop`.
// Instances are created, directly or indirectly, by `stream_conn_ptr`
// factories, and registered with the loop.
//
// Supports three creation paths:
//
// 1. Already-connected socket (whether from `connect` or `accept`): use the
//    `stream_conn_ptr` constructor directly. The socket must be non-blocking
//    and connected.
//
// 2. Async connect: use the static `stream_conn_ptr::connect()` factory. It
//    creates the socket, optionally binds the local end, calls `connect(2)`,
//    and registers with the loop. When the kernel reports the outcome,
//    `on_writable()` or `on_error()` transitions either to connected state
//    (notifying any write waiter) or closes the connection.
//
// 3. Listening: use the static `stream_conn_ptr::listen()` factory. It creates
//    the socket, sets `SO_REUSEADDR`, binds, and calls `listen(2)`. `EPOLLIN`
//    events call `accept4` in a drain loop; each accepted connection is
//    created as a self-owning `stream_conn` in the loop, with a copy of the
//    listener's handlers. `send()` and other data-path operations are disabled
//    on a listening `stream_conn`.
//
// Thread safety: `send()`, `close()`, `hangup()`, and the destructor are safe
// to call from any thread. They route work to the command queue via
// `epoll_loop::post()`. All actual I/O and epoll-mask mutations run
// exclusively on the loop thread.
//
// Send path: `send(std::string&&)` takes ownership of the caller's string.
// If the send queue is empty, an immediate `::write` is attempted. If all
// bytes are written, the string is discarded and `on_writable()` notifies any
// pending write waiter immediately because no outbound bytes remain pending.
// Otherwise the remainder of the string is added to the send queue and
// `EPOLLOUT` is armed so that subsequent `EPOLLOUT` events flush the send
// queue. When the queue empties, `EPOLLOUT` is disarmed and `on_writable()`
// notifies any pending write waiter.
//
// Receive path: `EPOLLIN` is armed while a one-shot read waiter is active or
// a persistent `on_data` handler is installed. When `EPOLLIN` fires,
// `on_readable()` fills the receive buffer via `resize_and_overwrite` (C++23,
// no zero-initialization) and delivers the data to any pending read waiter.
// The user may `std::move` from the buffer to steal its allocation; the next
// read reallocates as needed.
//
// Close: `close()` is graceful. If the send queue is non-empty, the socket
// closes only after it drains. `hangup()` discards pending outbound data and
// closes immediately. The destructor of `stream_conn_ptr` uses `hangup()`, so
// you should call `close()` in the non-error path.
//
// Supports persistent callbacks via `stream_conn_handlers`, and `send()`.
//
// Two additional per-call models are provided by `async_conn.h`:
// `async_conn_cb` (one-shot callbacks) and `async_conn_coro` (coroutines).
// Both temporarily redirect `active_handlers_` so `stream_conn` is unaware
// of them.
class stream_conn final: public io_conn {
public:
  // Default receive-buffer capacity per connection, in bytes.
  static constexpr size_t default_recv_buf_size = 16384;

  // True if the connection has not yet been closed.
  [[nodiscard]] bool is_open() const noexcept {
    return open_.load(std::memory_order::relaxed);
  }

  // Change the per-connection receive-buffer target used for future reads.
  // `size` must be non-zero. The actual buffer string size used by a given
  // read may be somewhat larger if more capacity is available. Safe to call
  // from any thread. Returns false if `size == 0`.
  bool set_recv_buf_size(size_t size) {
    if (size == 0) return false;
    recv_buf_capacity_.store(
        std::min(size, std::numeric_limits<std::size_t>::max() / 2),
        std::memory_order::relaxed);
    return true;
  }

  // The current per-connection receive-buffer target used for future reads.
  // The actual buffer string size used by a given read may be somewhat
  // larger if more capacity is available. Safe to call from any thread.
  [[nodiscard]] size_t recv_buf_size() const noexcept {
    return recv_buf_capacity_.load(std::memory_order::relaxed);
  }

  // Whether the read side is still open. Safe to call from any thread. Returns
  // false if the connection is closed or the read side has been shut down.
  [[nodiscard]] bool can_read() const noexcept {
    return read_open_.load(std::memory_order::relaxed);
  }

  // Whether the write side is still open. Safe to call from any thread.
  // Returns false if the connection is closed or the write side has been shut
  // down.
  [[nodiscard]] bool can_write() const noexcept {
    return write_open_.load(std::memory_order::relaxed);
  }

  // The remote peer address supplied at construction. Safe to call from any
  // thread.
  [[nodiscard]] const net_endpoint& remote_endpoint() const noexcept {
    return remote_;
  }

  // The local address this socket is bound to. Useful after `listen()` on
  // port 0 to discover the OS-assigned port. Safe to call from any thread.
  [[nodiscard]] net_endpoint local_endpoint() const noexcept {
    return net_endpoint{sock()};
  }

  // Take ownership of `buf` and start sending it. Safe to call from any
  // thread. Success does not mean that the buffer has been fully sent.
  // Instead, send completion is signaled via the `on_drain` callback.
  bool send(std::string&& buf) {
    if (buf.empty()) return false;
    if (!open_.load(std::memory_order::relaxed)) return false;
    if (!write_open_.load(std::memory_order::relaxed)) return false;
    return execute_or_post([p = self(), b = std::move(buf)]() mutable {
      p->enqueue_send(std::move(b));
      return true;
    });
  }

  // Start a graceful close. Drains pending sends first, then shuts down the
  // socket. Safe to call from any thread. Once this is called, destructing the
  // owning `stream_conn_ptr` does not cause a forceful close.
  bool close() {
    graceful_close_started_ = true;
    loop_.post([p = self()] { p->do_close(); });
    return true;
  }

  // Start a forceful close. Pending sends are discarded and SO_LINGER is
  // disabled. Safe to call from any thread.
  bool hangup() {
    return execute_or_post([p = self()] { return p->do_hangup(); });
  }

  // Shut down the local read side while keeping the write side available.
  // Safe to call from any thread.
  bool shutdown_read(execution exec = execution::blocking) {
    if (!open_.load(std::memory_order::relaxed)) return false;
    return exec_lambda(exec, [p = self()] { return p->do_shutdown_read(); });
  }

  // Shut down the local write side while keeping the read side available.
  // Safe to call from any thread.
  bool shutdown_write(execution exec = execution::blocking) {
    if (!open_.load(std::memory_order::relaxed)) return false;
    return exec_lambda(exec, [p = self()] { return p->do_shutdown_write(); });
  }

private:
  enum class allow : bool { ctor };

  friend class async_conn_base;

public:
  // Constructor. Technically public to allow `std::make_shared<stream_conn>`
  // from `stream_conn_ptr` factories and `handle_listen()`, but gated behind a
  // private type. Use `stream_conn_ptr` to construct an instance.
  explicit stream_conn(allow, epoll_loop& loop, net_socket&& sock,
      const net_endpoint& remote, stream_conn_handlers&& h, size_t rbs,
      bool connecting, bool listening) noexcept
      : io_conn{std::move(sock)}, loop_{loop}, remote_{remote},
        own_handlers_{std::move(h)}, active_handlers_{&own_handlers_},
        recv_buf_capacity_{
            std::min(rbs, std::numeric_limits<std::size_t>::max() / 2)},
        open_{true}, connecting_{connecting}, listening_{listening} {
    // Listening sockets have no writable data path.
    if (listening) write_open_.store(false, std::memory_order::relaxed);
  }

private:
  friend class stream_conn_ptr;

  epoll_loop& loop_;
  net_endpoint remote_;

  // The connection's own persistent handlers. Never moved or destroyed while
  // the connection is alive. `active_handlers_` always points here unless an
  // `async_conn_base` has temporarily redirected it to its own handlers.
  stream_conn_handlers own_handlers_;

  // Atomic pointer to the currently active handlers. Normally points to
  // `own_handlers_`. `async_conn_base::install_handlers()` atomically swaps
  // this to its own `handlers_` member; its destructor swaps it back.
  std::atomic<stream_conn_handlers*> active_handlers_;

  // Send queue.
  // TODO: Consider using an object pool owned by the loop to reduce the
  // overhead of empty deques.
  std::deque<std::string> send_queue_;

  // Unsent tail of `send_queue_.front()`. Empty iff `send_queue_` is empty.
  std::string_view head_span_;

  // Receive staging buffer. Resized without zero-initialization before each
  // `::read`. The user may `std::move` from the buffer in `on_data`.
  // TODO: Consider using an object pool owned by the loop to reduce overhead
  // from holding large buffers in idle connections.
  std::string recv_buf_;

  // Minimum size for `recv_buf_`.
  std::atomic<size_t> recv_buf_capacity_{default_recv_buf_size};

  // Cleared atomically by `do_close_now()`. Read from any thread via
  // `is_open()`.
  std::atomic_bool open_;

  //  Set by `close()` to start a graceful close, preventing the destructor
  //  of `stream_conn_ptr` from closing forcefully.
  std::atomic_bool graceful_close_started_{false};

  // Whether the read and write sides of the socket are open (assuming `open_`
  // is true). Cleared by `do_shutdown_read()` and `do_shutdown_write()`
  std::atomic_bool read_open_{true};
  std::atomic_bool write_open_{true};

  // Set to true by `register_with_loop()` after successful epoll registration.
  // Before this point, `execute_or_post` defers inline execution even on the
  // loop thread, so that operations posted before registration (e.g., `send()`
  // from the loop thread immediately after `adopt()`) do not attempt epoll
  // mutations on an unregistered fd.
  std::atomic_bool registered_{false};

  // True while an async connect is in progress. Cleared by
  // `handle_connect()` once `SO_ERROR` is checked. During this phase,
  // `on_readable`, `on_writable`, and `on_error` all route to
  // `handle_connect()` instead of the normal data paths.
  bool connecting_ = false;

  // True for listening sockets created by `stream_conn_ptr::listen()`. In
  // this phase, `on_readable` routes to `handle_listen()` to drain accepted
  // connections, `on_writable` is a no-op, and the write data path is
  // disabled.
  bool listening_ = false;

  // Set by `do_close()` to request a full close after pending writes drain.
  bool close_requested_ = false;

  // Set by `notify_close_once` to ensure `on_close` is delivered at most
  // once.
  bool close_notified_ = false;

  // Return a `shared_ptr<stream_conn>` to `*this`.
  [[nodiscard]] std::shared_ptr<stream_conn> self() {
    return std::static_pointer_cast<stream_conn>(shared_from_this());
  }

  // Register `net_socket` with the loop. Stores a shared owner in the loop's
  // registration map, keeping the state alive as long as the fd is
  // registered, even if its `stream_conn_ptr` is destructed. (However, its
  // destruction will start a forceful close.)
  //
  // In connecting mode, arms `EPOLLOUT` (so the first `on_writable()`,
  // `on_readable()`, or `on_error()` can call `handle_connect()`) and
  // suppresses `EPOLLIN` until the connect resolves. In connected mode,
  // derives initial read/write interest from `wants_read_events()` and
  // `send_queue_` so that handlers or queued sends registered before this call
  // are correctly armed.
  void register_with_loop() {
    if (!open_.load(std::memory_order::relaxed)) return;
    const bool want_write = connecting_ || !send_queue_.empty();
    // Suppress reads while connecting: `handle_connect()` calls
    // `refresh_read_interest()` once `SO_ERROR` confirms success.
    const bool want_read = !connecting_ && wants_read_events();
    if (loop_.register_socket(shared_from_this(), want_read, want_write))
      registered_.store(true, std::memory_order::relaxed);
    else
      do_close_now(close_mode::forceful);
  }

  // `io_conn` overrides: called on the loop thread by `dispatch_event`.
  void on_readable() override {
    assert(loop_.is_loop_thread());
    if (listening_) {
      handle_listen();
      return;
    }
    if (connecting_) {
      handle_connect();
      return;
    }
    handle_readable();
  }

  void on_writable() override {
    assert(loop_.is_loop_thread());
    if (listening_) return;
    if (connecting_) {
      handle_connect();
      return;
    }
    flush_send_queue();
  }

  void on_error() override {
    assert(loop_.is_loop_thread());
    if (!open_.load(std::memory_order::relaxed)) return;
    if (listening_) {
      do_close_now(close_mode::forceful);
      return;
    }
    if (connecting_) {
      handle_connect();
      return;
    }
    if (read_open_.load(std::memory_order::relaxed)) {
      if (wants_read_events()) {
        (void)handle_readable();
      } else {
        char byte = '\0';
        const ssize_t peeked = sock().recv(&byte, 1, MSG_PEEK | MSG_DONTWAIT);
        if (peeked == 0) {
          handle_read_eof();
        } else if (peeked < 0 && os_file::is_hard_error()) {
          do_close_now(close_mode::forceful);
        }
      }
    } else {
      maybe_finish_after_side_close();
    }
  }

  // Run `fn` immediately if on the loop thread and already registered with the
  // loop, otherwise post it. Mirrors `epoll_loop::execute_or_post` but guards
  // against running inline before `register_with_loop()` has executed, which
  // would cause silently failed attempts at epoll mutations (e.g.,
  // `set_writable`) on an unregistered fd.
  template<typename FN>
  bool execute_or_post(FN&& fn) {
    if (loop_.is_loop_thread() && registered_.load(std::memory_order::relaxed))
      return fn();
    loop_.post(std::forward<FN>(fn));
    return true;
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
  bool exec_lambda(execution exec, FN&& fn) {
    const auto is_loop_thread = loop_.is_loop_thread();
    const auto registered = registered_.load(std::memory_order::relaxed);
    if (is_loop_thread && registered) return fn();
    if (exec == execution::nonblocking || is_loop_thread || !registered) {
      loop_.post(std::forward<FN>(fn));
      return true;
    }
    return loop_.post_and_wait(std::forward<FN>(fn));
  }

  // Read interest is needed while in listening mode (always) or while the
  // active `on_data` handler is installed.
  [[nodiscard]] bool wants_read_events() const noexcept {
    assert(loop_.is_loop_thread());
    if (!open_.load(std::memory_order::relaxed)) return false;
    if (listening_) return true;
    return (read_open_.load(std::memory_order::relaxed) &&
            !!active_handlers_.load(std::memory_order::acquire)->on_data);
  }

  // Apply the current read-interest policy to the loop without disturbing
  // the always-armed error/hangup notifications.
  void refresh_read_interest() {
    assert(loop_.is_loop_thread());
    if (!open_.load(std::memory_order::relaxed)) return;
    (void)loop_.set_readable(sock(), wants_read_events());
  }

  // Deliver `on_close` at most once for the lifetime of the connection.
  void notify_close_once() {
    assert(loop_.is_loop_thread());
    if (close_notified_) return;
    close_notified_ = true;
    auto* h = active_handlers_.load(std::memory_order::acquire);
    if (h->on_close) h->on_close(*this);
  }

  // Deliver newly read data to the active `on_data` handler (which may be
  // `own_handlers_.on_data` or one installed by an `async_conn_base`).
  void notify_read_ready() {
    assert(loop_.is_loop_thread());
    auto* h = active_handlers_.load(std::memory_order::acquire);
    if (h->on_data) h->on_data(*this, recv_buf_);
    refresh_read_interest();
  }

  // Report read-side closure to any active `on_data` handler. An empty
  // `recv_buf_` signals closure to handlers that distinguish it from real
  // data.
  void notify_read_closed() {
    assert(loop_.is_loop_thread());
    auto* h = active_handlers_.load(std::memory_order::acquire);
    if (h->on_data) {
      recv_buf_.clear();
      h->on_data(*this, recv_buf_);
    }
    refresh_read_interest();
  }

  // Notify that all queued outbound data has been fully drained.
  void notify_drained() {
    auto* h = active_handlers_.load(std::memory_order::acquire);
    if (h->on_drain) h->on_drain(*this);
  }

  // Shut down the local read side and notify any blocked read waiter.
  [[nodiscard]] bool do_shutdown_read() {
    assert(loop_.is_loop_thread());
    if (!open_.load(std::memory_order::relaxed) ||
        !read_open_.load(std::memory_order::relaxed))
      return false;
    if (!sock().shutdown(SHUT_RD)) {
      do_close_now(close_mode::forceful);
      return false;
    }
    read_open_.store(false, std::memory_order::relaxed);
    loop_.set_readable(sock(), false);
    // Notify any `async_conn_base` that may have a pending read waiter.
    // `notify_read_closed()` calls `active_handlers_->on_data` with an empty
    // buffer; persistent own handlers use `on_close` instead.
    if (active_handlers_.load(std::memory_order::relaxed) != &own_handlers_)
      notify_read_closed();
    maybe_finish_after_side_close();
    return true;
  }

  // Shut down the local write side and discard any unsent outbound data.
  [[nodiscard]] bool do_shutdown_write() {
    assert(loop_.is_loop_thread());
    if (!open_.load(std::memory_order::relaxed) ||
        !write_open_.load(std::memory_order::relaxed))
      return false;
    if (!sock().shutdown(SHUT_WR)) {
      do_close_now(close_mode::forceful);
      return false;
    }
    write_open_.store(false, std::memory_order::relaxed);
    send_queue_.clear();
    head_span_ = {};
    loop_.set_writable(sock(), false);
    maybe_finish_after_side_close();
    return true;
  }

  // Fully close once both local directions have been shut down.
  void maybe_finish_after_side_close(close_mode mode = close_mode::graceful) {
    assert(loop_.is_loop_thread());
    if (!read_open_.load(std::memory_order::relaxed) &&
        !write_open_.load(std::memory_order::relaxed))
      do_close_now(mode);
  }

  // Handle EOF by closing the read side and finishing any pending close.
  //
  // Disarms both `EPOLLIN` and `EPOLLRDHUP` immediately: after EOF, the peer
  // has done `SHUT_WR`, so `EPOLLRDHUP` would otherwise remain
  // level-triggered and repeatedly wake the loop while the write side is
  // still open.
  void handle_read_eof() {
    assert(loop_.is_loop_thread());
    read_open_.store(false, std::memory_order::relaxed);
    loop_.set_readable(sock(), false);
    loop_.set_rdhup(sock(), false);
    if (active_handlers_.load(std::memory_order::acquire) != &own_handlers_)
      notify_read_closed();
    notify_close_once();
    if (close_requested_ && send_queue_.empty()) {
      do_close_now();
      return;
    }
    // If no `on_close` handler is installed, initiate a graceful close so
    // that connections without an external owner are fully torn down rather
    // than left half-open indefinitely. Connections that need the write side
    // after peer EOF must install an `on_close` handler and call `close()` or
    // `hangup()` only when done.
    if (!active_handlers_.load(std::memory_order::acquire)->on_close) {
      do_close();
      return;
    }
    maybe_finish_after_side_close();
  }

  // Handle write failure by aborting queued sends and closing as needed.
  void handle_write_failure() {
    assert(loop_.is_loop_thread());
    if (!write_open_.exchange(false, std::memory_order::relaxed)) return;
    send_queue_.clear();
    head_span_ = {};
    loop_.set_writable(sock(), false);
    if (close_requested_ || !read_open_.load(std::memory_order::relaxed)) {
      do_close_now(close_mode::forceful);
      return;
    }
    maybe_finish_after_side_close(close_mode::forceful);
  }

  // Resolve a pending async connect by checking `SO_ERROR`.
  //
  // On failure, closes the connection (notifying any pending read waiter and
  // firing `on_close`).
  //
  // On success, transitions to connected state: arms `EPOLLIN` if a read
  // waiter or `on_data` handler is active. If no sends are queued, disarms
  // `EPOLLOUT` and fires `notify_drained()` immediately — this is how the
  // caller learns the connection is established. If sends were queued before
  // the connect resolved, `EPOLLOUT` stays armed; `notify_drained()` fires
  // once the queue drains.
  //
  // Called from `on_readable()`, `on_writable()`, and `on_error()` while
  // `connecting_` is set.
  void handle_connect() {
    assert(loop_.is_loop_thread());
    assert(connecting_);
    connecting_ = false;

    const auto err = sock().get_option<int>(SOL_SOCKET, SO_ERROR);
    if (!err || *err != 0) {
      do_close_now(close_mode::forceful);
      return;
    }

    // Arm reads if an `on_data` handler is installed.
    refresh_read_interest();

    if (send_queue_.empty()) {
      // No pending sends: disarm `EPOLLOUT` and signal that writes are open.
      loop_.set_writable(sock(), false);
      notify_drained();
    }
    // Otherwise `EPOLLOUT` is already armed; the next `on_writable` will
    // call `flush_send_queue`, which fires `notify_drained` when done.
  }

  // Drain all pending connections. Each accepted connection gets a copy of
  // the listener's handlers and is immediately registered with the loop as a
  // self-owning connection.
  void handle_listen() {
    assert(loop_.is_loop_thread());
    while (true) {
      auto accepted = sock().accept();
      if (!accepted) break;
      auto peer = std::make_shared<stream_conn>(allow::ctor, loop_,
          std::move(accepted->first), net_endpoint{accepted->second},
          stream_conn_handlers{own_handlers_}, default_recv_buf_size,
          /*connecting=*/false, /*listening=*/false);
      peer->register_with_loop();
    }
  }

  // Read available data into `recv_buf_` without zero-initializing it
  // (C++23 `resize_and_overwrite`), then deliver to any pending read waiter.
  bool handle_readable() {
    if (!read_open_.load(std::memory_order::relaxed)) return false;

    const auto recv_buf_capacity =
        recv_buf_capacity_.load(std::memory_order::relaxed);
    no_zero::rightsize_to(recv_buf_, recv_buf_capacity, recv_buf_capacity * 2);

    if (!sock().recv(recv_buf_)) {
      // Distinguish between EOF and error.
      if (recv_buf_.empty())
        do_close_now(close_mode::forceful);
      else
        handle_read_eof();

      return false;
    }

    // If we read something, notify the user.
    if (!recv_buf_.empty()) notify_read_ready();
    return true;
  }

  // Enqueue `their_buf` for sending. Attempts an immediate `::send` when the
  // queue is empty; if any bytes remain (partial write or EAGAIN) they are
  // pushed onto `send_queue_` and tracked by `head_span_`. `EPOLLOUT` is
  // armed when the queue becomes non-empty; `flush_send_queue()` drains it
  // on subsequent `EPOLLOUT` events.
  bool enqueue_send(std::string&& their_buf) {
    assert(loop_.is_loop_thread());
    // Their buf is our buf now.
    auto buf = std::move(their_buf);

    if (!open_.load(std::memory_order::relaxed) ||
        !write_open_.load(std::memory_order::relaxed))
      return false;

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
    if (!sock().send(buf_view)) {
      handle_write_failure();
      return false;
    }

    // If fully sent, nothing to queue.
    if (buf_view.empty()) {
      notify_drained();
      return true;
    }

    // If we couldn't send all of it, push to queue and arm `EPOLLOUT`.
    // Note: We don't reuse `buf_view` because, in principle, moving a string
    // could change the buffer location (such as with SSO).
    const size_t sent = buf.size() - buf_view.size();
    send_queue_.push_back(std::move(buf));
    head_span_ = send_queue_.front();
    head_span_.remove_prefix(sent);
    loop_.set_writable(sock());
    return true;
  }

  // Drain `send_queue_` as far as `::write` allows, advancing `head_span_`.
  // When a string is fully sent, `pop_front()` destroys it immediately.
  // Disarms `EPOLLOUT` and notifies any pending write waiter when the queue
  // empties (or calls `do_close_now()` if a graceful close was requested).
  bool flush_send_queue() {
    // Guard against being called after `do_close_now()` (e.g., when both
    // `EPOLLIN` and `EPOLLOUT` fire in the same event and the readable
    // handler closes the connection before we get here).
    if (!open_.load(std::memory_order::relaxed)) return false;

    // NOTE: It would be cool if we could gather all the buffers and write
    // them in a single call...

    // Write until we're out of data, are blocked, or fail.
    while (!send_queue_.empty()) {
      // If we can't write at all, close immediately.
      if (!sock().send(head_span_)) {
        handle_write_failure();
        return false;
      }

      // If we weren't able to write the whole buffer, try later; keep
      // `EPOLLOUT` armed.
      if (!head_span_.empty()) return true;

      // If all gone, move on to the next string in the queue.
      send_queue_.pop_front();
      if (!send_queue_.empty()) head_span_ = send_queue_.front();
    }

    // Queue fully drained, so no need to keep `EPOLLOUT` armed.
    loop_.set_writable(sock(), false);

    // If we were waiting to close, do so now. Notify any `async_conn_base`
    // write waiter of the successful drain before closing; bare persistent
    // handlers receive only `on_close`.
    if (close_requested_) {
      if (active_handlers_.load(std::memory_order::acquire) != &own_handlers_)
        notify_drained();
      do_close_now();
      return false;
    }

    // Notify any pending write waiter that all outbound data has drained.
    notify_drained();
    return true;
  }

  // Graceful close: if data is pending let `flush_send_queue()` finish
  // first.
  void do_close() {
    assert(loop_.is_loop_thread());
    if (!open_.load(std::memory_order::relaxed)) return;
    close_requested_ = true;
    if (send_queue_.empty()) do_close_now();
  }

  // Forceful close: discard pending sends and close immediately.
  bool do_hangup() {
    assert(loop_.is_loop_thread());
    if (!open_.load(std::memory_order::relaxed)) return false;
    // TODO: Deal with redundancy between this and do_close_now().
    send_queue_.clear();
    head_span_ = {};
    close_requested_ = false;

    do_close_now(close_mode::forceful);
    return true;
  }

  // Unconditional close. Idempotent via `open_.exchange(false)`.
  void do_close_now(close_mode mode = close_mode::graceful) {
    assert(loop_.is_loop_thread());
    if (!open_.exchange(false)) return;

    read_open_.store(false, std::memory_order::relaxed);
    write_open_.store(false, std::memory_order::relaxed);

    loop_.unregister_socket(sock());
    sock().close(mode);

    send_queue_.clear();
    head_span_ = {};
    close_requested_ = false;

    // `on_close` notifies any pending `async_conn_base` waiters (coro or cb).
    // The handler is always posted-resume, never inline, so any in-progress
    // `await_suspend` on the call stack has returned before the coroutine
    // continues -- preventing use-after-free when `do_close_now` fires from
    // within `enqueue_send` -> `await_suspend`.
    notify_close_once();
  }
};

// A move-only smart pointer that owns a `stream_conn`. Despite being
// implemented with a `shared_ptr`, a `stream_conn_ptr` fully owns the
// `stream_conn` and removes it from the `epoll_loop` on destruction.
//
// The only public methods are factory creators (`adopt`, `connect`, `listen`),
// `close()`, `release()`, and `operator->`, which provides access to the full
// `stream_conn` API.
class stream_conn_ptr {
public:
  // Default constructor — creates an empty (invalid) handle. `operator bool`
  // returns false.
  stream_conn_ptr() noexcept = default;

  stream_conn_ptr(stream_conn_ptr&& rhs) noexcept = default;
  stream_conn_ptr(const stream_conn_ptr&) = delete;

  stream_conn_ptr& operator=(stream_conn_ptr&& rhs) = default;
  stream_conn_ptr& operator=(const stream_conn_ptr&) = delete;

  // Performs `hangup()` on destruction. If you want to close cleanly, you must
  // call `close()` before the instance is destructed.
  ~stream_conn_ptr() {
    try {
      if (!conn_ ||
          conn_->graceful_close_started_.load(std::memory_order::relaxed))
        return;
      (void)conn_->loop_.execute_or_post([p = std::move(conn_)] {
        p->do_hangup();
        return true;
      });
    } // NOLINTNEXTLINE(bugprone-empty-catch)
    catch (...) {
      // Don't let exceptions escape the destructor.
    }
  }

  // Return a const reference to the underlying `shared_ptr` without releasing
  // ownership. The caller may copy it to extend the connection's lifetime
  // beyond that of this `stream_conn_ptr`.
  [[nodiscard]] const std::shared_ptr<stream_conn>& pointer() const {
    return conn_;
  }

  // Release ownership of the connection. The connection remains registered
  // with the loop and is responsible for itself.
  std::shared_ptr<stream_conn> release() { return std::move(conn_); }

  // Adopt an already-connected, non-blocking `sock` and register it with
  // `loop`. `remote` records the peer address for diagnostics. May be called
  // from any thread. Socket must be nonblocking.
  [[nodiscard]] static stream_conn_ptr adopt(epoll_loop& loop,
      net_socket&& sock, const net_endpoint& remote,
      stream_conn_handlers&& h = {},
      size_t recv_buf_size = stream_conn::default_recv_buf_size) {
    assert((sock.get_flags().value_or(0) & O_NONBLOCK) != 0);
    return stream_conn_ptr{loop, std::move(sock), remote, std::move(h),
        recv_buf_size, /*connecting=*/false, /*listening=*/false};
  }

  // Initiate an async connection to `remote`. Creates a non-blocking socket
  // matching `remote`'s address family, optionally binds the local end to
  // `local` (if non-empty), then calls `connect(2)`. The socket is registered
  // with `EPOLLOUT` if `connect(2)` yields `EINPROGRESS`; the first
  // `on_writable()`, `on_readable()`, or `on_error()` then calls
  // `handle_connect()`, which either transitions to connected state (notifying
  // any pending write waiter) or closes the connection (notifying any pending
  // read waiter and firing `on_close`). If `connect(2)` succeeds immediately
  // (e.g., loopback or Unix-domain sockets), a posted task fires
  // `notify_drained()` directly after registration.
  //
  // Returns an invalid (empty) handle if the socket cannot be created, `local`
  // cannot be bound, or `connect(2)` fails synchronously; `errno` is set by
  // the failing syscall. Async failure (e.g., `ECONNREFUSED` delivered later)
  // goes through the normal close path.
  [[nodiscard]] static stream_conn_ptr connect(epoll_loop& loop,
      const net_endpoint& remote, stream_conn_handlers&& h = {},
      const net_endpoint& local = net_endpoint::invalid,
      size_t recv_buf_size = stream_conn::default_recv_buf_size) {
    auto sock = net_socket::create_for(remote);
    if (!sock.is_open()) return {};

    if (local && !sock.bind(local)) return {};

    const auto result = sock.connect(remote);
    if (result && !*result) return {};
    const auto still_connecting = !result;

    auto ptr = stream_conn_ptr{loop, std::move(sock), remote, std::move(h),
        recv_buf_size, /*connecting=*/still_connecting, /*listening=*/false};

    // When `connect(2)` succeeds immediately, `handle_connect()` never fires
    // via `EPOLLOUT`. Post a follow-up task to mirror its success path:
    // `register_with_loop()` already armed `EPOLLIN` correctly; all that
    // remains is to signal readiness via `notify_drained()` if no sends are
    // pending (otherwise `flush_send_queue()` will call it when the queue
    // drains, as in the normal data path).
    if (!still_connecting) {
      loop.post([p = ptr.conn_] {
        if (p->open_.load(std::memory_order::relaxed) &&
            p->send_queue_.empty())
          p->notify_drained();
      });
    }

    return ptr;
  }

  // Create a non-blocking listening socket bound to `local`, register it with
  // `loop`, and return a handle. `EPOLLIN` events drain all pending
  // connections via `accept4`; each creates a self-owning `stream_conn` with a
  // copy of `h`. Returns an invalid handle if socket creation, `SO_REUSEADDR`,
  // `bind`, or `listen(2)` fails; `errno` is set by the failing syscall.
  // `reuse_port` enables `SO_REUSEPORT` for multi-process load balancing.
  [[nodiscard]] static stream_conn_ptr listen(epoll_loop& loop,
      const net_endpoint& local, stream_conn_handlers&& h = {},
      bool reuse_port = false) {
    auto sock = net_socket::create_for(local);
    if (!sock.is_open()) return {};

    if (!sock.set_reuse_addr()) return {};
    if (reuse_port && !sock.set_reuse_port()) return {};
    if (!sock.bind(local)) return {};
    if (!sock.listen()) return {};

    return stream_conn_ptr{loop, std::move(sock), net_endpoint::invalid,
        std::move(h), stream_conn::default_recv_buf_size,
        /*connecting=*/false, /*listening=*/true};
  }

  // Start a graceful close. Drains pending sends first, then shuts down the
  // socket. Safe to call from any thread. Once this is called, destructing
  // this object does not cause a forceful close.
  bool close() {
    if (!conn_) return false;
    return conn_->close();
  }

  // Access the underlying `stream_conn` directly.
  stream_conn* operator->() noexcept { return conn_.get(); }
  const stream_conn* operator->() const noexcept { return conn_.get(); }

  // True if this handle is non-empty (i.e., holds a connection).
  explicit operator bool() const noexcept { return conn_ != nullptr; }

private:
  // Private constructor used by `connect()`, `listen()`, and `adopt()`.
  explicit stream_conn_ptr(epoll_loop& loop, net_socket&& sock,
      const net_endpoint& remote, stream_conn_handlers&& h,
      size_t recv_buf_size, bool connecting, bool listening) {
    assert((sock.get_flags().value_or(0) & O_NONBLOCK) != 0);
    if (recv_buf_size == 0) recv_buf_size = stream_conn::default_recv_buf_size;
    conn_ = std::make_shared<stream_conn>(stream_conn::allow::ctor, loop,
        std::move(sock), remote, std::move(h), recv_buf_size, connecting,
        listening);
    loop.post([p = conn_] { p->register_with_loop(); });
  }

  std::shared_ptr<stream_conn> conn_;
};

}} // namespace corvid::proto
