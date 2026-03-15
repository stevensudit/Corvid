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
#include <memory>
#include <string>
#include <string_view>

#ifdef __linux__
#include <unistd.h>
#endif

#include "io_loop.h"
#include "loop_task.h"
#include "ip_endpoint.h"
#include "../strings/no_zero.h"

namespace corvid { inline namespace proto {

using namespace corvid::strings::no_zero_funcs;

// User-supplied callbacks for a `tcp_conn`. All fields are optional; a null
// handler is silently skipped when its event fires.
//
//  `on_data(str)`  -- fired when data arrives. `str` is the internal receive
//                     buffer resized to the number of bytes read; the user may
//                     `std::move` from it to steal the allocation without an
//                     extra copy. The reference is valid only during the call.
//  `on_drain()`    -- fired when the send queue empties after a buffered
//                     (EPOLLOUT-driven) write. Not fired when `send()` writes
//                     all bytes immediately.
//  `on_close()`    -- fired once on EOF or I/O error; the connection is gone.
struct tcp_conn_handlers {
  std::function<void(std::string&)> on_data = nullptr;
  std::function<void()> on_drain = nullptr;
  std::function<void()> on_close = nullptr;
};

// A non-blocking TCP connection driven by an `io_loop`.
//
// `tcp_conn` is a movable handle that wraps a `shared_ptr` to internal state
// (`state`). Note that, despite using `shared_ptr`, a `tcp_conn` fully owns
// the `state` and removes it from the `io_loop` on close.
//
// The state also serves as the `io_conn` registered with the loop, so there is
// exactly ONE heap allocation per connection: no separate `io_handlers`
// lambdas and no second `impl` object.
//
// Thread safety: `send()`, `close()`, and the destructor are safe to call from
// any thread. They route work to the loop via `io_loop::post()`. All actual
// I/O and epoll-mask mutations run exclusively on the loop thread.
//
// Send path: `send(std::string&&)` takes ownership of the caller's string.
// If the send queue is empty, an immediate `::write` is attempted. If all
// bytes are written, the string is discarded with no queuing. Otherwise the
// string is pushed onto `send_queue_` (a `std::deque<std::string>`) and
// `head_span_` is set to the unsent tail; `EPOLLOUT` is armed. Subsequent
// `EPOLLOUT` events drive `do_flush_send_buf()`. When the queue empties,
// `EPOLLOUT` is disarmed and `on_drain` fires. `std::deque` is used instead of
// `std::vector` because `push_back` does not move existing elements, keeping
// `head_span_` (which points into `front()`) valid.
//
// Receive path: when `EPOLLIN` fires, the receive buffer is resized to
// `recv_buf_capacity_` bytes via `resize_and_overwrite` (C++23, no
// zero-initialization), filled by `::read`, then trimmed to the actual count
// before `on_data` is called. The user may `std::move` from the buffer to
// steal its allocation; the next read reallocates if needed.
//
// Close: `close()` is graceful -- if the send queue is non-empty, the socket
// closes only after it drains. The destructor has the same semantics.
//
// Linux only; on other platforms all methods are no-ops.
class tcp_conn {
public:
  // Default receive-buffer capacity per connection, in bytes.
  static constexpr size_t default_recv_buf_size = 16384;

  // Construct a connection from `sock` (must already be non-blocking) and post
  // its registration with `loop`. `remote` records the peer address for
  // diagnostics. May be called from any thread.
  // TODO: This should take a `tcp_socket` or we should get rid of the idea of
  // a `tcp_socket` entirely. Perhaps all we have are `ip_socket`s, and it's
  // the `*_conn` that specifies TCP or UDP.
  explicit tcp_conn(io_loop& loop, ip_socket&& sock, const ip_endpoint& remote,
      tcp_conn_handlers&& h, size_t recv_buf_size = default_recv_buf_size) {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    assert((sock.file().get_flags().value_or(0) & O_NONBLOCK) != 0);
#endif
    state_ = std::make_shared<state>(loop, std::move(sock), remote,
        std::move(h), recv_buf_size);
    loop.post([p = state_] { p->register_with_loop(); });
  }

  tcp_conn(tcp_conn&&) noexcept = default;
  tcp_conn(const tcp_conn&) = delete;

  tcp_conn& operator=(tcp_conn&&) noexcept = default;
  tcp_conn& operator=(const tcp_conn&) = delete;

  // Posts a graceful close to the loop. Safe to call from any thread.
  // TODO: Consider whether the destructor should instead do a forceful close.
  // That would avoid the allocation nonsense.
  // NOLINTBEGIN(bugprone-exception-escape)
  ~tcp_conn() {
    if (!state_) return;
    auto& loop = state_->loop_;
    loop.post([p = std::move(state_)] { p->do_close(); });
  }
  // NOLINTEND(bugprone-exception-escape)

