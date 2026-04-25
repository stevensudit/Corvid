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

#pragma region Enums

// `SOCK_*` wrapper for socket types and flags.
enum class socket_type : int {
  stream = SOCK_STREAM,       // 1
  datagram = SOCK_DGRAM,      // 2
  raw = SOCK_RAW,             // 3
  rdm = SOCK_RDM,             // 4
  seqpacket = SOCK_SEQPACKET, // 5
  dccp = SOCK_DCCP,           // 6
  packet = SOCK_PACKET,       // 10

  cloexec = SOCK_CLOEXEC,                // 0x0200'0000
  nonblock = SOCK_NONBLOCK,              // 0x0000'4000
  nonblock_cloexec = nonblock | cloexec, // 0x0200'4000
  sequence_mask = 0x0000'000F            // aka SOCK_TYPE_MASK
};

// `AF_*` wrapper for address family domains.
enum class address_family : int {
  unspecified = AF_UNSPEC,    // 0
  local = AF_LOCAL,           // 1
  unix = AF_LOCAL,            // 1, aka AF_LOCAL
  file = AF_LOCAL,            // 1, aka AF_LOCAL
  inet = AF_INET,             // 2
  ax25 = AF_AX25,             // 3
  ipx = AF_IPX,               // 4
  appletalk = AF_APPLETALK,   // 5
  netrom = AF_NETROM,         // 6
  bridge = AF_BRIDGE,         // 7
  atmpvc = AF_ATMPVC,         // 8
  x25 = AF_X25,               // 9
  inet6 = AF_INET6,           // 10
  rose = AF_ROSE,             // 11
  decnet = AF_DECnet,         // 12
  netbeui = AF_NETBEUI,       // 13
  security = AF_SECURITY,     // 14
  key = AF_KEY,               // 15
  netlink = AF_NETLINK,       // 16
  route = AF_ROUTE,           // 16, aka AF_NETLINK
  packet = AF_PACKET,         // 17
  ash = AF_ASH,               // 18
  econet = AF_ECONET,         // 19
  atmsvc = AF_ATMSVC,         // 20
  rds = AF_RDS,               // 21
  sna = AF_SNA,               // 22
  irda = AF_IRDA,             // 23
  pppox = AF_PPPOX,           // 24
  wanpipe = AF_WANPIPE,       // 25
  llc = AF_LLC,               // 26
  ib = AF_IB,                 // 27
  mpls = AF_MPLS,             // 28
  can = AF_CAN,               // 29
  tipc = AF_TIPC,             // 30
  bluetooth = AF_BLUETOOTH,   // 31
  iucv = AF_IUCV,             // 32
  rxrpc = AF_RXRPC,           // 33
  isdn = AF_ISDN,             // 34
  phonet = AF_PHONET,         // 35
  ieee802154 = AF_IEEE802154, // 36
  caif = AF_CAIF,             // 37
  alg = AF_ALG,               // 38
  nfc = AF_NFC,               // 39
  vsock = AF_VSOCK,           // 40
  kcm = AF_KCM,               // 41
  qipcrtr = AF_QIPCRTR,       // 42
  smc = AF_SMC,               // 43
  xdp = AF_XDP,               // 44
  mctp = AF_MCTP,             // 45
  max = AF_MAX,               // 46
  AF_MUST_BE_INT32 = 0x7FFF'FFFF
};

