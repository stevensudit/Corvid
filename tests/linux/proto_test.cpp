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
// with EPERM, so tests here that rely on real network sockets may fail even
// when the code is correct in a normal local environment.
#include "corvid/proto.h"

#include <type_traits>
#include <atomic>
#include <bit>
#include <format>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace std::chrono_literals;

namespace corvid { inline namespace proto {
struct iov_msghdr_test {
  template<bool Sender>
  static auto oversize_update() {
    proto::iov_msghdr<Sender> io;
    std::string buf = "abc";
    [[maybe_unused]] const bool appended = io.append(buf.data(), buf.size());

    io.last_op_.transferred = io.size() + 1;
    return std::pair{io.do_update_results(), io.last_op_};
  }
};
}} // namespace corvid::proto

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

#pragma region Endian

TEST_CASE("Endian", "[Endian]") {
  // Round-tripping restores the original value at every width.
  static_assert(ntoh16(hton16(0x1234)) == 0x1234);
  static_assert(ntoh32(hton32(0x01020304)) == 0x01020304);
  static_assert(ntoh64(hton64(0x0102030405060708)) == 0x0102030405060708);

  // On a little-endian host, conversion reverses the bytes; on a big-endian
  // host, it is the identity.
  constexpr bool little = std::endian::native == std::endian::little;
  static_assert(!little || hton16(0x1234) == 0x3412);
  static_assert(!little || hton32(0x01020304) == 0x04030201);
  static_assert(!little || hton64(0x0102030405060708) == 0x0807060504030201);
  static_assert(little || hton32(0x01020304) == 0x01020304);

#ifdef __SIZEOF_INT128__
  constexpr auto v128 =
      (static_cast<__uint128_t>(0x0102030405060708) << 64) |
      0x090A0B0C0D0E0F10;
  static_assert(ntoh128(hton128(v128)) == v128);
  static_assert(
      !little ||
      hton128(v128) == ((static_cast<__uint128_t>(0x100F0E0D0C0B0A09) << 64) |
                           0x0807060504030201));
#endif

  // Runtime anchor.
  CHECK(ntoh32(hton32(0xDEADBEEF)) == 0xDEADBEEF);
}

#pragma endregion
#pragma region Construction

TEST_CASE("Construction", "[Ipv4Addr]") {
  // Default construction yields the "any" address, which is also empty.
  if (true) {
    ipv4_addr a;
    CHECK(a.is_any());
    CHECK(a.empty());
    CHECK_FALSE(bool(a));
    CHECK(a.to_uint32() == 0U);
  }

  // Named constant: any.
  if (true) {
    auto a = ipv4_addr::any;
    CHECK(a.is_any());
    CHECK(a.to_uint32() == 0U);
  }

  // Named constant: loopback.
  if (true) {
    auto a = ipv4_addr::loopback;
    CHECK(a.is_loopback());
    CHECK(a.to_uint32() == 0x7f000001U);
  }

  // Named constant: broadcast.
  if (true) {
    auto a = ipv4_addr::broadcast;
    CHECK(a.is_broadcast());
    CHECK(a.to_uint32() == 0xffffffffU);
  }

  // Construct from four octets.
  if (true) {
    ipv4_addr a{192, 168, 1, 1};
    CHECK(a.to_uint32() == 0xc0a80101U);
    auto o = a.octets();
    CHECK(o[0] == 192U);
    CHECK(o[1] == 168U);
    CHECK(o[2] == 1U);
    CHECK(o[3] == 1U);
  }

  // Construct from host-byte-order uint32_t.
  if (true) {
    ipv4_addr a{uint32_t{0x01020304U}};
    auto o = a.octets();
    CHECK(o[0] == 1U);
    CHECK(o[1] == 2U);
    CHECK(o[2] == 3U);
    CHECK(o[3] == 4U);
  }

  // Non-zero address is not empty; operator bool() reflects this.
  if (true) {
    ipv4_addr a{1, 2, 3, 4};
    CHECK_FALSE(a.empty());
    CHECK(bool(a));
  }
}

#pragma endregion
#pragma region Parse

TEST_CASE("Parse", "[Ipv4Addr]") {
  // Valid addresses.
  if (true) {
    auto a = ipv4_addr::parse("0.0.0.0");
    REQUIRE(a.has_value());
    CHECK(a->is_any());

    auto b = ipv4_addr::parse("127.0.0.1");
    REQUIRE(b.has_value());
    CHECK(b->is_loopback());

    auto c = ipv4_addr::parse("255.255.255.255");
    REQUIRE(c.has_value());
    CHECK(c->is_broadcast());

    auto d = ipv4_addr::parse("192.168.1.100");
    REQUIRE(d.has_value());
    CHECK(d->to_uint32() == 0xc0a80164U);

    // Single-digit octets.
    auto e = ipv4_addr::parse("1.2.3.4");
    REQUIRE(e.has_value());
    CHECK(e->to_uint32() == 0x01020304U);
  }

  // Invalid: too few octets.
  if (true) {
    CHECK_FALSE(ipv4_addr::parse("192.168.1").has_value());
    CHECK_FALSE(ipv4_addr::parse("192.168").has_value());
    CHECK_FALSE(ipv4_addr::parse("192").has_value());
    CHECK_FALSE(ipv4_addr::parse("").has_value());
  }

  // Invalid: too many octets.
  if (true) { CHECK_FALSE(ipv4_addr::parse("1.2.3.4.5").has_value()); }

  // Invalid: octet out of range.
  if (true) {
    CHECK_FALSE(ipv4_addr::parse("256.0.0.1").has_value());
    CHECK_FALSE(ipv4_addr::parse("1.2.3.999").has_value());
  }

  // Invalid: leading zeros.
  if (true) {
    CHECK_FALSE(ipv4_addr::parse("01.2.3.4").has_value());
    CHECK_FALSE(ipv4_addr::parse("1.02.3.4").has_value());
  }

  // Invalid: non-numeric characters.
  if (true) {
    CHECK_FALSE(ipv4_addr::parse("a.b.c.d").has_value());
    CHECK_FALSE(ipv4_addr::parse("192.168.1.x").has_value());
    CHECK_FALSE(ipv4_addr::parse("192.168.1.1 ").has_value());
    CHECK_FALSE(ipv4_addr::parse(" 192.168.1.1").has_value());
  }

  // A sole zero octet is valid (not a leading zero).
  if (true) {
    auto a = ipv4_addr::parse("0.0.0.0");
    CHECK(a.has_value());
  }
}

#pragma endregion
#pragma region Classification

TEST_CASE("Classification", "[Ipv4Addr]") {
  // is_loopback(): entire 127.0.0.0/8 range.
  if (true) {
    CHECK(ipv4_addr(127, 0, 0, 1).is_loopback());
    CHECK(ipv4_addr(127, 255, 255, 255).is_loopback());
    CHECK_FALSE(ipv4_addr(128, 0, 0, 1).is_loopback());
  }

  // is_multicast(): 224.0.0.0/4.
  if (true) {
    CHECK(ipv4_addr(224, 0, 0, 1).is_multicast());
    CHECK(ipv4_addr(239, 255, 255, 255).is_multicast());
    CHECK_FALSE(ipv4_addr(223, 0, 0, 1).is_multicast());
    CHECK_FALSE(ipv4_addr(240, 0, 0, 1).is_multicast());
  }

  // is_private(): RFC 1918 ranges.
  if (true) {
    // 10.0.0.0/8
    CHECK(ipv4_addr(10, 0, 0, 1).is_private());
    CHECK(ipv4_addr(10, 255, 255, 255).is_private());
    CHECK_FALSE(ipv4_addr(11, 0, 0, 1).is_private());

    // 172.16.0.0/12
    CHECK(ipv4_addr(172, 16, 0, 1).is_private());
    CHECK(ipv4_addr(172, 31, 255, 255).is_private());
    CHECK_FALSE(ipv4_addr(172, 15, 0, 1).is_private());
    CHECK_FALSE(ipv4_addr(172, 32, 0, 1).is_private());

    // 192.168.0.0/16
    CHECK(ipv4_addr(192, 168, 0, 1).is_private());
    CHECK(ipv4_addr(192, 168, 255, 255).is_private());
    CHECK_FALSE(ipv4_addr(192, 169, 0, 1).is_private());

    // Public addresses are not private.
    CHECK_FALSE(ipv4_addr(8, 8, 8, 8).is_private());
  }

  // is_broadcast().
  if (true) {
    CHECK(ipv4_addr::broadcast.is_broadcast());
    CHECK_FALSE(ipv4_addr(255, 255, 255, 254).is_broadcast());
  }

  // is_any().
  if (true) {
    CHECK(ipv4_addr::any.is_any());
    CHECK_FALSE(ipv4_addr(0, 0, 0, 1).is_any());
  }
}

#pragma endregion
#pragma region Comparison

TEST_CASE("Comparison", "[Ipv4Addr]") {
  ipv4_addr a{10, 0, 0, 1};
  ipv4_addr b{10, 0, 0, 2};
  ipv4_addr c{10, 0, 0, 1};

  CHECK(a == c);
  CHECK_FALSE(a == b);
  CHECK(a != b);
  CHECK(a < b);
  CHECK(b > a);
  CHECK(a <= c);
  CHECK(a >= c);
}

#pragma endregion
#pragma region Formatting

TEST_CASE("Formatting", "[Ipv4Addr]") {
  CHECK(ipv4_addr::any.to_string() == "0.0.0.0");
  CHECK(ipv4_addr::loopback.to_string() == "127.0.0.1");
  CHECK(ipv4_addr::broadcast.to_string() == "255.255.255.255");
  CHECK(ipv4_addr(192, 168, 1, 100).to_string() == "192.168.1.100");

  // std::format goes through the formatter, not to_string.
  CHECK(std::format("{}", ipv4_addr(192, 168, 1, 100)) == "192.168.1.100");
  CHECK(std::format("{}", ipv4_addr::any) == "0.0.0.0");

  // Conversion to bool is explicit, so an address cannot promote into
  // arithmetic or comparison with integers.
  static_assert(!std::is_convertible_v<ipv4_addr, bool>);
  static_assert(std::is_constructible_v<bool, ipv4_addr>);

  // Round-trip: parse then format.
  auto addr = ipv4_addr::parse("10.20.30.40");
  REQUIRE(addr.has_value());
  CHECK(addr->to_string() == "10.20.30.40");
}

#pragma endregion
#pragma region PosixInterop

TEST_CASE("PosixInterop", "[Ipv4Addr]") {
  ipv4_addr a{192, 168, 1, 1};

  // Convert to `in_addr` and back.
  in_addr raw = a.to_in_addr();
  ipv4_addr b{raw};
  CHECK(a == b);

  // Verify network byte order: 192.168.1.1 = 0xc0a80101 in host order,
  // so `s_addr` should be 0x0101a8c0 on a little-endian host.
  CHECK(ntohl(raw.s_addr) == a.to_uint32());
}

#pragma endregion
#pragma region Construction

TEST_CASE("Construction", "[Ipv6Addr]") {
  if (true) {
    ipv6_addr a;
    CHECK(a.is_any());
    CHECK(a.to_string() == "::");
  }

  if (true) {
    auto a = ipv6_addr::any;
    CHECK(a.is_any());
  }

  if (true) {
    auto a = ipv6_addr::loopback;
    CHECK(a.is_loopback());
    CHECK(a.to_string() == "::1");
  }

  if (true) {
    ipv6_addr a{0x2001, 0x0db8, 0, 0, 0, 0, 0, 1};
    auto words = a.words();
    CHECK(words[0] == 0x2001U);
    CHECK(words[1] == 0x0db8U);
    CHECK(words[7] == 1U);
  }

  if (true) {
    ipv6_addr a{std::array<uint8_t, 16>{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 1}};
    CHECK(a.to_string() == "2001:db8::1");
  }
}

#pragma endregion
#pragma region Parse

