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
using namespace std::chrono_literals;
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

// `SO_*` wrapper for socket options.
enum class socket_option : int {
  debug = SO_DEBUG,                                                 // 1
  reuse_addr = SO_REUSEADDR,                                        // 2
  type = SO_TYPE,                                                   // 3
  error = SO_ERROR,                                                 // 4
  dontroute = SO_DONTROUTE,                                         // 5
  broadcast = SO_BROADCAST,                                         // 6
  sndbuf = SO_SNDBUF,                                               // 7
  rcvbuf = SO_RCVBUF,                                               // 8
  keep_alive = SO_KEEPALIVE,                                        // 9
  oob_inline = SO_OOBINLINE,                                        // 10
  no_check = SO_NO_CHECK,                                           // 11
  priority = SO_PRIORITY,                                           // 12
  linger = SO_LINGER,                                               // 13
  bsd_compat = SO_BSDCOMPAT,                                        // 14
  reuse_port = SO_REUSEPORT,                                        // 15
  passcred = SO_PASSCRED,                                           // 16
  peercred = SO_PEERCRED,                                           // 17
  rcvlowat = SO_RCVLOWAT,                                           // 18
  sndlowat = SO_SNDLOWAT,                                           // 19
  rcvtimeo = SO_RCVTIMEO,                                           // 20 (old)
  sndtimeo = SO_SNDTIMEO,                                           // 21 (old)
  security_authentication = SO_SECURITY_AUTHENTICATION,             // 22
  security_encryption_transport = SO_SECURITY_ENCRYPTION_TRANSPORT, // 23
  security_encryption_network = SO_SECURITY_ENCRYPTION_NETWORK,     // 24
  bind_to_device = SO_BINDTODEVICE,                                 // 25
  attach_filter = SO_ATTACH_FILTER,                                 // 26
  get_filter = SO_GET_FILTER,                              // 26 (again)
  detach_filter = SO_DETACH_FILTER,                        // 27
  peername = SO_PEERNAME,                                  // 28
  timestamp_old = SO_TIMESTAMP_OLD,                        // 29
  acceptconn = SO_ACCEPTCONN,                              // 30
  peersec = SO_PEERSEC,                                    // 31
  sndbufforce = SO_SNDBUFFORCE,                            // 32
  rcvbufforce = SO_RCVBUFFORCE,                            // 33
  passsec = SO_PASSSEC,                                    // 34
  timestamp_ns_old = SO_TIMESTAMPNS_OLD,                   // 35
  mark = SO_MARK,                                          // 36
  timestamping_old = SO_TIMESTAMPING_OLD,                  // 37
  protocol = SO_PROTOCOL,                                  // 38
  domain = SO_DOMAIN,                                      // 39
  rxq_ovfl = SO_RXQ_OVFL,                                  // 40
  wifi_status = SO_WIFI_STATUS,                            // 41
  peek_off = SO_PEEK_OFF,                                  // 42
  nofcs = SO_NOFCS,                                        // 43
  lock_filter = SO_LOCK_FILTER,                            // 44
  select_err_queue = SO_SELECT_ERR_QUEUE,                  // 45
  busy_poll = SO_BUSY_POLL,                                // 46
  max_pacing_rate = SO_MAX_PACING_RATE,                    // 47
  bpf_extensions = SO_BPF_EXTENSIONS,                      // 48
  incoming_cpu = SO_INCOMING_CPU,                          // 49
  attach_bpf = SO_ATTACH_BPF,                              // 50
  detach_bpf = SO_DETACH_BPF,                              // 27 (again)
  attach_reuseport_cbpf = SO_ATTACH_REUSEPORT_CBPF,        // 51
  attach_reuseport_ebpf = SO_ATTACH_REUSEPORT_EBPF,        // 52
  cnx_advice = SO_CNX_ADVICE,                              // 53
  scm_timestamping_opt_stats = SCM_TIMESTAMPING_OPT_STATS, // 54
  meminfo = SO_MEMINFO,                                    // 55
  incoming_napi_id = SO_INCOMING_NAPI_ID,                  // 56
  cookie = SO_COOKIE,                                      // 57
  scm_timestamping_pktinfo = SCM_TIMESTAMPING_PKTINFO,     // 58
  peergroups = SO_PEERGROUPS,                              // 59
  zerocopy = SO_ZEROCOPY,                                  // 60
  txtime = SO_TXTIME,                                      // 61
  scm_txtime = SCM_TXTIME,                                 // 61 (again)
  bind_to_ifindex = SO_BINDTOIFINDEX,                      // 62
  timestamp_new = SO_TIMESTAMP_NEW,                        // 63
  timestamp_ns_new = SO_TIMESTAMPNS_NEW,                   // 64
  timestamping_new = SO_TIMESTAMPING_NEW,                  // 65
  rcvtimeo_new = SO_RCVTIMEO_NEW,                          // 66
  sndtimeo_new = SO_SNDTIMEO_NEW,                          // 67
  detach_reuseport_bpf = SO_DETACH_REUSEPORT_BPF,          // 68
  prefer_busy_poll = SO_PREFER_BUSY_POLL,                  // 69
  busy_poll_budget = SO_BUSY_POLL_BUDGET,                  // 70
  netns_cookie = SO_NETNS_COOKIE,                          // 71
  buf_lock = SO_BUF_LOCK,                                  // 72
  reserve_mem = SO_RESERVE_MEM,                            // 73
  tx_rehash = SO_TXREHASH,                                 // 74
  rcvmark = SO_RCVMARK,                                    // 75
  passpidfd = SO_PASSPIDFD,                                // 76
  peerpidfd = SO_PEERPIDFD,                                // 77
  SO_MUST_BE_INT32 = 0x7FFF'FFFF
};

