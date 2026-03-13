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

#include <optional>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace corvid { inline namespace proto {

// Platform socket handle type and invalid-handle sentinel.
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
using socket_handle_t = int;
inline constexpr socket_handle_t invalid_socket_handle = -1;
#else
// Placeholder for non-POSIX platforms (e.g., Windows `SOCKET`).
using socket_handle_t = int;
inline constexpr socket_handle_t invalid_socket_handle = -1;
#endif

// RAII socket handle with type-safe option methods.
//
// `ip_socket` wraps a platform socket handle (fd on POSIX, SOCKET on Windows)
// with RAII lifetime management. It is movable and non-copyable. The generic
// `set_option`/`get_option` methods provide type-safe access to `setsockopt`/
// `getsockopt`, and named helpers (e.g., `set_reuse_addr`, `set_nodelay`)
// cover the most common options.
//
// Intended as a base class for `tcp_socket` and `udp_socket`, which partition
// protocol-specific construction and operations.
//
// Platform-specific code is isolated in a guarded section. The handle type
// and invalid-handle sentinel are abstracted via `socket_handle_t` and
// `invalid_socket_handle`.
class ip_socket {
public:
  ip_socket(const ip_socket&) = delete;
  ip_socket& operator=(const ip_socket&) = delete;

  ip_socket(ip_socket&& other) noexcept : handle_{other.handle_} {
    other.handle_ = invalid_socket_handle;
  }

  ip_socket& operator=(ip_socket&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.handle_;
      other.handle_ = invalid_socket_handle;
    }
    return *this;
  }

  ~ip_socket() { close(); }

  // True if the handle is valid (i.e., the socket is open).
  [[nodiscard]] bool is_open() const noexcept {
    return handle_ != invalid_socket_handle;
  }

  explicit operator bool() const noexcept { return is_open(); }

  // Return the raw platform handle.
  [[nodiscard]] socket_handle_t handle() const noexcept { return handle_; }

  // Close the socket. Idempotent. Returns true when socket was open and is now
  // closed, false if it was already closed.
  bool close() noexcept {
    if (handle_ == invalid_socket_handle) return false;
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    ::close(handle_);
#endif
    handle_ = invalid_socket_handle;
    return true;
  }

  // Release ownership and return the handle without closing it.
  [[nodiscard]] socket_handle_t release() noexcept {
    const auto h = handle_;
    handle_ = invalid_socket_handle;
    return h;
  }

  // Platform-specific option access and named helpers.
  // Isolated here so that porting to a new OS requires changes only in this
  // guarded section and the platform header includes above.

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Set a socket option. Returns true on success. Templated to infer
  // `sizeof(T)` automatically and hide the `reinterpret_cast` required by
  // the C `setsockopt` API; callers pass a typed value directly.
  template<typename T>
  bool set_option(int level, int optname, const T& value) noexcept {
    return ::setsockopt(handle_, level, optname,
               reinterpret_cast<const char*>(&value),
               static_cast<socklen_t>(sizeof(T))) == 0;
  }

  // Get a socket option. Returns `std::nullopt` on failure.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_option(int level, int optname) const noexcept {
    T value{};
    socklen_t len = sizeof(T);
    if (::getsockopt(handle_, level, optname, reinterpret_cast<char*>(&value),
            &len) != 0)
      return std::nullopt;
    return value;
  }

  // Allow reuse of a recently-freed local address (`SO_REUSEADDR`).
  bool set_reuse_addr(bool on = true) noexcept {
    return set_option(SOL_SOCKET, SO_REUSEADDR, int{on});
  }

  // Allow multiple sockets to bind the same port (`SO_REUSEPORT`).
  bool set_reuse_port(bool on = true) noexcept {
    return set_option(SOL_SOCKET, SO_REUSEPORT, int{on});
  }

  // Disable Nagle algorithm for lower latency (`TCP_NODELAY`).
  bool set_nodelay(bool on = true) noexcept {
    return set_option(IPPROTO_TCP, TCP_NODELAY, int{on});
  }

  // Enable TCP keepalive probes (`SO_KEEPALIVE`).
  bool set_keepalive(bool on = true) noexcept {
    return set_option(SOL_SOCKET, SO_KEEPALIVE, int{on});
  }

  // Set receive buffer size in bytes (`SO_RCVBUF`).
  bool set_recv_buffer_size(int bytes) noexcept {
    return set_option(SOL_SOCKET, SO_RCVBUF, bytes);
  }

  // Set send buffer size in bytes (`SO_SNDBUF`).
  bool set_send_buffer_size(int bytes) noexcept {
    return set_option(SOL_SOCKET, SO_SNDBUF, bytes);
  }

  // Invoke `fcntl(cmd, args...)` on the handle. Returns -1 on failure.
  template<typename... Args>
  [[nodiscard]] int control(int cmd, Args&&... args) const noexcept {
    return ::fcntl(handle_, cmd, std::forward<Args>(args)...);
  }

  // Return the fd status flags via `fcntl(F_GETFL)`. Returns -1 on failure.
  [[nodiscard]] int get_flags() const noexcept { return control(F_GETFL); }

  // Enable or disable non-blocking I/O via `fcntl(F_SETFL, O_NONBLOCK)`.
  bool set_nonblocking(bool on = true) noexcept {
    const int flags = get_flags();
    if (flags < 0) return false;
    const int new_flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return control(F_SETFL, new_flags) == 0;
  }
#endif

protected:
  // Default-construct with an invalid handle. For use by subclasses that
  // create their handle via the constructor below or by other means.
  ip_socket() noexcept = default;

  // Adopt an existing handle. The socket takes ownership.
  explicit ip_socket(socket_handle_t h) noexcept : handle_{h} {}

  // Create a new socket via `::socket(domain, type, protocol)`. On failure,
  // `handle_` remains `invalid_socket_handle`.
  ip_socket(int domain, int type, int protocol) noexcept {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    handle_ = ::socket(domain, type, protocol);
#endif
  }

private:
  socket_handle_t handle_{invalid_socket_handle};
};

}} // namespace corvid::proto
