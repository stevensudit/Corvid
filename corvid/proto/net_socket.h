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

// Linux-only: Windows sockets are a different API (Winsock `SOCKET`).
#ifdef _WIN32
#error "\"net_socket.h\" is Linux-only."
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <sys/socket.h>

#include "../enums/bool_enums.h"
#include "../filesys/os_file.h"
#include "sockaddr_view.h"
#include "socket_enums.h"

namespace corvid { inline namespace proto {
using namespace std::chrono_literals;
using namespace bool_enums;

#pragma region net_socket

// RAII network socket with type-safe option methods.
//
// `net_socket` is-an `os_file`, adding socket-specific operations on top
// of the shared fd ownership and control helpers. Movable, non-copyable.
//
// `bind` and `connect` accept a `sockaddr_view`. `net_endpoint` converts
// implicitly, so it can be passed directly.
class [[nodiscard]] net_socket: public os_file {
#pragma region Construction
public:
  using handle_t = os_file::file_handle_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;

  net_socket() noexcept = default;
  explicit net_socket(os_file&& file) noexcept : os_file{std::move(file)} {}

  net_socket(net_socket&&) noexcept = default;
  net_socket(const net_socket&) = delete;

  net_socket& operator=(net_socket&&) noexcept = default;
  net_socket& operator=(const net_socket&) = delete;

#pragma endregion
#pragma region Destruction

  // Close the socket.
  //
  // Idempotent. Returns true when the socket was open and is now closed, false
  // if it could not be closed (likely because it already was).
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  [[nodiscard]] bool close() noexcept { return os_file::close(); }

  // Close the socket using the specified mode.
  //
  // In `graceful` mode, performs a normal close (FIN/ACK). In `forceful` mode,
  // performs a forceful close (RST).
  [[nodiscard]] bool close(close_mode mode) noexcept {
    if (mode == close_mode::forceful && is_open())
      (void)set_option(socket_option::linger,
          linger{.l_onoff = 1, .l_linger = 0});

    return os_file::close();
  }

  // Shut down part of a full-duplex connection.
  [[nodiscard]] bool shutdown(shutdown_how how) noexcept {
    assert(is_open());
    return ::shutdown(handle(), *how) == 0;
  }

#pragma endregion
#pragma region Factories

  // Create an IPv4 socket.
  //
  // Defaults to non-blocking TCP (`SOCK_STREAM | SOCK_NONBLOCK |
  // SOCK_CLOEXEC`). Pass `message_style::datagram` for UDP, or
  // `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_ipv4(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(address_family::inet, exec, style);
  }

  // Create an IPv6 socket.
  //
  // Defaults to non-blocking TCP (`SOCK_STREAM | SOCK_NONBLOCK |
  // SOCK_CLOEXEC`). Pass `message_style::datagram` for UDP, or
  // `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_ipv6(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(address_family::inet6, exec, style);
  }

  // Create a Unix domain socket.
  //
  // Defaults to non-blocking stream (`SOCK_STREAM | SOCK_NONBLOCK |
  // SOCK_CLOEXEC`). Pass `message_style::datagram` for a connectionless UDS,
  // or `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_uds(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(address_family::unix, exec, style);
  }

  // Create a socket whose address family matches `target`.
  //
  // The family is read from the address; if it is unrecognized, the
  // underlying `socket(2)` call will fail and the returned socket will not be
  // open. Defaults to non-blocking stream (`SOCK_STREAM | SOCK_NONBLOCK |
  // SOCK_CLOEXEC`). Does not bind on `target`.
  [[nodiscard]] static net_socket create_for(sockaddr_view target,
      execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    if (!target) return {};
    return do_create(address_family{target.family()}, exec, style);
  }

  // Create a blocking socket and connect it to `target`.
  //
  // As synchronous I/O is not scalable, this is a convenience factory for
  // simple use cases, meant to work with other utility methods with "sync" in
  // their name.
  [[nodiscard]] static net_socket create_sync_connected(sockaddr_view target,
      std::chrono::milliseconds timeout = 1s) noexcept {
    auto sock = net_socket::create_for(target, execution::blocking);
    if (!sock.is_open()) return {};
    if (timeout > 0ms) {
      const timeval tv{timeout / 1s, (timeout % 1s) / 1us};
      if (!sock.set_option(socket_option::rcvtimeo, tv) ||
          !sock.set_option(socket_option::sndtimeo, tv))
        return {};
    }
    if (!sock.connect(target).value_or(false)) return {};
    return sock;
  }

  // Create a connected pair of UDS sockets.
  [[nodiscard]] static std::pair<net_socket, net_socket>
  create_pair(address_family domain = address_family::unix,
      socket_type type = socket_type::stream,
      execution exec = execution::nonblocking) noexcept {
    auto combined_type = *type | *socket_type::cloexec;
    if (exec == execution::nonblocking)
      combined_type |= *socket_type::nonblock;
    int fds[2]{};
    if (::socketpair(*domain, combined_type, 0, fds) == 0)
      return {net_socket{os_file{fds[0]}}, net_socket{os_file{fds[1]}}};
    return {};
  }

