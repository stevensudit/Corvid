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

// Codex note: in this sandbox, creating AF_INET/AF_INET6 sockets can fail
// with EPERM, so the network-socket portions of this test file may fail even
// when the code is correct in a normal local environment.

#include "corvid/filesys.h"
#include "corvid/proto/net_endpoint.h"
#include "corvid/enums/enum_conversion.h"
#include "catch2_main.h"

#include <cstdlib>
#include <fcntl.h>
#include <string_view>
#include <unistd.h>
#include <system_error>
#include <sys/socket.h>

using namespace corvid;

bool is_codex() {
  const char* value = std::getenv("CODEX_SANDBOX_NETWORK_DISABLED");
  return value && std::string_view{value} == "1";
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

// Helper: create a non-blocking pipe and wrap each end in an `os_file`.
std::pair<os_file, os_file> make_nb_pipe() {
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
    throw std::system_error(errno, std::generic_category(), "pipe2");
  return {os_file{fds[0]}, os_file{fds[1]}};
}

// Helper: create a blocking pipe and wrap each end in an `os_file`.
std::pair<os_file, os_file> make_blocking_pipe() {
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC) != 0)
    throw std::system_error(errno, std::generic_category(), "pipe2");
  return {os_file{fds[0]}, os_file{fds[1]}};
}

#pragma region Lifecycle

TEST_CASE("Lifecycle", "[NetSocket]") {
  if (is_codex()) return;

  // Default-constructed socket is invalid.
  if (true) {
    net_socket s;
    CHECK_FALSE(s.is_open());
    CHECK_FALSE(static_cast<bool>(s));
    CHECK(s.handle() == net_socket::invalid_handle);
    CHECK_FALSE(s.close());
  }

  // A real socket is open; closing it twice is idempotent.
  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};
    CHECK(s.is_open());
    CHECK(static_cast<bool>(s));
    CHECK(s.handle() != net_socket::invalid_handle);
    CHECK(s.close());
    CHECK_FALSE(s.is_open());
    CHECK_FALSE(s.close());
  }

  // Destructor closes an open socket (no crash or leak).
  if (true) { net_socket s{address_family::inet, socket_type::stream, {}}; }
}

#pragma endregion

#pragma region Move

TEST_CASE("Move", "[NetSocket]") {
  if (is_codex()) return;

  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    net_socket a{address_family::inet, socket_type::stream, {}};
    const auto h = a.handle();
    net_socket b{std::move(a)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(a.is_open());
    CHECK(b.is_open());
    CHECK(b.handle() == h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    net_socket a{address_family::inet, socket_type::stream, {}};
    net_socket b{address_family::inet, socket_type::stream, {}};
    const auto h = a.handle();
    b = std::move(a);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(a.is_open());
    CHECK(b.is_open());
    CHECK(b.handle() == h);
  }

  // Self-assignment is a no-op.
  if (true) {
    net_socket a{address_family::inet, socket_type::stream, {}};
    const auto h = a.handle();
    // Route through a pointer to defeat -Wself-move while still exercising
    // the self-assignment path.
    auto* p = &a;
    a = std::move(*p);
    CHECK(a.is_open());
    CHECK(a.handle() == h);
  }
}

#pragma endregion

#pragma region Release

TEST_CASE("Release", "[NetSocket]") {
  if (is_codex()) return;

  // `release` yields the handle without closing it; socket becomes invalid.
  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};
    const auto h = s.release();
    CHECK(h != net_socket::invalid_handle);
    CHECK_FALSE(s.is_open());
    ::close(h);
  }
}

#pragma endregion

#pragma region Options