  // True if the connection has not yet been closed.
  [[nodiscard]] bool is_open() const noexcept {
    return state_ && state_->open_.load(std::memory_order_relaxed);
  }

  // The remote peer address supplied at construction.
  [[nodiscard]] const ip_endpoint& remote_endpoint() const noexcept {
    return state_->remote_;
  }

  // Take ownership of `buf` and post it for sending on the loop thread.
  // `buf` must be non-empty. Safe to call from any thread.
  void send(std::string&& buf) {
    // Note: The reason we don't just call `enqueue_send` directly is that it
    // mutates loop-owned connection state and performs socket I/O against the
    // same fd that the loop may also be touching.
    if (!state_ || buf.empty()) return;
    if (!state_->open_.load(std::memory_order_relaxed)) return;
    auto& loop = state_->loop_;
    loop.post([p = state_, b = std::move(buf)]() mutable {
      p->enqueue_send(std::move(b));
    });
  }

  // Post a graceful close to the loop. Pending sends drain first.
  // Safe to call from any thread.
  // TODO: Add a bool to force a hard close that discards pending sends and
  // hangs up on the connection.
  void close() {
    if (!state_) return;
    auto& loop = state_->loop_;
    loop.post([p = state_] { p->do_close(); });
  }

  // Return an awaitable that suspends the calling coroutine until one batch
  // of data arrives or the connection closes. Returns the received bytes as a
  // `std::string`; an empty string signals that the connection was already
  // closed or closed before data arrived. The caller should check
  // `is_open()` to distinguish a closed connection from a zero-byte read.
  //
  // At most one `async_read` may be outstanding at a time.
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
  // At most one `async_send` may be outstanding at a time.
  // Must be awaited from the loop thread.
  [[nodiscard]] auto async_send(std::string&& buf) noexcept {
    return state::async_send_awaitable{state_.get(), std::move(buf)};
  }

private:
  // Internal state. Inherits from `io_conn` so it can be registered directly
  // with the loop.
  struct state final: io_conn {
    io_loop& loop_;
    ip_endpoint remote_;
    tcp_conn_handlers handlers_;

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
    size_t recv_buf_capacity_{default_recv_buf_size};

    // Cleared atomically by `do_close_now()`. Read from any thread via
    // `tcp_conn::is_open()`.
    std::atomic<bool> open_;

    // Set by `do_close()` when there is pending data; causes
    // `do_flush_send_buf()` to call `do_close_now()` after the queue drains.
    bool closing_ = false;

    // Coroutine handle registered by `async_read_awaitable::await_suspend`.
    // Cleared and resumed (via `loop_.post`) by `handle_readable()` when data
    // arrives, or by `do_close_now()` when the connection closes.
    std::coroutine_handle<> pending_read_;

    // Buffer where `handle_readable()` deposits received bytes before
    // resuming a coroutine waiting in `async_read()`. Moved into by
    // `handle_readable`, moved out of by `async_read_awaitable::await_resume`.
    std::string pending_read_data_;

    // Coroutine handle registered by `async_send_awaitable::await_suspend`.
    // Cleared and resumed (via `loop_.post`) by `do_flush_send_buf()` when
    // the send queue empties, or by `do_close_now()`.
    std::coroutine_handle<> pending_drain_;

    explicit state(io_loop& loop, ip_socket&& sock, const ip_endpoint& remote,
        tcp_conn_handlers&& h, size_t rbs) noexcept
        : io_conn{std::move(sock)}, loop_{loop}, remote_{remote},
          handlers_{std::move(h)}, recv_buf_capacity_{rbs}, open_{true} {}

    // Register `ip_socket` with the loop. Stores a shared owner in the loop's
    // registration map, keeping the state alive as long as the fd is
    // registered, even if its `tcp_conn` is destructed.
    void register_with_loop() {
#ifdef __linux__
      if (!open_.load(std::memory_order_relaxed)) return;
      (void)loop_.register_socket(shared_from_this());
#endif
    }

    // `io_conn` overrides -- called on the loop thread by `dispatch_event`.
    void on_readable() override { handle_readable(); }
    void on_writable() override { do_flush_send_buf(); }
    void on_error() override { do_close_now(); }

    // Read available data into `recv_buf_` without zero-initializing it
    // (C++23 `resize_and_overwrite`), then deliver to `on_data`. Closes on
    // EOF or unrecoverable error.
    bool handle_readable() {
#ifdef __linux__
      no_zero::enlarge_to(recv_buf_, recv_buf_capacity_);
      if (!sock().file().read(recv_buf_)) {
        do_close_now();
        return false;
      }

      if (!recv_buf_.empty()) {
        if (pending_read_) {
          pending_read_data_ = std::move(recv_buf_);
          loop_.post([h = std::exchange(pending_read_, {})] { h.resume(); });
        } else if (handlers_.on_data) {
          handlers_.on_data(recv_buf_);
        }
      }
#endif
      return true;
    }