// `IPPROTO_*` wrapper for protocol types.
enum class protocol_type : int {
  ip = IPPROTO_IP,             // 0
  icmp = IPPROTO_ICMP,         // 1
  igmp = IPPROTO_IGMP,         // 2
  ipip = IPPROTO_IPIP,         // 4
  tcp = IPPROTO_TCP,           // 6
  egp = IPPROTO_EGP,           // 8
  pup = IPPROTO_PUP,           // 12
  udp = IPPROTO_UDP,           // 17
  idp = IPPROTO_IDP,           // 22
  tp = IPPROTO_TP,             // 29
  dccp = IPPROTO_DCCP,         // 33
  ipv6 = IPPROTO_IPV6,         // 41
  routing = IPPROTO_ROUTING,   // 43
  fragment = IPPROTO_FRAGMENT, // 44
  rsvp = IPPROTO_RSVP,         // 46
  gre = IPPROTO_GRE,           // 47
  esp = IPPROTO_ESP,           // 50
  ah = IPPROTO_AH,             // 51
  icmpv6 = IPPROTO_ICMPV6,     // 58
  none = IPPROTO_NONE,         // 59
  dstopts = IPPROTO_DSTOPTS,   // 60
  mtp = IPPROTO_MTP,           // 92
  beetph = IPPROTO_BEETPH,     // 94, was IPPROTO_IPIP
  encap = IPPROTO_ENCAP,       // 98
  pim = IPPROTO_PIM,           // 103
  comp = IPPROTO_COMP,         // 108
  l2tp = IPPROTO_L2TP,         // 115
  sctp = IPPROTO_SCTP,         // 132
  mh = IPPROTO_MH,             // 135
  udplite = IPPROTO_UDPLITE,   // 136
  mpls = IPPROTO_MPLS,         // 137
  ethernet = IPPROTO_ETHERNET, // 143
  raw = IPPROTO_RAW,           // 255
  max = 256,
  IPPROTO_MUST_BE_INT32 = 0x7FFF'FFFF
};
}} // namespace corvid::filesys

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::filesys::socket_type> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::filesys::socket_type,
        "stream, datagram, raw, rdm, seqpacket, dccp, U7, U8, U9, packet">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::filesys::address_family> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::filesys::address_family,
        "unspecified, local, inet, ax25, ipx, appletalk, netrom, bridge, "
        "atmpvc, x25, inet6, rose, decnet, netbeui,  security, key, "
        "netlink, packet, ash, econet, atmsvc, rds, sna, irda, pppox, "
        "wanpipe, llc, ib, mpls, can, tipc, bluetooth,  iucv, rxrpc, isdn, "
        "phonet, ieee802154, caif, alg, nfc, vsock, kcm, qipcrtr, smc, xdp, "
        "mctp">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::filesys::protocol_type> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::filesys::protocol_type,
        "ip, icmp, igmp, U4, ipip, U6, tcp, U8, egp, U10, U11, pup, U13, U14, "
        "U15, U16, udp, U18, U19, U20, U21, idp, U23, U24, U25, U26, U27, "
        "U28, U29, U30, U31, U32, dccp, U34, U35, U36, U37, U38, U39, U40, "
        "ipv6, U42, routing, fragment, U45, rsvp, gre, U48, U49, esp, ah, "
        "U52, U53, U54, U55, U56, U57, icmpv6, none, dstopts, U61, U62, U63, "
        "U64, U65, U66, U67, U68, U69, U70, U71, U72, U73, U74, U75, U76, "
        "U77, U78, U79, U80, U81, U82, U83, U84, U85, U86, U87, U88, U89, "
        "U90, U91, U92, mtp, U93, beetph, U95, U96, U97, encap, U99, U100, "
        "U101, pim, U104, U105, U106, U107, comp, U109, U110, U111, U112, "
        "U113, U114, l2tp, U116, U117, U118, U119, U120, U121, U122, U123, "
        "U124, U125, U126, U127, U128, U129, U130, U131, sctp, U133, U134, "
        "mh, udplite, mpls, U138, U139, U140, U141, U142, ethernet, raw">();

namespace corvid { inline namespace filesys {

#pragma endregion
#pragma region net_socket

// RAII IP socket with type-safe option methods.
//
// `net_socket` is-an `os_file`, adding socket-specific operations on top
// of the shared fd ownership and control helpers. Movable, non-copyable.
//
// `bind` and `connect` accept a `sockaddr_storage`. `net_endpoint`
// converts implicitly, so it can be passed directly. `accept` returns the
// peer address as a raw `sockaddr_storage`; use
// `net_endpoint{sockaddr_storage}` to convert it if needed.
class [[nodiscard]] net_socket: public os_file {
#pragma region Construction
public:
  using handle_t = os_file::file_handle_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;

  net_socket() noexcept = default;
  explicit net_socket(address_family domain, socket_type type,
      protocol_type protocol) noexcept
      : os_file(::socket(*domain, *type, *protocol)) {}
  explicit net_socket(os_file&& file) noexcept : os_file(std::move(file)) {}

  net_socket(net_socket&&) noexcept = default;
  net_socket(const net_socket&) = delete;

  net_socket& operator=(net_socket&&) noexcept = default;
  net_socket& operator=(const net_socket&) = delete;

#pragma endregion
#pragma region Destruction

  // Close the socket. Idempotent. Returns true when the socket was open and
  // is now closed, false if it could not be closed (likely because it
  // already was).
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  [[nodiscard]] bool close() noexcept { return os_file::close(); }

  // Close the socket. In `graceful` mode, performs a normal close (FIN/ACK).
  // In `forceful` mode, performs a forceful close (RST).
  [[nodiscard]] bool close(close_mode mode) noexcept {
    if (mode == close_mode::forceful && is_open())
      (void)set_option(SOL_SOCKET, SO_LINGER,
          linger{.l_onoff = 1, .l_linger = 0});

    return os_file::close();
  }

  // Shut down part of a full-duplex connection. `how` is one of `SHUT_RD`,
  // `SHUT_WR`, or `SHUT_RDWR`. Returns true on success.
  [[nodiscard]] bool shutdown(int how) noexcept {
    assert(is_open());
    return ::shutdown(handle(), how) == 0;
  }

#pragma endregion
#pragma region Factories

  // Create an IPv4 socket. Defaults to non-blocking TCP (`SOCK_STREAM |
  // SOCK_NONBLOCK | SOCK_CLOEXEC`). Pass `message_style::datagram` for UDP,
  // or `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_ipv4(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(address_family::inet, exec, style);
  }

  // Create an IPv6 socket. Defaults to non-blocking TCP (`SOCK_STREAM |
  // SOCK_NONBLOCK | SOCK_CLOEXEC`). Pass `message_style::datagram` for UDP,
  // or `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_ipv6(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(address_family::inet6, exec, style);
  }