TEST_CASE("Parse", "[Ipv6Addr]") {
  if (true) {
    auto a = ipv6_addr::parse("::");
    REQUIRE(a.has_value());
    CHECK(a->is_any());

    auto b = ipv6_addr::parse("::1");
    REQUIRE(b.has_value());
    CHECK(b->is_loopback());

    auto c = ipv6_addr::parse("2001:db8::1");
    REQUIRE(c.has_value());
    CHECK(c->to_string() == "2001:db8::1");

    auto d = ipv6_addr::parse("2001:0DB8:0000:0000:0000:0000:0000:0001");
    REQUIRE(d.has_value());
    CHECK(d->to_string() == "2001:db8::1");

    auto e = ipv6_addr::parse("fe80::1234:5678");
    REQUIRE(e.has_value());
    CHECK(e->is_link_local());

    auto f = ipv6_addr::parse("::ffff:192.168.1.1");
    REQUIRE(f.has_value());
    auto fw = f->words();
    CHECK(fw[4] == 0U);
    CHECK(fw[5] == 0xFFFFU);
    CHECK(fw[6] == 0xC0A8U);
    CHECK(fw[7] == 0x0101U);
    auto fb = f->bytes();
    CHECK(fb[14] == 1U);
    CHECK(fb[15] == 1U);

    auto g = ipv6_addr::parse("2001:db8::192.0.2.33");
    REQUIRE(g.has_value());
    auto gw = g->words();
    CHECK(gw[6] == 0xC000U);
    CHECK(gw[7] == 0x0221U);
  }

  if (true) {
    CHECK_FALSE(ipv6_addr::parse("").has_value());
    CHECK_FALSE(ipv6_addr::parse(":").has_value());
    CHECK_FALSE(ipv6_addr::parse(":::").has_value());
    CHECK_FALSE(ipv6_addr::parse("2001:::1").has_value());
    CHECK_FALSE(ipv6_addr::parse("2001:db8::1::").has_value());
    CHECK_FALSE(ipv6_addr::parse("1:2:3:4:5:6:7").has_value());
    CHECK_FALSE(ipv6_addr::parse("1:2:3:4:5:6:7:8:9").has_value());
    CHECK_FALSE(ipv6_addr::parse("12345::1").has_value());
    CHECK_FALSE(ipv6_addr::parse("gggg::1").has_value());
    CHECK_FALSE(ipv6_addr::parse("1:2:3:4:5:6:7:192.168.1.1").has_value());
    CHECK_FALSE(ipv6_addr::parse("::ffff:999.168.1.1").has_value());
    CHECK_FALSE(ipv6_addr::parse("::ffff:192.168.1").has_value());
  }
}

#pragma endregion
#pragma region Classification

TEST_CASE("Classification", "[Ipv6Addr]") {
  if (true) {
    CHECK(ipv6_addr::loopback.is_loopback());
    CHECK_FALSE(ipv6_addr::any.is_loopback());
  }

  if (true) {
    CHECK(ipv6_addr(0xff02, 0, 0, 0, 0, 0, 0, 1).is_multicast());
    CHECK_FALSE(ipv6_addr(0xfe02, 0, 0, 0, 0, 0, 0, 1).is_multicast());
  }

  if (true) {
    CHECK(ipv6_addr(0xfe80, 0, 0, 0, 0, 0, 0, 1).is_link_local());
    CHECK(ipv6_addr(0xfebf, 0, 0, 0, 0, 0, 0, 1).is_link_local());
    CHECK_FALSE(ipv6_addr(0xfec0, 0, 0, 0, 0, 0, 0, 1).is_link_local());
  }

  if (true) {
    CHECK(ipv6_addr(0xfc00, 0, 0, 0, 0, 0, 0, 1).is_unique_local());
    CHECK(ipv6_addr(0xfd12, 0, 0, 0, 0, 0, 0, 1).is_unique_local());
    CHECK_FALSE(ipv6_addr(0xfe00, 0, 0, 0, 0, 0, 0, 1).is_unique_local());
  }
}

#pragma endregion
#pragma region Comparison

TEST_CASE("Comparison", "[Ipv6Addr]") {
  ipv6_addr a{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1};
  ipv6_addr b{0x2001, 0xdb8, 0, 0, 0, 0, 0, 2};
  ipv6_addr c{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1};

  CHECK(a == c);
  CHECK_FALSE(a == b);
  CHECK(a != b);
  CHECK(a < b);
  CHECK(b > a);
}

#pragma endregion
#pragma region Formatting

TEST_CASE("Formatting", "[Ipv6Addr]") {
  CHECK(ipv6_addr::any.to_string() == "::");
  CHECK(ipv6_addr::loopback.to_string() == "::1");
  CHECK((ipv6_addr(0x2001, 0xdb8, 0, 0, 1, 0, 0, 1).to_string()) ==
        ("2001:db8::1:0:0:1"));
  CHECK((ipv6_addr(0x2001, 0xdb8, 0, 1, 0, 0, 0, 1).to_string()) ==
        ("2001:db8:0:1::1"));

  auto addr = ipv6_addr::parse("2001:db8::abcd");
  REQUIRE(addr.has_value());
  CHECK(addr->to_string() == "2001:db8::abcd");

  // A lone zero group is not compressed.
  CHECK(ipv6_addr(1, 0, 2, 3, 4, 5, 6, 7).to_string() == "1:0:2:3:4:5:6:7");

  // std::format goes through the formatter, not to_string.
  CHECK(std::format("{}", ipv6_addr(0x2001, 0xdb8, 0, 1, 0, 0, 0, 1)) ==
        "2001:db8:0:1::1");
  CHECK(std::format("{}", ipv6_addr::any) == "::");

  // Conversion to bool is explicit, so an address cannot promote into
  // arithmetic or comparison with integers.
  static_assert(!std::is_convertible_v<ipv6_addr, bool>);
  static_assert(std::is_constructible_v<bool, ipv6_addr>);
}

#pragma endregion
#pragma region PosixInterop

TEST_CASE("PosixInterop", "[Ipv6Addr]") {
  ipv6_addr a{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1};

  in6_addr raw = a.to_in6_addr();
  ipv6_addr b{raw};
  CHECK(a == b);
  CHECK(raw.s6_addr[0] == 0x20U);
  CHECK(raw.s6_addr[1] == 0x01U);
  CHECK(raw.s6_addr[15] == 0x01U);
}

#pragma endregion
#pragma region Construction

TEST_CASE("Construction", "[NetEndpoint]") {
  if (true) {
    net_endpoint ep;
    CHECK(ep.empty());
    CHECK_FALSE(ep.as_sockaddr_view().is_v4());
    CHECK_FALSE(ep.as_sockaddr_view().is_v6());
    CHECK_FALSE(ep.as_sockaddr_view().is_uds());
    CHECK(ep.to_string() == "(invalid)");
  }

  if (true) {
    net_endpoint ep{ipv4_addr(127, 0, 0, 1), 80};
    REQUIRE(ep.as_sockaddr_view().is_v4());
    CHECK(ep.port() == 80U);
    CHECK(ep.v4()->to_string() == "127.0.0.1");
  }

  if (true) {
    net_endpoint ep{ipv6_addr::loopback, 443};
    REQUIRE(ep.as_sockaddr_view().is_v6());
    CHECK(ep.port() == 443U);
    CHECK(ep.v6()->to_string() == "::1");
  }

  // UDS: construct from sockaddr_un directly.
  if (true) {
    sockaddr_un raw{};
    raw.sun_family = AF_UNIX;
    const std::string_view path = "/tmp/test.sock";
    path.copy(raw.sun_path, sizeof(raw.sun_path) - 1);

    net_endpoint ep{raw};
    CHECK_FALSE(ep.empty());
    CHECK(ep.as_sockaddr_view().is_uds());
    CHECK_FALSE(ep.as_sockaddr_view().is_v4());
    CHECK_FALSE(ep.as_sockaddr_view().is_v6());
    CHECK(ep.as_sockaddr_view().uds_path() == path);
  }

  // UDS: a path too long for `sun_path` is rejected, not truncated.
  if (true) {
    const std::string long_path(200, 'x');
    net_endpoint ep{"/" + long_path};
    CHECK(ep.empty());
  }

  // ANS: construct from "@name" string.
  if (true) {
    net_endpoint ep{"@myservice"};
    CHECK(!ep.empty());
    CHECK(ep.as_sockaddr_view().is_uds());
    CHECK(ep.as_sockaddr_view().is_ans());
    CHECK_FALSE(ep.as_sockaddr_view().is_v4());
    CHECK_FALSE(ep.as_sockaddr_view().is_v6());
    // `uds_path` skips the leading '\0' and returns the length-delimited
    // name.
    CHECK(ep.as_sockaddr_view().uds_path() == "myservice");
  }

  // ANS: a name too long for `sun_path` is rejected, not truncated.
  if (true) {
    const std::string long_name(200, 'y');
    net_endpoint ep{"@" + long_name};
    CHECK(ep.empty());
  }

  // `operator->` drills down to the `sockaddr_view` accessors.
  if (true) {
    net_endpoint ep{"/run/app.sock"};
    CHECK(ep->is_uds());
    CHECK(ep->uds_path() == "/run/app.sock");
    CHECK(net_endpoint{"1.2.3.4:80"}->port() == 80U);
  }

  // Conversion to bool is explicit, so an endpoint cannot promote into
  // arithmetic or comparison with integers.
  static_assert(!std::is_convertible_v<net_endpoint, bool>);
  static_assert(std::is_constructible_v<bool, net_endpoint>);
}

#pragma endregion
#pragma region Parse

TEST_CASE("Parse", "[NetEndpoint]") {
  if (true) {
    net_endpoint a{"192.168.1.10:8080"};
    CHECK(!a.empty());
    REQUIRE(a.as_sockaddr_view().is_v4());
    CHECK(a.port() == 8080U);
    CHECK(a.v4()->to_string() == "192.168.1.10");

    net_endpoint b{"[2001:db8::1]:443"};
    CHECK(!b.empty());
    REQUIRE(b.as_sockaddr_view().is_v6());
    CHECK(b.port() == 443U);
    CHECK(b.v6()->to_string() == "2001:db8::1");
  }

  if (true) {
    CHECK(net_endpoint{""}.empty());
    CHECK(net_endpoint{"127.0.0.1"}.empty());
    CHECK(net_endpoint{"127.0.0.1:"}.empty());
    CHECK(net_endpoint{"127.0.0.1:99999"}.empty());
    CHECK(net_endpoint{"2001:db8::1:443"}.empty());
    CHECK(net_endpoint{"[2001:db8::1]"}.empty());
    CHECK(net_endpoint{"[2001:db8::1]:"}.empty());
    CHECK(net_endpoint{"[2001:db8::1]:70000"}.empty());
  }

  // Port range boundary: `std::from_chars` itself rejects values that do not
  // fit in `uint16_t`.
  if (true) {
    net_endpoint ep{"1.2.3.4:65535"};
    CHECK(!ep.empty());
    CHECK(ep.port() == 65535U);
    CHECK(net_endpoint{"1.2.3.4:65536"}.empty());
  }

  // A leading `/` produces a UDS endpoint.
  if (true) {
    net_endpoint ep{"/run/app.sock"};
    CHECK(!ep.empty());
    CHECK(ep.as_sockaddr_view().is_uds());
    CHECK(ep.as_sockaddr_view().uds_path() == "/run/app.sock");
  }

  // The `string_view` constructor also accepts UDS paths.
  if (true) {
    net_endpoint ep{std::string_view{"/var/run/foo.sock"}};
    CHECK(ep.as_sockaddr_view().is_uds());
    CHECK_FALSE(ep.as_sockaddr_view().is_ans());
    CHECK(ep.as_sockaddr_view().uds_path() == "/var/run/foo.sock");
  }

  // A leading `@` produces an ANS endpoint.
  if (true) {
    net_endpoint ep{"@abstract"};
    CHECK(!ep.empty());
    CHECK(ep.as_sockaddr_view().is_uds());
    CHECK(ep.as_sockaddr_view().is_ans());
    CHECK(ep.as_sockaddr_view().uds_path() == "abstract");
  }

  // The `string_view` constructor also accepts ANS names.
  if (true) {
    net_endpoint ep{std::string_view{"@svc"}};
    CHECK(ep.as_sockaddr_view().is_ans());
    CHECK(ep.as_sockaddr_view().uds_path() == "svc");
  }

  // An IPv4-mapped IPv6 address (e.g., `[::ffff:192.168.1.1]:80`) is stored
  // as `AF_INET6` with no unwrapping. `to_string` formats the address in
  // pure colon-hex (RFC 5952), so `::ffff:192.168.1.1` appears as
  // `::ffff:c0a8:101`.
  if (true) {
    net_endpoint ep{"[::ffff:192.168.1.1]:80"};
    CHECK(!ep.empty());
    CHECK(ep.as_sockaddr_view().is_v6());
    CHECK_FALSE(ep.as_sockaddr_view().is_v4());
    CHECK(ep.port() == 80U);
    CHECK(ep.to_string() == "[::ffff:c0a8:101]:80");
  }
}

#pragma endregion
#pragma region Comparison

TEST_CASE("Comparison", "[NetEndpoint]") {
  net_endpoint a{ipv4_addr(10, 0, 0, 1), 80};
  net_endpoint b{ipv4_addr(10, 0, 0, 1), 81};
  net_endpoint c{ipv4_addr(10, 0, 0, 1), 80};

  CHECK(a == c);
  CHECK_FALSE(a == b);
  CHECK(a < b);

  // UDS endpoints compare by path.
  net_endpoint u1{"/a.sock"};
  net_endpoint u2{"/b.sock"};
  net_endpoint u3{"/a.sock"};
  CHECK((!u1.empty() && !u2.empty() && !u3.empty()));
  CHECK(u1 == u3);
  CHECK_FALSE(u1 == u2);
  CHECK(u1 < u2);

  // UDS and IPv4 compare by family.
  CHECK(a != u1);

  // ANS endpoints compare by the length-delimited name.
  auto n1 = net_endpoint{"@same"};
  auto n2 = net_endpoint{"@same"};
  auto n3 = net_endpoint{"@zzz"};
  CHECK((!n1.empty() && !n2.empty() && !n3.empty()));
  CHECK(n1 == n2);
  CHECK(n1 < n3);

  // ANS and regular UDS are unequal (sun_path[0] differs: '\0' vs '/').
  CHECK(n1 != u1);

  // The address length participates in ANS identity: "abc" and "abc\0\0" are
  // distinct names, not equal-after-padding.
  auto short_name = net_endpoint{"@abc"};
  auto padded_name = net_endpoint{std::string_view{"@abc\0\0", 6}};
  CHECK((short_name.as_sockaddr_view().is_ans() &&
         padded_name.as_sockaddr_view().is_ans()));
  CHECK(short_name != padded_name);
  CHECK(short_name.as_sockaddr_view().uds_path().size() == 3U);
  CHECK(padded_name.as_sockaddr_view().uds_path().size() == 5U);
}