#pragma endregion
#pragma region Options

  // Set a socket option.
  //
  // Templated to infer `sizeof(T)` automatically and hide the
  // `reinterpret_cast` required by the C `setsockopt` API; callers pass a
  // typed value directly.
  template<typename T>
  [[nodiscard]] bool
  set_raw_option(int level, int optname, const T& value) noexcept {
    assert(is_open());
    return ::setsockopt(handle(), level, optname,
               reinterpret_cast<const char*>(&value),
               static_cast<socklen_t>(sizeof(T))) == 0;
  }

  // Set a socket option at the `SOL_SOCKET` level.
  template<typename T>
  [[nodiscard]] bool
  set_option(socket_option optname, const T& value) noexcept {
    return set_raw_option(SOL_SOCKET, *optname, value);
  }

  // Set a socket option at the `IPPROTO_TCP` level.
  template<typename T>
  [[nodiscard]] bool set_option(tcp_option optname, const T& value) noexcept {
    return set_raw_option(*protocol_type::tcp, *optname, value);
  }

  // Get a socket option.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_raw_option(int level, int optname) const noexcept {
    assert(is_open());
    T value{};
    socklen_t len{sizeof(T)};
    if (::getsockopt(handle(), level, optname, reinterpret_cast<char*>(&value),
            &len) != 0)
      return std::nullopt;
    return value;
  }

  // Get a socket option at the `SOL_SOCKET` level.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_option(socket_option optname) const noexcept {
    return get_raw_option<T>(SOL_SOCKET, *optname);
  }

  // Get a socket option at the `IPPROTO_TCP` level.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_option(tcp_option optname) const noexcept {
    return get_raw_option<T>(*protocol_type::tcp, *optname);
  }

  // Allow reuse of a recently freed local address (`SO_REUSEADDR`).
  [[nodiscard]] bool set_reuse_addr(bool on = true) noexcept {
    return set_option(socket_option::reuse_addr, int{on});
  }

  // Allow multiple sockets to bind the same port (`SO_REUSEPORT`).
  [[nodiscard]] bool set_reuse_port(bool on = true) noexcept {
    return set_option(socket_option::reuse_port, int{on});
  }

  // Disable Nagle algorithm for lower latency (`TCP_NODELAY`).
  [[nodiscard]] bool set_nodelay(bool on = true) noexcept {
    return set_option(tcp_option::nodelay, int{on});
  }

  // Enable TCP keepalive probes (`SO_KEEPALIVE`).
  [[nodiscard]] bool set_keepalive(bool on = true) noexcept {
    return set_option(socket_option::keep_alive, int{on});
  }

  // Set receive buffer size in bytes (`SO_RCVBUF`).
  [[nodiscard]] bool set_recv_buffer_size(int bytes) noexcept {
    return set_option(socket_option::rcvbuf, bytes);
  }

  // Set send buffer size in bytes (`SO_SNDBUF`).
  [[nodiscard]] bool set_send_buffer_size(int bytes) noexcept {
    return set_option(socket_option::sndbuf, bytes);
  }

