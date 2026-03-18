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
#include <coroutine>
#include <cstddef>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>

#include <unistd.h>

#include "epoll_loop.h"
#include "loop_task.h"
#include "net_endpoint.h"
#include "../strings/no_zero.h"

namespace corvid { inline namespace proto {

using namespace corvid::strings::no_zero_funcs;

// User-supplied persistent callbacks for a `stream_conn`. All fields are
// optional; a null handler is silently skipped when its event fires.
//
//  `on_data(str)`  -- fired when data arrives. `str` is the internal receive
//                     buffer resized to the number of bytes read; the user may
//                     `std::move` from it to steal the allocation without an
//                     extra copy. The reference is valid only during the call.
//  `on_drain()`    -- fired when a `send()` finishes with no outbound bytes
//                     left pending. This includes both immediate writes and
//                     buffered (`EPOLLOUT`-driven) drains.
//  `on_close()`    -- fired once when the read side closes: either peer EOF or
//                     an I/O error. Writes may still be possible (half-close);
//                     call `close()` or `hangup()` to shut down fully.
struct stream_conn_handlers {
  std::function<void(std::string&)> on_data = nullptr;
  std::function<void()> on_drain = nullptr;
  std::function<void()> on_close = nullptr;
};

// A non-blocking connected stream socket driven by an `epoll_loop`.
//
// `stream_conn` is a movable handle that wraps a `shared_ptr` to internal state
// (`state`). Note that, despite using `shared_ptr`, a `stream_conn` fully owns
// the `state` and removes it from the `epoll_loop` on close.
//
// The state also serves as the `io_conn` registered with the loop, so there is
// exactly ONE heap allocation per connection: no separate `io_handlers`
// lambdas and no second `impl` object.
//
// Thread safety: `send()`, `close()`, `hangup()`, and the destructor are safe
// to call from any thread. They route work to the loop via
// `epoll_loop::post()`. All actual I/O and epoll-mask mutations run
// exclusively on the loop thread.
//
// Send path: `send(std::string&&)` takes ownership of the caller's string.
// If the send queue is empty, an immediate `::write` is attempted. If all
// bytes are written, the string is discarded and `on_drain` fires immediately
// because no outbound bytes remain pending. Otherwise the string is pushed
// onto `send_queue_` (a `std::deque<std::string>`) and `head_span_` is set to
// the unsent tail; `EPOLLOUT` is armed. Subsequent `EPOLLOUT` events drive
// `do_flush_send_buf()`. When the queue empties, `EPOLLOUT` is disarmed and
// `on_drain` fires. `std::deque` is used instead of `std::vector` because
// `push_back` does not move existing elements, keeping `head_span_` (which
// points into `front()`) valid.
//
// Receive path: `stream_conn` arms `EPOLLIN` only when a persistent `on_data`
// handler exists or a one-shot read waiter is pending. When `EPOLLIN` fires,
// the receive buffer is resized to `recv_buf_capacity_` bytes via
// `resize_and_overwrite` (C++23, no zero-initialization), filled by `::read`,
// then trimmed (without reallocating) to the actual count before delivery. The
// user may `std::move` from the buffer to steal its allocation; the next read
// reallocates if needed.
//
// Close: `close()` is graceful. If the send queue is non-empty, the socket
// closes only after it drains. `hangup()` discards pending outbound data and
// closes immediately. The destructor uses `hangup()`.
//
// Supports three async models:
// 1. Persistent callbacks via `stream_conn_handlers`.
// 2. Coroutine waiters via `async_read()` / `async_send()`.
// 3. One-shot callbacks via `async_cb_read()` / `async_cb_write()`.
//
// Precedence is per-direction (read vs. write):
// - If a one-shot waiter is pending, it gets the next event for that
//   direction.
// - Coroutine waiters and per-call callback waiters share the same one-shot
//   waiter slot, so they are mutually exclusive.
// - Persistent handlers (`on_data`, `on_drain`, `on_close`) are fallback
//   notifications used only when no one-shot waiter is pending for that
//   direction.
//
// Parallel attempts fail cleanly rather than replacing an existing waiter:
// - `async_cb_read()` / `async_cb_write()` return `false`.
// - `async_read()` / `async_send()` require the caller to avoid overlap; a
//   second concurrent wait is a programming error (asserted in debug builds).
//
// A pending read callback completes with an empty buffer if the read side
// closes before data arrives. The callback can distinguish that case by
// retaining access to the `stream_conn` and checking `can_read()` / `is_open()`.
// A pending write waiter completes on either success or failure: coroutine
// sends resume, while per-call callback sends receive `cb(bool completed)`.
//
class stream_conn {
public:
  using async_read_cb = std::function<void(std::string&)>;
  using async_write_cb = std::function<void(bool completed)>;