#pragma endregion
#pragma region Formatting

TEST_CASE("Formatting", "[NetEndpoint]") {
  auto v4 = net_endpoint{ipv4_addr(127, 0, 0, 1), 80};
  auto v6 = net_endpoint{ipv6_addr::loopback, 443};
  CHECK(v4.to_string() == "127.0.0.1:80");
  CHECK(v6.to_string() == "[::1]:443");

  auto uds = net_endpoint{"/tmp/app.sock"};
  CHECK(!uds.empty());
  CHECK(uds.to_string() == "unix:/tmp/app.sock");

  // ANS: a name with no embedded null prints in full.
  auto ans = net_endpoint{"@svc"};
  CHECK(!ans.empty());
  CHECK(ans.to_string() == "unix:@svc");

  // ANS: a name with an embedded null truncates the display there and
  // appends the full name length. Pass a `string_view` that includes the
  // embedded null.
  if (true) {
    net_endpoint ep{std::string_view{"@abc\0def", 8}};
    CHECK(ep.as_sockaddr_view().is_ans());
    CHECK(ep.as_sockaddr_view().uds_path().size() == 7U);
    CHECK(ep.to_string() == "unix:@abc (len 7)");
  }

  // ANS: trailing zeros are part of the name, so they trigger the same
  // truncated-display form.
  if (true) {
    net_endpoint ep{std::string_view{"@abc\0\0", 6}};
    CHECK(ep.as_sockaddr_view().is_ans());
    CHECK(ep.to_string() == "unix:@abc (len 5)");
  }

  // ANS: name that fills the entire 107-byte buffer with no null uses the
  // full length. Pass a `string_view` of 108 chars (leading '@' + 107 'x').
  if (true) {
    const std::string max_name(107, 'x');
    const std::string full_name = "@" + max_name;
    net_endpoint ep{std::string_view{full_name}};
    CHECK(ep.as_sockaddr_view().is_ans());
    CHECK(ep.to_string() == ("unix:@" + max_name));
  }

  // std::format goes through the formatter, not to_string.
  CHECK(std::format("{}", v4) == "127.0.0.1:80");
  CHECK(std::format("{}", v6) == "[::1]:443");
  CHECK(std::format("{}", uds) == "unix:/tmp/app.sock");
  CHECK(std::format("{}", ans) == "unix:@svc");
  CHECK(std::format("{}", net_endpoint{}) == "(invalid)");

  // A `sockaddr_view` formats directly, identically to the endpoint over
  // which it is taken.
  CHECK(std::format("{}", v4.as_sockaddr_view()) == "127.0.0.1:80");
  CHECK(std::format("{}", v6.as_sockaddr_view()) == "[::1]:443");
  CHECK(std::format("{}", uds.as_sockaddr_view()) == "unix:/tmp/app.sock");
  CHECK(std::format("{}", sockaddr_view{}) == "(invalid)");
}

#pragma endregion
#pragma region PosixInterop

TEST_CASE("PosixInterop", "[NetEndpoint]") {
  if (true) {
    net_endpoint ep{ipv4_addr(192, 168, 1, 2), 1234};
    auto raw = ep.as_sockaddr_view().as_sockaddr_in();
    net_endpoint roundtrip{raw};
    CHECK(roundtrip == ep);

    net_endpoint from_sockaddr{reinterpret_cast<const sockaddr&>(raw),
        sizeof(raw)};
    CHECK(from_sockaddr == ep);

    auto storage = ep.as_sockaddr_storage();
    auto* as_v4 = reinterpret_cast<const sockaddr_in*>(&storage);
    CHECK(as_v4->sin_family == AF_INET);
    CHECK(ntohs(as_v4->sin_port) == 1234U);
  }

  if (true) {
    net_endpoint ep{ipv6_addr(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1), 4321};
    auto raw = ep.as_sockaddr_view().as_sockaddr_in6();
    net_endpoint roundtrip{raw};
    CHECK(roundtrip == ep);

    net_endpoint from_sockaddr{reinterpret_cast<const sockaddr&>(raw),
        sizeof(raw)};
    CHECK(from_sockaddr == ep);

    auto storage = ep.as_sockaddr_storage();
    auto* as_v6 = reinterpret_cast<const sockaddr_in6*>(&storage);
    CHECK(as_v6->sin6_family == AF_INET6);
    CHECK(ntohs(as_v6->sin6_port) == 4321U);
  }

  // UDS: roundtrip through `as_sockaddr_un` and back.
  if (true) {
    net_endpoint ep{"/tmp/interop.sock"};
    CHECK(!ep.empty());

    auto raw = ep.as_sockaddr_view().as_sockaddr_un();
    CHECK(raw.sun_family == static_cast<sa_family_t>(AF_UNIX));
    CHECK(std::string_view{raw.sun_path} == "/tmp/interop.sock");

    net_endpoint roundtrip{raw};
    CHECK(roundtrip == ep);

    net_endpoint from_sockaddr{reinterpret_cast<const sockaddr&>(raw),
        ep.sockaddr_size()};
    CHECK(from_sockaddr == ep);

    // The stated length must be exact: passing the whole-struct size for a
    // shorter path fails.
    net_endpoint overstated{reinterpret_cast<const sockaddr&>(raw),
        sizeof(raw)};
    CHECK(overstated.empty());

    CHECK((ep.sockaddr_size()) ==
          (static_cast<socklen_t>(
              offsetof(sockaddr_un, sun_path) +
              std::strlen("/tmp/interop.sock") + 1)));
  }

  // A maximum-length pathname (107 chars) still derives its length from the
  // contents.
  if (true) {
    sockaddr_un raw{};
    raw.sun_family = AF_UNIX;
    const std::string long_path = "/" + std::string(106, 'x');
    long_path.copy(raw.sun_path, long_path.size());
    net_endpoint ep{raw};
    CHECK(!ep.empty());
    CHECK(ep.as_sockaddr_view().uds_path() == long_path);
    CHECK(ep.sockaddr_size() ==
          offsetof(sockaddr_un, sun_path) + long_path.size() + 1);
  }

  // ANS: build sockaddr_un manually (as the kernel would return it) and
  // roundtrip through net_endpoint.
  if (true) {
    sockaddr_un raw{};
    raw.sun_family = AF_UNIX;
    // sun_path[0] = '\0' (already from zero-init); copy name after it.
    const std::string_view name = "myabstract";
    name.copy(raw.sun_path + 1, sizeof(raw.sun_path) - 1);
    // Pass sizeof(sockaddr_un) as len: full buffer is the name.
    const socklen_t len = sizeof(sockaddr_un);

    net_endpoint ep{reinterpret_cast<const sockaddr&>(raw), len};
    CHECK(ep.as_sockaddr_view().is_ans());
    CHECK(ep.as_sockaddr_view().uds_path().size() == 107U);
    CHECK(ep.as_sockaddr_view().uds_path().substr(0, 10) == name);

    // Roundtrip via as_sockaddr_un() plus the explicit length: the struct
    // alone cannot carry ANS identity.
    auto raw2 = ep.as_sockaddr_view().as_sockaddr_un();
    net_endpoint ep2{reinterpret_cast<const sockaddr&>(raw2),
        ep.sockaddr_size()};
    CHECK(ep2 == ep);

    // The full buffer was given as the length, so that is the name's extent.
    CHECK(ep.sockaddr_size() == sizeof(sockaddr_un));
  }

  // The sockaddr_un constructor rejects ANS: without a length, the name's
  // extent is unknowable.
  if (true) {
    sockaddr_un raw{};
    raw.sun_family = AF_UNIX;
    const std::string_view name = "abstract";
    name.copy(raw.sun_path + 1, sizeof(raw.sun_path) - 1);
    CHECK(net_endpoint{raw}.empty());
  }

  // A kernel-style short ANS length is honored exactly, and reassignment
  // fully replaces the previous state, ghost bytes included.
  if (true) {
    sockaddr_un raw{};
    raw.sun_family = AF_UNIX;
    const std::string_view name = "abc";
    name.copy(raw.sun_path + 1, name.size());
    const auto len = static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path) + 1 + name.size());

    // Start from an endpoint whose storage held a long path.
    net_endpoint ep{"/a/very/long/path/that/fills/many/bytes/of/storage"};
    REQUIRE(!ep.empty());
    CHECK(
        ep.assign(sockaddr_view{reinterpret_cast<const sockaddr&>(raw), len}));
    CHECK(ep.as_sockaddr_view().is_ans());
    CHECK(ep.as_sockaddr_view().uds_path() == name);
    CHECK(ep == net_endpoint{"@abc"});

    // A pathname arriving with a padded (whole-struct) length is rejected:
    // the stated length must be exact.
    sockaddr_un padded{};
    padded.sun_family = AF_UNIX;
    const std::string_view path = "/tmp/pad.sock";
    path.copy(padded.sun_path, path.size());
    net_endpoint from_padded{reinterpret_cast<const sockaddr&>(padded),
        sizeof(padded)};
    CHECK(from_padded.empty());
  }

  // A failed assign reports false and leaves the endpoint empty, discarding
  // the previous state.
  if (true) {
    net_endpoint ep{ipv4_addr::loopback, 80};
    REQUIRE(!ep.empty());
    // A truncated IPv6 address: the stated length is shorter than the
    // contents require.
    sockaddr_in6 bogus{};
    bogus.sin6_family = AF_INET6;
    CHECK_FALSE(ep.assign(sockaddr_view{
        reinterpret_cast<const sockaddr&>(bogus), sizeof(sockaddr_in)}));
    CHECK(ep.empty());
  }
}

#pragma endregion
#pragma region NumericIPv4

TEST_CASE("NumericIPv4", "[DnsResolve]") {
  // Numeric IPv4 addresses are resolved without a DNS lookup.
  auto result = dns_resolver::find_all("127.0.0.1", 80);
  CHECK_FALSE(result.empty());
  bool found = false;
  for (const auto& ep : result) {
    if (ep.as_sockaddr_view().is_v4() && ep.v4()->is_loopback() &&
        ep.port() == 80)
      found = true;
  }
  CHECK(found);
}

#pragma endregion
#pragma region NumericIPv6

TEST_CASE("NumericIPv6", "[DnsResolve]") {
  // Numeric IPv6 addresses are resolved without a DNS lookup.
  auto result = dns_resolver::find_all("::1", 443, address_family::inet6);
  CHECK_FALSE(result.empty());
  bool found = false;
  for (const auto& ep : result) {
    if (ep.as_sockaddr_view().is_v6() && ep.v6()->is_loopback() &&
        ep.port() == 443)
      found = true;
  }
  CHECK(found);
}

#pragma endregion
#pragma region Localhost

TEST_CASE("Localhost", "[DnsResolve]") {
  // "localhost" is defined in /etc/hosts on all major POSIX systems.
  auto result = dns_resolver::find_all("localhost", 8080);
  CHECK_FALSE(result.empty());
  // Every returned endpoint must use the requested port.
  for (const auto& ep : result) CHECK(ep.port() == 8080U);
  // At least one result should be a loopback address.
  bool found = false;
  for (const auto& ep : result) {
    if ((ep.as_sockaddr_view().is_v4() && ep.v4()->is_loopback()) ||
        (ep.as_sockaddr_view().is_v6() && ep.v6()->is_loopback()))
      found = true;
  }
  CHECK(found);
}

#pragma endregion
#pragma region FamilyFilter

TEST_CASE("FamilyFilter", "[DnsResolve]") {
  // With `inet`, every result must be an IPv4 endpoint.
  auto v4 = dns_resolver::find_all("localhost", 80, address_family::inet);
  for (const auto& ep : v4) CHECK(ep.as_sockaddr_view().is_v4());

  // With `inet6`, every result must be an IPv6 endpoint.
  auto v6 = dns_resolver::find_all("localhost", 80, address_family::inet6);
  for (const auto& ep : v6) CHECK(ep.as_sockaddr_view().is_v6());
}

#pragma endregion
#pragma region InvalidHost

TEST_CASE("InvalidHost", "[DnsResolve]") {
  // The `.invalid` TLD (RFC 2606) must not resolve.
  auto result = dns_resolver::find_all("no-such-host.invalid", 80);
  CHECK(result.empty());
}