// "TCP_* wrapper for TCP-level socket options".
enum class tcp_option : int {
  nodelay = TCP_NODELAY,                           // 1
  maxseg = TCP_MAXSEG,                             // 2
  cork = TCP_CORK,                                 // 3
  keep_idle = TCP_KEEPIDLE,                        // 4
  keep_intvl = TCP_KEEPINTVL,                      // 5
  keep_cnt = TCP_KEEPCNT,                          // 6
  syncnt = TCP_SYNCNT,                             // 7
  linger2 = TCP_LINGER2,                           // 8
  defer_accept = TCP_DEFER_ACCEPT,                 // 9
  window_clamp = TCP_WINDOW_CLAMP,                 // 10
  info = TCP_INFO,                                 // 11
  quickack = TCP_QUICKACK,                         // 12
  congestion = TCP_CONGESTION,                     // 13
  md5sig = TCP_MD5SIG,                             // 14
  cookie_transactions = TCP_COOKIE_TRANSACTIONS,   // 15
  thin_linear_timeouts = TCP_THIN_LINEAR_TIMEOUTS, // 16
  thin_dupack = TCP_THIN_DUPACK,                   // 17
  user_timeout = TCP_USER_TIMEOUT,                 // 18
  repair = TCP_REPAIR,                             // 19
  repair_queue = TCP_REPAIR_QUEUE,                 // 20
  queue_seq = TCP_QUEUE_SEQ,                       // 21
  repair_options = TCP_REPAIR_OPTIONS,             // 22
  fastopen = TCP_FASTOPEN,                         // 23
  timestamp = TCP_TIMESTAMP,                       // 24
  notsent_lowat = TCP_NOTSENT_LOWAT,               // 25
  cc_info = TCP_CC_INFO,                           // 26
  save_syn = TCP_SAVE_SYN,                         // 27
  saved_syn = TCP_SAVED_SYN,                       // 28
  repair_window = TCP_REPAIR_WINDOW,               // 29
  fastopen_connect = TCP_FASTOPEN_CONNECT,         // 30
  ulp = TCP_ULP,                                   // 31
  md5sig_ext = TCP_MD5SIG_EXT,                     // 32
  fastopen_key = TCP_FASTOPEN_KEY,                 // 33
  fastopen_no_cookie = TCP_FASTOPEN_NO_COOKIE,     // 34
  zerocopy_receive = TCP_ZEROCOPY_RECEIVE,         // 35
  inq = TCP_INQ,                                   // 36
  tx_delay = TCP_TX_DELAY,                         // 37
  TCP_MUST_BE_INT32 = 0x7FFF'FFFF
};

}} // namespace corvid::filesys

