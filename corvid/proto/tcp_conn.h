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
#include <cerrno>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <string>

#ifdef __linux__
#include <unistd.h>
#endif

#include "io_loop.h"
#include "ip_endpoint.h"

namespace corvid { inline namespace proto {

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
// (`state`). The state also serves as the `io_conn` registered with the loop,
// so there is exactly ONE heap allocation per connection -- no separate
// `io_handlers` lambdas and no second `impl` object.
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
// `EPOLLOUT` events drive `flush_send_buf_()`. When the queue empties,
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
  explicit tcp_conn(io_loop& loop, ip_socket sock, ip_endpoint remote,
      tcp_conn_handlers h, size_t recv_buf_size = default_recv_buf_size);

  tcp_conn(tcp_conn&&) noexcept = default;
  tcp_conn& operator=(tcp_conn&&) noexcept = default;
  tcp_conn(const tcp_conn&) = delete;
  tcp_conn& operator=(const tcp_conn&) = delete;

  // Posts a graceful close to the loop. Safe to call from any thread.
  ~tcp_conn();

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
  void send(std::string&& buf);

  // Post a graceful close to the loop. Pending sends drain first.
  // Safe to call from any thread.
  void close();

private:
  // Internal state. Inherits from `io_conn` so it can be registered directly
  // with the loop -- eliminating the separate lambda/handler allocation used
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
    std::deque<std::string> send_queue_;

    // Unsent tail of `send_queue_.front()`. Empty iff `send_queue_` is empty.
    std::span<const char> head_span_;

    // Receive staging buffer. Resized without zero-initialization before each
    // `::read`. The user may `std::move` from the buffer in `on_data`.
    std::string recv_buf_;

    size_t recv_buf_capacity_{default_recv_buf_size};

    // Cleared atomically by `close_now_()`. Read from any thread via
    // `tcp_conn::is_open()`.
    std::atomic<bool> open_;

    // Set by `do_close_()` when there is pending data; causes
    // `flush_send_buf_()` to call `close_now_()` after the queue drains.
    bool closing_ = false;

    explicit state(io_loop& loop, ip_socket sock, ip_endpoint remote,
        tcp_conn_handlers h, size_t rbs) noexcept
        : loop_{loop}, sock_{std::move(sock)}, remote_{std::move(remote)},
          handlers_{std::move(h)}, recv_buf_capacity_{rbs}, open_{true} {}

    // Register `sock_` with the loop. The caller passes `self` (the
    // `shared_ptr` to this object) so it ends up stored in the loop's
    // registration map -- keeping the state alive as long as the fd is
    // registered, independently of how many `tcp_conn` handles exist.
    void do_open_(std::shared_ptr<state> self) {
#ifdef __linux__
      if (!open_.load(std::memory_order_relaxed)) return;
      loop_.add_conn(sock_, std::move(self));
#else
      (void)self;
#endif
    }

    // `io_conn` overrides -- called on the loop thread by `dispatch_event`.
    void on_readable() override { handle_readable_(); }
    void on_writable() override { flush_send_buf_(); }
    void on_error() override { close_now_(); }

    // Read available data into `recv_buf_` without zero-initializing it
    // (C++23 `resize_and_overwrite`), then deliver to `on_data`. Closes on
    // EOF or unrecoverable error.
    void handle_readable_() {
#ifdef __linux__
      recv_buf_.resize_and_overwrite(recv_buf_capacity_,
          [](char*, std::size_t n) noexcept { return n; });
      const ssize_t n =
          ::read(sock_.file().handle(), recv_buf_.data(), recv_buf_.size());
      if (n > 0) {
        recv_buf_.resize(static_cast<std::size_t>(n));
        if (handlers_.on_data) handlers_.on_data(recv_buf_);
      } else {
        recv_buf_.resize(0);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        close_now_(); // EOF (n == 0) or unrecoverable error
      }
#endif
    }