  // Create a Unix domain socket. Defaults to non-blocking stream
  // (`SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC`). Pass
  // `message_style::datagram` for a connectionless UDS, or
  // `execution::blocking` to omit `SOCK_NONBLOCK`.
  [[nodiscard]] static net_socket
  create_uds(execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(address_family::unix, exec, style);
  }

  // Create a socket whose address family matches `addr`. The family is read
  // from `addr.ss_family`; if it is unrecognized, the underlying `socket(2)`
  // call will fail and the returned socket will not be open. Defaults to
  // non-blocking stream (`SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC`).
  [[nodiscard]] static net_socket create_for(const sockaddr_storage& addr,
      execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(address_family{addr.ss_family}, exec, style);
  }

  // Create a connected pair of sockets.
  [[nodiscard]] static std::pair<net_socket, net_socket>
  create_pair(address_family domain = address_family::unix,
      socket_type type = socket_type::stream,
      execution exec = execution::nonblocking) noexcept {
    auto combined_type = *type;
    if (exec == execution::nonblocking)
      combined_type |= SOCK_NONBLOCK | SOCK_CLOEXEC;
    int fds[2];
    if (::socketpair(*domain, combined_type, 0, fds) == 0)
      return {net_socket{os_file{fds[0]}}, net_socket{os_file{fds[1]}}};
    return {};
  }

#pragma endregion
#pragma region Options

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
  // On success, resizes `data` to the number of bytes read and returns true.
  // A "soft" failure (e.g., EAGAIN) is treated as success with zero bytes
  // read. On EOF/disconnect, leaves `data` unchanged and returns false. On
  // hard failure, clears `data` and returns false.
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
    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    return ::recv(handle(), buf, len, flags);
  }

  // Receive a message into `msg`, forwarding directly to POSIX `recvmsg`.
  // See "iov_msghdr.h".
  [[nodiscard]] ssize_t recv(msghdr& msg, int flags = 0) const noexcept {
    assert(is_open());
    return ::recvmsg(handle(), &msg, flags);
  }

  // Peek at the socket, without consuming data, to determine whether EOF has
  // been reached. Returns `true` if the peer has closed the connection
  // (EOF), `false` if data is available (not EOF), or `std::nullopt` on any
  // error (hard or soft) that prevents a determination (e.g., `EAGAIN`,
  // `EBADF`).
  [[nodiscard]] std::optional<bool> peek_eof() const noexcept {
    char byte;
    const ssize_t n = recv(&byte, 1, MSG_PEEK | MSG_DONTWAIT);
    if (n == 0) return true;
    if (n > 0) return false;
    return std::nullopt;
  }

#pragma endregion
#pragma region Send

  // Send as much of `data` as possible on the socket. On success, removes
  // the written prefix from `data` and returns true. On failure, leaves
  // `data` unchanged and returns false. A "soft" failure (e.g., EAGAIN) is
  // treated as success with no progress.
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

  // Send a message described by `msg`, forwarding to POSIX `sendmsg`. See
  // "iov_msghdr.h".
  [[nodiscard]] ssize_t
  send(msghdr& msg, int flags = MSG_NOSIGNAL) const noexcept {
    assert(is_open());
    return ::sendmsg(handle(), &msg, flags);
  }

#pragma endregion
#pragma region Connecting

  // Return the POSIX socket address size for `addr`. For IPv4 and IPv6,
  // returns the fixed struct size. For UDS pathname sockets, returns only
  // the significant portion of `sun_path` (path length + null terminator +
  // header). For ANS (abstract name sockets, where `sun_path[0] == '\0'`),
  // returns `sizeof(sockaddr_un)` so the full name buffer is transmitted.
  // For unrecognized families, returns `sizeof(sockaddr_storage)`.
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
    if (e_code_is(EC::inprogress)) return std::nullopt;
    return false;
  }

  // Mark the socket as passive and ready to accept connections. `backlog`
  // is the maximum pending connection queue length. Returns true on success.
  [[nodiscard]] bool listen(int backlog = SOMAXCONN) noexcept {
    assert(is_open());
    return ::listen(handle(), backlog) == 0;
  }

  // Accept a pending connection. The returned socket is created with
  // `SOCK_CLOEXEC | SOCK_NONBLOCK` via `accept4`. Returns `std::nullopt`
  // when no connection is available (`EAGAIN`/`EWOULDBLOCK`) or an error
  // occurs. The peer address is returned as a raw `sockaddr_storage`; use
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
  [[nodiscard]] static net_socket do_create(address_family domain,
      execution exec, message_style style) noexcept {
    socket_type type =
        (style == message_style::stream)
            ? socket_type::stream
            : socket_type::datagram;
    if (exec == execution::nonblocking)
      type = socket_type{*type | *socket_type::nonblock};
    return net_socket{domain, type, protocol_type{0}};
  }
};

#pragma endregion
}} // namespace corvid::filesys