#pragma endregion
#pragma region Recv

  // Read up to `data.size() - offset` bytes into `data` starting at
  // `offset`.
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
  recv_at(std::string& data, size_t offset, msg_flags flags = {}) const {
    if (offset >= data.size()) return true;

    // Unlike the raw `recv(void*, len)` / `recv(msghdr&)` overloads, this path
    // intentionally permits a closed socket: the kernel returns -1 with
    // `EBADF` and the hard-failure branch below trims `data` and reports
    // false. Analyzer flags handle() = -1 as invalid; that's by design here.
    //
    // `BlockInCriticalSection` is suppressed because the analyzer can't tell
    // `weak_ptr::lock` (atomic CAS on the control block, no critical section)
    // from `mutex::lock`; once it sees a `shared_from_this` anywhere upstream
    // it tags this `recv` as blocking-in-a-lock. We don't call `recv` while
    // holding any actual lock.
    // NOLINTBEGIN(clang-analyzer-unix.StdCLibraryFunctions,clang-analyzer-unix.BlockInCriticalSection)
    const auto n =
        ::recv(handle(), data.data() + offset, data.size() - offset, *flags);
    // NOLINTEND(clang-analyzer-unix.StdCLibraryFunctions,clang-analyzer-unix.BlockInCriticalSection)
    if (n == 0) return false;

    no_zero{data}.trim_to(offset + ((n > 0) ? static_cast<size_t>(n) : 0));
    if (n < 0) return !os_file::is_hard_error();
    return true;
  }

  // Read up to `data.size` bytes from the socket into `data`, honoring
  // `flags` as in POSIX `::recv`.
  //
  // On success, resizes `data` to the number of bytes read and returns true.
  // A "soft" failure (e.g., `EAGAIN`) is treated as success with zero bytes
  // read. On EOF/disconnect, leaves `data` unchanged and returns false. On
  // hard failure, clears `data` and returns false.
  //
  // Status       |  Return  | `data`
  // Success         true      resized to bytes read
  // Soft failure    true      resized to zero (no new data)
  // EOF             false     unchanged, so not empty
  // Hard failure    false     cleared (empty)
  [[nodiscard]] bool recv(std::string& data, msg_flags flags = {}) const {
    return recv_at(data, 0, flags);
  }

  // Receive raw bytes into `buf`, forwarding directly to POSIX `recv`.
  [[nodiscard]] ssize_t
  recv(void* buf, size_t len, msg_flags flags = {}) const noexcept {
    assert(is_open());
    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    return ::recv(handle(), buf, len, *flags);
  }

  // Receive a message into `msg`, forwarding directly to POSIX `recvmsg`.
  //
  // See "iov_msghdr.h".
  [[nodiscard]] ssize_t
  recv(msghdr& msgh, msg_flags flags = {}) const noexcept {
    assert(is_open());
    return ::recvmsg(handle(), &msgh, *flags);
  }

  // Peek at the socket, without consuming data, to determine whether EOF has
  // been reached.
  //
  // Returns `true` if the peer has closed the connection (EOF), `false` if
  // data is available (not EOF), or `std::nullopt` on any error (hard or soft)
  // that prevents a determination (e.g., `EAGAIN`, `EBADF`).
  [[nodiscard]] std::optional<bool> peek_eof() const noexcept {
    if (!is_open()) return false;
    char byte{};
    const auto n = recv(&byte, 1, msg_flags::peek | msg_flags::dontwait);
    if (n == 0) return true;
    if (n > 0) return false;
    return std::nullopt;
  }

  // Read synchronous socket until `delim` appears in the accumulated buffer.
  //
  // Returns everything up to and including `delim`; trailing bytes stay in
  // `buf` for a subsequent call. Returns empty on EOF, hard error, timeout,
  // or if `buf` would grow beyond `max_size` without finding the delimiter.
  //
  // This is a utility method, not optimized for performance.
  [[nodiscard]] std::string recv_sync_until(std::string& buf,
      std::string_view delim, size_t max_size = 4096UZ * 16) const {
    for (;;) {
      if (const auto pos = buf.find(delim); pos != std::string::npos) {
        const auto end = pos + delim.size();
        auto out = buf.substr(0, end);
        buf.erase(0, end);
        return out;
      }
      const auto old_size = buf.size();
      if (old_size >= max_size) break;
      no_zero{buf}.resize_to(std::min(old_size + 4096, max_size));
      if (!recv_at(buf, old_size) || buf.size() == old_size) break;
    }
    buf.clear();
    return {};
  }

  // Ensure `buf` contains pending bytes to process.
  //
  // If `buf` is non-empty, returns true immediately (the caller still has
  // unprocessed bytes from a previous read). Otherwise reads the next chunk
  // from `sock` into `buf`. Returns false on EOF, hard error, or timeout, with
  // `buf` cleared.
  [[nodiscard]] bool
  recv_sync_chunk(std::string& buf, size_t max_bytes = 4096UZ) const {
    if (!buf.empty()) return true;
    if (!recv(*no_zero{buf}.enlarge_to(max_bytes))) {
      buf.clear();
      return false;
    }
    return !buf.empty();
  }

  // Drain any trailing bytes from synchronous socket, up to `max_bytes`, and
  // return true iff the peer reached clean EOF (FIN).
  //
  // Returns false on hard error (e.g., RST) or on timeout without EOF. Useful
  // for asserting that a server closed the connection cleanly after sending
  // its response.
  //
  // This is a utility method, not optimized for performance.
  [[nodiscard]] bool recv_sync_drain_to_eof(
      size_t max_bytes = 4096UZ * 4) const {
    std::string buf;
    for (auto bytes_read = 0UZ; bytes_read < max_bytes;
        bytes_read += buf.size())
    {
      const auto chunk = std::min<size_t>(4096, max_bytes - bytes_read);
      if (!recv(*no_zero{buf}.resize_to(chunk))) return !buf.empty();
      if (buf.empty()) return false;
    }
    return false;
  }