#pragma endregion
#pragma region Success

TEST_CASE("Success", "[DnsResolveOne]") {
  // Numeric loopback resolves to exactly one endpoint with the right port.
  const auto ep = dns_resolver::find_one("127.0.0.1", 80);
  CHECK(ep.as_sockaddr_view().is_v4());
  CHECK(ep.port() == 80U);
}

#pragma endregion
#pragma region Failure

TEST_CASE("Failure", "[DnsResolveOne]") {
  // An unresolvable host returns a default-constructed (invalid) endpoint.
  const auto ep = dns_resolver::find_one("no-such-host.invalid", 80);
  CHECK(ep == net_endpoint{});
}

#pragma endregion

// Minimal `epoll_io_conn` that counts how many times each virtual is called.
struct counting_conn: epoll_io_conn {
  using epoll_io_conn::epoll_io_conn;

  int readable = 0;
  int writable = 0;
  int error = 0;
  bool on_readable() override {
    ++readable;
    return true;
  }
  bool on_writable() override {
    ++writable;
    return true;
  }
  bool on_error() override {
    ++error;
    return true;
  }
};
#pragma region Lifecycle

TEST_CASE("Lifecycle", "[IoLoop]") {
  // Construction succeeds; an empty poll returns 0 events.
  auto loop = epoll_loop::make();
  CHECK(loop->run_once(0) == 0);
}

#pragma endregion
#pragma region Post

TEST_CASE("Post", "[IoLoop]") {
  auto loop = epoll_loop::make();

  int fired = 0;
  CHECK(loop->post([&] {
    ++fired;
    return true;
  }));

  // post() callback runs at the top of the next run_once(), even with no
  // I/O events.
  CHECK(loop->run_once(0) >= 0);
  CHECK(fired == 1);
}

#pragma endregion
#pragma region PreStartWorkIsQueued

TEST_CASE("PreStartWorkIsQueued", "[IoLoop]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  CHECK(loop->is_loop_thread());

  auto conn = std::make_shared<counting_conn>(std::move(a));
  REQUIRE(loop->register_socket(conn, false, false));
  CHECK_FALSE(loop->register_socket(conn, false, false));

  auto msg_view = std::string_view{"hi"};
  REQUIRE((b.send(msg_view) && msg_view.empty()));
  CHECK(conn->readable == 0);

  // The first pump drains the queued registration work, but read interest is
  // still disabled so the buffered data should remain undispatched.
  CHECK(loop->run_once(0) == 0);
  CHECK(conn->readable == 0);

  REQUIRE(loop->enable_reads(*conn, true));
  CHECK(loop->run_once(0) >= 0);
  CHECK(conn->readable == 1);
}

#pragma endregion
#pragma region SelfDestroyOnLoopThread

// A loop-thread callback that holds the last `unique_ptr<epoll_loop_runner>`
// destroys the runner from inside the worker thread. The destructor detaches
// the jthread and returns; the worker then unwinds out of `epoll_loop::run`
// and finishes its cleanup. The worker keeps its own ref to `runner_state`,
// so the post-`run` cleanup must not touch any state belonging to the
// already-freed handle.
//
// Synchronize with the detached worker via `finished_signal`, which the
// worker notifies after `~epoll_loop` has run. This gives TSAN a real
// happens-before edge with the worker's exit (no sleep).
TEST_CASE("SelfDestroyOnLoopThread", "[IoLoop]") {
  auto runner = std::make_unique<epoll_loop_runner>();
  auto* loop = runner->loop();
  REQUIRE(loop != nullptr);
  auto finished = runner->finished_signal();

  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  REQUIRE(loop->post([r = std::move(runner)]() mutable {
    r.reset();
    return true;
  }));

  REQUIRE(finished->wait_for_value(1s, true));
}

#pragma endregion

// `register_socket` dispatches `on_readable` via virtual `epoll_io_conn`
// override; `unregister_socket` stops further dispatch. Double-register and
// double-unregister both return false.
#pragma region RegisterUnregister

TEST_CASE("RegisterUnregister", "[IoLoop]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  REQUIRE(loop->register_socket(conn));

  auto msg_view = std::string_view{"hi"};
  REQUIRE((b.send(msg_view) && msg_view.empty()));

  CHECK(loop->run_once(0) >= 0);
  CHECK(conn->readable == 1);
  CHECK(conn->writable == 0);
  CHECK(conn->error == 0);

  // Drain the data from the registered socket so the fd is no longer readable.
  std::string buf(8, '\0');
  (void)conn->sock().read(buf);

  REQUIRE(loop->unregister_socket(conn->sock()));
  CHECK_FALSE(loop->unregister_socket(conn->sock())); // already removed

  // No events after unregistering.
  CHECK(loop->run_once(0) == 0);
  CHECK(conn->readable == 1);
}

#pragma endregion

// `enable_writes(true)` arms `EPOLLOUT`; the kernel fires it when the kernel
// send buffer has space, which it does immediately on a fresh socketpair.
#pragma region SetWritable

TEST_CASE("SetWritable", "[IoLoop]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  REQUIRE(loop->register_socket(conn));

  // `EPOLLOUT` is not initially armed; no writable event.
  CHECK(loop->run_once(0) == 0);
  CHECK(conn->writable == 0);

  REQUIRE(loop->enable_writes(*conn, true));
  CHECK(loop->run_once(0) >= 0);
  CHECK(conn->writable >= 1);

  // Disarm; no further writable events.
  REQUIRE(loop->enable_writes(*conn, false));
  const int w = conn->writable;
  CHECK(loop->run_once(0) == 0);
  CHECK(conn->writable == w);
}

#pragma endregion
#pragma region SetReadable

TEST_CASE("SetReadable", "[IoLoop]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  REQUIRE(loop->register_socket(conn, false, false));

  auto first = std::string_view{"hi"};
  REQUIRE((b.send(first) && first.empty()));

  CHECK(loop->run_once(0) == 0);
  CHECK(conn->readable == 0);
  CHECK(conn->writable == 0);

  REQUIRE(loop->enable_reads(*conn, true));
  CHECK(loop->run_once(0) >= 0);
  CHECK(conn->readable == 1);
  CHECK(conn->writable == 0);

  std::string buf(8, '\0');
  (void)conn->sock().read(buf);

  REQUIRE(loop->enable_reads(*conn, false));
  REQUIRE(loop->enable_writes(*conn, true));
  CHECK(loop->run_once(0) >= 0);
  CHECK(conn->readable == 1);
  CHECK(conn->writable >= 1);
}

#pragma endregion

// When `EPOLLERR` or `EPOLLHUP` fires together with `EPOLLOUT`, `on_error` is
// called but `on_writable` is skipped (the early return path in
// `dispatch_event`).
#pragma region ErrorSkipsWritable

TEST_CASE("ErrorSkipsWritable", "[IoLoop]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  REQUIRE(loop->register_socket(conn));
  REQUIRE(loop->enable_writes(*conn, true)); // arm EPOLLOUT

  (void)b.close(); // triggers EPOLLHUP on `a`

  CHECK(loop->run_once(0) >= 0);
  CHECK(conn->error == 1);
  CHECK(conn->writable == 0); // must not fire when error/hup is reported
}

#pragma endregion

// The default `epoll_io_conn::on_error` implementation falls through to
// `on_readable`. Verify this by registering a subclass that overrides only
// `on_readable` and confirming it is called when the peer closes.
#pragma region DefaultOnError

TEST_CASE("DefaultOnError", "[IoLoop]") {
  struct readable_only_conn: epoll_io_conn {
    using epoll_io_conn::epoll_io_conn;
    int readable = 0;
    bool on_readable() override {
      ++readable;
      return true;
    }
    // on_error not overridden; default calls on_readable()
  };

  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  auto conn = std::make_shared<readable_only_conn>(std::move(a));
  REQUIRE(loop->register_socket(conn));

  (void)b.close(); // EPOLLHUP -> default on_error() -> on_readable()
  CHECK(loop->run_once(0) >= 0);

  CHECK(conn->readable >= 1);
}

#pragma endregion

// Helper: allocate `cap` bytes into `rb.buffer`, set `begin`/`end`, and
// default `min_capacity` to the actual post-allocation capacity so that
// resize logic does not fire unexpectedly in tests that do not set it.
// Fills active region [b..e) with `ch` so moves can be verified.
static void setup_rb(epoll_recv_buffer& rb, size_t cap, size_t b, size_t e,
    char ch = 'X') {
  no_zero{rb.buffer}.enlarge_to(cap);
  rb.min_capacity = rb.buffer.capacity();
  if (b < e) std::fill(rb.buffer.data() + b, rb.buffer.data() + e, ch);
  rb.begin.store(b, std::memory_order::relaxed);
  rb.end.store(e, std::memory_order::relaxed);
}

// When there are no active bytes, compact always resets begin and end to 0.
#pragma region Compact_NoActiveBytes

TEST_CASE("Compact_NoActiveBytes", "[RecvBuffer]") {
  if (true) {
    // begin == end == 0: already at front, cheap reset is a no-op.
    epoll_recv_buffer rb;
    setup_rb(rb, 64, 0, 0);
    rb.compact();
    CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
    CHECK(rb.end.load(std::memory_order::relaxed) == 0U);
    CHECK(rb.write_space() == rb.buffer.capacity());
  }
  if (true) {
    // begin == end > 0: cheap reset reclaims all space.
    epoll_recv_buffer rb;
    setup_rb(rb, 64, 40, 40);
    rb.compact();
    CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
    CHECK(rb.end.load(std::memory_order::relaxed) == 0U);
    CHECK(rb.write_space() == rb.buffer.capacity());
  }
}

#pragma endregion

// When write space is exhausted (end == capacity) but begin > 0, compact
// must memmove to reclaim space before the active region.
#pragma region Compact_MustCompact

TEST_CASE("Compact_MustCompact", "[RecvBuffer]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  const size_t b = cap / 4; // begin at 1/4 mark (not past it: worth_it false)
  setup_rb(rb, cap, b, cap, 'A'); // end == capacity: must compact
  rb.compact();
  CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
  CHECK(rb.end.load(std::memory_order::relaxed) == (cap - b));
  CHECK(rb.buffer[0] == 'A');
  CHECK((rb.write_space()) > (0U));
}

#pragma endregion

// When begin is past the 1/4 mark and end is past the 3/4 mark, compact
// proactively moves bytes to avoid a short recv on the next call.
#pragma region Compact_WorthIt

TEST_CASE("Compact_WorthIt", "[RecvBuffer]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  const size_t b = (cap / 4) + 1;     // just past 1/4 mark
  const size_t e = (cap / 4 * 3) + 1; // just past 3/4 mark
  setup_rb(rb, cap, b, e, 'B');
  rb.compact();
  CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
  CHECK(rb.end.load(std::memory_order::relaxed) == (e - b));
  CHECK(rb.buffer[0] == 'B');
}

#pragma endregion

// When neither "must" nor "worth it" applies, begin and end are left
// unchanged to avoid a pointless memmove.
#pragma region Compact_SkipsUnnecessaryMove

TEST_CASE("Compact_SkipsUnnecessaryMove", "[RecvBuffer]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  const size_t b = cap / 4; // at the 1/4 mark, not past it: worth_it false
  const size_t e = cap / 2; // end well before 3/4 mark, write_space > 0
  setup_rb(rb, cap, b, e);
  rb.compact();
  CHECK(rb.begin.load(std::memory_order::relaxed) == b);
  CHECK(rb.end.load(std::memory_order::relaxed) == e);
}

#pragma endregion

// When target exceeds current capacity, the buffer is grown and active bytes
// are copied to the front of the new buffer.
#pragma region Compact_GrowOnRequest

TEST_CASE("Compact_GrowOnRequest", "[RecvBuffer]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  setup_rb(rb, cap, 10, 30, 'C');
  rb.compact(cap * 2);
  CHECK(rb.buffer.capacity() >= (cap * 2));
  CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
  CHECK(rb.end.load(std::memory_order::relaxed) == 20U);
  CHECK(rb.buffer[0] == 'C');
}

#pragma endregion

// When current capacity is below min_capacity, compact grows the buffer
// and syncs min_capacity to the actual post-resize capacity.
#pragma region Compact_GrowToMinCapacity

TEST_CASE("Compact_GrowToMinCapacity", "[RecvBuffer]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 32, 0, 10, 'D');
  const size_t configured = rb.buffer.capacity() * 2;
  rb.min_capacity = configured;
  rb.compact();
  CHECK(rb.buffer.capacity() >= configured);
  CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
  CHECK(rb.end.load(std::memory_order::relaxed) == 10U);
  CHECK(rb.buffer[0] == 'D');
  // min_capacity is synced to the actual post-resize capacity.
  CHECK(size_t(rb.min_capacity) == rb.buffer.capacity());
}

#pragma endregion

// When the buffer has bloated beyond 2x min_capacity and all active data
// fits in min_capacity, compact shrinks back to min_capacity.
#pragma region Compact_Shrink