template<>
constexpr inline auto
    corvid::enums::registry::enum_spec_v<corvid::filesys::socket_type> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::filesys::socket_type,
            "-, stream, datagram, raw, rdm, seqpacket, dccp, , , , packet">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::filesys::address_family> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::filesys::address_family,
        "unspecified, local, inet, ax25, ipx, appletalk, netrom, bridge, "
        "atmpvc, x25, inet6, rose, decnet, netbeui,  security, key, netlink, "
        "packet, ash, econet, atmsvc, rds, sna, irda, pppox, wanpipe, llc, "
        "ib, mpls, can, tipc, bluetooth,  iucv, rxrpc, isdn, phonet, "
        "ieee802154, caif, alg, nfc, vsock, kcm, qipcrtr, smc, xdp, mctp">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::filesys::protocol_type> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::filesys::protocol_type,
        "ip, icmp, igmp, , ipip, , tcp, , egp, , , , pup, , , , , udp, , , , "
        ", idp, , , , , , , , , , , dccp, , , , , , , , ipv6, , routing, "
        "fragment, , rsvp, gre, , , esp, ah, , , , , , , icmpv6, none, "
        "dstopts, , , , , , , , , , , , , , , , , , , , , , , , , , , , , , , "
        ", mtp, , beetph, , , , encap, , , , , pim, , , , , comp, , , , , , , "
        "l2tp, , , , , , , , , , , , , , , , , sctp, , , mh, udplite, mpls, , "
        ", , , , ethernet, , , , , , , , , , , , , , , , , , , , ,  , ,  ,  , "
        ", , , , , , , , , , , , , , , , , , , , , , , , , , , , ,  , , ,  , "
        ", , , , , , , , , , , , , , , , , ,  , , , , , , , , , , , , , , , , "
        ", , , , , , , , , , , , , , , , , , , , raw">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::filesys::socket_option> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::filesys::socket_option,
        ",debug, reuse_addr, type, error, dontroute, broadcast, sndbuf, "
        "rcvbuf, keep_alive, oob_inline, no_check, priority, linger, "
        "bsd_compat, reuse_port,  passcred, peercred, rcvlowat, sndlowat, "
        "rcvtimeo, sndtimeo, security_authentication, "
        "security_encryption_transport, security_encryption_network, "
        "bind_to_device, attach_filter, detach_filter, peername, "
        "timestamp_old, acceptconn, peersec, sndbufforce, rcvbufforce, "
        "passsec, timestamp_ns_old, mark, timestamping_old, protocol, domain, "
        "rxq_ovfl, wifi_status, peek_off, nofcs, lock_filter, "
        "select_err_queue, busy_poll, max_pacing_rate, bpf_extensions, "
        "incoming_cpu, attach_bpf, attach_reuseport_cbpf, "
        "attach_reuseport_ebpf, cnx_advice, scm_timestamping_opt_stats, "
        "meminfo, incoming_napi_id, cookie, scm_timestamping_pktinfo, "
        "peergroups, zerocopy, txtime, bind_to_ifindex, "
        "timestamp_new, timestamp_ns_new, timestamping_new, rcvtimeo_new, "
        "sndtimeo_new, detach_reuseport_bpf, prefer_busy_poll, "
        "busy_poll_budget, netns_cookie, buf_lock, reserve_mem, tx_rehash, "
        "rcvmark, passpidfd, peerpidfd">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::filesys::tcp_option> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::filesys::tcp_option,
        ",nodelay, maxseg, cork, keep_idle, keep_intvl, keep_cnt, syncnt, "
        "linger2, defer_accept, window_clamp, info, quickack, congestion, "
        "md5sig, cookie_transactions, thin_linear_timeouts, thin_dupack, "
        "user_timeout, repair, repair_queue, queue_seq, repair_options, "
        "fastopen, timestamp, notsent_lowat, cc_info, save_syn, saved_syn, "
        "repair_window, fastopen_connect, ulp, md5sig_ext, fastopen_key, "
        "fastopen_no_cookie, zerocopy_receive, inq, tx_delay">();

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
      (void)set_option(socket_option::linger,
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
  // Does not bind on `addr`.
  [[nodiscard]] static net_socket create_for(const sockaddr_storage& addr,
      execution exec = execution::nonblocking,
      message_style style = message_style::stream) noexcept {
    return do_create(address_family{addr.ss_family}, exec, style);
  }

  // Create a blocking socket and connect it to `addr`.
  //
  // As synchronous I/O is not scalable, this is a convenience factory for
  // simple use cases, mean to work with other utility methods with "sync" in
  // their name.
  [[nodiscard]] static net_socket create_sync_connected(
      const sockaddr_storage& addr, std::chrono::milliseconds timeout = 1s) {
    auto sock = net_socket::create_for(addr, execution::blocking);
    if (!sock.is_open()) return {};
    if (timeout.count() > 0) {
      const timeval tv{timeout.count() / 1000,
          (timeout.count() % 1000) * 1000};
      if (!sock.set_option(socket_option::rcvtimeo, tv) ||
          !sock.set_option(socket_option::sndtimeo, tv))
        return {};
    }
    if (::connect(sock.handle(), reinterpret_cast<const sockaddr*>(&addr),
            sockaddr_size(addr)) != 0)
      return {};
    return sock;
  }

  // Create a connected pair of sockets.
  [[nodiscard]] static std::pair<net_socket, net_socket>
  create_pair(address_family domain = address_family::unix,
      socket_type type = socket_type::stream,
      execution exec = execution::nonblocking) noexcept {
    auto combined_type = *type | *socket_type::cloexec;
    if (exec == execution::nonblocking)
      combined_type |= *socket_type::nonblock;
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

  // Get a socket option. Returns `std::nullopt` on failure.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_raw_option(int level, int optname) const noexcept {
    assert(is_open());
    T value{};
    socklen_t len = sizeof(T);
    if (::getsockopt(handle(), level, optname, reinterpret_cast<char*>(&value),
            &len) != 0)
      return std::nullopt;
    return value;
  }

  // Get a socket option at the `SOL_SOCKET` level. Returns `std::nullopt` on
  // failure.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_option(socket_option optname) const noexcept {
    return get_raw_option<T>(SOL_SOCKET, *optname);
  }

  // Get a socket option at the `IPPROTO_TCP` level. Returns `std::nullopt` on
  // failure.
  template<typename T>
  [[nodiscard]] std::optional<T>
  get_option(tcp_option optname) const noexcept {
    return get_raw_option<T>(*protocol_type::tcp, *optname);
  }

  // Allow reuse of a recently-freed local address (`SO_REUSEADDR`).
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

    // Unlike the raw `recv(void*, len)` / `recv(msghdr&)` overloads, this
    // path intentionally permits a closed socket: the kernel returns -1
    // with `EBADF` and the hard-failure branch below trims `data` and reports
    // false. Analyzer flags handle() = -1 as invalid; that's by design here.
    // NOLINTBEGIN(clang-analyzer-unix.StdCLibraryFunctions)
    const ssize_t n =
        ::recv(handle(), data.data() + offset, data.size() - offset, *flags);
    // NOLINTEND(clang-analyzer-unix.StdCLibraryFunctions)
    if (n == 0) return false;

    no_zero::trim_to(data, offset + (n > 0 ? static_cast<size_t>(n) : 0));
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
  // See "iov_msghdr.h".
  [[nodiscard]] ssize_t
  recv(msghdr& msgh, msg_flags flags = {}) const noexcept {
    assert(is_open());
    return ::recvmsg(handle(), &msgh, *flags);
  }

  // Peek at the socket, without consuming data, to determine whether EOF has
  // been reached. Returns `true` if the peer has closed the connection
  // (EOF), `false` if data is available (not EOF), or `std::nullopt` on any
  // error (hard or soft) that prevents a determination (e.g., `EAGAIN`,
  // `EBADF`).
  [[nodiscard]] std::optional<bool> peek_eof() const noexcept {
    char byte;
    const ssize_t n = recv(&byte, 1, msg_flags::peek | msg_flags::dontwait);
    if (n == 0) return true;
    if (n > 0) return false;
    return std::nullopt;
  }

  // Read synchronous socket until `delim` appears in the accumulated buffer.
  // Returns everything up to and including `delim`; trailing bytes stay in
  // `buf` for a subsequent call. Returns empty on EOF, hard error, or timeout.
  //
  // This is a utility method, not optimized for performance.
  [[nodiscard]] std::string
  recv_sync_until(std::string& buf, std::string_view delim) const {
    while (true) {
      const auto pos = buf.find(delim);
      if (pos != std::string::npos) {
        const auto end = pos + delim.size();
        std::string out = buf.substr(0, end);
        buf.erase(0, end);
        return out;
      }
      const size_t old_size = buf.size();
      no_zero::resize_to(buf, old_size + 4096);
      if (!recv(buf)) {
        buf.clear();
        return {};
      }
    }
  }

  // Ensure `buf` contains pending bytes to process. If `buf` is non-empty,
  // returns true immediately (the caller still has unprocessed bytes from a
  // previous read). Otherwise reads the next chunk from `sock` into `buf`.
  // Returns false on EOF, hard error, or timeout, with `buf` cleared.
  [[nodiscard]] bool recv_sync_chunk(std::string& buf) const {
    if (!buf.empty()) return true;
    no_zero::enlarge_to(buf, 4096);
    if (!recv(buf)) {
      buf.clear();
      return false;
    }
    return !buf.empty();
  }

  // Drain any trailing bytes from synchronous socket, up to `max_bytes`, and
  // return true iff the peer reached clean EOF (FIN). Returns false on hard
  // error (e.g., RST) or on timeout without EOF. Useful for asserting that a
  // server closed the connection cleanly after sending its response.
  //
  // This is a utility method, not optimized for performance.
  [[nodiscard]] bool recv_sync_drain_to_eof(
      size_t max_bytes = 4096 * 4ULL) const {
    std::string buf;
    size_t loops = (max_bytes + 4095) / 4096;
    for (size_t loop = 0; loop < loops; ++loop) {
      no_zero::enlarge_to(buf, 4096);
      if (!recv(buf)) return !buf.empty();
      if (buf.empty()) return false;
    }
    return false;
  }

