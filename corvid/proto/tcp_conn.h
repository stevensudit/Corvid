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
#include <array>
#include <cerrno>
#include <cstddef>
#include <functional>
#include <span>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

#include "io_loop.h"
#include "ip_endpoint.h"

namespace corvid { inline namespace proto {

// User-supplied callbacks for a `tcp_conn`. All fields are optional; a null
// handler is silently skipped when its event fires.
//
//  `on_data(span)`  -- fired when data arrives; `span` is valid only for the
//                      duration of the callback.
//  `on_drain()`     -- fired when the outbound buffer is fully drained after
//                      a buffered send (i.e., when `EPOLLOUT` disarms).
//  `on_close()`     -- fired on EOF or I/O error; the connection is gone.
struct tcp_conn_handlers {
  std::function<void(std::span<const char>)> on_data = nullptr;
  std::function<void()> on_drain = nullptr;
  std::function<void()> on_close = nullptr;
};

// A single non-blocking TCP connection managed by an `io_loop`.
//
// `tcp_conn` owns an `ip_socket` (which must already be open and set to
// non-blocking mode) and registers it with the supplied `io_loop` in the
// constructor. The loop's epoll callbacks are wired to internal methods that
// perform I/O and invoke the user-supplied `tcp_conn_handlers`.
//
// Send path: `send()` tries an immediate `::write`. Any bytes not written in
// that first call are appended to an internal send buffer and `EPOLLOUT` is
// armed. When `EPOLLOUT` fires the buffer is drained; `EPOLLOUT` is then
// disarmed and `on_drain` is called. If the immediate write drains all data,
// no `on_drain` is fired -- `on_drain` is exclusively a buffer-drained
// notification. The send buffer grows without bound; apply backpressure at the
// call site when needed.
//
// Receive path: when `EPOLLIN` fires, `::read` fills the internal receive
// buffer and `on_data` is called with a `std::span<const char>`. The span is
// valid only for the duration of the callback.
//
// Close: `close()` initiates a graceful shutdown. If there is pending data in
// the send buffer, the close is deferred until the buffer drains. Call from
// the loop thread.
//
// Lifetime: `tcp_conn` is non-movable. The `io_loop` stores `[this]` lambdas,
// so the `tcp_conn` must remain at a stable address for as long as it is
// registered. The destructor unregisters and closes; it must be called on the
// loop thread.
//
// Linux only; on other platforms the constructor is a no-op and all methods
// are inert.
class tcp_conn {
public:
  // Size of the stack-allocated receive staging buffer, in bytes.
  static constexpr std::size_t recv_buf_size = 4096;

  // Construct a connection from `sock` (must be non-blocking) and register it
  // with `loop`. `remote` records the peer's address for diagnostics. Must be
  // called on the loop thread.
  tcp_conn(io_loop& loop, ip_socket sock, ip_endpoint remote,
      tcp_conn_handlers h)
      : loop_{loop}, sock_{std::move(sock)}, remote_{remote},
        handlers_{std::move(h)} {
#ifdef __linux__
    loop_.add(sock_,
        {
            .on_readable = [this] { handle_readable_(); },
            .on_writable = [this] { handle_writable_(); },
            .on_error = [this] { handle_error_(); },
        });
#endif
  }

  tcp_conn(const tcp_conn&) = delete;
  tcp_conn& operator=(const tcp_conn&) = delete;
  tcp_conn(tcp_conn&&) = delete;
  tcp_conn& operator=(tcp_conn&&) = delete;

  // Unregister from the loop and close the socket. Must be called on the loop
  // thread. Note: `on_close` fires from here if the socket is still open, so
  // the user's callback must not attempt to destroy this object.
  ~tcp_conn() { do_close_(); }

  // True if the underlying socket is open.
  [[nodiscard]] bool is_open() const noexcept { return sock_.is_open(); }

  // The remote peer's address as supplied at construction.
  [[nodiscard]] const ip_endpoint& remote_endpoint() const noexcept {
    return remote_;
  }