TEST_CASE("Compact_Shrink", "[RecvBuffer]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t bloated_cap = rb.buffer.capacity();
  // configured = cap/4; current = cap = 4*configured > 2*configured: bloated.
  const size_t configured = bloated_cap / 4;
  // Fill active region [4..4+configured) then override min_capacity.
  setup_rb(rb, bloated_cap, 4, 4 + configured,
      'E');                     // active_len == configured
  rb.min_capacity = configured; // set after setup_rb, which resets it
  rb.compact();
  CHECK((rb.buffer.capacity()) < (bloated_cap));
  CHECK(rb.buffer.capacity() >= configured);
  CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
  CHECK(rb.end.load(std::memory_order::relaxed) == configured);
  CHECK(rb.buffer[0] == 'E');
}

#pragma endregion

// Shrink is skipped when active_len exceeds min_capacity; the shrunken
// buffer would not fit the active data.
#pragma region Compact_ShrinkSkippedIfActiveWontFit

TEST_CASE("Compact_ShrinkSkippedIfActiveWontFit", "[RecvBuffer]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  // active_len = cap/2 > configured = cap/4: shrink condition fails.
  setup_rb(rb, cap, 0, cap / 2);
  rb.min_capacity = cap / 4; // set after setup_rb, which resets it
  rb.compact();
  CHECK(rb.buffer.capacity() == cap); // no resize
  // begin=0, end unchanged (no memmove: begin not past 1/4 mark).
  CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
  CHECK(rb.end.load(std::memory_order::relaxed) == (cap / 2));
}

#pragma endregion

// When target > 0 but does not exceed current capacity, no resize occurs.
#pragma region Compact_NoResizeWhenTargetFits

TEST_CASE("Compact_NoResizeWhenTargetFits", "[RecvBuffer]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  rb.compact(cap / 2); // target <= current: no resize
  CHECK(rb.buffer.capacity() == cap);
}

#pragma endregion

// `update_active_view` advances `begin` by the number of bytes the parser
// moved past in an `active_view` snapshot. Calling it in stages is
// equivalent to a single `consume` for the total consumed bytes.
#pragma region UpdateActiveView

TEST_CASE("UpdateActiveView", "[RecvBufferView]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 5, 'X');

  epoll_recv_buffer_view v{rb, [](size_t, size_t) {}};

  // First look: 5 bytes available.
  std::string_view sv = v;
  CHECK(sv.size() == 5U);

  // Advance 2 bytes into the snapshot, then inform the view.
  sv.remove_prefix(2);
  v.update_active_view(sv);
  CHECK(rb.begin.load(std::memory_order::relaxed) == 2U);

  // Second look: 3 bytes remain at the new `begin`.
  sv = v;
  CHECK(sv.size() == 3U);
  CHECK(sv[0] == 'X');

  // Consume the rest.
  sv.remove_prefix(3);
  v.update_active_view(sv);
  CHECK(rb.begin.load(std::memory_order::relaxed) == 5U);
}

#pragma endregion

// The moved-from view must not fire the resume callback; only the final
// owner fires it, exactly once. `last_seen_end_` and `new_buffer_size_` are
// carried over by the move so the callback receives correct values.
#pragma region MoveSemantics

TEST_CASE("MoveSemantics", "[RecvBufferView]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 10, 'X');

  if (true) {
    // Move construction: moved-from is null; moved-to fires resume.
    size_t fired_new_size{};
    size_t fired_lse{};
    {
      epoll_recv_buffer_view v1{rb,
          [&](size_t n, size_t lse) {
            fired_new_size = n;
            fired_lse = lse;
          }};
      epoll_recv_buffer_view v2{std::move(v1)}; // v1 now null

      // v2 retains full buffer access.
      CHECK(v2.active_view().size() == 10U); // also sets last_seen_end_ = 10
      v2.expand_to(128);
      // v1 destructs silently; v2 destructs and fires resume(128, 10).
    }
    CHECK(fired_new_size == 128U);
    CHECK(fired_lse == 10U);
  }
}

#pragma endregion

// try_take_full returns false and leaves everything unchanged when `end` is
// not at the physical end of the buffer.
#pragma region TryTakeFull_Fail

TEST_CASE("TryTakeFull_Fail", "[RecvBufferView]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 5, 'X');
  const size_t cap = rb.buffer.capacity();

  epoll_recv_buffer_view v{rb, [](size_t, size_t) {}};
  std::string out;
  std::string_view sv;
  CHECK_FALSE(v.try_take_full(out, sv));
  CHECK(out.empty());
  CHECK(sv.empty());
  CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
  CHECK(rb.end.load(std::memory_order::relaxed) == 5U);
  CHECK(rb.buffer.capacity() == cap);
}

#pragma endregion

// try_take_full succeeds when `end == capacity`. The backing buffer is
// swapped into `out`, `view` covers the active region inside `out`, and
// indices are reset. The destructor callback receives `lse == 0`.
#pragma region TryTakeFull_Success

TEST_CASE("TryTakeFull_Success", "[RecvBufferView]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  // Active region starts mid-buffer so the view offset is verified.
  setup_rb(rb, cap, 10, cap, 'A');

  auto cb_lse = 1UZ; // non-zero sentinel; confirmed reset to 0 by destructor
  {
    epoll_recv_buffer_view v{rb, [&](size_t, size_t lse) { cb_lse = lse; }};
    std::string out;
    std::string_view sv;
    CHECK(v.try_take_full(out, sv));

    // `out` holds the full old buffer.
    CHECK(out.size() == cap);
    // `sv` covers the active portion inside `out`.
    CHECK(sv.data() == (out.data() + 10));
    CHECK(sv.size() == (cap - 10U));
    CHECK(sv[0] == 'A');
    // Backing buffer maintains the `size == capacity` invariant.
    CHECK(rb.buffer.size() == rb.buffer.capacity());
    CHECK(rb.begin.load(std::memory_order::relaxed) == 0U);
    CHECK(rb.end.load(std::memory_order::relaxed) == 0U);
  } // destructor fires with lse == 0
  CHECK(cb_lse == 0U);
}

#pragma endregion

// Pre-loading `out` with a large allocation causes try_take_full to steal it
// for the backing buffer, avoiding a reallocation on the next cycle.
#pragma region TryTakeFull_StealAllocation

TEST_CASE("TryTakeFull_StealAllocation", "[RecvBufferView]") {
  epoll_recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  setup_rb(rb, cap, 0, cap, 'B');

  // Give `out` a large allocation to steal.
  std::string out;
  no_zero{out}.enlarge_to(512);
  const size_t big_cap = out.capacity();
  CHECK(big_cap >= 512U);

  epoll_recv_buffer_view v{rb, [](size_t, size_t) {}};
  std::string_view sv;
  CHECK(v.try_take_full(out, sv));

  // The internal buffer now holds the stolen large allocation.
  CHECK(rb.buffer.capacity() >= big_cap);
  // `out` holds the data that was in the buffer.
  CHECK(sv.size() == cap);
  CHECK(sv[0] == 'B');
}

#pragma endregion
#pragma region Lifecycle

TEST_CASE("Lifecycle", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  const net_endpoint remote{ipv4_addr::loopback, 9999};
  {
    auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), remote, {});
    // open_ is set in the state constructor before the post fires.
    CHECK(conn->is_open());
    CHECK(conn->remote_endpoint() == remote);
    CHECK(loop->run_once(0) >= 0); // process posted do_open()
  }
  // destructor posted do_hangup(); process it, then verify loop is clean.
  CHECK(loop->run_once(0) >= 0);
  CHECK(loop->run_once(0) == 0);
}

#pragma endregion
#pragma region Receive

TEST_CASE("Receive", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  std::string received;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data = [&](epoll_stream_conn&, epoll_recv_buffer_view v) {
        std::string_view av = v;
        received.assign(av);
        v.consume(av.size());
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // process posted do_open()

  const std::string msg{"hello"};
  auto msg_view = std::string_view{msg};
  REQUIRE((b.send(msg_view) && msg_view.empty()));

  CHECK(loop->run_once(0) >= 0); // dispatch EPOLLIN
  CHECK(received == msg);
}

#pragma endregion
#pragma region SetRecvBufSize

TEST_CASE("SetRecvBufSize", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  // `recv_buf_size` is a target for future compactions; the actual per-read
  // limit may be larger when the allocator (or SSO) provides extra capacity.
  // Test that the getter/setter works and that all data arrives correctly.
  std::string received;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data =
              [&](epoll_stream_conn&, epoll_recv_buffer_view v) {
                std::string_view av = v;
                received.append(av);
                v.consume(av.size());
                return true;
              }},
      4);
  CHECK(loop->run_once(0) >= 0); // process posted register_with_loop

  CHECK(conn->recv_buf_size() == 4U);

  auto first = std::string_view{"abcd1234"};
  REQUIRE((b.send(first) && first.empty()));
  for (auto ndx = 0; ndx < 4 && received.size() < 8; ++ndx)
    CHECK(loop->run_once(0) >= 0);
  CHECK(received == "abcd1234");

  REQUIRE(conn->set_recv_buf_size(8));
  CHECK(conn->recv_buf_size() == 8U);

  auto second = std::string_view{"ABCDEFGHijkl"};
  REQUIRE((b.send(second) && second.empty()));
  for (auto ndx = 0; ndx < 4 && received.size() < 20; ++ndx)
    CHECK(loop->run_once(0) >= 0);
  CHECK(received == "abcd1234ABCDEFGHijkl");
}

#pragma endregion
#pragma region PeerClose

TEST_CASE("PeerClose", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  bool closed = false;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_close = [&](epoll_stream_conn&) {
        closed = true;
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // process posted do_open()

  REQUIRE(b.shutdown(shutdown_how::wr));

  CHECK(loop->run_once(0) >= 0); // dispatch readable event with EOF (HUP)

  CHECK(closed);

  CHECK(conn->is_open());
  CHECK_FALSE(conn->can_read());
  CHECK(conn->can_write());

  CHECK(conn->send(std::string{"still-open"}));
  CHECK(loop->run_once(0) >= 0);
  std::string buf;
  no_zero{buf}.enlarge_to(32);
  REQUIRE(b.read(buf));
  CHECK(buf == "still-open");

  CHECK(conn->close());
  CHECK(loop->run_once(0) >= 0);
  CHECK_FALSE(conn->is_open());
}

#pragma endregion

// When the peer sends data and then half-closes before the receiver reads,
// the first `handle_readable` delivers the data via `on_data`. The subsequent
// EOF event enters `handle_read_eof` with a non-empty buffer and no live view,
// so it must dispatch `on_data` once more with the residual bytes (at which
// point `can_read` is already false) before firing `on_close`.
#pragma region PeerClose_WithBufferedData

TEST_CASE("PeerClose_WithBufferedData", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  int data_count = 0;
  bool read_open_at_eof_dispatch = true;
  std::string received;
  bool closed = false;

  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data =
              [&](epoll_stream_conn& c, epoll_recv_buffer_view v) {
                std::string_view av = v;
                // First call: consume only 2 bytes, leaving "llo" in the
                // buffer so that `handle_read_eof` sees a non-empty buffer.
                const size_t n = (data_count == 0) ? 2 : av.size();
                received.append(av.substr(0, n));
                v.consume(n);
                if (data_count == 1) read_open_at_eof_dispatch = c.can_read();
                ++data_count;
                return true;
              },
          .on_close =
              [&](epoll_stream_conn&) {
                closed = true;
                return true;
              }});
  CHECK(loop->run_once(0) >= 0); // process posted register_with_loop

  const std::string msg{"hello"};
  auto msg_view = std::string_view{msg};
  REQUIRE((b.send(msg_view) && msg_view.empty()));
  REQUIRE(b.shutdown(shutdown_how::wr)); // send EOF after data

  // First iteration: reads "hello", dispatches on_data (consumes "he").
  CHECK(loop->run_once(0) >= 0);
  CHECK(data_count == 1);
  CHECK(received == "he");
  CHECK_FALSE(closed);

  // Second iteration: EOF arrives; `handle_read_eof` finds "llo" in the
  // buffer, dispatches on_data with residual bytes, then fires on_close.
  CHECK(loop->run_once(0) >= 0);
  CHECK(data_count == 2);
  CHECK(received == "hello");
  CHECK_FALSE(read_open_at_eof_dispatch); // read side already closed
  CHECK(closed);
}

#pragma endregion
#pragma region Send

TEST_CASE("Send", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  CHECK(loop->run_once(0) >= 0); // process posted do_open()

  CHECK(conn->send(std::string{"world"}));
  // process posted enqueue() -> immediate ::write
  CHECK(loop->run_once(0) >= 0);

  // Data written by enqueue() is now in the kernel buffer.
  std::string buf;
  no_zero{buf}.enlarge_to(16);
  REQUIRE(b.read(buf));
  CHECK(buf.size() == 5U);
  CHECK(buf == "world");
}

#pragma endregion
#pragma region ManualClose

