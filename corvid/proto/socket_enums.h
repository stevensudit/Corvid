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
#error "\"socket_enums.h\" is Linux-only."
#endif
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#include "../enums/sequence_enum.h"

namespace corvid { inline namespace proto {

#pragma region socket_type

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

consteval auto corvid_enum_spec(socket_type*) {
  return corvid::enums::sequence::make_sequence_enum_spec<socket_type,
      "stream,datagram,raw,rdm,seqpacket,dccp,,,,packet", wrapclip{},
      socket_type{1}>();
}

#pragma endregion
#pragma region address_family

// `AF_*` wrapper for address family domains.
// NOLINTNEXTLINE(performance-enum-size)
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
};

consteval auto corvid_enum_spec(address_family*) {
  return corvid::enums::sequence::make_sequence_enum_spec<address_family,
      "unspecified,local,inet,ax25,ipx,appletalk,netrom,bridge,atmpvc,x25,"
      "inet6,rose,decnet,netbeui,security,key,netlink,packet,ash,econet,"
      "atmsvc,rds,sna,irda,pppox,wanpipe,llc,ib,mpls,can,tipc,bluetooth,iucv,"
      "rxrpc,isdn,phonet,ieee802154,caif,alg,nfc,vsock,kcm,qipcrtr,smc,xdp,"
      "mctp">();
}

#pragma endregion
#pragma region protocol_type

// `IPPROTO_*` wrapper for protocol types.
// NOLINTNEXTLINE(performance-enum-size)
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
};

consteval auto corvid_enum_spec(protocol_type*) {
  return corvid::enums::sequence::make_sequence_enum_spec<protocol_type,
      "0,ip,icmp,igmp,,ipip,,tcp,,egp,,,,pup|17,udp,,,,,idp|29,tp|33,dccp|41,"
      "ipv6,,routing,fragment,,rsvp,gre,,,esp,ah|58,icmpv6,none,dstopts|92,"
      "mtp,,beetph|98,encap|103,pim|108,comp|115,l2tp|132,sctp,,,mh,udplite,"
      "mpls|143,ethernet|255,raw">();
}

#pragma endregion
#pragma region socket_option

// `SO_*` wrapper for socket options.
// NOLINTNEXTLINE(performance-enum-size)
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
};

consteval auto corvid_enum_spec(socket_option*) {
  return corvid::enums::sequence::make_sequence_enum_spec<socket_option,
      "debug,reuse_addr,type,error,dontroute,broadcast,sndbuf,rcvbuf,keep_"
      "alive,oob_inline,no_check,priority,linger,bsd_compat,reuse_port,"
      "passcred,peercred,rcvlowat,sndlowat,rcvtimeo,sndtimeo,security_"
      "authentication,security_encryption_transport,security_encryption_"
      "network,bind_to_device,attach_filter,detach_filter,peername,timestamp_"
      "old,acceptconn,peersec,sndbufforce,rcvbufforce,passsec,timestamp_ns_"
      "old,mark,timestamping_old,protocol,domain,rxq_ovfl,wifi_status,peek_"
      "off,nofcs,lock_filter,select_err_queue,busy_poll,max_pacing_rate,bpf_"
      "extensions,incoming_cpu,attach_bpf,attach_reuseport_cbpf,attach_"
      "reuseport_ebpf,cnx_advice,scm_timestamping_opt_stats,meminfo,incoming_"
      "napi_id,cookie,scm_timestamping_pktinfo,peergroups,zerocopy,txtime,"
      "bind_to_ifindex,timestamp_new,timestamp_ns_new,timestamping_new,"
      "rcvtimeo_new,sndtimeo_new,detach_reuseport_bpf,prefer_busy_poll,busy_"
      "poll_budget,netns_cookie,buf_lock,reserve_mem,tx_rehash,rcvmark,"
      "passpidfd,peerpidfd",
      wrapclip{}, socket_option{1}>();
}

#pragma endregion
#pragma region tcp_option

// "TCP_* wrapper for TCP-level socket options".
// NOLINTNEXTLINE(performance-enum-size)
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
};

consteval auto corvid_enum_spec(tcp_option*) {
  return corvid::enums::sequence::make_sequence_enum_spec<tcp_option,
      "nodelay,maxseg,cork,keep_idle,keep_intvl,keep_cnt,syncnt,linger2,defer_"
      "accept,window_clamp,info,quickack,congestion,md5sig,cookie_"
      "transactions,thin_linear_timeouts,thin_dupack,user_timeout,repair,"
      "repair_queue,queue_seq,repair_options,fastopen,timestamp,notsent_lowat,"
      "cc_info,save_syn,saved_syn,repair_window,fastopen_connect,ulp,md5sig_"
      "ext,fastopen_key,fastopen_no_cookie,zerocopy_receive,inq,tx_delay",
      wrapclip{}, tcp_option{1}>();
}

#pragma endregion

}} // namespace corvid::proto