    // Called on the loop thread from `post()` to enqueue `buf` for sending.
    // Attempts an immediate `::write` when the queue is empty; if any bytes
    // remain (partial write or EAGAIN) they are pushed onto `send_queue_` and
    // tracked by `head_span_`. `EPOLLOUT` is armed when the queue becomes
    // non-empty; `do_flush_send_buf()` drains it on subsequent `EPOLLOUT`
    // events.
    bool enqueue_send(std::string&& buf) {
#ifdef __linux__
      if (!open_.load(std::memory_order_relaxed)) return false;

      // If there are writes queued ahead of us, just add this to the back:
      // `EPOLLOUT` already armed.
      if (!send_queue_.empty()) {
        send_queue_.push_back(std::move(buf));
        return true;
      }

      // Write as much as we can of the new buffer.
      auto buf_view = std::string_view{buf};
      if (!sock().file().write(buf_view)) {
        // If we can't write at all, close immediately.
        buf.clear();
        do_close_now();
        return false;
      }

      // If fully written, nothing to queue.
      if (buf_view.empty()) {
        buf.clear();
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
#else
      (void)buf;
#endif
      return true;
    }

    // Drain `send_queue_` as far as `::write` allows, advancing `head_span_`.
    // When a string is fully sent, `pop_front()` destroys it immediately.
    // Disarms `EPOLLOUT` and calls `on_drain` (or `do_close_now()` if
    // `closing_`) when the queue empties.
    bool do_flush_send_buf() {
#ifdef __linux__
      // Guard against being called after `do_close_now()` (e.g., when both
      // `EPOLLIN` and `EPOLLOUT` fire in the same event and the readable
      // handler closes the connection before we get here).
      if (!open_.load(std::memory_order_relaxed)) return false;

      // Write until we're out of data, are blocked, or fail.
      while (!send_queue_.empty()) {
        // If we can't write at all, close immediately.
        if (!sock().file().write(head_span_)) {
          do_close_now();
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
      if (closing_) {
        do_close_now();
        return false;
      }

      // Inform the user that the queue is fully drained.
      if (pending_drain_) {
        loop_.post([h = std::exchange(pending_drain_, {})] { h.resume(); });
      } else if (handlers_.on_drain) {
        handlers_.on_drain();
      }
#endif
      return true;
    }

    // Graceful close: if data is pending let `do_flush_send_buf()` finish
    // first.
    void do_close() {
      if (!open_.load(std::memory_order_relaxed)) return;
      closing_ = true;
      if (send_queue_.empty()) do_close_now();
    }

    // Unconditional close. Idempotent via `open_.exchange(false)`.
    void do_close_now() {
      if (!open_.exchange(false)) return;
      loop_.unregister_socket(sock());
      sock().close();
      send_queue_.clear();
      head_span_ = {};
      closing_ = false;
      // Resume any suspended coroutines so they can observe the closed
      // state. Both handles are posted (not resumed directly) so that any
      // in-progress `await_suspend` on the call stack has already returned
      // before the coroutine continues. This prevents use-after-free of the
      // coroutine frame when `do_close_now` is triggered from within
      // `enqueue_send` -> `await_suspend`.
      if (pending_read_)
        loop_.post([h = std::exchange(pending_read_, {})] { h.resume(); });
      if (pending_drain_)
        loop_.post([h = std::exchange(pending_drain_, {})] { h.resume(); });
      if (handlers_.on_close) handlers_.on_close();
    }

    // Awaitable returned by `tcp_conn::async_read()`. Suspends the calling
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
        return !s_->open_.load(std::memory_order_relaxed);
      }

      void await_suspend(std::coroutine_handle<> h) const noexcept {
        s_->pending_read_ = h;
      }

      [[nodiscard]] std::string await_resume() const noexcept {
        return std::move(s_->pending_read_data_);
      }
    };

    // Awaitable returned by `tcp_conn::async_send(std::string)`. Passes
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

      [[nodiscard]] bool await_ready() const noexcept { return false; }

      // Returns `false` to cancel suspension (immediate resume) when the
      // write completed synchronously. Returns `true` to stay suspended in
      // all other cases (queued for drain, or already-posted close resume).
      bool await_suspend(std::coroutine_handle<> h) {
        s_->pending_drain_ = h;
        s_->enqueue_send(std::move(buf_));

        // Case 1: `do_close_now` fired inside `enqueue_send`; it already
        // cleared `pending_drain_` and posted `h.resume()`. Stay suspended.
        if (!s_->pending_drain_) return true;

        // Case 2: Write completed synchronously: queue is still empty.
        // Cancel suspension so the coroutine resumes without a round-trip
        // through the loop.
        if (s_->send_queue_.empty()) {
          s_->pending_drain_ = {};
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

  std::shared_ptr<state> state_;
};
}} // namespace corvid::proto