TEST_CASE("ManualClose", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  bool closed = false;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_close = [&](epoll_stream_conn&) {
        closed = true;
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // process posted do_open()

  CHECK(conn.close());
  CHECK(loop->run_once(0) >= 0); // process posted do_close() -> close_now()

  CHECK_FALSE(conn->is_open());
  CHECK(closed);

  // Destructor posts a hangup; it must be idempotent after close().
  CHECK(loop->run_once(0) >= 0);
  CHECK(loop->run_once(0) == 0);
}

#pragma endregion
#pragma region DrainAfterBufferedSend

TEST_CASE("DrainAfterBufferedSend", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  // Restrict the kernel send buffer so that a large write is partial.
  // The kernel may round up, but typically honors a small value closely
  // enough to force at least one EAGAIN before the full payload drains.
  constexpr int small_buf = 4096;
  CHECK(a.set_send_buffer_size(small_buf));

  // Payload larger than the send buffer to reliably exercise the EPOLLOUT
  // path. 256 KB is large enough on typical Linux systems.
  const std::string payload(256ULL * 1024ULL, 'x');

  int drain_count = 0;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_drain = [&](epoll_stream_conn&) {
        ++drain_count;
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // process posted do_open()

  CHECK(conn->send(std::string{payload})); // copy payload into send
  // Drain by reading from `b` and running the loop until all data arrives.
  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  while (received.size() < payload.size()) {
    CHECK(loop->run_once(0) >= 0);
    no_zero{tmp}.enlarge_to(4096);
    while (b.read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero{tmp}.enlarge_to(4096);
    }
  }

  // All bytes must arrive intact.
  CHECK(received.size() == payload.size());
  CHECK(received == payload);

  // `on_drain` must have fired at least once (via the EPOLLOUT path).
  CHECK(drain_count >= 1);
}

#pragma endregion
#pragma region DrainAfterImmediateSend

TEST_CASE("DrainAfterImmediateSend", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  int drain_count = 0;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_drain = [&](epoll_stream_conn&) {
        ++drain_count;
        return true;
      }});
  // `register_with_loop` arms `EPOLLOUT` for all non-listening sockets, so
  // the first writable event fires `on_drain` even with an empty send queue.
  CHECK(loop->run_once(0) >= 0); // register_with_loop + initial EPOLLOUT
  CHECK(drain_count == 1);       // initial drain fired

  CHECK(conn->send(std::string{"hello"}));
  CHECK(loop->run_once(0) >= 0); // enqueue_send() + EPOLLOUT drain
  CHECK(drain_count == 2);       // second drain after send queue empties

  std::string received;
  no_zero{received}.enlarge_to(16);
  REQUIRE(b.read(received));
  CHECK(received == "hello");
}

#pragma endregion
#pragma region SendRejectsOnlyEmptyBuffers

