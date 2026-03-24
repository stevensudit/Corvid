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
#include <cstddef>
#include <cstring>
#include <optional>
#include <utility>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "../enums/bool_enums.h"

#include "os_file.h"

namespace corvid { inline namespace filesys {
using namespace bool_enums;

// RAII IP socket with type-safe option methods.
//
// `net_socket` is-an `os_file`, adding socket-specific operations on top of
// the shared fd ownership and control helpers. Movable, non-copyable.
//
// `bind` and `connect` accept a `sockaddr_storage`. `net_endpoint` converts
// implicitly, so it can be passed directly. `accept` returns the peer address
// as a raw `sockaddr_storage`; use `net_endpoint{sockaddr_storage}` to convert
// it if needed.
class [[nodiscard]] net_socket: public os_file {
public:
  using handle_t = os_file::file_handle_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;

  net_socket() noexcept = default;
  explicit net_socket(int domain, int type, int protocol) noexcept
      : os_file(::socket(domain, type, protocol)) {}
  explicit net_socket(os_file&& file) noexcept : os_file(std::move(file)) {}

  net_socket(net_socket&&) noexcept = default;
  net_socket(const net_socket&) = delete;

  net_socket& operator=(net_socket&&) noexcept = default;
  net_socket& operator=(const net_socket&) = delete;

  ~net_socket() = default;

  // Create an IPv4 socket. Defaults to non-blocking TCP (`SOCK_STREAM |
  // SOCK_NONBLOCK | SOCK_CLOEXEC`). Pass `message_style::datagram` for UDP,
  // or `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_ipv4(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(AF_INET, exec, style);
  }

  // Create an IPv6 socket. Defaults to non-blocking TCP (`SOCK_STREAM |
  // SOCK_NONBLOCK | SOCK_CLOEXEC`). Pass `message_style::datagram` for UDP,
  // or `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_ipv6(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(AF_INET6, exec, style);
  }

