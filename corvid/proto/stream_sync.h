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
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>

#include <sys/socket.h>
#include <sys/time.h>

#include "../filesys/net_socket.h"
#include "../strings/no_zero.h"
#include "net_endpoint.h"

namespace corvid { inline namespace proto {

// Blocking synchronous stream-socket client.
//
// Wraps a `net_socket` created in blocking mode. Intended for tests and small
// tools that need to talk to a server without the overhead of an `epoll_loop`,
// or the complication of using one to test one. Does NOT depend on
// `epoll_loop` or `stream_conn`.
//
// An optional per-syscall timeout can be set at construction time via
// `SO_RCVTIMEO` / `SO_SNDTIMEO`. Any error (EOF, hard error, or timeout) marks
// the connection as closed; subsequent calls fail immediately. These "fail
// fast" semantics are appropriate for test code where a timeout means the test
// has gone wrong, not something to retry.
//
// Typical usage:
//
//   auto conn = stream_sync::connect(ep, 5s);
//   assert(conn);
//   conn.send("GET / HTTP/1.0\r\n\r\n");
//   auto headers = conn.recv_until("\r\n\r\n");
//   auto body    = conn.recv_exact(content_length);
//
// `recv_until` and `recv_exact` use an internal buffer so unconsumed bytes
// from one call are available to the next.
class stream_sync {
public:
  static constexpr size_t default_max_bytes = 1024ULL * 1024ULL; // 1 MiB

  stream_sync() = default;

  stream_sync(stream_sync&&) noexcept = default;
  stream_sync& operator=(stream_sync&&) noexcept = default;

  stream_sync(const stream_sync&) = delete;
  stream_sync& operator=(const stream_sync&) = delete;

  // Connect to `ep`. If `timeout` is nonzero, it is applied as a per-syscall
  // limit to each `send` / `recv` call (not the total operation). Returns a
  // falsy `stream_sync` on connection failure.
  [[nodiscard]] static stream_sync
  connect(const net_endpoint& ep, std::chrono::milliseconds timeout = {}) {
    stream_sync s;
    s.sock_ = net_socket::create_for(ep, execution::blocking);
    if (!s.sock_.is_open()) return {};

    if (timeout.count() > 0) {
      const auto ms = timeout.count();
      const timeval tv{.tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000};
      if (!s.sock_.set_option(SOL_SOCKET, SO_RCVTIMEO, tv) ||
          !s.sock_.set_option(SOL_SOCKET, SO_SNDTIMEO, tv))
        return {};
    }

    auto result = s.sock_.connect(ep);
    if (!result.value_or(false)) return {};
    return s;
  }

  [[nodiscard]] bool is_open() const noexcept { return sock_.is_open(); }
  [[nodiscard]] explicit operator bool() const noexcept { return is_open(); }

  [[nodiscard]] bool close() noexcept {
    errno_on_close_ = errno;
    return sock_.close();
  }

  // Send all of `data`, looping on partial writes. Closes and returns false on
  // any error or timeout.
  [[nodiscard]] bool send(std::string_view data) {
    while (!data.empty()) {
      const size_t before = data.size();
      if (!sock_.send(data) || data.size() == before) return close() && false;
    }
    return true;
  }

  // Receive up to `max_bytes`, returning whatever arrives first (draining the
  // internal buffer before reading the socket). Returns an empty string on
  // EOF/error/timeout, or if `max_bytes` is zero.
  [[nodiscard]] std::string recv(size_t max_bytes = default_max_bytes) {
    if (max_bytes == 0) return {};
    if (buf_.empty() && !do_recv()) return {};
    const size_t take = std::min(buf_.size(), max_bytes);
    std::string out = buf_.substr(0, take);
    buf_.erase(0, take);
    return out;
  }

  // Receive exactly `n` bytes, looping as needed. Returns a string shorter
  // than `n` on EOF/error/timeout.
  [[nodiscard]] std::string recv_exact(size_t n) {
    while (buf_.size() < n)
      if (!do_recv()) return std::exchange(buf_, {});

    std::string out = buf_.substr(0, n);
    buf_.erase(0, n);
    return out;
  }

  // Receive bytes until `delim` appears in the accumulated data, or until
  // `max_bytes` total bytes are buffered. Returns everything up to and
  // including `delim` and leaves any trailing bytes in the internal buffer.
  // Returns an empty string if `delim` is empty, on EOF/error/timeout, or if
  // `max_bytes` is reached without finding `delim`.
  [[nodiscard]] std::string
  recv_until(std::string_view delim, size_t max_bytes = default_max_bytes) {
    if (delim.empty()) return {};
    for (;;) {
      const auto pos = buf_.find(delim);
      if (pos != std::string::npos) {
        const size_t end = pos + delim.size();
        std::string out = buf_.substr(0, end);
        buf_.erase(0, end);
        return out;
      }
      if (buf_.size() >= max_bytes) return {};
      if (!do_recv()) return {};
    }
  }

  [[nodiscard]] int errno_on_close() const noexcept { return errno_on_close_; }

private:
  net_socket sock_;
  std::string buf_;
  int errno_on_close_{};

  // Append one chunk of data from the socket to `buf_`. Closes and returns
  // false on EOF, hard error, or timeout.
  [[nodiscard]] bool do_recv() {
    static constexpr size_t chunk_size = 4096;
    const size_t old_size = buf_.size();
    no_zero::resize_to(buf_, old_size + chunk_size);
    const ssize_t n = sock_.recv(buf_.data() + old_size, chunk_size, 0);
    if (n <= 0) return close() && false;
    no_zero::trim_to(buf_, old_size + n);
    return true;
  }
};

}} // namespace corvid::proto