  // Default receive-buffer capacity per connection, in bytes.
  static constexpr size_t default_recv_buf_size = 16384;

  // Construct a connection from `sock` (must already be non-blocking) and post
  // its registration with `loop`. `remote` records the peer address for
  // diagnostics. May be called from any thread.
  explicit stream_conn(epoll_loop& loop, net_socket&& sock,
      const net_endpoint& remote, stream_conn_handlers&& h = {},
      size_t recv_buf_size = default_recv_buf_size) {
    assert((sock.get_flags().value_or(0) & O_NONBLOCK) != 0);
    if (recv_buf_size == 0) recv_buf_size = default_recv_buf_size;
    state_ = std::make_shared<state>(loop, std::move(sock), remote,
        std::move(h), recv_buf_size);
    loop.post([p = state_] { p->register_with_loop(); });
  }

  stream_conn(stream_conn&&) noexcept = default;
  stream_conn(const stream_conn&) = delete;

  stream_conn& operator=(stream_conn&&) noexcept = default;
  stream_conn& operator=(const stream_conn&) = delete;

  // Performs `hangup()` on destruction. If you want to close cleanly, you must
  // call `close()` before the instance is destructed.
  ~stream_conn() {
    try {
      if (!state_) return;
      if (state_->graceful_close_started_) return;
      (void)state_->loop_.execute_or_post([p = std::move(state_)] {
        p->do_hangup();
        return true;
      });
    } // NOLINTNEXTLINE(bugprone-empty-catch)
    catch (...) {
      // Don't let exceptions escape the destructor.
    }
  }

  // True if the connection has not yet been closed.
  [[nodiscard]] bool is_open() const noexcept {
    return state_ && state_->open_.load(std::memory_order::relaxed);
  }

  // Change the per-connection receive buffer size used for future reads.
  // `bytes` must be non-zero. Safe to call from any thread. Returns false if
  // the handle is empty or `bytes == 0`.
  bool set_recv_buf_size(size_t bytes) {
    if (!state_ || bytes == 0) return false;
    state_->recv_buf_capacity_.store(bytes, std::memory_order::relaxed);
    return true;
  }

  // The current per-connection receive buffer size used for future reads.
  // Safe to call from any thread.
  [[nodiscard]] size_t recv_buf_size() const noexcept {
    if (!state_) return 0;
    return state_->recv_buf_capacity_.load(std::memory_order::relaxed);
  }

  // Whether the read side is still open. Safe to call from any thread. Returns
  // false if the connection is closed or the read side has been shut down.
  [[nodiscard]] bool can_read() const noexcept {
    return state_ && state_->read_open_.load(std::memory_order::relaxed);
  }

  // Whether the write side is still open. Safe to call from any thread.
  // Returns false if the connection is closed or the write side has been shut
  // down.
  [[nodiscard]] bool can_write() const noexcept {
    return state_ && state_->write_open_.load(std::memory_order::relaxed);
  }

  // The remote peer address supplied at construction. Requires a valid
  // connection. Safe to call from any thread.
  [[nodiscard]] const net_endpoint& remote_endpoint() const noexcept {
    if (!state_) return net_endpoint::invalid;
    return state_->remote_;
  }

  // Take ownership of `buf` and start sending it. Safe to call from any
  // thread.
  bool send(std::string&& buf) {
    if (!state_ || buf.empty()) return false;
    if (!state_->open_.load(std::memory_order::relaxed)) return false;
    if (!state_->write_open_.load(std::memory_order::relaxed)) return false;
    return state_->loop_.execute_or_post(
        [p = state_, b = std::move(buf)]() mutable {
          p->enqueue_send(std::move(b));
          return true;
        });
  }

  // Start a graceful close. Drains pending sends first, then shuts down the
  // socket. Safe to call from any thread. Once this is called, destructing the
  // object does not cause a rude close.
  bool close() {
    if (!state_) return false;
    state_->graceful_close_started_ = true;
    state_->loop_.post([p = state_] { p->do_close(); });
    return true;
  }