  // Queue `data` for sending. Tries an immediate `::write` first; any
  // unwritten bytes are appended to the send buffer and `EPOLLOUT` is armed.
  // Must be called on the loop thread.
  void send(std::span<const char> data) {
    if (!sock_.is_open() || data.empty()) return;
#ifdef __linux__
    if (send_buf_offset_ == send_buf_.size()) {
      // Send buffer is empty; attempt a direct write.
      const ssize_t n =
          ::write(sock_.file().handle(), data.data(), data.size());
      if (n > 0)
        data = data.subspan(static_cast<std::size_t>(n));
      else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        do_close_();
        return;
      }
      if (data.empty()) return; // all written immediately
    }
    // Buffer the remaining bytes and arm `EPOLLOUT` if not already armed.
    // Invariant: `EPOLLOUT` is armed iff `send_buf_offset_ <
    // send_buf_.size()`.
    const bool need_arm = (send_buf_offset_ == send_buf_.size());
    send_buf_.insert(send_buf_.end(), data.begin(), data.end());
    if (need_arm) loop_.set_writable(sock_);
#endif
  }

  // Initiate a graceful close. If the send buffer is non-empty, the socket is
  // closed after the buffer drains; otherwise it is closed immediately. Must
  // be called on the loop thread.
  void close() {
    if (!sock_.is_open()) return;
    if (send_buf_offset_ < send_buf_.size()) {
      closing_ = true; // defer: flush_send_buf_() will call do_close_()
    } else {
      do_close_();
    }
  }

private:
  // Called by the loop when `EPOLLIN` fires. Reads available data and delivers
  // it to `on_data`. Closes on EOF (0 bytes) or a non-retriable error.
  void handle_readable_() {
#ifdef __linux__
    const ssize_t n =
        ::read(sock_.file().handle(), recv_buf_.data(), recv_buf_.size());
    if (n > 0) {
      if (handlers_.on_data)
        handlers_.on_data({recv_buf_.data(), static_cast<std::size_t>(n)});
    } else {
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
      do_close_(); // EOF (n == 0) or error
    }
#endif
  }

  // Called by the loop when `EPOLLOUT` fires. Delegates to `flush_send_buf_`.
  void handle_writable_() { flush_send_buf_(); }

  // Called by the loop when `EPOLLERR` or `EPOLLHUP` fires.
  void handle_error_() { do_close_(); }

  // Drain `send_buf_` as much as possible. When the buffer is fully drained,
  // `EPOLLOUT` is disarmed and `on_drain` is called (unless `closing_` is
  // set, in which case `do_close_()` is called instead).
  //
  // The send buffer uses an offset to avoid O(n) front-erases. Capacity is
  // reclaimed only when the buffer is fully drained.
  void flush_send_buf_() {
#ifdef __linux__
    while (send_buf_offset_ < send_buf_.size()) {
      const ssize_t n = ::write(sock_.file().handle(),
          send_buf_.data() + send_buf_offset_,
          send_buf_.size() - send_buf_offset_);
      if (n > 0) {
        send_buf_offset_ += static_cast<std::size_t>(n);
      } else {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        do_close_();
        return;
      }
    }
    // Buffer fully drained: reset and disarm `EPOLLOUT`.
    send_buf_.clear();
    send_buf_offset_ = 0;
    loop_.set_writable(sock_, false);
    if (closing_) {
      do_close_();
      return;
    }
    if (handlers_.on_drain) handlers_.on_drain();
#endif
  }

  // Close the socket immediately and fire `on_close`. Idempotent: the
  // `is_open()` guard prevents double-close.
  void do_close_() {
    if (!sock_.is_open()) return;
    loop_.unregister(sock_);
    sock_.close();
    send_buf_.clear();
    send_buf_offset_ = 0;
    closing_ = false;
    if (handlers_.on_close) handlers_.on_close();
  }

  io_loop& loop_;
  ip_socket sock_;
  ip_endpoint remote_;
  tcp_conn_handlers handlers_;
  // Outbound data waiting to be written. `send_buf_offset_` tracks how far
  // `flush_send_buf_()` has consumed the buffer, deferring memory reclamation
  // until the buffer is fully drained.
  std::vector<char> send_buf_;
  std::size_t send_buf_offset_ = 0;
  // Staging buffer for a single `::read` call.
  std::array<char, recv_buf_size> recv_buf_{};
  // True after `close()` is called while the send buffer is non-empty.
  bool closing_ = false;
};

}} // namespace corvid::proto