TEST_CASE("Options", "[NetSocket]") {
  if (is_codex()) return;

  // Named option helpers round-trip through `get_option`.
  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};

    CHECK(s.set_reuse_addr(true));
    auto v = s.get_option<int>(socket_option::reuse_addr);
    CHECK(v.has_value());
    CHECK(*v != 0);

    CHECK(s.set_reuse_addr(false));
    v = s.get_option<int>(socket_option::reuse_addr);
    CHECK(v.has_value());
    CHECK(*v == 0);

    CHECK(s.set_reuse_port(true));
    CHECK(s.set_keepalive(true));
    CHECK(s.set_nodelay(true));
  }

  // Buffer size helpers: kernel may round up, so just verify >= requested.
  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};
    CHECK(s.set_recv_buffer_size(65536));
    CHECK(s.set_send_buffer_size(65536));
    auto r = s.get_option<int>(socket_option::rcvbuf);
    CHECK(r.has_value());
    CHECK(*r >= 65536);
    auto t = s.get_option<int>(socket_option::sndbuf);
    CHECK(t.has_value());
    CHECK(*t >= 65536);
  }
}

#pragma endregion

#pragma region Nonblocking

TEST_CASE("Nonblocking", "[NetSocket]") {
  if (is_codex()) return;

  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};

    CHECK(s.set_nonblocking(true));
    CHECK(bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));

    CHECK(s.set_nonblocking(false));
    CHECK_FALSE(
        bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
  }
}

#pragma endregion

#pragma region SendRecv

TEST_CASE("SendRecv", "[NetSocket]") {
  int fds[2];
  REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

  net_socket a{os_file{fds[0]}};
  net_socket b{os_file{fds[1]}};

  auto msg = std::string_view{"hello"};
  CHECK(a.send(msg));
  CHECK(msg.empty());

  std::string buf(16, '\0');
  CHECK(b.recv(buf));
  CHECK(buf == "hello");

  constexpr char raw_msg[] = "raw";
  CHECK(a.send(raw_msg, sizeof(raw_msg) - 1) == 3);

  char raw_buf[8]{};
  CHECK(b.recv(raw_buf, sizeof(raw_buf), {}) == 3);
  const auto raw_view = std::string_view{raw_buf, 3};
  CHECK(raw_view == "raw");
}

#pragma endregion

#pragma region RecvAtContract

TEST_CASE("RecvAtContract", "[NetSocket]") {
  auto [reader, writer] = net_socket::create_pair();
  REQUIRE(reader);
  REQUIRE(writer);
  CHECK(reader.set_nonblocking(true));

  // Soft errors (EAGAIN on a non-blocking empty socket) trim back to the
  // supplied offset and succeed.
  if (true) {
    std::string buf(8, '\0');
    CHECK(reader.recv_at(buf, 5));
    CHECK(buf.size() == 5U);
  }

  // EOF leaves the caller's string unchanged and reports failure.
  if (true) {
    CHECK(writer.close());
    std::string buf(8, '\0');
    CHECK_FALSE(reader.recv_at(buf, 3));
    CHECK(buf.size() == 8U);
  }

  // `recv` still clears on hard failure.
  if (true) {
    CHECK(reader.close());
    std::string buf(8, '\0');
    errno = 0;
    CHECK_FALSE(reader.recv(buf));
    CHECK(buf.empty());
    CHECK(errno == EBADF);
  }
}

#pragma endregion

#pragma region BindListenAccept

TEST_CASE("BindListenAccept", "[NetSocket]") {
  if (is_codex()) return;

  // Bind a listening socket to a free loopback port.
  net_socket listener{address_family::inet, socket_type::stream, {}};
  CHECK(listener.is_open());
  CHECK(listener.set_reuse_addr());
  CHECK(listener.bind(net_endpoint{ipv4_addr::loopback, 0}));
  CHECK(listener.listen());

  // Retrieve the OS-assigned port via `getsockname`.
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  CHECK(::getsockname(listener.handle(), reinterpret_cast<sockaddr*>(&bound),
            &bound_len) == 0);
  const uint16_t port = ntohs(bound.sin_port);
  CHECK(port != 0U);

  // Connect a client to the listening socket.
  net_socket client{address_family::inet, socket_type::stream, {}};
  CHECK(client.is_open());
  CHECK(
      client.connect(net_endpoint{ipv4_addr::loopback, port}).value_or(false));

  // Accept the connection on the listener side.
  auto result = listener.accept();
  CHECK(result.has_value());
  CHECK(result->first.is_open());
  const auto peer = net_endpoint{result->second};
  CHECK(peer.is_v4());
  CHECK(peer.v4()->is_loopback());
}