  // Shut down the local read side while keeping the write side available.
  // Safe to call from any thread.
  bool shutdown_read(execution exec = execution::blocking) {
    if (!state_) return false;
    if (!state_->open_.load(std::memory_order::relaxed)) return false;
    return exec_lambda(exec, [p = state_] { return p->do_shutdown_read(); });
  }

  // Shut down the local write side while keeping the read side available.
  // Safe to call from any thread.
  bool shutdown_write(execution exec = execution::blocking) {
    if (!state_) return false;
    if (!state_->open_.load(std::memory_order::relaxed)) return false;
    return exec_lambda(exec, [p = state_] { return p->do_shutdown_write(); });
  }

  // Post a forceful close to the loop. Pending sends are discarded and
  // SO_LINGER is disabled. Safe to call from any thread.
  bool hangup() {
    if (!state_) return false;
    return state_->loop_.execute_or_post([p = state_] {
      return p->do_hangup();
    });
  }

  // Return an awaitable that suspends the calling coroutine until one batch
  // of data arrives or the connection closes. Returns the received bytes as a
  // `std::string`; an empty string signals that the connection was already
  // closed or closed before data arrived. The caller should check
  // `is_open()` to distinguish a closed connection from a zero-byte read.
  //
  // At most one read waiter (`async_read` or `async_cb_read`) may be
  // outstanding at a time.
  // Must be awaited from the loop thread (e.g., inside a `loop_task`
  // coroutine spawned from a loop callback or `post()`'d function).
  [[nodiscard]] auto async_read() noexcept {
    return state::async_read_awaitable{state_.get()};
  }

  // Return an awaitable that takes ownership of `buf`, enqueues it for
  // sending, and suspends the calling coroutine until the send queue fully
  // drains (or the connection closes). If `buf` is written synchronously
  // without queuing, the coroutine resumes immediately without a loop
  // round-trip.
  //
  // At most one write waiter (`async_send` or `async_cb_write`) may be
  // outstanding at a time.
  // Must be awaited from the loop thread.
  [[nodiscard]] auto async_send(std::string&& buf) noexcept {
    return state::async_send_awaitable{state_.get(), std::move(buf)};
  }

  // Register a one-shot callback for the next chunk of readable data. Callback
  // delivery is asynchronous. However, when used outside the loop thread and
  // with `execution::blocking`, this function will block until the I/O is
  // registered, giving it a chance to fail immediately if another read
  // waiter is pending.
  //
  // The callback, `cb`, is invoked inline on the loop thread with the
  // internal receive buffer so that the caller may copy or move it.
  //
  // Returns false if the connection is closed, `cb` is null, or another
  // async read waiter is already pending. If the connection closes before
  // data arrives, `cb` is invoked with an empty string; code that retains
  // access to the `stream_conn` can call `is_open()` to distinguish that case
  // from real data.
  bool async_cb_read(async_read_cb cb, execution exec = execution::blocking) {
    if (!state_ || !cb) return false;
    if (!state_->open_.load(std::memory_order::relaxed)) return false;
    if (!state_->read_open_.load(std::memory_order::relaxed)) return false;
    return exec_lambda(exec, [p = state_, cb = std::move(cb)]() mutable {
      return p->register_async_cb_read(std::move(cb));
    });
  }

  // Register to write the data in `buf` and receive a one-shot callback when
  // all data has been written. Callback delivery is asynchronous. However,
  // when used outside the loop thread and with `execution::blocking`, this
  // function will block until the I/O is registered, giving it a chance to
  // fail immediately if another write waiter is pending.
  //
  // Takes ownership of `buf`, sends it, and invoke `cb` afterwards. When the
  // callback gets `completed == true`, that means the buffered write fully
  // drained; `completed == false` means the connection closed or failed before
  // all bytes were sent, possibly after a partial write
  //
  // Returns false if the connection is closed, `buf` is empty, `cb` is null,
  // or another async write waiter is already pending.
  bool async_cb_write(std::string&& buf, async_write_cb cb,
      execution exec = execution::blocking) {
    if (!state_ || buf.empty() || !cb) return false;
    if (!state_->open_.load(std::memory_order::relaxed)) return false;
    if (!state_->write_open_.load(std::memory_order::relaxed)) return false;
    return exec_lambda(exec,
        [p = state_, b = std::move(buf), cb = std::move(cb)]() mutable {
          return p->register_async_cb_write(std::move(b), std::move(cb));
        });
  }

private:
  // Internal state. Inherits from `io_conn` so it can be registered directly
  // with the loop.
  struct state final: io_conn {
    // One pending read completion, satisfied by either a coroutine or
    // callback.
    struct pending_read_op {
      std::coroutine_handle<> coro;
      async_read_cb cb;