TEST_CASE("SendRejectsOnlyEmptyBuffers", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  int drain_count = 0;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_drain = [&](epoll_stream_conn&) {
        ++drain_count;
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // register_with_loop + initial EPOLLOUT
  CHECK(drain_count == 1);

  CHECK_FALSE(conn->send(std::string{}));
  CHECK_FALSE(conn->send(std::string{}, std::string{}));
  CHECK(loop->run_once(0) >= 0);
  CHECK(drain_count == 1);

  CHECK(conn->send(std::string{}, std::string{"hello"}, std::string{}));
  CHECK(loop->run_once(0) >= 0);
  CHECK(drain_count == 2);

  std::string received;
  no_zero{received}.enlarge_to(16);
  REQUIRE(b.read(received));
  CHECK(received == "hello");
}

#pragma endregion

// Verify that sending multiple non-empty buffers in a single call delivers all
// of them, not just the first. Regression test for the OR-fold short-circuit
// bug in `enqueue_send`, where buffers after the first were silently dropped.
#pragma region SendMultipleBuffers

TEST_CASE("SendMultipleBuffers", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  CHECK(loop->run_once(0) >= 0); // process posted do_open()

  CHECK(conn->send(std::string{"hello"}, std::string{" "},
      std::string{"world"}));
  CHECK(loop->run_once(0) >= 0); // enqueue_send() -> immediate flush

  std::string received;
  no_zero{received}.enlarge_to(32);
  REQUIRE(b.read(received));
  CHECK(received == "hello world");
}

#pragma endregion
#pragma region ShutdownWrite

TEST_CASE("ShutdownWrite", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  std::string received;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data = [&](epoll_stream_conn&, epoll_recv_buffer_view v) {
        std::string_view av = v;
        received.assign(av);
        v.consume(av.size());
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // process posted register_with_loop

  CHECK(conn->can_read());
  CHECK(conn->can_write());
  CHECK(conn->shutdown_write());
  CHECK(conn->is_open());
  CHECK(conn->can_read());
  CHECK_FALSE(conn->can_write());
  CHECK_FALSE(conn->send(std::string{"nope"}));

  const std::string msg{"inbound"};
  auto msg_view = std::string_view{msg};
  REQUIRE((b.send(msg_view) && msg_view.empty()));
  CHECK(loop->run_once(0) >= 0);
  CHECK(received == msg);
}

#pragma endregion
#pragma region ShutdownRead

TEST_CASE("ShutdownRead", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  int data_count = 0;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data = [&](epoll_stream_conn&, epoll_recv_buffer_view v) {
        v.consume(std::string_view{v}.size());
        ++data_count;
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // process posted register_with_loop

  CHECK(conn->can_read());
  CHECK(conn->can_write());
  REQUIRE(conn->shutdown_read());
  CHECK(conn->is_open());
  CHECK_FALSE(conn->can_read());
  CHECK(conn->can_write());

  CHECK(conn->send(std::string{"outbound"}));
  CHECK(loop->run_once(0) >= 0);

  std::string buf;
  no_zero{buf}.enlarge_to(32);
  REQUIRE(b.read(buf));
  CHECK(buf == "outbound");
  CHECK(data_count == 0);
}

#pragma endregion
#pragma region ShutdownBothCloses

TEST_CASE("ShutdownBothCloses", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  CHECK(loop->run_once(0) >= 0); // process posted register_with_loop

  REQUIRE(conn->shutdown_write());
  CHECK(conn->is_open());
  CHECK_FALSE(conn->can_write());
  CHECK(conn->can_read());

  REQUIRE(conn->shutdown_read());
  CHECK_FALSE(conn->is_open());
  CHECK_FALSE(conn->can_read());
  CHECK_FALSE(conn->can_write());
}

#pragma endregion
#pragma region GracefulClose

TEST_CASE("GracefulClose", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  constexpr int small_buf = 4096;
  CHECK(a.set_send_buffer_size(small_buf));

  const std::string payload(64ULL * 1024ULL, 'z');

  bool closed = false;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_close = [&](epoll_stream_conn&) {
        closed = true;
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // process posted do_open()

  // Queue data then immediately request a close; the close must be deferred
  // until the send queue drains.
  CHECK(conn->send(std::string{payload})); // copy payload into send
  CHECK(conn->close());

  // Drain all data from `b` while running the loop.
  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  while (!closed) {
    CHECK(loop->run_once(0) >= 0);
    no_zero{tmp}.enlarge_to(4096);
    while (b.read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero{tmp}.enlarge_to(4096);
    }
  }

  CHECK(received.size() == payload.size());
  CHECK(received == payload);
  CHECK(closed);
  CHECK_FALSE(conn->is_open());
}

#pragma endregion
#pragma region CloseThenDestructStaysGraceful

TEST_CASE("CloseThenDestructStaysGraceful", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  constexpr int small_buf = 4096;
  CHECK(a.set_send_buffer_size(small_buf));

  const std::string payload(64ULL * 1024ULL, 'g');

  bool closed = false;
  {
    auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
        {.on_close = [&](epoll_stream_conn&) {
          closed = true;
          return true;
        }});
    CHECK(loop->run_once(0) >= 0); // process posted register_with_loop

    CHECK(conn->send(std::string{payload}));
    CHECK(conn->close());
  }

  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  for (auto ndx = 0; ndx < 512 && !closed; ++ndx) {
    CHECK(loop->run_once(0) >= 0);
    no_zero{tmp}.enlarge_to(4096);
    while (b.read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero{tmp}.enlarge_to(4096);
    }
  }

  CHECK(closed);
  CHECK(received.size() == payload.size());
  CHECK(received == payload);
  no_zero{tmp}.enlarge_to(1);
  CHECK_FALSE(b.read(tmp));
}

#pragma endregion

// With `coordination_policy::bilateral` set, `close` shuts down writes after
// the send queue drains, then waits for the peer to close before firing
// `on_close`. Incoming data arriving during the drain is silently discarded.
#pragma region MutualClose

TEST_CASE("MutualClose", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  bool closed = false;
  auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_close = [&](epoll_stream_conn&) {
        closed = true;
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // process posted register_with_loop

  conn->set_shutdown(coordination_policy::bilateral);
  CHECK(conn->shutdown() == coordination_policy::bilateral);
  CHECK(conn->close());
  CHECK(loop->run_once(0) >= 0); // process do_close() -> do_finish_close()

  // After close() with bilateral coordination, conn shuts down its write side
  // but stays open waiting for the peer to close.
  CHECK(conn->is_open());
  CHECK_FALSE(conn->can_write());
  CHECK_FALSE(closed);

  // Send data from the peer; handle_drain_reads discards it.
  auto msg = std::string_view{"some data"};
  REQUIRE((b.send(msg) && msg.empty()));
  CHECK(loop->run_once(0) >= 0); // handle_drain_reads discards data
  CHECK_FALSE(closed);           // still waiting for peer to close

  // Peer closes: handle_drain_reads sees EOF and fires do_close_now ->
  // on_close.
  REQUIRE(b.close());
  CHECK(loop->run_once(0) >= 0);

  CHECK(closed);
  CHECK_FALSE(conn->is_open());
}

#pragma endregion

// Verify that a listener created with `coordination_policy::bilateral`
// propagates the policy to every accepted connection, and that the accepted
// connection's bilateral handshake completes correctly end to end.
//
// The critical invariant under test: with `bilateral` coordination, the
// server's `on_close` does NOT fire after `conn.close()` alone, it fires only
// after the peer (client) has also closed. Since the client is still open when
// we check, the absence of `on_close` is logically guaranteed, not
// timing-based.
#pragma region Listen_MutualClose

TEST_CASE("Listen_MutualClose", "[StreamConn]") {
  epoll_loop_runner loop;

  // Set in `on_data` after the server calls `conn.close`, so the test thread
  // knows the server has initiated its half-close.
  notifiable<bool> close_initiated{false};
  // Set in `on_close` once the server connection fully closes.
  notifiable<bool> server_closed{false};
  // The `coordination_policy` as seen on the accepted connection inside
  // `on_data` -- confirms the policy was copied from the listener.
  notifiable<coordination_policy> accepted_policy{
      coordination_policy::unilateral};

  auto listener = epoll_stream_conn_ptr::listen(loop.loop()->self(),
      net_endpoint{ipv4_addr::loopback, 0},
      {.on_data =
              [&](epoll_stream_conn& conn, epoll_recv_buffer_view v) {
                accepted_policy.notify_one(conn.shutdown());
                std::string_view av = v;
                v.consume(av.size());
                bool ok = conn.close();
                close_initiated.notify_one(true);
                return ok;
              },
          .on_close =
              [&](epoll_stream_conn&) {
                server_closed.notify_one(true);
                return true;
              }},
      coordination_policy::bilateral);
  REQUIRE(listener);

  // The listener itself must carry the shutdown policy.
  CHECK(listener->shutdown() == coordination_policy::bilateral);

  const net_endpoint server_ep = listener->local_endpoint();
  REQUIRE(server_ep);

  // Connect and send a message to trigger `on_data` on the accepted
  // connection.
  auto client =
      epoll_stream_conn_ptr::connect(loop.loop()->self(), server_ep, {});
  REQUIRE(client);
  REQUIRE(client->send(std::string{"ping"}));

  // Wait until the server has received data and called `conn.close()`.
  REQUIRE(close_initiated.wait_for_value(std::chrono::seconds{5}, true));

  // The accepted connection must have inherited the shutdown policy from the
  // listener.
  CHECK(accepted_policy.get() == coordination_policy::bilateral);

  // The server shut down its write side but is waiting for the client to
  // close. The client has not closed yet, so `on_close` cannot have fired.
  CHECK_FALSE(server_closed.get());

  // Client closes, unblocking the bilateral close. Server's `on_close` fires.
  REQUIRE(client->close());
  REQUIRE(server_closed.wait_for_value(std::chrono::seconds{5}, true));
}

#pragma endregion
#pragma region DestructorHangsUp

TEST_CASE("DestructorHangsUp", "[StreamConn]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  constexpr int small_buf = 4096;
  CHECK(a.set_send_buffer_size(small_buf));

  const std::string payload(256ULL * 1024ULL, 'q');

  bool closed = false;
  {
    auto conn = epoll_stream_conn_ptr::adopt(loop, std::move(a), {},
        {.on_close = [&](epoll_stream_conn&) {
          closed = true;
          return true;
        }});
    CHECK(loop->run_once(0) >= 0); // process posted register_with_loop
    CHECK(conn->send(std::string{payload}));
  }

  // drain posted enqueue_send() then posted hangup()
  CHECK(loop->run_once(0) >= 0);

  std::string received;
  std::string tmp;
  no_zero{tmp}.enlarge_to(4096);
  while (b.read(tmp) && !tmp.empty()) {
    received.append(tmp);
    no_zero{tmp}.enlarge_to(4096);
  }

  CHECK(closed);
  CHECK((received.size()) < (payload.size()));
}

#pragma endregion
#pragma region EchoServer

TEST_CASE("EchoServer", "[StreamConn]") {
  epoll_loop_runner loop;

  // Bind a non-blocking listener to an OS-assigned loopback port.
  // Each accepted connection is self-owning and gets a copy of the listener's
  // handlers, so no external handle is needed.
  auto listener = epoll_stream_conn_ptr::listen(loop.loop()->self(),
      net_endpoint{ipv4_addr::loopback, 0},
      {.on_data = [](epoll_stream_conn& conn, epoll_recv_buffer_view v) {
        std::string_view av = v;
        bool ok = conn.send(std::string{av});
        v.consume(av.size());
        return ok;
      }});
  REQUIRE(listener);

  // Sniff out the port from the listener socket so we can connect to it.
  const net_endpoint server_ep = listener->local_endpoint();
  REQUIRE(server_ep);

  // Connect to the server, send a message once the connection is established,
  // and accumulate the echo in `received`.
  constexpr std::string_view msg{"hello echo"};
  std::string received;
  notifiable<bool> done{false};
  epoll_stream_conn_ptr client_conn;

  client_conn = epoll_stream_conn_ptr::connect(loop.loop()->self(), server_ep,
      {.on_data =
              [&](epoll_stream_conn&, epoll_recv_buffer_view v) {
                std::string_view av = v;
                received.append(av);
                v.consume(av.size());
                if (received.size() >= msg.size()) done.notify_one(true);
                return true;
              },
          .on_drain =
              [&, sent = false](epoll_stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return false;
                return conn.send(std::string{msg});
              }});
  REQUIRE(client_conn);

  REQUIRE(done.wait_for_value(std::chrono::seconds{5}, true));
  CHECK(received == std::string{msg});
}

#pragma endregion

// `epoll_stream_conn_with_state` tests.

// Verify that `adopt` creates a typed handle, that state is zero-initialized,
// and that it is mutable via the typed pointer.
#pragma region Adopt

TEST_CASE("Adopt", "[StreamConnWithState]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  using conn_t = epoll_stream_conn_with_state<int>;
  auto conn =
      epoll_stream_conn_ptr_with<conn_t>::adopt(loop, std::move(a), {});
  REQUIRE(conn);
  CHECK(conn->state() == 0);
  conn->state() = 42;
  CHECK(conn->state() == 42);
}

#pragma endregion

// Verify that `from` correctly downcasts an `epoll_stream_conn&` inside a
// callback.
#pragma region From

TEST_CASE("From", "[StreamConnWithState]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  using conn_t = epoll_stream_conn_with_state<int>;
  int seen = -1;
  auto conn = epoll_stream_conn_ptr_with<conn_t>::adopt(loop, std::move(a), {},
      {.on_data = [&](epoll_stream_conn& c, epoll_recv_buffer_view v) {
        auto& typed = conn_t::from(c);
        typed.state() += 1;
        seen = typed.state();
        std::string_view av = v;
        v.consume(av.size());
        return true;
      }});
  CHECK(loop->run_once(0) >= 0); // register_with_loop

  conn->state() = 9;

  const std::string msg{"hi"};
  auto sv = std::string_view{msg};
  REQUIRE((b.send(sv) && sv.empty()));
  CHECK(loop->run_once(0) >= 0); // EPOLLIN -> on_data

  CHECK(seen == 10); // state was 9, incremented to 10 inside the callback
}

#pragma endregion

// Verify that connections accepted via listen() have the right concrete type
// and that each has an independent `TState`.
#pragma region Listen

TEST_CASE("Listen", "[StreamConnWithState]") {
  epoll_loop_runner loop;

  using conn_t = epoll_stream_conn_with_state<int>;
  notifiable<int> received_state{-1};

  auto listener = epoll_stream_conn_ptr_with<conn_t>::listen(
      loop.loop()->self(), net_endpoint{ipv4_addr::loopback, 0},
      {.on_data = [&](epoll_stream_conn& c, epoll_recv_buffer_view v) {
        auto& typed = conn_t::from(c);
        typed.state() += 1;
        std::string_view av = v;
        v.consume(av.size());
        received_state.notify_one(typed.state());
        return true;
      }});
  REQUIRE(listener);

  const net_endpoint server_ep = listener->local_endpoint();
  REQUIRE(server_ep);

  auto client =
      epoll_stream_conn_ptr::connect(loop.loop()->self(), server_ep, {});
  REQUIRE(client);

  const std::string msg{"ping"};
  REQUIRE(client->send(std::string{msg}));

  // State starts at 0 and is incremented to 1 in the first on_data call.
  REQUIRE(received_state.wait_for_value(std::chrono::seconds{5}, 1));
}

#pragma endregion

// Verify that an `epoll_stream_conn_ptr_with<Derived>` is implicitly
// convertible to the untyped `epoll_stream_conn_ptr` and the resulting handle
// is usable.
#pragma region Covariance

TEST_CASE("Covariance", "[StreamConnPtr]") {
  auto loop = epoll_loop::make();
  auto [a, b] = net_socket::create_pair();

  using conn_t = epoll_stream_conn_with_state<int>;
  bool closed = false;
  epoll_stream_conn_ptr_with<conn_t> typed =
      epoll_stream_conn_ptr_with<conn_t>::adopt(loop, std::move(a), {},
          {.on_close = [&](epoll_stream_conn&) {
            closed = true;
            return true;
          }});
  REQUIRE(typed);

  // Implicit upcast.
  epoll_stream_conn_ptr base = std::move(typed);
  REQUIRE(base);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK_FALSE(typed); // ownership transferred

  CHECK(loop->run_once(0) >= 0); // register_with_loop
  CHECK(base->close());
  (void)b.close();
  CHECK(loop->run_once(0) >= 0);
  CHECK(closed);
}

#pragma endregion

// Verify that overriding accept_clone to return nullptr causes handle_listen
// to silently skip the connection (no on_data fired, drain loop continues).
#pragma region AcceptClone_Nullptr

TEST_CASE("AcceptClone_Nullptr", "[StreamConnWithState]") {
  struct rejecting_conn: epoll_stream_conn_with_state<int> {
    using epoll_stream_conn_with_state<int>::epoll_stream_conn_with_state;

  protected:
    [[nodiscard]] std::shared_ptr<epoll_stream_conn> accept_clone(net_socket&&,
        const net_endpoint&, epoll_stream_conn_handlers) const override {
      return nullptr;
    }
  };

  epoll_loop_runner loop;

  int data_calls = 0;
  auto listener = epoll_stream_conn_ptr_with<rejecting_conn>::listen(
      loop.loop()->self(), net_endpoint{ipv4_addr::loopback, 0},
      {.on_data = [&](epoll_stream_conn&, epoll_recv_buffer_view v) {
        ++data_calls;
        std::string_view av = v;
        v.consume(av.size());
        return true;
      }});
  REQUIRE(listener);

  const net_endpoint server_ep = listener->local_endpoint();
  REQUIRE(server_ep);

  // Connect; the server drops the accepted socket immediately. Blocking on
  // `recv` provides reliable synchronization: once it returns empty (EOF),
  // the server has already processed and discarded the connection.
  auto client = net_socket::create_for(server_ep, execution::blocking);
  REQUIRE(client.is_open());
  REQUIRE(client.connect(server_ep).value_or(false));
  std::string buf;
  no_zero{buf}.enlarge_to(64);
  // recv returns false on EOF; buf is left unchanged (non-empty after the
  // pre-grow above).
  CHECK_FALSE(client.recv(buf));

  CHECK(data_calls == 0);
}

#pragma endregion

// Single complete frame delivered in one call.
#pragma region CompleteLine

TEST_CASE("CompleteLine", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv{"text\r\n"};
  std::string_view text;
  CHECK(p.parse(sv, text) == true);
  CHECK(text == "text");
  CHECK(sv.empty()); // `sv` advanced past the frame
}

#pragma endregion

// Empty view: incomplete with zero bytes scanned.
#pragma region IncompleteEmpty

TEST_CASE("IncompleteEmpty", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv;
  std::string_view text;
  CHECK(p.parse(sv, text) == std::nullopt);
  CHECK(p.bytes_scanned() == 0U);
}

#pragma endregion

// Partial frame with no sentinel: incomplete, bytes_scanned updated.
#pragma region IncompletePartial

TEST_CASE("IncompletePartial", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv{"text"};
  std::string_view text;
  CHECK(p.parse(sv, text) == std::nullopt);
  CHECK(p.bytes_scanned() == 4U);
}

#pragma endregion

// Sentinel split across two calls: "\r" arrives first, "\n" in the next view.
#pragma region SplitSentinel

TEST_CASE("SplitSentinel", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view text;

  // First call: only "\r" present -- not a complete sentinel.
  std::string_view sv1{"text\r"};
  CHECK(p.parse(sv1, text) == std::nullopt);
  CHECK(p.bytes_scanned() == 5U);

  // Second call: the same bytes now extended with "\n".
  std::string_view sv2{"text\r\n"};
  CHECK(p.parse(sv2, text) == true);
  CHECK(text == "text");
  CHECK(sv2.empty());
}

#pragma endregion

// Two frames in the same view: parse twice with reset() between.
#pragma region MultipleFrames

TEST_CASE("MultipleFrames", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv{"line1\r\nline2\r\n"};
  std::string_view text;

  CHECK(p.parse(sv, text) == true);
  CHECK(text == "line1");
  p.reset();

  CHECK(p.parse(sv, text) == true);
  CHECK(text == "line2");
  CHECK(sv.empty());
}

#pragma endregion

// Bare sentinel with no preceding text: complete with empty text.
#pragma region EmptyLine

TEST_CASE("EmptyLine", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv{"\r\n"};
  std::string_view text;
  CHECK(p.parse(sv, text) == true);
  CHECK(text.empty());
  CHECK(sv.empty());
}

#pragma endregion

// Exceeding max_length with no sentinel present returns false. The last
// sentinel-size - 1 bytes could still be the start of a split sentinel, so
// only the bytes before them count as frame content.
#pragma region TooLong

TEST_CASE("TooLong", "[TerminatedTextParser]") {
  // 9 bytes could still be 8 content bytes plus '\r': incomplete, not too
  // long.
  {
    terminated_text_parser::state s{"\r\n", 8};
    terminated_text_parser p{s};
    std::string_view sv{"123456789"};
    std::string_view text;
    CHECK(p.parse(sv, text) == std::nullopt);
  }
  // 10 bytes put at least 9 content bytes before any possible sentinel.
  {
    terminated_text_parser::state s{"\r\n", 8};
    terminated_text_parser p{s};
    std::string_view sv{"1234567890"};
    std::string_view text;
    CHECK(p.parse(sv, text) == false);
  }
}

#pragma endregion

// A frame of exactly max_length content whose stream pauses mid-sentinel is
// not rejected prematurely; acceptance does not depend on how the input was
// chunked across reads.
#pragma region TooLong_SplitSentinel

TEST_CASE("TooLong_SplitSentinel", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n", 8};
  terminated_text_parser p{s};
  std::string_view text;

  std::string_view sv{"12345678\r"};
  CHECK(p.parse(sv, text) == std::nullopt);

  // The rest of the sentinel arrives; the frame completes at the limit.
  std::string_view sv2{"12345678\r\n"};
  CHECK(p.parse(sv2, text) == true);
  CHECK(text == "12345678");
}

#pragma endregion

// Sentinel found after max_length bytes returns false; input is not modified.
// A frame of exactly max_length bytes succeeds.
#pragma region TooLong_WithSentinel

TEST_CASE("TooLong_WithSentinel", "[TerminatedTextParser]") {
  // 9 bytes before "\r\n": over the limit of 8.
  {
    terminated_text_parser::state s{"\r\n", 8};
    terminated_text_parser p{s};
    std::string_view sv{"123456789\r\n"};
    std::string_view text;
    CHECK(p.parse(sv, text) == false);
    CHECK(sv == "123456789\r\n"); // input not modified
  }
  // Exactly 8 bytes before "\r\n": at the limit, succeeds.
  {
    terminated_text_parser::state s{"\r\n", 8};
    terminated_text_parser p{s};
    std::string_view sv{"12345678\r\n"};
    std::string_view text;
    CHECK(p.parse(sv, text) == true);
    CHECK(text == "12345678");
  }
}

#pragma endregion

// With max_length == 0, the same input returns incomplete (no limit enforced).
#pragma region NoLimit

TEST_CASE("NoLimit", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n", 0};
  terminated_text_parser p{s};
  std::string_view sv{"123456789"};
  std::string_view text;
  CHECK(p.parse(sv, text) == std::nullopt);
}

#pragma endregion

// Custom sentinel ":" extracts the text up to the first colon.
#pragma region CustomSentinel

TEST_CASE("CustomSentinel", "[TerminatedTextParser]") {
  terminated_text_parser::state s{":"};
  terminated_text_parser p{s};
  std::string_view sv{"Content-Type: text/html"};
  std::string_view text;
  CHECK(p.parse(sv, text) == true);
  CHECK(text == "Content-Type");
  CHECK(sv == " text/html");
}

#pragma endregion

// After complete + reset(), the parser correctly handles a second frame.
#pragma region Reset

TEST_CASE("Reset", "[TerminatedTextParser]") {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view text;

  std::string_view sv1{"first\r\n"};
  CHECK(p.parse(sv1, text) == true);
  CHECK(text == "first");
  p.reset();
  CHECK(p.bytes_scanned() == 0U);

  // Confirm state is clean: a fresh incomplete call should update
  // bytes_scanned.
  std::string_view sv2{"second"};
  CHECK(p.parse(sv2, text) == std::nullopt);
  CHECK(p.bytes_scanned() == 6U);
}

#pragma endregion

// ---------------------------------------------------------------------------
// base_64 tests
// ---------------------------------------------------------------------------

// RFC 4648 test vectors: encode produces the canonical Base64 output.
#pragma region Encode_KnownVectors

TEST_CASE("Encode_KnownVectors", "[Base64]") {
  CHECK(base_64::encode("") == "");
  CHECK(base_64::encode("f") == "Zg==");
  CHECK(base_64::encode("fo") == "Zm8=");
  CHECK(base_64::encode("foo") == "Zm9v");
  CHECK(base_64::encode("foob") == "Zm9vYg==");
  CHECK(base_64::encode("fooba") == "Zm9vYmE=");
  CHECK(base_64::encode("foobar") == "Zm9vYmFy");
}

#pragma endregion

// decode returns empty vector for empty input.
#pragma region Decode_Empty

TEST_CASE("Decode_Empty", "[Base64]") {
  auto result = base_64::decode("");
  CHECK(result.empty());
}

#pragma endregion

// RFC 4648 test vectors: decode recovers the original bytes.
#pragma region Decode_KnownVectors

TEST_CASE("Decode_KnownVectors", "[Base64]") {
  auto check = [](std::string_view encoded, std::string_view expected) {
    auto result = base_64::decode(encoded);
    CHECK((std::string(result.begin(), result.end())) ==
          (std::string(expected)));
  };
  check("Zg==", "f");
  check("Zm8=", "fo");
  check("Zm9v", "foo");
  check("Zm9vYg==", "foob");
  check("Zm9vYmE=", "fooba");
  check("Zm9vYmFy", "foobar");
}

#pragma endregion

// decode rejects input whose length is not a multiple of 4.
#pragma region Decode_InvalidLength

TEST_CASE("Decode_InvalidLength", "[Base64]") {
  CHECK(base_64::decode("Zg").empty());
  CHECK(base_64::decode("Zm8").empty());
  CHECK(base_64::decode("Zm9vY").empty());
}

#pragma endregion

// decode rejects input containing characters outside the Base64 alphabet.
#pragma region Decode_InvalidChar

TEST_CASE("Decode_InvalidChar", "[Base64]") {
  CHECK(base_64::decode("Zg=!").empty());
  CHECK(base_64::decode("Z!==").empty());
  CHECK(base_64::decode("!g==").empty());
}

#pragma endregion

// encode then decode returns the original bytes (round-trip), exercising all
// three remainder cases (0, 1, and 2 leftover bytes before padding).
#pragma region RoundTrip_Short

TEST_CASE("RoundTrip_Short", "[Base64]") {
  for (const std::string_view sv : {"", "A", "AB", "ABC", "ABCD"}) {
    const std::string encoded = base_64::encode(sv);
    const auto decoded = base_64::decode(encoded);
    CHECK(std::string(decoded.begin(), decoded.end()) == std::string(sv));
  }
}

#pragma endregion

// Round-trip across all 256 byte values to confirm the decode table is
// the exact inverse of the encode alphabet.
#pragma region RoundTrip_AllBytes

TEST_CASE("RoundTrip_AllBytes", "[Base64]") {
  std::vector<uint8_t> all_bytes(256);
  for (auto ndx = 0UZ; ndx < 256; ++ndx) all_bytes[ndx] = uint8_t(ndx);

  const std::string encoded = base_64::encode(
      std::span<const uint8_t>{all_bytes.data(), all_bytes.size()});
  const auto decoded = base_64::decode(encoded);

  CHECK(decoded == all_bytes);
}

#pragma endregion

// ---------------------------------------------------------------------------
// sha_1 tests
// ---------------------------------------------------------------------------

// RFC 3174 (and FIPS 180) test vectors, covering the empty message, a
// single-block message, a message whose padding forces a second block, and a
// million-byte multi-block message.
#pragma region Sha1_KnownVectors

TEST_CASE("Sha1_KnownVectors", "[Sha1]") {
  CHECK(sha_1::digest("") ==
        sha_1::digest_t{0xDA39A3EEU, 0x5E6B4B0DU, 0x3255BFEFU, 0x95601890U,
            0xAFD80709U});
  CHECK(sha_1::digest("abc") ==
        sha_1::digest_t{0xA9993E36U, 0x4706816AU, 0xBA3E2571U, 0x7850C26CU,
            0x9CD0D89DU});
  CHECK(sha_1::digest(
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        sha_1::digest_t{0x84983E44U, 0x1C3BD26EU, 0xBAAE4AA1U, 0xF95129E5U,
            0xE54670F1U});
  CHECK(sha_1::digest("The quick brown fox jumps over the lazy dog") ==
        sha_1::digest_t{0x2FD4E1C6U, 0x7A2D28FCU, 0xED849EE1U, 0xBB76E739U,
            0x1B93EB12U});
  CHECK(sha_1::digest(std::string(1'000'000, 'a')) ==
        sha_1::digest_t{0x34AA973CU, 0xD4C4DAA4U, 0xF61EEB2BU, 0xDBAD2731U,
            0x6534016FU});
}

#pragma endregion

// bytes() serializes the digest words big-endian, most significant byte
// first.
#pragma region Sha1_Bytes

TEST_CASE("Sha1_Bytes", "[Sha1]") {
  const auto raw = sha_1::bytes(sha_1::digest("abc"));
  const sha_1::bytes_t expected{0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A,
      0xBA, 0x3E, 0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D};
  CHECK(raw == expected);
}

#pragma endregion
#pragma region SegmentBookkeeping

TEST_CASE("SegmentBookkeeping", "[IovMsghdr]") {
  iov_msghdr_sender sender;
  CHECK(sender.empty());
  CHECK(sender.segments().empty());

  // Appending tracks the total byte count across segments.
  const std::string_view hello = "hello";
  const std::string_view world = "world!";
  CHECK(sender.append(hello));
  CHECK(sender.append(world));
  CHECK_FALSE(sender.empty());
  CHECK(sender.size() == 11U);
  CHECK(sender.segments().size() == 2U);

  // Empty segments are rejected, not stored.
  CHECK_FALSE(sender.append(std::string_view{}));
  CHECK(sender.segments().size() == 2U);

  // Clearing empties everything.
  sender.clear();
  CHECK(sender.empty());
  CHECK(sender.segments().empty());
  CHECK(sender.size() == 0U);
}

#pragma endregion
#pragma region GatherScatterRoundTrip

TEST_CASE("GatherScatterRoundTrip", "[IovMsghdr]") {
  auto [writer, reader] = net_socket::create_pair(address_family::unix,
      socket_type::stream, execution::blocking);
  REQUIRE(writer.is_open());
  REQUIRE(reader.is_open());

  // Gather two segments into one send.
  const std::string_view hello = "hello";
  const std::string_view world = "world!";
  iov_msghdr_sender sender;
  REQUIRE(sender.append(hello));
  REQUIRE(sender.append(world));
  CHECK(sender.send(writer));

  // The transfer ended exactly on the last segment boundary, so the index
  // points one past it.
  CHECK(sender.last_op().transferred == 11U);
  CHECK(sender.last_op().index == 2U);
  CHECK(sender.last_op().offset == 0U);

  // A follow-up send first consumes everything, then trivially succeeds with
  // nothing left to write.
  CHECK(sender.send(writer));
  CHECK(sender.last_op().transferred == 0U);
  CHECK(sender.empty());

  // Scatter the bytes across two receive buffers, with room to spare in the
  // second, so the index points within it.
  std::string first(4, '\0');
  std::string second(8, '\0');
  iov_msghdr_receiver receiver;
  REQUIRE(receiver.append(first.data(), first.size()));
  REQUIRE(receiver.append(second.data(), second.size()));
  CHECK(receiver.recv(reader));
  CHECK(receiver.last_op().transferred == 11U);
  CHECK(receiver.last_op().index == 1U);
  CHECK(receiver.last_op().offset == 7U);
  CHECK(first == "hell");
  CHECK(second.substr(0, 7) == "oworld!");
}

#pragma endregion
#pragma region PartialConsumeAdvances

TEST_CASE("PartialConsumeAdvances", "[IovMsghdr]") {
  auto [writer, reader] = net_socket::create_pair(address_family::unix,
      socket_type::stream, execution::blocking);
  REQUIRE(writer.is_open());
  REQUIRE(reader.is_open());

  // Two receive buffers, filled partway into the second.
  std::string first(4, '\0');
  std::string second(6, '\0');
  iov_msghdr_receiver receiver;
  REQUIRE(receiver.append(first.data(), first.size()));
  REQUIRE(receiver.append(second.data(), second.size()));

  REQUIRE(writer.send_sync_all("freedom"));
  CHECK(receiver.recv(reader));
  CHECK(receiver.last_op().transferred == 7U);
  CHECK(receiver.last_op().index == 1U);
  CHECK(receiver.last_op().offset == 3U);
  CHECK(first == "free");
  CHECK(second.substr(0, 3) == "dom");

  // The next receive first consumes past those 7 bytes, leaving the tail of
  // the second buffer as the only active segment.
  REQUIRE(writer.send_sync_all("end"));
  CHECK(receiver.recv(reader));
  CHECK(receiver.segments().size() == 1U);
  CHECK(receiver.size() == 3U);
  CHECK(receiver.last_op().transferred == 3U);
  CHECK(receiver.last_op().index == 1U);
  CHECK(receiver.last_op().offset == 0U);
  CHECK(second == "domend");
}

#pragma endregion
#pragma region CompactRetiresSlack

TEST_CASE("CompactRetiresSlack", "[IovMsghdr]") {
  auto [writer, reader] = net_socket::create_pair(address_family::unix,
      socket_type::stream, execution::blocking);
  REQUIRE(writer.is_open());
  REQUIRE(reader.is_open());

  // Build one-byte receive segments over a 20-char buffer, then fill the
  // first 17 exactly.
  std::string buf(20, '\0');
  iov_msghdr_receiver receiver;
  for (auto ndx = 0UZ; ndx < buf.size(); ++ndx)
    REQUIRE(receiver.append(buf.data() + ndx, 1));
  REQUIRE(writer.send_sync_all("seventeen-letters"));
  CHECK(receiver.recv(reader));
  CHECK(receiver.last_op().transferred == 17U);

  // With more than `initial_segments` of leading slack, compact retires the
  // consumed segments.
  CHECK(receiver.compact());
  CHECK(receiver.segments().size() == 3U);
  CHECK(receiver.size() == 3U);

  // With no slack left, compact declines, which is also success.
  CHECK(receiver.compact());
  CHECK(receiver.segments().size() == 3U);

  CHECK(buf.substr(0, 17) == "seventeen-letters");
}

#pragma endregion
#pragma region OversizeTransferIsHardFailure

TEST_CASE("OversizeTransferIsHardFailure", "[IovMsghdr]") {
  if (true) {
    const auto [ok, op] =
        corvid::proto::iov_msghdr_test::oversize_update<true>();
    CHECK_FALSE(ok);
    CHECK(op.transferred == proto::iov_msghdr_sender::npos);
    CHECK(op.index == proto::iov_msghdr_sender::npos);
    CHECK(op.offset == proto::iov_msghdr_sender::npos);
  }

  if (true) {
    const auto [ok, op] =
        corvid::proto::iov_msghdr_test::oversize_update<false>();
    CHECK_FALSE(ok);
    CHECK(op.transferred == proto::iov_msghdr_receiver::npos);
    CHECK(op.index == proto::iov_msghdr_receiver::npos);
    CHECK(op.offset == proto::iov_msghdr_receiver::npos);
  }
}

#pragma endregion

// ---------------------------------------------------------------------------
// epoll_http_server tests
// ---------------------------------------------------------------------------

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