  // Create a Unix domain socket. Defaults to non-blocking stream
  // (`SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC`). Pass
  // `message_style::datagram` for a connectionless UDS, or
  // `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_uds(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(AF_UNIX, exec, style);
  }

  // Create a socket whose address family matches `addr`. The family is read
  // from `addr.ss_family`; if it is unrecognized, the underlying `socket(2)`
  // call will fail and the returned socket will not be open. Defaults to
  // non-blocking stream (`SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC`).
  [[nodiscard]] static net_socket create_for(const sockaddr_storage& addr,
      execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(addr.ss_family, exec, style);
  }

  // Close the socket. Idempotent. Returns true when the socket was open and
  // is now closed, false if it could not be closed (likely because it already
  // was).
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  [[nodiscard]] bool close() noexcept { return os_file::close(); }

  // Close the socket with the option to perform a forceful close (e.g., via
  // `SO_LINGER` with a zero timeout).
  [[nodiscard]] bool close(close_mode mode) noexcept {
    if (mode == close_mode::forceful && is_open())
      (void)set_option(SOL_SOCKET, SO_LINGER,
          linger{.l_onoff = 1, .l_linger = 0});

    return os_file::close();
  }

  // Set a socket option. Returns true on success. Templated to infer
  // `sizeof(T)` automatically and hide the `reinterpret_cast` required by
  // the C `setsockopt` API; callers pass a typed value directly.
  template<typename T>
  [[nodiscard]] bool
  set_option(int level, int optname, const T& value) noexcept {
    assert(is_open());
    return ::setsockopt(handle(), level, optname,
               reinterpret_cast<const char*>(&value),
               static_cast<socklen_t>(sizeof(T))) == 0;
  }

  // Get a socket option. Returns `std::nullopt` on failure.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_option(int level, int optname) const noexcept {
    assert(is_open());
    T value{};
    socklen_t len = sizeof(T);
    if (::getsockopt(handle(), level, optname, reinterpret_cast<char*>(&value),
            &len) != 0)
      return std::nullopt;
    return value;
  }

  // Allow reuse of a recently-freed local address (`SO_REUSEADDR`).
  [[nodiscard]] bool set_reuse_addr(bool on = true) noexcept {
    return set_option(SOL_SOCKET, SO_REUSEADDR, int{on});
  }

  // Allow multiple sockets to bind the same port (`SO_REUSEPORT`).
  [[nodiscard]] bool set_reuse_port(bool on = true) noexcept {
    return set_option(SOL_SOCKET, SO_REUSEPORT, int{on});
  }

  // Disable Nagle algorithm for lower latency (`TCP_NODELAY`).
  [[nodiscard]] bool set_nodelay(bool on = true) noexcept {
    return set_option(IPPROTO_TCP, TCP_NODELAY, int{on});
  }

  // Enable TCP keepalive probes (`SO_KEEPALIVE`).
  [[nodiscard]] bool set_keepalive(bool on = true) noexcept {
    return set_option(SOL_SOCKET, SO_KEEPALIVE, int{on});
  }

  // Set receive buffer size in bytes (`SO_RCVBUF`).
  [[nodiscard]] bool set_recv_buffer_size(int bytes) noexcept {
    return set_option(SOL_SOCKET, SO_RCVBUF, bytes);
  }

  // Set send buffer size in bytes (`SO_SNDBUF`).
  [[nodiscard]] bool set_send_buffer_size(int bytes) noexcept {
    return set_option(SOL_SOCKET, SO_SNDBUF, bytes);
  }

  // Read up to `data.size() - offset` bytes into `data` starting at `offset`.
  //
  // On success, trims `data` to `offset + bytes_read` and returns true. On
  // EOF, leaves `data` unchanged and returns false. On soft error (EAGAIN),
  // trims `data` to `offset` (no new data) and returns true. On hard error,
  // returns false.
  //
  // Status       |  Return  | `data`
  // Success         true      resized to offset + bytes read
  // Soft failure    true      resized to offset (no new data)
  // EOF             false     unchanged, so not empty
  // Hard failure    false     resized to offset
  [[nodiscard]] bool
  recv_at(std::string& data, size_t offset, int flags = 0) const {
    if (offset >= data.size()) return true;

    const ssize_t n =
        ::recv(handle(), data.data() + offset, data.size() - offset, flags);
    if (n == 0) return false;

    no_zero::trim_to(data, offset + (n > 0 ? static_cast<size_t>(n) : 0));
    if (n < 0) return !os_file::is_hard_error();
    return true;
  }

  // Read up to `data.size` bytes from the socket into `data`, honoring
  // `flags` as in POSIX `::recv`.
  //
  // On success, resizes `data` to the number of bytes read and returns true. A
  // "soft" failure (e.g., EAGAIN) is treated as success with zero bytes read.
  // On EOF/disconnect, leaves `data` unchanged and returns false. On hard
  // failure, clears `data` and returns false.
  //
  // Status       |  Return  | `data`
  // Success         true      resized to bytes read
  // Soft failure    true      resized to zero (no new data)
  // EOF             false     unchanged, so not empty
  // Hard failure    false     cleared (empty)
  [[nodiscard]] bool recv(std::string& data, int flags = 0) const {
    return recv_at(data, 0, flags);
  }

  // Receive raw bytes into `buf`, forwarding directly to POSIX `recv`.
  [[nodiscard]] ssize_t recv(void* buf, size_t len, int flags) const noexcept {
    assert(is_open());
    return ::recv(handle(), buf, len,
        flags); // NOLINT(clang-analyzer-unix.BlockInCriticalSection)
  }

  // Peek at the socket, without consuming data, to determine whether EOF has
  // been reached. Returns `true` if the peer has closed the connection (EOF),
  // `false` if data is available (not EOF), or `std::nullopt` on any error
  // (hard or soft) that prevents a determination (e.g., `EAGAIN`, `EBADF`).
  [[nodiscard]] std::optional<bool> peek_eof() const noexcept {
    char byte;
    const ssize_t n = recv(&byte, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0) return true;
    if (n > 0) return false;
    return std::nullopt;
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

  // Send raw bytes from `buf`, forwarding to POSIX `send`.
  [[nodiscard]] ssize_t
  send(const void* buf, size_t len, int flags = MSG_NOSIGNAL) const noexcept {
    assert(is_open());
    return ::send(handle(), buf, len, flags);
  }

  // Return the POSIX socket address size for `addr`. For IPv4 and IPv6,
  // returns the fixed struct size. For UDS pathname sockets, returns only the
  // significant portion of `sun_path` (path length + null terminator +
  // header). For ANS (abstract name sockets, where `sun_path[0] == '\0'`),
  // returns `sizeof(sockaddr_un)` so the full name buffer is transmitted. For
  // unrecognized families, returns `sizeof(sockaddr_storage)`.
  [[nodiscard]] static socklen_t sockaddr_size(
      const sockaddr_storage& addr) noexcept {
    if (addr.ss_family == AF_INET) return sizeof(sockaddr_in);
    if (addr.ss_family == AF_INET6) return sizeof(sockaddr_in6);
    if (addr.ss_family == AF_UNIX) {
      const auto& sun = reinterpret_cast<const sockaddr_un&>(addr);
      if (sun.sun_path[0] == '\0') return sizeof(sockaddr_un); // ANS
      return static_cast<socklen_t>(
          offsetof(sockaddr_un, sun_path) + std::strlen(sun.sun_path) + 1);
    }
    return sizeof(sockaddr_storage);
  }

  // Bind the socket to a local address. Returns true on success.
  [[nodiscard]] bool bind(const sockaddr_storage& addr) noexcept {
    assert(is_open());
    return ::bind(handle(), reinterpret_cast<const sockaddr*>(&addr),
               sockaddr_size(addr)) == 0;
  }

  // Initiate a connection to `addr`. Returns `true` on immediate success,
  // `std::nullopt` when the connection is in progress (`EINPROGRESS`), or
  // `false` on hard failure. For non-blocking sockets, arm `EPOLLOUT` and
  // check `SO_ERROR` on the next writable event to confirm in-progress
  // connects.
  [[nodiscard]] std::optional<bool> connect(
      const sockaddr_storage& addr) noexcept {
    assert(is_open());
    if (::connect(handle(), reinterpret_cast<const sockaddr*>(&addr),
            sockaddr_size(addr)) == 0)
      return true;
    if (errno == EINPROGRESS) return std::nullopt;
    return false;
  }

  // Mark the socket as passive and ready to accept connections. `backlog`
  // is the maximum pending connection queue length. Returns true on success.
  [[nodiscard]] bool listen(int backlog = SOMAXCONN) noexcept {
    assert(is_open());
    return ::listen(handle(), backlog) == 0;
  }

  // Shut down part of a full-duplex connection. `how` is one of `SHUT_RD`,
  // `SHUT_WR`, or `SHUT_RDWR`. Returns true on success.
  [[nodiscard]] bool shutdown(int how) noexcept {
    assert(is_open());
    return ::shutdown(handle(), how) == 0;
  }

  // Accept a pending connection. The returned socket is created with
  // `SOCK_CLOEXEC | SOCK_NONBLOCK` via `accept4`. Returns `std::nullopt` when
  // no connection is available (`EAGAIN`/`EWOULDBLOCK`) or an error occurs.
  // The peer address is returned as a raw `sockaddr_storage`; use
  // `net_endpoint{sockaddr_storage}` to convert it if needed.
  [[nodiscard]] std::optional<std::pair<net_socket, sockaddr_storage>>
  accept() noexcept {
    assert(is_open());
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    const int fd = ::accept4(handle(), reinterpret_cast<sockaddr*>(&addr),
        &len, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) return std::nullopt;
    return std::pair{net_socket{os_file{fd}}, addr};
  }

private:
  [[nodiscard]] static net_socket
  do_create(int domain, execution exec, message_style style) noexcept {
    int type = (style == message_style::stream) ? SOCK_STREAM : SOCK_DGRAM;
    type |= SOCK_CLOEXEC;
    if (exec == execution::nonblocking) type |= SOCK_NONBLOCK;
    return net_socket{domain, type, 0};
  }
};

}} // namespace corvid::filesys