      [[nodiscard]] bool has_waiter() const noexcept {
        assert(!coro || !cb);
        return coro || static_cast<bool>(cb);
      }
    };

    // One pending write completion, satisfied by either a coroutine or
    // callback.  Normally, only `coro` or `cb` are set.
    struct pending_write_op {
      std::coroutine_handle<> coro;
      async_write_cb cb;

      [[nodiscard]] bool has_waiter() const noexcept {
        assert(!coro || !cb);
        return coro || static_cast<bool>(cb);
      }
    };

    epoll_loop& loop_;
    net_endpoint remote_;
    stream_conn_handlers handlers_;

    // Outbound queue. `std::deque` is used because its `push_back` does not
    // move existing elements, so `head_span_` (pointing into `front()`) stays
    // valid even as new strings are appended. Each string is destroyed by
    // `pop_front()` as soon as it is fully sent.
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

    // Size to expand `recv_buf_` to before each read.
    std::atomic<size_t> recv_buf_capacity_{default_recv_buf_size};

    // Cleared atomically by `do_close_now()`. Read from any thread via
    // `stream_conn::is_open()`.
    std::atomic_bool open_;

    //  Set by `close()` to start a graceful close, preventing the destructor
    //  from closing rudely.
    std::atomic_bool graceful_close_started_{false};

    // Whether the read and write sides of the socket are open.
    std::atomic_bool read_open_{true};
    std::atomic_bool write_open_{true};

    // Set by `do_close()` to request a full close after pending writes drain.
    bool close_requested_ = false;

    // Set by `notify_close_once` to ensure `on_close` is delivered at most
    // once.
    bool close_notified_ = false;

    // One-shot read waiter installed by either `async_read()` or
    // `async_cb_read()`.
    pending_read_op pending_read_;

    // Receive staging buffer filled by `handle_readable()`. Delivered directly
    // to callback-based readers (`async_cb_read()` or `handlers_.on_data`), or
    // moved into `pending_read_data_` to resume a coroutine waiting in
    // `async_read()`.
    std::string pending_read_data_;

    // One-shot write waiter installed by either `async_send()` or
    // `async_cb_write()`.
    pending_write_op pending_write_;

    explicit state(epoll_loop& loop, net_socket&& sock,
        const net_endpoint& remote, stream_conn_handlers&& h, size_t rbs) noexcept
        : io_conn{std::move(sock)}, loop_{loop}, remote_{remote},
          handlers_{std::move(h)}, recv_buf_capacity_{rbs}, open_{true} {}

    // Read interest is needed only while callback mode is always listening or
    // a one-shot read waiter is actively awaiting the next chunk.
    [[nodiscard]] bool wants_read_events() const noexcept {
      return open_.load(std::memory_order::relaxed) &&
             read_open_.load(std::memory_order::relaxed) &&
             (handlers_.on_data || pending_read_.has_waiter());
    }

    // Apply the current read-interest policy to the loop without disturbing
    // the always-armed error/hangup notifications.
    void refresh_read_interest() {
      assert(loop_.is_loop_thread());
      if (!open_.load(std::memory_order::relaxed)) return;
      (void)loop_.set_readable(sock(), wants_read_events());
    }

    // Install a one-shot read callback if the read side is still available.
    [[nodiscard]] bool register_async_cb_read(async_read_cb cb) {
      assert(loop_.is_loop_thread());
      if (!open_.load(std::memory_order::relaxed) ||
          !read_open_.load(std::memory_order::relaxed) ||
          pending_read_.has_waiter())
        return false;
      pending_read_.cb = std::move(cb);
      refresh_read_interest();
      return true;
    }

    // Install a one-shot write callback and enqueue the outbound payload.
    [[nodiscard]] bool
    register_async_cb_write(std::string&& buf, async_write_cb cb) {
      assert(loop_.is_loop_thread());
      if (!open_.load(std::memory_order::relaxed) ||
          !write_open_.load(std::memory_order::relaxed) ||
          pending_write_.has_waiter())
        return false;
      pending_write_.cb = std::move(cb);
      if (enqueue_send(std::move(buf))) return true;
      if (pending_write_.cb) {
        auto failed_cb = std::move(pending_write_.cb);
        failed_cb(false);
      }
      return true;
    }