#pragma endregion
#pragma region Send

  // Send as much of `data` as possible on the socket. On success, removes
  // the written prefix from `data` and returns true. On failure, leaves
  // `data` unchanged and returns false. A "soft" failure (e.g., `EAGAIN`)
  // is treated as success with no progress.
  [[nodiscard]] bool send(std::string_view& data) const noexcept {
    if (data.empty()) return true;

    const ssize_t n = send(data.data(), data.size());
    if (n <= 0) return !os_file::is_hard_error();

    data.remove_prefix(static_cast<size_t>(n));
    return true;
  }

  // Send raw bytes from `buf`, forwarding to POSIX `send`.
  [[nodiscard]] ssize_t send(const void* buf, size_t len,
      msg_flags flags = msg_flags::nosignal) const noexcept {
    assert(is_open());
    return ::send(handle(), buf, len, *flags);
  }

  // Send a message described by `msgh`, forwarding to POSIX `sendmsg`. See
  // "iov_msghdr.h".
  [[nodiscard]] ssize_t
  send(msghdr& msgh, msg_flags flags = msg_flags::nosignal) const noexcept {
    assert(is_open());
    return ::sendmsg(handle(), &msgh, *flags);
  }

  // Send all of `data` on a synchronous socket, looping as needed. On success,
  // clears `data` and returns true.
  [[nodiscard]] bool send_sync_all(std::string_view data) const noexcept {
    while (!data.empty()) {
      const auto prev = data.size();
      if (!send(data) || data.size() == prev) return false;
    }
    return true;
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
    if (addr.ss_family == *address_family::inet) return sizeof(sockaddr_in);
    if (addr.ss_family == *address_family::inet6) return sizeof(sockaddr_in6);
    if (addr.ss_family == *address_family::unix) {
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

  // Accept a pending connection. Returns `std::nullopt` when no connection
  // is available (`EAGAIN`/`EWOULDBLOCK`) or an error occurs. The peer
  // address is returned as a raw `sockaddr_storage`; use
  // `net_endpoint{sockaddr_storage}` to convert it if needed.
  [[nodiscard]] std::optional<std::pair<net_socket, sockaddr_storage>>
  accept() noexcept {
    assert(is_open());
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    const int fd = ::accept4(handle(), reinterpret_cast<sockaddr*>(&addr),
        &len, *socket_type::nonblock_cloexec);
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
    type = socket_type{*type | *socket_type::cloexec};
    if (exec == execution::nonblocking)
      type = socket_type{*type | *socket_type::nonblock};
    return net_socket{domain, type, protocol_type{0}};
  }
};

#pragma endregion
}} // namespace corvid::filesys
