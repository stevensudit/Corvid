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
#include <utility>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "../enums/bool_enums.h"

#include "ip_endpoint.h"
#include "os_file.h"

namespace corvid { inline namespace proto {
using namespace bool_enums;

// RAII IP socket with type-safe option methods.
//
// `ip_socket` is-an `os_file`, adding socket-specific operations on top of
// the shared fd ownership and control helpers. Movable, non-copyable.
class [[nodiscard]] ip_socket: public os_file {
public:
  using handle_t = os_file::file_handle_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;

  ip_socket() noexcept = default;
  ip_socket(int domain, int type, int protocol) noexcept
      : os_file(::socket(domain, type, protocol)) {}
  explicit ip_socket(os_file&& file) noexcept : os_file(std::move(file)) {}

  ip_socket(ip_socket&&) noexcept = default;
  ip_socket(const ip_socket&) = delete;

  ip_socket& operator=(ip_socket&&) noexcept = default;
  ip_socket& operator=(const ip_socket&) = delete;

  ~ip_socket() = default;

  // Close the socket. Idempotent. Returns true when the socket was open and
  // is now closed, false if it could not be closed (likely because it already
  // was).
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  bool close() noexcept { return os_file::close(); }

  // Close the socket with the option to perform a forceful close (e.g., via
  // `SO_LINGER` with a zero timeout).
  bool close(close_mode mode) noexcept {
    if (mode == close_mode::forceful && is_open())
      set_option(SOL_SOCKET, SO_LINGER, linger{.l_onoff = 1, .l_linger = 0});

    return os_file::close();
  }

  // Set a socket option. Returns true on success. Templated to infer
  // `sizeof(T)` automatically and hide the `reinterpret_cast` required by
  // the C `setsockopt` API; callers pass a typed value directly.
  template<typename T>
  bool set_option(int level, int optname, const T& value) noexcept {
    return ::setsockopt(handle(), level, optname,
               reinterpret_cast<const char*>(&value),
               static_cast<socklen_t>(sizeof(T))) == 0;
  }

  // Get a socket option. Returns `std::nullopt` on failure.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_option(int level, int optname) const noexcept {
    T value{};
    socklen_t len = sizeof(T);
    if (::getsockopt(handle(), level, optname, reinterpret_cast<char*>(&value),
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

  // Set non-blocking I/O mode (delegates to `os_file::set_nonblocking()`).
  [[nodiscard]] bool set_nonblocking(bool on = true) noexcept {
    return os_file::set_nonblocking(on);
  }

  // Read up to `data.size()` bytes from the socket into `data`, honoring
  // `flags` as in POSIX `recv()`.
  //
  // On success, resizes `data` to the number of bytes read and returns true. A
  // "soft" failure (e.g., EAGAIN) is treated as success with zero bytes read.
  // On EOF/disconnect, leaves `data` unchanged and returns false. On hard
  // failure, clears `data` and returns false.
  [[nodiscard]] bool recv(std::string& data, int flags = 0) const {
    if (data.empty()) return true;

    const ssize_t n = ::recv(handle(), data.data(), data.size(), flags);
    if (n == 0) return false;

    no_zero::trim_to(data, n);
    if (n < 0) return !os_file::is_hard_error();
    return true;
  }

  // Receive raw bytes into `buf`, forwarding directly to POSIX `recv()`.
  [[nodiscard]] ssize_t recv(void* buf, size_t len, int flags) const noexcept {
    return ::recv(handle(), buf, len, flags);
  }

  // Send as much of `data` as possible on the socket. On success, removes the
  // written prefix from `data` and returns true. On failure, leaves `data`
  // unchanged and returns false. A "soft" failure (e.g., EAGAIN) is treated
  // as success with no progress.
  [[nodiscard]] bool send(std::string_view& data) const noexcept {
    if (data.empty()) return true;

    const ssize_t n = send(data.data(), data.size());
    if (n <= 0) return !os_file::is_hard_error();

    data.remove_prefix(static_cast<size_t>(n));
    return true;
  }

  // Send raw bytes from `buf`, forwarding to POSIX `send()`.
  [[nodiscard]] ssize_t
  send(const void* buf, size_t len, int flags = MSG_NOSIGNAL) const noexcept {
    return ::send(handle(), buf, len, flags);
  }

  // Bind the socket to a local endpoint. Returns true on success.
  [[nodiscard]] bool bind(const ip_endpoint& ep) noexcept {
    const auto [sa, len] = ep.as_sockaddr();
    return ::bind(handle(), sa, len) == 0;
  }

  // Initiate a connection to `ep`. For non-blocking sockets, `EINPROGRESS`
  // is treated as success (the connection is in progress). Returns true on
  // success or when the connection is underway.
  [[nodiscard]] bool connect(const ip_endpoint& ep) noexcept {
    const auto [sa, len] = ep.as_sockaddr();
    return ::connect(handle(), sa, len) == 0 || errno == EINPROGRESS;
  }

  // Mark the socket as passive and ready to accept connections. `backlog`
  // is the maximum pending connection queue length. Returns true on success.
  [[nodiscard]] bool listen(int backlog = SOMAXCONN) noexcept {
    return ::listen(handle(), backlog) == 0;
  }

  // Shut down part of a full-duplex connection. `how` is one of `SHUT_RD`,
  // `SHUT_WR`, or `SHUT_RDWR`. Returns true on success.
  [[nodiscard]] bool shutdown(int how) noexcept {
    return ::shutdown(handle(), how) == 0;
  }

  // Accept a pending connection. The returned socket is created with
  // `SOCK_CLOEXEC | SOCK_NONBLOCK` via `accept4`. Returns `std::nullopt` when
  // no connection is available (`EAGAIN`/`EWOULDBLOCK`) or an error occurs.
  [[nodiscard]] std::optional<std::pair<ip_socket, ip_endpoint>>
  accept() noexcept {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    const int fd = ::accept4(handle(), reinterpret_cast<sockaddr*>(&addr),
        &len, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) return std::nullopt;
    ip_endpoint peer;
    if (addr.ss_family == AF_INET)
      peer = ip_endpoint{*reinterpret_cast<const sockaddr_in*>(&addr)};
    else if (addr.ss_family == AF_INET6)
      peer = ip_endpoint{*reinterpret_cast<const sockaddr_in6*>(&addr)};
    return std::pair{ip_socket{os_file{fd}}, peer};
  }
};

}} // namespace corvid::proto