    // Deliver `on_close` at most once for the lifetime of the connection.
    void notify_close_once() {
      assert(loop_.is_loop_thread());
      if (close_notified_) return;
      close_notified_ = true;
      if (handlers_.on_close) handlers_.on_close();
    }

    // Deliver newly read data to the waiting coroutine, callback, or handler.
    void notify_read_ready() {
      if (pending_read_.coro) {
        pending_read_data_ = std::move(recv_buf_);
        loop_.post([h = std::exchange(pending_read_.coro, {})] {
          h.resume();
        });
      } else if (pending_read_.cb) {
        auto cb = std::move(pending_read_.cb);
        cb(recv_buf_);
      } else if (handlers_.on_data) {
        handlers_.on_data(recv_buf_);
      }
      refresh_read_interest();
    }

    // Report read-side closure to any pending one-shot read waiter.
    void notify_read_closed() {
      if (pending_read_.coro) {
        pending_read_data_.clear();
        loop_.post([h = std::exchange(pending_read_.coro, {})] {
          h.resume();
        });
      } else if (pending_read_.cb) {
        recv_buf_.clear();
        auto cb = std::move(pending_read_.cb);
        cb(recv_buf_);
      }
      refresh_read_interest();
    }

    // Fail any pending one-shot write waiter after a close or send failure.
    void fail_pending_write_waiters() {
      if (pending_write_.coro)
        loop_.post([h = std::exchange(pending_write_.coro, {})] {
          h.resume();
        });
      if (pending_write_.cb) {
        auto cb = std::move(pending_write_.cb);
        cb(false);
      }
    }

    // Notify that all queued outbound data has been fully drained.
    void notify_drained() {
      if (pending_write_.coro) {
        loop_.post([h = std::exchange(pending_write_.coro, {})] {
          h.resume();
        });
      } else if (pending_write_.cb) {
        auto cb = std::move(pending_write_.cb);
        cb(true);
      } else if (handlers_.on_drain) {
        handlers_.on_drain();
      }
    }

    // Complete any pending write waiter when a graceful close drains fully.
    void complete_pending_writes_after_drain() {
      if (pending_write_.has_waiter()) notify_drained();
    }

    // Shut down the local read side and notify any blocked read waiter.
    [[nodiscard]] bool do_shutdown_read() {
      if (!open_.load(std::memory_order::relaxed) ||
          !read_open_.load(std::memory_order::relaxed))
        return false;
      if (!sock().shutdown(SHUT_RD)) {
        do_close_now(close_mode::forceful);
        return false;
      }
      read_open_.store(false, std::memory_order::relaxed);
      loop_.set_readable(sock(), false);
      if (pending_read_.has_waiter()) notify_read_closed();
      maybe_finish_after_side_close();
      return true;
    }

    // Shut down the local write side and discard any unsent outbound data.
    [[nodiscard]] bool do_shutdown_write() {
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
      fail_pending_write_waiters();
      maybe_finish_after_side_close();
      return true;
    }

    // Fully close once both local directions have been shut down.
    void maybe_finish_after_side_close(
        close_mode mode = close_mode::graceful) {
      if (!read_open_.load(std::memory_order::relaxed) &&
          !write_open_.load(std::memory_order::relaxed))
        do_close_now(mode);
    }

    // Handle EOF by closing the read side and finishing any pending close.
    void handle_read_eof() {
      read_open_.store(false, std::memory_order::relaxed);
      if (pending_read_.has_waiter()) notify_read_closed();
      notify_close_once();
      if (close_requested_ && send_queue_.empty()) {
        do_close_now();
        return;
      }
      maybe_finish_after_side_close();
    }

    // Handle write failure by aborting queued sends and closing as needed.
    void handle_write_failure() {
      if (!write_open_.exchange(false, std::memory_order::relaxed)) return;
      send_queue_.clear();
      head_span_ = {};
      loop_.set_writable(sock(), false);
      fail_pending_write_waiters();
      if (close_requested_ || !read_open_.load(std::memory_order::relaxed)) {
        do_close_now(close_mode::forceful);
        return;
      }
      maybe_finish_after_side_close(close_mode::forceful);
    }

