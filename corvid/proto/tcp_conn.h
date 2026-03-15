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
#include <memory>
#include <string>
#include <string_view>

#ifdef __linux__
#include <unistd.h>
#endif

#include "io_loop.h"
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
  explicit tcp_conn(io_loop& loop, ip_socket&& sock, const ip_endpoint& remote,
      tcp_conn_handlers&& h, size_t recv_buf_size = default_recv_buf_size) {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    assert(sock.file().get_flags() >= 0 &&
           (sock.file().get_flags() & O_NONBLOCK) != 0);
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
  ~tcp_conn() {
    if (!state_) return;
    auto& loop = state_->loop_;
    loop.post([p = std::move(state_)] { p->do_close(); });
  }

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

private:
  // Internal state. Inherits from `io_conn` so it can be registered directly
  // with the loop, eliminating the separate lambda/handler allocation used
  // by the `io_handlers`-based approach.
  struct state final: io_conn {
    io_loop& loop_;
    ip_socket sock_;
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

    explicit state(io_loop& loop, ip_socket&& sock, const ip_endpoint& remote,
        tcp_conn_handlers&& h, size_t rbs) noexcept
        : loop_{loop}, sock_{std::move(sock)}, remote_{remote},
          handlers_{std::move(h)}, recv_buf_capacity_{rbs}, open_{true} {}

    // Register `sock_` with the loop. Stores a shared owner in the loop's
    // registration map, keeping the state alive as long as the fd is
    // registered, even if its `tcp_conn` is destructed.
    void register_with_loop() {
#ifdef __linux__
      if (!open_.load(std::memory_order_relaxed)) return;
      auto self = shared_from_this();
      loop_.add_conn(sock_, std::move(self));
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
      if (!sock_.file().read(recv_buf_)) {
        do_close_now();
        return false;
      }

      if (!recv_buf_.empty() && handlers_.on_data)
        handlers_.on_data(recv_buf_);
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
      if (!sock_.file().write(buf_view)) {
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
      loop_.set_writable(sock_);
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
      // Write until we're out of data, are blocked, or fail.
      while (!send_queue_.empty()) {
        // If we can't write at all, close immediately.
        if (!sock_.file().write(head_span_)) {
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
      loop_.set_writable(sock_, false);

      // If we were waiting to close, do it now.
      if (closing_) {
        do_close_now();
        return false;
      }

      // Inform the user that the queue is fully drained.
      if (handlers_.on_drain) handlers_.on_drain();
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
      loop_.unregister(sock_);
      sock_.close();
      send_queue_.clear();
      head_span_ = {};
      closing_ = false;
      if (handlers_.on_close) handlers_.on_close();
    }
  };

  std::shared_ptr<state> state_;
};
}} // namespace corvid::proto
