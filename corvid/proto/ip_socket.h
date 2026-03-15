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

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include "../enums/bool_enums.h"

#include "ip_endpoint.h"
#include "os_file.h"

namespace corvid { inline namespace proto {
using namespace bool_enums;

// RAII IP socket with type-safe option methods.
//
// Owns an `os_file` handle; lifecycle management and fd-level operations are
// delegated to it. Movable, non-copyable. Intended as a base class for
// `tcp_socket` and `udp_socket`.
class ip_socket {
public:
  using handle_t = os_file::file_handle_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;

  ip_socket() noexcept = default;
  ip_socket(ip_socket&&) noexcept = default;
  ip_socket(const ip_socket&) = delete;

  ip_socket& operator=(ip_socket&&) noexcept = default;
  ip_socket& operator=(const ip_socket&) = delete;

  ~ip_socket() = default;

  // True if the socket handle is valid.
  [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }
  explicit operator bool() const noexcept { return bool{file_}; }

  // Access the underlying file.
  [[nodiscard]] decltype(auto) file(this auto& self) noexcept {
    return (self.file_);
  }

  // Close the socket. Idempotent. Returns true when the socket was open and
  // is now closed, false if it could not be closed (likely because it already
  // was).
  bool close() noexcept { return file_.close(); }

  // Close the socket with the option to perform a forceful close (e.g., via
  // `SO_LINGER` with a zero timeout).
  bool close(close_mode mode) noexcept {
    if (mode == close_mode::forceful && file_.is_open()) {
      const linger abortive{.l_onoff = 1, .l_linger = 0};
      set_option(SOL_SOCKET, SO_LINGER, abortive);
    }

    return file_.close();
  }

  // Socket-specific option access and named helpers.
  // Isolated here so that porting to a new OS requires changes only in this
  // guarded section and the platform header includes above.

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Set a socket option. Returns true on success. Templated to infer
  // `sizeof(T)` automatically and hide the `reinterpret_cast` required by
  // the C `setsockopt` API; callers pass a typed value directly.
  template<typename T>
  bool set_option(int level, int optname, const T& value) noexcept {
    return ::setsockopt(file_.handle(), level, optname,
               reinterpret_cast<const char*>(&value),
               static_cast<socklen_t>(sizeof(T))) == 0;
  }

  // Get a socket option. Returns `std::nullopt` on failure.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_option(int level, int optname) const noexcept {
    T value{};
    socklen_t len = sizeof(T);
    if (::getsockopt(file_.handle(), level, optname,
            reinterpret_cast<char*>(&value), &len) != 0)
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
    return file_.set_nonblocking(on);
  }

  // Bind the socket to a local endpoint. Returns true on success.
  [[nodiscard]] bool bind(const ip_endpoint& ep) noexcept {
    const auto sa = ep.to_sockaddr_storage();
    const socklen_t len =
        ep.is_v4() ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
    return ::bind(file_.handle(), reinterpret_cast<const sockaddr*>(&sa),
               len) == 0;
  }

  // Initiate a connection to `ep`. For non-blocking sockets, `EINPROGRESS`
  // is treated as success (the connection is in progress). Returns true on
  // success or when the connection is underway.
  [[nodiscard]] bool connect(const ip_endpoint& ep) noexcept {
    const auto sa = ep.to_sockaddr_storage();
    const socklen_t len =
        ep.is_v4() ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
    return ::connect(file_.handle(), reinterpret_cast<const sockaddr*>(&sa),
               len) == 0 ||
           errno == EINPROGRESS;
  }

  // Mark the socket as passive and ready to accept connections. `backlog`
  // is the maximum pending connection queue length. Returns true on success.
  [[nodiscard]] bool listen(int backlog = SOMAXCONN) noexcept {
    return ::listen(file_.handle(), backlog) == 0;
  }

  // Accept a pending connection. On Linux, the returned socket is created
  // with `SOCK_CLOEXEC | SOCK_NONBLOCK` via `accept4`. Returns `std::nullopt`
  // when no connection is available (`EAGAIN`/`EWOULDBLOCK`) or an error
  // occurs.
  [[nodiscard]] std::optional<std::pair<ip_socket, ip_endpoint>>
  accept() noexcept {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
#ifdef __linux__
    const int fd = ::accept4(file_.handle(),
        reinterpret_cast<sockaddr*>(&addr), &len,
        SOCK_CLOEXEC | SOCK_NONBLOCK);
#else
    const int fd =
        ::accept(file_.handle(), reinterpret_cast<sockaddr*>(&addr), &len);
#endif
    if (fd < 0) return std::nullopt;
    ip_endpoint peer;
    if (addr.ss_family == AF_INET)
      peer = ip_endpoint{*reinterpret_cast<const sockaddr_in*>(&addr)};
    else if (addr.ss_family == AF_INET6)
      peer = ip_endpoint{*reinterpret_cast<const sockaddr_in6*>(&addr)};
    return std::pair{ip_socket{fd}, peer};
  }
#endif

protected:
  // Construct from an existing native handle; takes ownership.
  explicit ip_socket(handle_t handle) noexcept : file_{handle} {}

  // Create a new socket via `::socket(domain, type, protocol)`. On failure,
  // `file_` remains invalid.
  static auto make_ip_socket(int domain, int type, int protocol) noexcept {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    return ::socket(domain, type, protocol);
#else
    return os_file::invalid_file_handle;
#endif
  }

private:
  os_file file_;
};

}} // namespace corvid::proto