#pragma endregion
#pragma region Send

  // Send as much of `data` as possible on the socket.
  //
  // On success, removes the written prefix from `data` and returns true. On
  // failure, leaves `data` unchanged and returns false. A "soft" failure
  // (e.g., `EAGAIN`) is treated as success with no progress.
  [[nodiscard]] bool send(std::string_view& data) const noexcept {
    if (data.empty()) return true;

    const auto n = send(data.data(), data.size());
    if (n == 0) return false;
    if (n < 0) return !os_file::is_hard_error();

    data.remove_prefix(static_cast<size_t>(n));
    return true;
  }

  // Send raw bytes from `buf`, forwarding to POSIX `send`.
  [[nodiscard]] ssize_t send(const void* buf, size_t len,
      msg_flags flags = msg_flags::nosignal) const noexcept {
    assert(is_open());
    return ::send(handle(), buf, len, *flags);
  }

  // Send a message described by `msgh`, forwarding to POSIX `sendmsg`.
  //
  // See "iov_msghdr.h".
  [[nodiscard]] ssize_t
  send(msghdr& msgh, msg_flags flags = msg_flags::nosignal) const noexcept {
    assert(is_open());
    return ::sendmsg(handle(), &msgh, *flags);
  }

  // Send all of `data` on a synchronous socket, looping as needed.
  [[nodiscard]] bool send_sync_all(std::string_view data) const noexcept {
    while (!data.empty()) {
      const auto prev = data.size();
      if (!send(data) || data.size() == prev) return false;
    }
    return true;
  }
#pragma endregion
#pragma region Connecting

  // Bind the socket to a local address. Returns true on success.
  [[nodiscard]] bool bind(sockaddr_view target) noexcept {
    assert(is_open());
    return ::bind(handle(), target.addr, target.addrlen) == 0;
  }

  // Initiate a connection to `target`.
  //
  // Returns `true` on immediate success, `std::nullopt` when the connection is
  // in progress (`EINPROGRESS`), or `false` on hard failure. For non-blocking
  // sockets, arm `EPOLLOUT` and check `SO_ERROR` on the next writable event to
  // confirm in-progress connects.
  [[nodiscard]] std::optional<bool> connect(sockaddr_view target) noexcept {
    assert(is_open());
    if (::connect(handle(), target.addr, target.addrlen) == 0) return true;
    if (os_error::last().code() == EC::inprogress) return std::nullopt;
    return false;
  }

  // Mark the socket as passive and ready to accept connections.
  //
  // `backlog` is the maximum pending connection queue length. Returns true on
  // success.
  [[nodiscard]] bool listen(int backlog = SOMAXCONN) noexcept {
    assert(is_open());
    return ::listen(handle(), backlog) == 0;
  }

  // Accept a pending connection, filling `peer` with the peer address and
  // its exact kernel-reported length.
  //
  // Pass `net_endpoint::as_ref` to capture the address directly into an
  // endpoint. When no connection is available (`EAGAIN`/`EWOULDBLOCK`) or an
  // error occurs, returns a closed socket and leaves `peer` unmodified.
  [[nodiscard]] net_socket accept(sockaddr_buffer_ref peer) noexcept {
    assert(is_open());
    socklen_t len{sizeof(peer.addr)};
    const auto fd = ::accept4(handle(),
        reinterpret_cast<sockaddr*>(&peer.addr), &len,
        *socket_type::nonblock_cloexec);
    if (fd < 0) return {};
    peer.addrlen = len;
    return net_socket{os_file{fd}};
  }

#pragma endregion
#pragma region Implementation
private:
  [[nodiscard]] static net_socket do_create(address_family domain,
      execution exec, message_style style) noexcept {
    auto type =
        (style == message_style::stream)
            ? socket_type::stream
            : socket_type::datagram;
    type = socket_type{*type | *socket_type::cloexec};
    if (exec == execution::nonblocking)
      type = socket_type{*type | *socket_type::nonblock};
    return net_socket{os_file{::socket(*domain, *type, *protocol_type{0})}};
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::proto