    // Register `net_socket` with the loop. Stores a shared owner in the loop's
    // registration map, keeping the state alive as long as the fd is
    // registered, even if its `stream_conn` is destructed.
    void register_with_loop() {
      if (!open_.load(std::memory_order::relaxed)) return;
      (void)loop_.register_socket(shared_from_this(),
          static_cast<bool>(handlers_.on_data), false);
    }

    // `io_conn` overrides: called on the loop thread by `dispatch_event`.
    void on_readable() override { handle_readable(); }
    void on_writable() override { do_flush_send_buf(); }
    void on_error() override {
      if (!open_.load(std::memory_order::relaxed)) return;
      if (read_open_.load(std::memory_order::relaxed) && wants_read_events()) {
        (void)handle_readable();
      } else if (read_open_.load(std::memory_order::relaxed)) {
        char byte = '\0';
        const ssize_t peeked = sock().recv(&byte, 1, MSG_PEEK | MSG_DONTWAIT);
        if (peeked == 0) {
          handle_read_eof();
        } else if (peeked < 0 && os_file::is_hard_error()) {
          do_close_now(close_mode::forceful);
        }
      } else {
        maybe_finish_after_side_close();
      }
    }

    // Read available data into `recv_buf_` without zero-initializing it
    // (C++23 `resize_and_overwrite`), then deliver to `on_data`.
    bool handle_readable() {
      if (!read_open_.load(std::memory_order::relaxed)) return false;

      // Defensive coding to placate whiny AIs.
      const auto recv_buf_capacity = std::min(
          recv_buf_capacity_.load(std::memory_order::relaxed),
          std::numeric_limits<std::size_t>::max() / 2);

      no_zero::rightsize_to(recv_buf_, recv_buf_capacity,
          recv_buf_capacity * 2);

      if (!sock().recv(recv_buf_)) {
        // Distinguish between EOF and error.
        if (recv_buf_.empty()) {
          do_close_now(close_mode::forceful);
        } else {
          handle_read_eof();
        }
        return false;
      }

      // If we read something, notify the user.
      if (!recv_buf_.empty()) notify_read_ready();
      return true;
    }

    // Enqueue `their_buf` for sending. Attempts an immediate `::send` when the
    // queue is empty; if any bytes remain (partial write or EAGAIN) they are
    // pushed onto `send_queue_` and tracked by `head_span_`. `EPOLLOUT` is
    // armed when the queue becomes non-empty; `do_flush_send_buf()` drains it
    // on subsequent `EPOLLOUT` events.
    bool enqueue_send(std::string&& their_buf) {
      assert(loop_.is_loop_thread());
      auto buf = std::move(their_buf);

      if (!open_.load(std::memory_order::relaxed) ||
          !write_open_.load(std::memory_order::relaxed))
        return false;

      // If there are writes queued ahead of us, just add this to the back:
      // `EPOLLOUT` already armed.
      if (!send_queue_.empty()) {
        send_queue_.push_back(std::move(buf));
        return true;
      }

      // Write as much as we can of the new buffer.
      auto buf_view = std::string_view{buf};
      if (!sock().send(buf_view)) {
        handle_write_failure();
        return false;
      }

      // If fully written, nothing to queue.
      if (buf_view.empty()) {
        // Let `async_send()` keep its inline-resume fast path for immediate
        // completion; callback-based waiters and persistent handlers still
        // notify synchronously here.
        if (pending_write_.coro) return true;
        notify_drained();
        return true;
      }

      // If we couldn't write all of it, push to queue and arm `EPOLLOUT`.
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
    // Disarms `EPOLLOUT` and calls `on_drain` (or `do_close_now()` if
    // `closing_`) when the queue empties.
    bool do_flush_send_buf() {
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

      // If we were waiting to close, do it now.
      if (close_requested_) {
        complete_pending_writes_after_drain();
        do_close_now();
        return false;
      }

      // Inform the user that all outbound data has drained.
      notify_drained();
      return true;
    }

    // Graceful close: if data is pending let `do_flush_send_buf()` finish
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

      // Resume any suspended coroutines so they can observe the closed
      // state. Both handles are posted (not resumed directly) so that any
      // in-progress `await_suspend` on the call stack has already returned
      // before the coroutine continues. This prevents use-after-free of the
      // coroutine frame when `do_close_now` is triggered from within
      // `enqueue_send` -> `await_suspend`.
      if (pending_read_.has_waiter()) notify_read_closed();
      fail_pending_write_waiters();
      notify_close_once();
    }