#pragma endregion

#pragma region FactoryMethods

TEST_CASE("FactoryMethods", "[NetSocket]") {
  using namespace bool_enums;

  // create_ipv4 defaults to non-blocking TCP.
  if (true) {
    if (!is_codex()) {
      auto s = net_socket::create_ipv4();
      CHECK(s.is_open());
      CHECK(
          bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
      auto dom = s.get_option<int>(socket_option::domain);
      CHECK(dom.has_value());
      CHECK(*dom == AF_INET);
      auto type = s.get_option<int>(socket_option::type);
      CHECK(type.has_value());
      CHECK(*type == SOCK_STREAM);
    }
  }

  // create_ipv4 with blocking + datagram gives a blocking UDP socket.
  if (true) {
    if (!is_codex()) {
      auto s = net_socket::create_ipv4(execution::blocking,
          message_style::datagram);
      CHECK(s.is_open());
      CHECK_FALSE(
          bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
      auto dom = s.get_option<int>(socket_option::domain);
      CHECK(*dom == AF_INET);
      auto type = s.get_option<int>(socket_option::type);
      CHECK(type.has_value());
      CHECK(*type == SOCK_DGRAM);
    }
  }

  // create_ipv6 defaults to non-blocking TCP.
  if (true) {
    if (!is_codex()) {
      auto s = net_socket::create_ipv6();
      CHECK(s.is_open());
      CHECK(
          bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
      auto dom = s.get_option<int>(socket_option::domain);
      CHECK(dom.has_value());
      CHECK(*dom == AF_INET6);
      auto type = s.get_option<int>(socket_option::type);
      CHECK(type.has_value());
      CHECK(*type == SOCK_STREAM);
    }
  }

  // create_uds defaults to non-blocking stream.
  if (true) {
    auto s = net_socket::create_uds();
    CHECK(s.is_open());
    CHECK(bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
    auto dom = s.get_option<int>(socket_option::domain);
    CHECK(dom.has_value());
    CHECK(*dom == AF_UNIX);
    auto type = s.get_option<int>(socket_option::type);
    CHECK(type.has_value());
    CHECK(*type == SOCK_STREAM);
  }

  // create_uds with datagram style gives a SOCK_DGRAM UDS.
  if (true) {
    auto s = net_socket::create_uds(execution::nonblocking,
        message_style::datagram);
    CHECK(s.is_open());
    auto type = s.get_option<int>(socket_option::type);
    CHECK(type.has_value());
    CHECK(*type == SOCK_DGRAM);
  }
}

#pragma endregion

#pragma region SocketTypeString

TEST_CASE("SocketTypeString", "[NetSocket]") {
  // Sequence enum names round-trip correctly starting from `stream` = 1.
  using namespace corvid::strings;
  using T = socket_type;
  if (true) {
    CHECK(enum_as_string(T::stream) == "stream");
    CHECK(enum_as_string(T::datagram) == "datagram");
    CHECK(enum_as_string(T::raw) == "raw");
    CHECK(enum_as_string(T::seqpacket) == "seqpacket");
    CHECK(enum_as_string(T::packet) == "packet");
  }
  if (true) {
    constexpr T bad{0};
    CHECK(parse_enum("stream", bad) == T::stream);
    CHECK(parse_enum("datagram", bad) == T::datagram);
    CHECK(parse_enum("packet", bad) == T::packet);
  }
}

#pragma endregion

#pragma region AddressFamilyString

TEST_CASE("AddressFamilyString", "[NetSocket]") {
  // Sequence enum names starting from `unspecified` = 0.
  using namespace corvid::strings;
  using AF = address_family;
  if (true) {
    CHECK(enum_as_string(AF::unspecified) == "unspecified");
    CHECK(enum_as_string(AF::local) == "local");
    CHECK(enum_as_string(AF::inet) == "inet");
    CHECK(enum_as_string(AF::inet6) == "inet6");
    // `unix` and `file` are aliases for `local`; they share value 1.
    CHECK(enum_as_string(AF::unix) == "local");
  }
  if (true) {
    constexpr AF bad{-1};
    CHECK(parse_enum("unspecified", bad) == AF::unspecified);
    CHECK(parse_enum("inet", bad) == AF::inet);
    CHECK(parse_enum("inet6", bad) == AF::inet6);
  }
}

#pragma endregion

#pragma region ProtocolTypeString

TEST_CASE("ProtocolTypeString", "[NetSocket]") {
  // Each named protocol round-trips.
  using namespace corvid::strings;
  using P = protocol_type;
  if (true) {
    CHECK(enum_as_string(P::ip) == "ip");
    CHECK(enum_as_string(P::icmp) == "icmp");
    CHECK(enum_as_string(P::igmp) == "igmp");
    CHECK(enum_as_string(P::ipip) == "ipip");
    CHECK(enum_as_string(P::tcp) == "tcp");
    CHECK(enum_as_string(P::egp) == "egp");
    CHECK(enum_as_string(P::pup) == "pup");
    CHECK(enum_as_string(P::udp) == "udp");
    CHECK(enum_as_string(P::idp) == "idp");
    CHECK(enum_as_string(P::tp) == "tp");
    CHECK(enum_as_string(P::dccp) == "dccp");
    CHECK(enum_as_string(P::ipv6) == "ipv6");
    CHECK(enum_as_string(P::routing) == "routing");
    CHECK(enum_as_string(P::fragment) == "fragment");
    CHECK(enum_as_string(P::rsvp) == "rsvp");
    CHECK(enum_as_string(P::gre) == "gre");
    CHECK(enum_as_string(P::esp) == "esp");
    CHECK(enum_as_string(P::ah) == "ah");
    CHECK(enum_as_string(P::icmpv6) == "icmpv6");
    CHECK(enum_as_string(P::none) == "none");
    CHECK(enum_as_string(P::dstopts) == "dstopts");
    CHECK(enum_as_string(P::mtp) == "mtp");
    CHECK(enum_as_string(P::beetph) == "beetph");
    CHECK(enum_as_string(P::encap) == "encap");
    CHECK(enum_as_string(P::pim) == "pim");
    CHECK(enum_as_string(P::comp) == "comp");
    CHECK(enum_as_string(P::l2tp) == "l2tp");
    CHECK(enum_as_string(P::sctp) == "sctp");
    CHECK(enum_as_string(P::mh) == "mh");
    CHECK(enum_as_string(P::udplite) == "udplite");
    CHECK(enum_as_string(P::mpls) == "mpls");
    CHECK(enum_as_string(P::ethernet) == "ethernet");
    CHECK(enum_as_string(P::raw) == "raw");
  }
  if (true) {
    constexpr P bad{-1};
    CHECK(parse_enum("ip", bad) == P::ip);
    CHECK(parse_enum("tcp", bad) == P::tcp);
    CHECK(parse_enum("udp", bad) == P::udp);
  }
}

#pragma endregion

#pragma region SocketOptionString

TEST_CASE("SocketOptionString", "[NetSocket]") {
  // Sequence enum: named values 1 ("debug") through 77 ("peerpidfd").
  // Aliases (`get_filter`=26, `detach_bpf`=27, `scm_txtime`=61) share values
  // with primary names and are omitted from the spec; the primary name wins.
  using namespace corvid::strings;
  using SO = socket_option;
  if (true) {
    CHECK(enum_as_string(SO::debug) == "debug");
    CHECK(enum_as_string(SO::reuse_addr) == "reuse_addr");
    CHECK(enum_as_string(SO::type) == "type");
    CHECK(enum_as_string(SO::sndbuf) == "sndbuf");
    CHECK(enum_as_string(SO::keep_alive) == "keep_alive");
    CHECK(enum_as_string(SO::linger) == "linger");
    CHECK(enum_as_string(SO::reuse_port) == "reuse_port");
    CHECK(enum_as_string(SO::attach_filter) == "attach_filter");
    CHECK(enum_as_string(SO::detach_filter) == "detach_filter");
    CHECK(enum_as_string(SO::peername) == "peername");
    CHECK(enum_as_string(SO::protocol) == "protocol");
    CHECK(enum_as_string(SO::domain) == "domain");
    CHECK(enum_as_string(SO::attach_bpf) == "attach_bpf");
    CHECK(enum_as_string(SO::cookie) == "cookie");
    CHECK(enum_as_string(SO::txtime) == "txtime");
    CHECK(enum_as_string(SO::bind_to_ifindex) == "bind_to_ifindex");
    CHECK(enum_as_string(SO::peerpidfd) == "peerpidfd");
    // Aliases: primary name wins for the shared value.
    CHECK(enum_as_string(SO::get_filter) == "attach_filter");
    CHECK(enum_as_string(SO::detach_bpf) == "detach_filter");
    CHECK(enum_as_string(SO::scm_txtime) == "txtime");
  }
  if (true) {
    // Out-of-range values print as their numeric value.
    CHECK(enum_as_string(SO{0}) == "0");
    CHECK(enum_as_string(SO{78}) == "78");
  }
  if (true) {
    constexpr SO bad{0};
    CHECK(parse_enum("debug", bad) == SO::debug);
    CHECK(parse_enum("reuse_addr", bad) == SO::reuse_addr);
    CHECK(parse_enum("detach_filter", bad) == SO::detach_filter);
    CHECK(parse_enum("domain", bad) == SO::domain);
    CHECK(parse_enum("txtime", bad) == SO::txtime);
    CHECK(parse_enum("peerpidfd", bad) == SO::peerpidfd);
  }
}

#pragma endregion

#pragma region TcpOptionString

TEST_CASE("TcpOptionString", "[NetSocket]") {
  // Sequence enum: named values 1 ("nodelay") through 37 ("tx_delay").
  using namespace corvid::strings;
  using TO = tcp_option;
  if (true) {
    CHECK(enum_as_string(TO::nodelay) == "nodelay");
    CHECK(enum_as_string(TO::maxseg) == "maxseg");
    CHECK(enum_as_string(TO::cork) == "cork");
    CHECK(enum_as_string(TO::keep_idle) == "keep_idle");
    CHECK(enum_as_string(TO::syncnt) == "syncnt");
    CHECK(enum_as_string(TO::linger2) == "linger2");
    CHECK(enum_as_string(TO::defer_accept) == "defer_accept");
    CHECK(enum_as_string(TO::info) == "info");
    CHECK(enum_as_string(TO::quickack) == "quickack");
    CHECK(enum_as_string(TO::fastopen) == "fastopen");
    CHECK(enum_as_string(TO::notsent_lowat) == "notsent_lowat");
    CHECK(enum_as_string(TO::save_syn) == "save_syn");
    CHECK(enum_as_string(TO::saved_syn) == "saved_syn");
    CHECK(enum_as_string(TO::fastopen_connect) == "fastopen_connect");
    CHECK(enum_as_string(TO::zerocopy_receive) == "zerocopy_receive");
    CHECK(enum_as_string(TO::inq) == "inq");
    CHECK(enum_as_string(TO::tx_delay) == "tx_delay");
  }
  if (true) {
    // Out-of-range values print as their numeric value.
    CHECK(enum_as_string(TO{0}) == "0");
    CHECK(enum_as_string(TO{38}) == "38");
  }
  if (true) {
    constexpr TO bad{0};
    CHECK(parse_enum("nodelay", bad) == TO::nodelay);
    CHECK(parse_enum("linger2", bad) == TO::linger2);
    CHECK(parse_enum("fastopen", bad) == TO::fastopen);
    CHECK(parse_enum("saved_syn", bad) == TO::saved_syn);
    CHECK(parse_enum("tx_delay", bad) == TO::tx_delay);
  }
}

#pragma endregion

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