    // Called on the loop thread from `post()` to enqueue `buf` for sending.
    // Attempts an immediate `::write` when the queue is empty; if any bytes
    // remain (partial write or EAGAIN) they are pushed onto `send_queue_` and
    // tracked by `head_span_`. `EPOLLOUT` is armed when the queue becomes
    // non-empty; `flush_send_buf_()` drains it on subsequent `EPOLLOUT`
    // events.
    void enqueue(std::string&& buf) {
#ifdef __linux__
      if (!open_.load(std::memory_order_relaxed)) return;
      if (send_queue_.empty()) {
        // Attempt a direct write before queuing.
        const ssize_t n =
            ::write(sock_.file().handle(), buf.data(), buf.size());
        if (n == static_cast<ssize_t>(buf.size())) return; // fully written
        const auto sent =
            static_cast<std::size_t>(n > 0 ? n : 0); // 0 on EAGAIN
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          close_now_();
          return;
        }
        // Partial write or EAGAIN: push to queue and arm `EPOLLOUT`.
        send_queue_.push_back(std::move(buf));
        const auto& front = send_queue_.front();
        head_span_ = {front.data() + sent, front.size() - sent};
        loop_.set_writable(sock_);
      } else {
        // Queue non-empty: `EPOLLOUT` already armed; just append.
        send_queue_.push_back(std::move(buf));
      }
#else
      (void)buf;
#endif
    }

    // Drain `send_queue_` as far as `::write` allows, advancing `head_span_`.
    // When a string is fully sent, `pop_front()` destroys it immediately.
    // Disarms `EPOLLOUT` and calls `on_drain` (or `close_now_()` if
    // `closing_`) when the queue empties.
    void flush_send_buf_() {
#ifdef __linux__
      while (!send_queue_.empty()) {
        const ssize_t n = ::write(sock_.file().handle(), head_span_.data(),
            head_span_.size());
        if (n > 0) {
          head_span_ = head_span_.subspan(static_cast<std::size_t>(n));
          if (head_span_.empty()) {
            send_queue_.pop_front();
            if (!send_queue_.empty()) {
              const auto& front = send_queue_.front();
              head_span_ = {front.data(), front.size()};
            }
          }
        } else {
          if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
          close_now_();
          return;
        }
      }
      // Queue fully drained.
      loop_.set_writable(sock_, false);
      if (closing_) {
        close_now_();
        return;
      }
      if (handlers_.on_drain) handlers_.on_drain();
#endif
    }

    // Graceful close: if data is pending let `flush_send_buf_()` finish first.
    void do_close_() {
      if (!open_.load(std::memory_order_relaxed)) return;
      closing_ = true;
      if (send_queue_.empty()) close_now_();
    }

    // Unconditional close. Idempotent via `open_.exchange(false)`.
    void close_now_() {
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

inline tcp_conn::tcp_conn(io_loop& loop, ip_socket sock, ip_endpoint remote,
    tcp_conn_handlers h, std::size_t recv_buf_size) {
  state_ = std::make_shared<state>(loop, std::move(sock), std::move(remote),
      std::move(h), recv_buf_size);
  loop.post([p = state_] { p->do_open_(p); });
}

inline tcp_conn::~tcp_conn() {
  if (!state_) return;
  auto& loop = state_->loop_;
  loop.post([p = std::move(state_)] { p->do_close_(); });
}

inline void tcp_conn::send(std::string&& buf) {
  if (!state_ || buf.empty()) return;
  if (!state_->open_.load(std::memory_order_relaxed)) return;
  auto& loop = state_->loop_;
  loop.post([p = state_, b = std::move(buf)]() mutable {
    p->enqueue(std::move(b));
  });
}

inline void tcp_conn::close() {
  if (!state_) return;
  auto& loop = state_->loop_;
  loop.post([p = state_] { p->do_close_(); });
}

}} // namespace corvid::proto