    // Awaitable returned by `stream_conn::async_read()`. Suspends the calling
    // coroutine until one batch of data arrives or the connection closes.
    // `await_resume()` returns the received bytes (moved out of
    // `pending_read_data_`); an empty string means the connection was already
    // closed when `co_await` was evaluated, or it closed before data arrived.
    //
    // At most one `async_read` may be pending at a time. Must be awaited on
    // the loop thread.
    struct async_read_awaitable {
      state* s_;

      // Skip suspension if the connection is already closed; there will be
      // no future `handle_readable` to resume us.
      [[nodiscard]] bool await_ready() const noexcept {
        return !s_->open_.load(std::memory_order::relaxed) ||
               !s_->read_open_.load(std::memory_order::relaxed);
      }

      void await_suspend(std::coroutine_handle<> h) const {
        assert(!s_->pending_read_.has_waiter());
        s_->pending_read_.coro = h;
        s_->refresh_read_interest();
      }

      [[nodiscard]] std::string await_resume() const noexcept {
        return std::move(s_->pending_read_data_);
      }
    };

    // Awaitable returned by `stream_conn::async_send(std::string)`. Passes
    // ownership of `buf` to the send path and suspends the calling coroutine
    // until the send queue fully drains (or the connection closes).
    //
    // `pending_drain_` is set BEFORE `enqueue_send()` is called so that if
    // `enqueue_send` triggers `do_close_now` (write error), `do_close_now`
    // can clear the handle and post the resume before `await_suspend` returns.
    // `await_suspend` then sees `pending_drain_` as null and returns `true`
    // (stay suspended), which is correct: the resume was already posted.
    //
    // If `enqueue_send` writes all bytes synchronously (queue stays empty),
    // `await_suspend` clears `pending_drain_` and returns `false` to cancel
    // suspension, resuming the coroutine inline without posting.
    //
    // At most one `async_send` may be pending at a time. Must be awaited on
    // the loop thread.
    // NOLINTBEGIN(readability-convert-member-functions-to-static)
    struct async_send_awaitable {
      state* s_;
      std::string buf_;

      [[nodiscard]] bool await_ready() const noexcept {
        return !s_->open_.load(std::memory_order::relaxed) ||
               !s_->write_open_.load(std::memory_order::relaxed);
      }

      // Returns `false` to cancel suspension (immediate resume) when the
      // write completed synchronously. Returns `true` to stay suspended in
      // all other cases (queued for drain, or already-posted close resume).
      bool await_suspend(std::coroutine_handle<> h) {
        assert(!s_->pending_write_.has_waiter());
        s_->pending_write_.coro = h;
        s_->enqueue_send(std::move(buf_));

        // Case 1: `do_close_now` fired inside `enqueue_send`; it already
        // cleared `pending_write_.coro` and posted `h.resume()`. Stay
        // suspended.
        if (!s_->pending_write_.coro) return true;

        // Case 2: Write completed synchronously: queue is still empty.
        // Cancel suspension so the coroutine resumes without a round-trip
        // through the loop.
        if (s_->send_queue_.empty()) {
          s_->pending_write_.coro = {};
          return false;
        }

        // Case 3: Data was queued; `EPOLLOUT` will drive `do_flush_send_buf`
        // which will post the resume when the queue drains.
        return true;
      }

      void await_resume() noexcept {}
    };
    // NOLINTEND(readability-convert-member-functions-to-static)
  };

  // Execute the lambda `fn` on the loop thread.
  //
  // If we're already on the loop thread, runs it inline. Otherwise, posts it
  // to the queue, and either returns immediately (`execution::nonblocking`) or
  // waits for completion (`execution::blocking`). If we run synchronously, the
  // return value of the lambda is passed through; otherwise, it's always true.
  template<typename FN>
  bool exec_lambda(execution exec, FN&& fn) {
    auto& loop = state_->loop_;
    if (loop.is_loop_thread()) return fn();
    if (exec == execution::nonblocking) {
      loop.post(std::forward<FN>(fn));
      return true;
    }
    return loop.post_and_wait(std::forward<FN>(fn));
  }

  std::shared_ptr<state> state_;
};
}} // namespace corvid::proto
