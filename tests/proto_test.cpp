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
#include "../corvid/proto.h"

#include <type_traits>
#include <atomic>
#include <chrono>
#include <thread>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

void Ipv4Addr_Construction() {
  // Default construction yields the "any" address, which is also empty.
  if (true) {
    ipv4_addr a;
    EXPECT_TRUE(a.is_any());
    EXPECT_TRUE(a.empty());
    EXPECT_FALSE(bool(a));
    EXPECT_EQ(a.to_uint32(), 0U);
  }

  // Named constant: any.
  if (true) {
    auto a = ipv4_addr::any;
    EXPECT_TRUE(a.is_any());
    EXPECT_EQ(a.to_uint32(), 0U);
  }

  // Named constant: loopback.
  if (true) {
    auto a = ipv4_addr::loopback;
    EXPECT_TRUE(a.is_loopback());
    EXPECT_EQ(a.to_uint32(), 0x7f000001U);
  }

  // Named constant: broadcast.
  if (true) {
    auto a = ipv4_addr::broadcast;
    EXPECT_TRUE(a.is_broadcast());
    EXPECT_EQ(a.to_uint32(), 0xffffffffU);
  }

  // Construct from four octets.
  if (true) {
    ipv4_addr a{192, 168, 1, 1};
    EXPECT_EQ(a.to_uint32(), 0xc0a80101U);
    auto o = a.octets();
    EXPECT_EQ(o[0], 192U);
    EXPECT_EQ(o[1], 168U);
    EXPECT_EQ(o[2], 1U);
    EXPECT_EQ(o[3], 1U);
  }

  // Construct from host-byte-order uint32_t.
  if (true) {
    ipv4_addr a{uint32_t{0x01020304U}};
    auto o = a.octets();
    EXPECT_EQ(o[0], 1U);
    EXPECT_EQ(o[1], 2U);
    EXPECT_EQ(o[2], 3U);
    EXPECT_EQ(o[3], 4U);
  }

  // Non-zero address is not empty; operator bool() reflects this.
  if (true) {
    ipv4_addr a{1, 2, 3, 4};
    EXPECT_FALSE(a.empty());
    EXPECT_TRUE(bool(a));
  }
}

void Ipv4Addr_Parse() {
  // Valid addresses.
  if (true) {
    auto a = ipv4_addr::parse("0.0.0.0");
    EXPECT_TRUE(a.has_value());
    EXPECT_TRUE(a->is_any());

    auto b = ipv4_addr::parse("127.0.0.1");
    EXPECT_TRUE(b.has_value());
    EXPECT_TRUE(b->is_loopback());

    auto c = ipv4_addr::parse("255.255.255.255");
    EXPECT_TRUE(c.has_value());
    EXPECT_TRUE(c->is_broadcast());

    auto d = ipv4_addr::parse("192.168.1.100");
    EXPECT_TRUE(d.has_value());
    EXPECT_EQ(d->to_uint32(), 0xc0a80164U);

    // Single-digit octets.
    auto e = ipv4_addr::parse("1.2.3.4");
    EXPECT_TRUE(e.has_value());
    EXPECT_EQ(e->to_uint32(), 0x01020304U);
  }

  // Invalid: too few octets.
  if (true) {
    EXPECT_FALSE(ipv4_addr::parse("192.168.1").has_value());
    EXPECT_FALSE(ipv4_addr::parse("192.168").has_value());
    EXPECT_FALSE(ipv4_addr::parse("192").has_value());
    EXPECT_FALSE(ipv4_addr::parse("").has_value());
  }

  // Invalid: too many octets.
  if (true) { EXPECT_FALSE(ipv4_addr::parse("1.2.3.4.5").has_value()); }

  // Invalid: octet out of range.
  if (true) {
    EXPECT_FALSE(ipv4_addr::parse("256.0.0.1").has_value());
    EXPECT_FALSE(ipv4_addr::parse("1.2.3.999").has_value());
  }

  // Invalid: leading zeros.
  if (true) {
    EXPECT_FALSE(ipv4_addr::parse("01.2.3.4").has_value());
    EXPECT_FALSE(ipv4_addr::parse("1.02.3.4").has_value());
  }

  // Invalid: non-numeric characters.
  if (true) {
    EXPECT_FALSE(ipv4_addr::parse("a.b.c.d").has_value());
    EXPECT_FALSE(ipv4_addr::parse("192.168.1.x").has_value());
    EXPECT_FALSE(ipv4_addr::parse("192.168.1.1 ").has_value());
    EXPECT_FALSE(ipv4_addr::parse(" 192.168.1.1").has_value());
  }

  // A sole zero octet is valid (not a leading zero).
  if (true) {
    auto a = ipv4_addr::parse("0.0.0.0");
    EXPECT_TRUE(a.has_value());
  }
}

void Ipv4Addr_Classification() {
  // is_loopback(): entire 127.0.0.0/8 range.
  if (true) {
    EXPECT_TRUE(ipv4_addr(127, 0, 0, 1).is_loopback());
    EXPECT_TRUE(ipv4_addr(127, 255, 255, 255).is_loopback());
    EXPECT_FALSE(ipv4_addr(128, 0, 0, 1).is_loopback());
  }

  // is_multicast(): 224.0.0.0/4.
  if (true) {
    EXPECT_TRUE(ipv4_addr(224, 0, 0, 1).is_multicast());
    EXPECT_TRUE(ipv4_addr(239, 255, 255, 255).is_multicast());
    EXPECT_FALSE(ipv4_addr(223, 0, 0, 1).is_multicast());
    EXPECT_FALSE(ipv4_addr(240, 0, 0, 1).is_multicast());
  }

  // is_private(): RFC 1918 ranges.
  if (true) {
    // 10.0.0.0/8
    EXPECT_TRUE(ipv4_addr(10, 0, 0, 1).is_private());
    EXPECT_TRUE(ipv4_addr(10, 255, 255, 255).is_private());
    EXPECT_FALSE(ipv4_addr(11, 0, 0, 1).is_private());

    // 172.16.0.0/12
    EXPECT_TRUE(ipv4_addr(172, 16, 0, 1).is_private());
    EXPECT_TRUE(ipv4_addr(172, 31, 255, 255).is_private());
    EXPECT_FALSE(ipv4_addr(172, 15, 0, 1).is_private());
    EXPECT_FALSE(ipv4_addr(172, 32, 0, 1).is_private());

    // 192.168.0.0/16
    EXPECT_TRUE(ipv4_addr(192, 168, 0, 1).is_private());
    EXPECT_TRUE(ipv4_addr(192, 168, 255, 255).is_private());
    EXPECT_FALSE(ipv4_addr(192, 169, 0, 1).is_private());

    // Public addresses are not private.
    EXPECT_FALSE(ipv4_addr(8, 8, 8, 8).is_private());
  }

  // is_broadcast().
  if (true) {
    EXPECT_TRUE(ipv4_addr::broadcast.is_broadcast());
    EXPECT_FALSE(ipv4_addr(255, 255, 255, 254).is_broadcast());
  }

  // is_any().
  if (true) {
    EXPECT_TRUE(ipv4_addr::any.is_any());
    EXPECT_FALSE(ipv4_addr(0, 0, 0, 1).is_any());
  }
}

void Ipv4Addr_Comparison() {
  ipv4_addr a{10, 0, 0, 1};
  ipv4_addr b{10, 0, 0, 2};
  ipv4_addr c{10, 0, 0, 1};

  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b > a);
  EXPECT_TRUE(a <= c);
  EXPECT_TRUE(a >= c);
}

void Ipv4Addr_Formatting() {
  EXPECT_EQ(ipv4_addr::any.to_string(), "0.0.0.0");
  EXPECT_EQ(ipv4_addr::loopback.to_string(), "127.0.0.1");
  EXPECT_EQ(ipv4_addr::broadcast.to_string(), "255.255.255.255");
  EXPECT_EQ(ipv4_addr(192, 168, 1, 100).to_string(), "192.168.1.100");

  // Round-trip: parse then format.
  auto addr = ipv4_addr::parse("10.20.30.40");
  EXPECT_TRUE(addr.has_value());
  EXPECT_EQ(addr->to_string(), "10.20.30.40");
}

void Ipv4Addr_PosixInterop() {
  ipv4_addr a{192, 168, 1, 1};

  // Convert to `in_addr` and back.
  in_addr raw = a.to_in_addr();
  ipv4_addr b{raw};
  EXPECT_EQ(a, b);

  // Verify network byte order: 192.168.1.1 = 0xc0a80101 in host order,
  // so `s_addr` should be 0x0101a8c0 on a little-endian host.
  EXPECT_EQ(ntohl(raw.s_addr), a.to_uint32());
}

void Ipv6Addr_Construction() {
  if (true) {
    ipv6_addr a;
    EXPECT_TRUE(a.is_any());
    EXPECT_EQ(a.to_string(), "::");
  }

  if (true) {
    auto a = ipv6_addr::any;
    EXPECT_TRUE(a.is_any());
  }

  if (true) {
    auto a = ipv6_addr::loopback;
    EXPECT_TRUE(a.is_loopback());
    EXPECT_EQ(a.to_string(), "::1");
  }

  if (true) {
    ipv6_addr a{0x2001, 0x0db8, 0, 0, 0, 0, 0, 1};
    auto words = a.words();
    EXPECT_EQ(words[0], 0x2001U);
    EXPECT_EQ(words[1], 0x0db8U);
    EXPECT_EQ(words[7], 1U);
  }

  if (true) {
    ipv6_addr a{std::array<uint8_t, 16>{0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 1}};
    EXPECT_EQ(a.to_string(), "2001:db8::1");
  }
}

void Ipv6Addr_Parse() {
  if (true) {
    auto a = ipv6_addr::parse("::");
    EXPECT_TRUE(a.has_value());
    EXPECT_TRUE(a->is_any());

    auto b = ipv6_addr::parse("::1");
    EXPECT_TRUE(b.has_value());
    EXPECT_TRUE(b->is_loopback());

    auto c = ipv6_addr::parse("2001:db8::1");
    EXPECT_TRUE(c.has_value());
    EXPECT_EQ(c->to_string(), "2001:db8::1");

    auto d = ipv6_addr::parse("2001:0DB8:0000:0000:0000:0000:0000:0001");
    EXPECT_TRUE(d.has_value());
    EXPECT_EQ(d->to_string(), "2001:db8::1");

    auto e = ipv6_addr::parse("fe80::1234:5678");
    EXPECT_TRUE(e.has_value());
    EXPECT_TRUE(e->is_link_local());

    auto f = ipv6_addr::parse("::ffff:192.168.1.1");
    EXPECT_TRUE(f.has_value());
    auto fw = f->words();
    EXPECT_EQ(fw[4], 0U);
    EXPECT_EQ(fw[5], 0xFFFFU);
    EXPECT_EQ(fw[6], 0xC0A8U);
    EXPECT_EQ(fw[7], 0x0101U);
    auto fb = f->bytes();
    EXPECT_EQ(fb[14], 1U);
    EXPECT_EQ(fb[15], 1U);

    auto g = ipv6_addr::parse("2001:db8::192.0.2.33");
    EXPECT_TRUE(g.has_value());
    auto gw = g->words();
    EXPECT_EQ(gw[6], 0xC000U);
    EXPECT_EQ(gw[7], 0x0221U);
  }

  if (true) {
    EXPECT_FALSE(ipv6_addr::parse("").has_value());
    EXPECT_FALSE(ipv6_addr::parse(":").has_value());
    EXPECT_FALSE(ipv6_addr::parse(":::").has_value());
    EXPECT_FALSE(ipv6_addr::parse("2001:::1").has_value());
    EXPECT_FALSE(ipv6_addr::parse("2001:db8::1::").has_value());
    EXPECT_FALSE(ipv6_addr::parse("1:2:3:4:5:6:7").has_value());
    EXPECT_FALSE(ipv6_addr::parse("1:2:3:4:5:6:7:8:9").has_value());
    EXPECT_FALSE(ipv6_addr::parse("12345::1").has_value());
    EXPECT_FALSE(ipv6_addr::parse("gggg::1").has_value());
    EXPECT_FALSE(ipv6_addr::parse("1:2:3:4:5:6:7:192.168.1.1").has_value());
    EXPECT_FALSE(ipv6_addr::parse("::ffff:999.168.1.1").has_value());
    EXPECT_FALSE(ipv6_addr::parse("::ffff:192.168.1").has_value());
  }
}

void Ipv6Addr_Classification() {
  if (true) {
    EXPECT_TRUE(ipv6_addr::loopback.is_loopback());
    EXPECT_FALSE(ipv6_addr::any.is_loopback());
  }

  if (true) {
    EXPECT_TRUE(ipv6_addr(0xff02, 0, 0, 0, 0, 0, 0, 1).is_multicast());
    EXPECT_FALSE(ipv6_addr(0xfe02, 0, 0, 0, 0, 0, 0, 1).is_multicast());
  }

  if (true) {
    EXPECT_TRUE(ipv6_addr(0xfe80, 0, 0, 0, 0, 0, 0, 1).is_link_local());
    EXPECT_TRUE(ipv6_addr(0xfebf, 0, 0, 0, 0, 0, 0, 1).is_link_local());
    EXPECT_FALSE(ipv6_addr(0xfec0, 0, 0, 0, 0, 0, 0, 1).is_link_local());
  }

  if (true) {
    EXPECT_TRUE(ipv6_addr(0xfc00, 0, 0, 0, 0, 0, 0, 1).is_unique_local());
    EXPECT_TRUE(ipv6_addr(0xfd12, 0, 0, 0, 0, 0, 0, 1).is_unique_local());
    EXPECT_FALSE(ipv6_addr(0xfe00, 0, 0, 0, 0, 0, 0, 1).is_unique_local());
  }
}

void Ipv6Addr_Comparison() {
  ipv6_addr a{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1};
  ipv6_addr b{0x2001, 0xdb8, 0, 0, 0, 0, 0, 2};
  ipv6_addr c{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1};

  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(a < b);
  EXPECT_TRUE(b > a);
}

void Ipv6Addr_Formatting() {
  EXPECT_EQ(ipv6_addr::any.to_string(), "::");
  EXPECT_EQ(ipv6_addr::loopback.to_string(), "::1");
  EXPECT_EQ(ipv6_addr(0x2001, 0xdb8, 0, 0, 1, 0, 0, 1).to_string(),
      "2001:db8::1:0:0:1");
  EXPECT_EQ(ipv6_addr(0x2001, 0xdb8, 0, 1, 0, 0, 0, 1).to_string(),
      "2001:db8:0:1::1");

  auto addr = ipv6_addr::parse("2001:db8::abcd");
  EXPECT_TRUE(addr.has_value());
  EXPECT_EQ(addr->to_string(), "2001:db8::abcd");
}

void Ipv6Addr_PosixInterop() {
  ipv6_addr a{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1};

  in6_addr raw = a.to_in6_addr();
  ipv6_addr b{raw};
  EXPECT_EQ(a, b);
  EXPECT_EQ(raw.s6_addr[0], 0x20U);
  EXPECT_EQ(raw.s6_addr[1], 0x01U);
  EXPECT_EQ(raw.s6_addr[15], 0x01U);
}

void NetEndpoint_Construction() {
  if (true) {
    net_endpoint ep;
    EXPECT_TRUE(ep.empty());
    EXPECT_FALSE(ep.is_v4());
    EXPECT_FALSE(ep.is_v6());
    EXPECT_FALSE(ep.is_uds());
    EXPECT_EQ(ep.to_string(), "(invalid)");
  }

  if (true) {
    net_endpoint ep{ipv4_addr(127, 0, 0, 1), 80};
    EXPECT_TRUE(ep.is_v4());
    EXPECT_EQ(ep.port(), 80U);
    EXPECT_EQ(ep.v4()->to_string(), "127.0.0.1");
  }

  if (true) {
    net_endpoint ep{ipv6_addr::loopback, 443};
    EXPECT_TRUE(ep.is_v6());
    EXPECT_EQ(ep.port(), 443U);
    EXPECT_EQ(ep.v6()->to_string(), "::1");
  }

  // UDS: construct from sockaddr_un directly.
  if (true) {
    sockaddr_un raw{};
    raw.sun_family = AF_UNIX;
    const std::string_view path = "/tmp/test.sock";
    path.copy(raw.sun_path, sizeof(raw.sun_path) - 1);

    net_endpoint ep{raw};
    EXPECT_FALSE(ep.empty());
    EXPECT_TRUE(ep.is_uds());
    EXPECT_FALSE(ep.is_v4());
    EXPECT_FALSE(ep.is_v6());
    EXPECT_EQ(ep.uds_path(), path);
  }

  // UDS: path longer than 107 chars is silently truncated.
  if (true) {
    const std::string long_path(200, 'x');
    net_endpoint ep{"/" + long_path};
    EXPECT_TRUE(!ep.empty());
    EXPECT_TRUE(ep.is_uds());
    EXPECT_FALSE(ep.is_ans());
    EXPECT_EQ(ep.uds_path().size(), 107U);
    EXPECT_EQ(ep.uds_path()[0], '/');
  }

  // ANS: construct from "@name" string.
  if (true) {
    net_endpoint ep{"@myservice"};
    EXPECT_TRUE(!ep.empty());
    EXPECT_TRUE(ep.is_uds());
    EXPECT_TRUE(ep.is_ans());
    EXPECT_FALSE(ep.is_v4());
    EXPECT_FALSE(ep.is_v6());
    // `uds_path()` skips the leading '\0' and returns the full 107-byte
    // buffer.
    EXPECT_EQ(ep.uds_path().size(), 107U);
    EXPECT_EQ(ep.uds_path().substr(0, 9), "myservice");
    EXPECT_EQ(ep.uds_path()[9], '\0'); // trailing bytes are zero-padding
  }

  // ANS: name longer than 107 chars is silently truncated.
  if (true) {
    const std::string long_name(200, 'y');
    net_endpoint ep{"@" + long_name};
    EXPECT_TRUE(!ep.empty());
    EXPECT_TRUE(ep.is_ans());
    EXPECT_EQ(ep.uds_path().size(), 107U);
    EXPECT_EQ(ep.uds_path()[0], 'y');
  }
}

void NetEndpoint_Parse() {
  if (true) {
    net_endpoint a{"192.168.1.10:8080"};
    EXPECT_TRUE(!a.empty());
    EXPECT_TRUE(a.is_v4());
    EXPECT_EQ(a.port(), 8080U);
    EXPECT_EQ(a.v4()->to_string(), "192.168.1.10");

    net_endpoint b{"[2001:db8::1]:443"};
    EXPECT_TRUE(!b.empty());
    EXPECT_TRUE(b.is_v6());
    EXPECT_EQ(b.port(), 443U);
    EXPECT_EQ(b.v6()->to_string(), "2001:db8::1");
  }

  if (true) {
    EXPECT_TRUE(net_endpoint{""}.empty());
    EXPECT_TRUE(net_endpoint{"127.0.0.1"}.empty());
    EXPECT_TRUE(net_endpoint{"127.0.0.1:"}.empty());
    EXPECT_TRUE(net_endpoint{"127.0.0.1:99999"}.empty());
    EXPECT_TRUE(net_endpoint{"2001:db8::1:443"}.empty());
    EXPECT_TRUE(net_endpoint{"[2001:db8::1]"}.empty());
    EXPECT_TRUE(net_endpoint{"[2001:db8::1]:"}.empty());
    EXPECT_TRUE(net_endpoint{"[2001:db8::1]:70000"}.empty());
  }

  // A leading `/` produces a UDS endpoint.
  if (true) {
    net_endpoint ep{"/run/app.sock"};
    EXPECT_TRUE(!ep.empty());
    EXPECT_TRUE(ep.is_uds());
    EXPECT_EQ(ep.uds_path(), "/run/app.sock");
  }

  // The `string_view` constructor also accepts UDS paths.
  if (true) {
    net_endpoint ep{std::string_view{"/var/run/foo.sock"}};
    EXPECT_TRUE(ep.is_uds());
    EXPECT_FALSE(ep.is_ans());
    EXPECT_EQ(ep.uds_path(), "/var/run/foo.sock");
  }

  // A leading `@` produces an ANS endpoint.
  if (true) {
    net_endpoint ep{"@abstract"};
    EXPECT_TRUE(!ep.empty());
    EXPECT_TRUE(ep.is_uds());
    EXPECT_TRUE(ep.is_ans());
    EXPECT_EQ(ep.uds_path().size(), 107U);
    EXPECT_EQ(ep.uds_path().substr(0, 8), "abstract");
  }

  // The `string_view` constructor also accepts ANS names.
  if (true) {
    net_endpoint ep{std::string_view{"@svc"}};
    EXPECT_TRUE(ep.is_ans());
    EXPECT_EQ(ep.uds_path().size(), 107U);
    EXPECT_EQ(ep.uds_path().substr(0, 3), "svc");
  }

  // An IPv4-mapped IPv6 address (e.g., `[::ffff:192.168.1.1]:80`) is stored
  // as `AF_INET6` with no unwrapping. `to_string()` formats the address in
  // pure colon-hex (RFC 5952), so `::ffff:192.168.1.1` appears as
  // `::ffff:c0a8:101`.
  if (true) {
    net_endpoint ep{"[::ffff:192.168.1.1]:80"};
    EXPECT_TRUE(!ep.empty());
    EXPECT_TRUE(ep.is_v6());
    EXPECT_FALSE(ep.is_v4());
    EXPECT_EQ(ep.port(), 80U);
    EXPECT_EQ(ep.to_string(), "[::ffff:c0a8:101]:80");
  }
}

void NetEndpoint_Comparison() {
  net_endpoint a{ipv4_addr(10, 0, 0, 1), 80};
  net_endpoint b{ipv4_addr(10, 0, 0, 1), 81};
  net_endpoint c{ipv4_addr(10, 0, 0, 1), 80};

  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a < b);

  // UDS endpoints compare by path.
  net_endpoint u1{"/a.sock"};
  net_endpoint u2{"/b.sock"};
  net_endpoint u3{"/a.sock"};
  EXPECT_TRUE(!u1.empty() && !u2.empty() && !u3.empty());
  EXPECT_TRUE(u1 == u3);
  EXPECT_FALSE(u1 == u2);
  EXPECT_TRUE(u1 < u2);

  // UDS and IPv4 compare by family.
  EXPECT_NE(a, u1);

  // ANS endpoints compare by full sun_path buffer.
  auto n1 = net_endpoint{"@same"};
  auto n2 = net_endpoint{"@same"};
  auto n3 = net_endpoint{"@zzz"};
  EXPECT_TRUE(!n1.empty() && !n2.empty() && !n3.empty());
  EXPECT_TRUE(n1 == n2);
  EXPECT_TRUE(n1 < n3);

  // ANS and regular UDS are unequal (sun_path[0] differs: '\0' vs '/').
  EXPECT_NE(n1, u1);
}

void NetEndpoint_Formatting() {
  auto v4 = net_endpoint{ipv4_addr(127, 0, 0, 1), 80};
  auto v6 = net_endpoint{ipv6_addr::loopback, 443};
  EXPECT_EQ(v4.to_string(), "127.0.0.1:80");
  EXPECT_EQ(v6.to_string(), "[::1]:443");

  auto uds = net_endpoint{"/tmp/app.sock"};
  EXPECT_TRUE(!uds.empty());
  EXPECT_EQ(uds.to_string(), "unix:/tmp/app.sock");

  // ANS: name with no embedded null truncates at trailing zeros.
  auto ans = net_endpoint{"@svc"};
  EXPECT_TRUE(!ans.empty());
  EXPECT_EQ(ans.to_string(), "unix:@svc");

  // ANS: name without an embedded null truncates at the null, ignoring bytes
  // after it.
  if (true) {
    net_endpoint ep{"@abc"};
    EXPECT_TRUE(ep.is_ans());
    EXPECT_EQ(ep.to_string(), "unix:@abc");
  }

  // ANS: name with an embedded null truncates at the null, ignoring bytes
  // after it. Pass a `string_view` that includes the embedded null.
  if (true) {
    net_endpoint ep{std::string_view{"@abc\0def", 8}};
    EXPECT_TRUE(ep.is_ans());
    EXPECT_EQ(ep.to_string(), "unix:@abc (+)");
  }

  // ANS: name that fills the entire 107-byte buffer with no null uses the
  // full length. Pass a `string_view` of 108 chars (leading '@' + 107 'x').
  if (true) {
    const std::string max_name(107, 'x');
    const std::string full_name = "@" + max_name;
    net_endpoint ep{std::string_view{full_name}};
    EXPECT_TRUE(ep.is_ans());
    EXPECT_EQ(ep.to_string(), "unix:@" + max_name);
  }

  // ANS: name longer than 107 chars is truncated to 107, ignoring the excess.
  if (true) {
    const std::string max_name(107, 'x');
    const std::string full_name = "@" + max_name + "extra";
    net_endpoint ep{std::string_view{full_name}};
    EXPECT_TRUE(ep.is_ans());
    EXPECT_EQ(ep.to_string(), "unix:@" + max_name);
  }
}

void NetEndpoint_PosixInterop() {
  if (true) {
    net_endpoint ep{ipv4_addr(192, 168, 1, 2), 1234};
    auto raw = ep.as_sockaddr_in();
    net_endpoint roundtrip{raw};
    EXPECT_EQ(roundtrip, ep);

    net_endpoint from_sockaddr{reinterpret_cast<const sockaddr&>(raw),
        sizeof(raw)};
    EXPECT_EQ(from_sockaddr, ep);

    auto storage = ep.as_sockaddr_storage();
    auto* as_v4 = reinterpret_cast<const sockaddr_in*>(&storage);
    EXPECT_EQ(as_v4->sin_family, AF_INET);
    EXPECT_EQ(ntohs(as_v4->sin_port), 1234U);
  }

  if (true) {
    net_endpoint ep{ipv6_addr(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1), 4321};
    auto raw = ep.as_sockaddr_in6();
    net_endpoint roundtrip{raw};
    EXPECT_EQ(roundtrip, ep);

    net_endpoint from_sockaddr{reinterpret_cast<const sockaddr&>(raw),
        sizeof(raw)};
    EXPECT_EQ(from_sockaddr, ep);

    auto storage = ep.as_sockaddr_storage();
    auto* as_v6 = reinterpret_cast<const sockaddr_in6*>(&storage);
    EXPECT_EQ(as_v6->sin6_family, AF_INET6);
    EXPECT_EQ(ntohs(as_v6->sin6_port), 4321U);
  }

  // UDS: roundtrip through `as_sockaddr_un()` and back.
  if (true) {
    net_endpoint ep{"/tmp/interop.sock"};
    EXPECT_TRUE(!ep.empty());

    auto raw = ep.as_sockaddr_un();
    EXPECT_EQ(raw.sun_family, static_cast<sa_family_t>(AF_UNIX));
    EXPECT_EQ(std::string_view{raw.sun_path}, "/tmp/interop.sock");

    net_endpoint roundtrip{raw};
    EXPECT_EQ(roundtrip, ep);

    net_endpoint from_sockaddr{reinterpret_cast<const sockaddr&>(raw),
        sizeof(raw)};
    EXPECT_EQ(from_sockaddr, ep);

    EXPECT_EQ(ep.sockaddr_size(),
        static_cast<socklen_t>(
            offsetof(sockaddr_un, sun_path) +
            std::strlen("/tmp/interop.sock") + 1));
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
    EXPECT_TRUE(ep.is_ans());
    EXPECT_EQ(ep.uds_path().size(), 107U);
    EXPECT_EQ(ep.uds_path().substr(0, 10), name);

    // Roundtrip via as_sockaddr_un().
    auto raw2 = ep.as_sockaddr_un();
    net_endpoint ep2{raw2};
    EXPECT_EQ(ep2, ep);

    // sockaddr_size() for ANS is sizeof(sockaddr_un).
    EXPECT_EQ(ep.sockaddr_size(), sizeof(sockaddr_un));
  }
}

void DnsResolve_NumericIPv4() {
  // Numeric IPv4 addresses are resolved without a DNS lookup.
  auto result = dns_resolver::find_all("127.0.0.1", 80);
  EXPECT_FALSE(result.empty());
  bool found = false;
  for (const auto& ep : result) {
    if (ep.is_v4() && ep.v4()->is_loopback() && ep.port() == 80) found = true;
  }
  EXPECT_TRUE(found);
}

void DnsResolve_NumericIPv6() {
  // Numeric IPv6 addresses are resolved without a DNS lookup.
  auto result = dns_resolver::find_all("::1", 443, AF_INET6);
  EXPECT_FALSE(result.empty());
  bool found = false;
  for (const auto& ep : result) {
    if (ep.is_v6() && ep.v6()->is_loopback() && ep.port() == 443) found = true;
  }
  EXPECT_TRUE(found);
}

void DnsResolve_Localhost() {
  // "localhost" is defined in /etc/hosts on all major POSIX systems.
  auto result = dns_resolver::find_all("localhost", 8080);
  EXPECT_FALSE(result.empty());
  // Every returned endpoint must use the requested port.
  for (const auto& ep : result) EXPECT_EQ(ep.port(), 8080U);
  // At least one result should be a loopback address.
  bool found = false;
  for (const auto& ep : result) {
    if ((ep.is_v4() && ep.v4()->is_loopback()) ||
        (ep.is_v6() && ep.v6()->is_loopback()))
      found = true;
  }
  EXPECT_TRUE(found);
}

void DnsResolve_FamilyFilter() {
  // With `AF_INET`, every result must be an IPv4 endpoint.
  auto v4 = dns_resolver::find_all("localhost", 80, AF_INET);
  for (const auto& ep : v4) EXPECT_TRUE(ep.is_v4());

  // With `AF_INET6`, every result must be an IPv6 endpoint.
  auto v6 = dns_resolver::find_all("localhost", 80, AF_INET6);
  for (const auto& ep : v6) EXPECT_TRUE(ep.is_v6());
}

void DnsResolve_InvalidHost() {
  // The `.invalid` TLD (RFC 2606) must not resolve.
  auto result = dns_resolver::find_all("no-such-host.invalid", 80);
  EXPECT_TRUE(result.empty());
}

void DnsResolveOne_Success() {
  // Numeric loopback resolves to exactly one endpoint with the right port.
  const auto ep = dns_resolver::find_one("127.0.0.1", 80);
  EXPECT_TRUE(ep.is_v4());
  EXPECT_EQ(ep.port(), 80U);
}

void DnsResolveOne_Failure() {
  // An unresolvable host returns a default-constructed (invalid) endpoint.
  const auto ep = dns_resolver::find_one("no-such-host.invalid", 80);
  EXPECT_EQ(ep, net_endpoint{});
}

// Helper: create a connected socketpair and wrap each end in a `net_socket`.
// Caller must close both sockets when done (RAII via `net_socket` destructor).
// Plain struct (not `std::pair`) so structured bindings use direct member
// access rather than `std::tuple_element<>::type`.
struct sockpair_t {
  net_socket a;
  net_socket b;
};
// Make a pair of connected sockets, in non-blocking mode.
static sockpair_t make_nb_sockpair() {
  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) != 0)
    return {};
  return {net_socket{os_file{fds[0]}}, net_socket{os_file{fds[1]}}};
}

// Minimal `io_conn` that counts how many times each virtual is called.
struct counting_conn: io_conn {
  using io_conn::io_conn;

  int readable = 0;
  int writable = 0;
  int error = 0;
  void on_readable() override { ++readable; }
  void on_writable() override { ++writable; }
  void on_error() override { ++error; }
};

void IoLoop_Lifecycle() {
  // Construction succeeds; an empty poll returns 0 events.
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  EXPECT_EQ(loop.run_once(0), 0);
}

void IoLoop_Post() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();

  int fired = 0;
  loop.post([&] { ++fired; });

  // post() callback runs at the top of the next run_once(), even with no
  // I/O events.
  loop.run_once(0);
  EXPECT_EQ(fired, 1);
}

void IoLoop_PreStartWorkIsQueued() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  EXPECT_TRUE(loop.is_loop_thread());

  auto conn = std::make_shared<counting_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn, false, false));
  EXPECT_FALSE(loop.register_socket(conn, false, false));

  auto msg_view = std::string_view{"hi"};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());
  EXPECT_EQ(conn->readable, 0);

  // The first pump drains the queued registration work, but read interest is
  // still disabled so the buffered data should remain undispatched.
  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->readable, 0);

  EXPECT_TRUE(loop.set_readable(conn->sock(), true));
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(conn->readable, 1);
}

// `register_socket` dispatches `on_readable` via virtual `io_conn` override;
// `unregister_socket` stops further dispatch. Double-register and
// double-unregister both return false.
void IoLoop_RegisterUnregister() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn));

  auto msg_view = std::string_view{"hi"};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_EQ(conn->writable, 0);
  EXPECT_EQ(conn->error, 0);

  // Drain the data from the registered socket so the fd is no longer readable.
  std::string buf(8, '\0');
  (void)conn->sock().read(buf);

  EXPECT_TRUE(loop.unregister_socket(conn->sock()));
  EXPECT_FALSE(loop.unregister_socket(conn->sock())); // already removed

  // No events after unregistering.
  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
}

// `set_writable(true)` arms `EPOLLOUT`; the kernel fires it when the kernel
// send buffer has space, which it does immediately on a fresh socketpair.
void IoLoop_SetWritable() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn));

  // `EPOLLOUT` is not initially armed; no writable event.
  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->writable, 0);

  loop.set_writable(conn->sock(), true);
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_GE(conn->writable, 1);

  // Disarm; no further writable events.
  loop.set_writable(conn->sock(), false);
  const int w = conn->writable;
  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->writable, w);
}

void IoLoop_SetReadable() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn, false, false));

  auto first = std::string_view{"hi"};
  EXPECT_TRUE(b.send(first) && first.empty());

  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->readable, 0);
  EXPECT_EQ(conn->writable, 0);

  EXPECT_TRUE(loop.set_readable(conn->sock(), true));
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_EQ(conn->writable, 0);

  std::string buf(8, '\0');
  (void)conn->sock().read(buf);

  EXPECT_TRUE(loop.set_readable(conn->sock(), false));
  EXPECT_TRUE(loop.set_writable(conn->sock(), true));
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_GE(conn->writable, 1);
}

// When `EPOLLERR` or `EPOLLHUP` fires together with `EPOLLOUT`, `on_error` is
// called but `on_writable` is skipped (the early-return path in
// `dispatch_event`).
void IoLoop_ErrorSkipsWritable() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn));
  loop.set_writable(conn->sock(), true); // arm EPOLLOUT

  b.close(); // triggers EPOLLHUP on `a`

  loop.run_once(0);
  EXPECT_EQ(conn->error, 1);
  EXPECT_EQ(conn->writable, 0); // must not fire when error/hup is reported
}

// The default `io_conn::on_error()` implementation falls through to
// `on_readable()`. Verify this by registering a subclass that overrides only
// `on_readable` and confirming it is called when the peer closes.
void IoLoop_DefaultOnError() {
  struct readable_only_conn: io_conn {
    using io_conn::io_conn;
    int readable = 0;
    void on_readable() override { ++readable; }
    // on_error not overridden; default calls on_readable()
  };

  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<readable_only_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn));

  b.close(); // EPOLLHUP -> default on_error() -> on_readable()
  loop.run_once(0);

  EXPECT_GE(conn->readable, 1);
}

void IoLoop_IsLoopThreadIsPerLoop() {
  epoll_loop loop_a;
  epoll_loop loop_b;
  auto loop_b_scope = loop_b.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();
  auto conn = std::make_shared<counting_conn>(std::move(a));

  std::atomic_bool first_result{false};
  std::atomic_bool second_result{false};

  std::thread loop_thread{[&] { loop_a.run(10); }};
  EXPECT_TRUE(loop_a.wait_until_running(1000));

  loop_a.post([&] {
    first_result = loop_b.register_socket(conn, false, false);
    second_result = loop_b.register_socket(conn, false, false);
    loop_a.stop();
  });
  loop_thread.join();

  EXPECT_TRUE(first_result);
  EXPECT_TRUE(second_result);

  EXPECT_EQ(loop_b.run_once(0), 0);

  auto msg_view = std::string_view{"cross-loop"};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());
  EXPECT_EQ(loop_b.run_once(0), 0);

  EXPECT_TRUE(loop_b.set_readable(conn->sock(), true));
  EXPECT_EQ(loop_b.run_once(0), 1);
  EXPECT_EQ(conn->readable, 1);
}

void IoLoop_WaitUntilRunning() {
  epoll_loop loop;

  std::thread loop_thread{[&] { loop.run(10); }};
  EXPECT_TRUE(loop.wait_until_running(1000));

  loop.stop();
  loop_thread.join();
}

void IoLoop_WaitUntilRunning_TimesOut() {
  epoll_loop loop;
  EXPECT_FALSE(loop.wait_until_running(10));
}

void IoLoop_PostAndWait_StopRace() {
  constexpr int iterations = 64;
  std::atomic_int waiter_returns{0};
  std::atomic_int callback_runs{0};

  for (int i = 0; i < iterations; ++i) {
    epoll_loop loop{std::chrono::milliseconds{5}};
    notifiable<bool> release_blocker{false};
    std::atomic_bool blocker_entered{false};
    std::atomic_bool waiter_started{false};

    std::thread loop_thread{[&] { loop.run(10); }};
    EXPECT_TRUE(loop.wait_until_running(1000));

    loop.post([&] {
      blocker_entered = true;
      release_blocker.wait_until_value(true);
    });

    while (!blocker_entered.load(std::memory_order::relaxed))
      std::this_thread::yield();

    std::thread waiter{[&] {
      waiter_started = true;
      const bool result = loop.post_and_wait([&] {
        ++callback_runs;
        return true;
      });
      if (result) ++waiter_returns;
    }};

    while (!waiter_started.load(std::memory_order::relaxed))
      std::this_thread::yield();

    loop.stop();
    release_blocker.notify_one(true);

    waiter.join();
    loop_thread.join();
  }

  EXPECT_LE(waiter_returns.load(), iterations);
  EXPECT_LE(callback_runs.load(), iterations);
}

void StreamConn_Lifecycle() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  const net_endpoint remote{ipv4_addr::loopback, 9999};
  {
    stream_conn conn{loop, std::move(a), remote, {}};
    // open_ is set in the state constructor before the post fires.
    EXPECT_TRUE(conn.is_open());
    EXPECT_EQ(conn.remote_endpoint(), remote);
    loop.run_once(0); // process posted do_open_()
  }
  // destructor posted do_close_(); process it, then verify loop is clean.
  loop.run_once(0);
  EXPECT_EQ(loop.run_once(0), 0);
}

void StreamConn_Receive() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  stream_conn conn{loop, std::move(a), {},
      {.on_data = [&](std::string& d) { received = std::move(d); }}};
  loop.run_once(0); // process posted do_open_()

  const std::string msg{"hello"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_EQ(loop.run_once(0), 1); // dispatch EPOLLIN
  EXPECT_EQ(received, msg);
}

void StreamConn_SetRecvBufSize() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::vector<size_t> chunk_sizes;
  stream_conn conn{loop, std::move(a), {},
      {.on_data = [&](std::string& d) { chunk_sizes.push_back(d.size()); }},
      4};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_EQ(conn.recv_buf_size(), 4U);

  auto first = std::string_view{"abcd1234"};
  EXPECT_TRUE(b.send(first) && first.empty());
  EXPECT_EQ(loop.run_once(0), 1); // first read capped at 4 bytes
  EXPECT_EQ(chunk_sizes.size(), 1U);
  EXPECT_EQ(chunk_sizes[0], 4U);

  EXPECT_TRUE(conn.set_recv_buf_size(8));
  EXPECT_EQ(conn.recv_buf_size(), 8U);

  EXPECT_EQ(loop.run_once(0), 1); // drain remaining 4 bytes
  EXPECT_EQ(chunk_sizes.size(), 2U);
  EXPECT_EQ(chunk_sizes[1], 4U);

  auto second = std::string_view{"ABCDEFGHijkl"};
  EXPECT_TRUE(b.send(second) && second.empty());
  EXPECT_EQ(loop.run_once(0), 1); // next read should use updated sizing hint
  EXPECT_EQ(chunk_sizes.size(), 3U);
  EXPECT_GE(chunk_sizes[2], 8U);
  EXPECT_LE(chunk_sizes[2], 12U);
}

void StreamConn_PeerClose() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool closed = false;
  stream_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
  loop.run_once(0); // process posted do_open_()

  EXPECT_TRUE(b.shutdown(SHUT_WR));
  loop.run_once(0); // dispatch EOF/HUP

  EXPECT_TRUE(closed);

  EXPECT_TRUE(conn.is_open());
  EXPECT_FALSE(conn.can_read());
  EXPECT_TRUE(conn.can_write());

  conn.send(std::string{"still-open"});
  loop.run_once(0);

  std::string buf;
  no_zero::enlarge_to(buf, 32);
  EXPECT_TRUE(b.read(buf));
  EXPECT_EQ(buf, "still-open");

  conn.close();
  loop.run_once(0);
  EXPECT_FALSE(conn.is_open());
}

void StreamConn_Send() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted do_open_()

  conn.send(std::string{"world"});
  loop.run_once(0); // process posted enqueue() -> immediate ::write

  // Data written by enqueue() is now in the kernel buffer.
  std::string buf;
  no_zero::enlarge_to(buf, 16);
  EXPECT_TRUE(b.read(buf));
  EXPECT_EQ(buf.size(), 5U);
  EXPECT_EQ(buf, "world");
}

void StreamConn_ManualClose() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool closed = false;
  stream_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
  loop.run_once(0); // process posted do_open_()

  conn.close();
  loop.run_once(0); // process posted do_close_() -> close_now_()

  EXPECT_FALSE(conn.is_open());
  EXPECT_TRUE(closed);

  // Destructor posts a hangup; it must be idempotent after close().
  loop.run_once(0);
  EXPECT_EQ(loop.run_once(0), 0);
}

void StreamConn_DrainAfterBufferedSend() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  // Restrict the kernel send buffer so that a large write is partial.
  // The kernel may round up, but typically honors a small value closely
  // enough to force at least one EAGAIN before the full payload drains.
  constexpr int small_buf = 4096;
  a.set_send_buffer_size(small_buf);

  // Payload larger than the send buffer to reliably exercise the EPOLLOUT
  // path. 256 KB is large enough on typical Linux systems.
  const std::string payload(256ULL * 1024ULL, 'x');

  int drain_count = 0;
  stream_conn conn{loop, std::move(a), {}, {.on_drain = [&] { ++drain_count; }}};
  loop.run_once(0); // process posted do_open_()

  conn.send(std::string{payload}); // copy payload into send
  // Drain by reading from `b` and running the loop until all data arrives.
  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  while (received.size() < payload.size()) {
    loop.run_once(0);
    no_zero::enlarge_to(tmp, 4096);
    while (b.read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero::enlarge_to(tmp, 4096);
    }
  }

  // All bytes must arrive intact.
  EXPECT_EQ(received.size(), payload.size());
  EXPECT_EQ(received, payload);

  // `on_drain` must have fired at least once (via the EPOLLOUT path).
  EXPECT_GE(drain_count, 1);
}

void StreamConn_DrainAfterImmediateSend() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  int drain_count = 0;
  stream_conn conn{loop, std::move(a), {}, {.on_drain = [&] { ++drain_count; }}};
  loop.run_once(0); // process posted register_with_loop

  conn.send(std::string{"hello"});
  loop.run_once(0); // process posted enqueue_send()

  std::string received;
  no_zero::enlarge_to(received, 16);
  EXPECT_TRUE(b.read(received));
  EXPECT_EQ(received, "hello");
  EXPECT_EQ(drain_count, 1);
}

void StreamConn_AsyncCbRead() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.async_cb_read([&](std::string& data) {
    received = std::move(data);
  }));

  const std::string msg{"callback-read"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  loop.run_once(0); // dispatch EPOLLIN -> inline callback

  EXPECT_EQ(received, msg);
}

void StreamConn_AsyncCbRead_PreservesEarlyData() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  const std::string msg{"early-callback-read"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_EQ(loop.run_once(0),
      0); // read interest is disabled; data stays queued

  EXPECT_TRUE(conn.async_cb_read([&](std::string& data) {
    received = std::move(data);
  }));

  EXPECT_EQ(loop.run_once(0), 1); // enabling EPOLLIN surfaces buffered data
  EXPECT_EQ(received, msg);
}

void StreamConn_AsyncCbRead_DuplicateRejected() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  int callback_count = 0;
  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.async_cb_read([&](std::string&) { ++callback_count; }));
  EXPECT_FALSE(conn.async_cb_read([&](std::string&) { ++callback_count; }));

  const std::string msg{"one"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  loop.run_once(0); // dispatch EPOLLIN -> one callback

  EXPECT_EQ(callback_count, 1);
}

void StreamConn_AsyncCbRead_PeerClose() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received{"sentinel"};
  int callback_count = 0;
  bool closed = false;
  stream_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.async_cb_read([&](std::string& data) {
    received = std::move(data);
    ++callback_count;
  }));

  b.close();
  loop.run_once(0); // dispatch EPOLLHUP -> close_now -> inline callback

  EXPECT_EQ(callback_count, 1);
  EXPECT_TRUE(received.empty());
  EXPECT_TRUE(closed);
  EXPECT_TRUE(conn.is_open());
  EXPECT_FALSE(conn.can_read());
  EXPECT_TRUE(conn.can_write());

  conn.close();
  loop.run_once(0);
  EXPECT_FALSE(conn.is_open());
}

void StreamConn_AsyncCbWrite() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool completed = false;
  int callback_count = 0;
  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.async_cb_write(std::string{"callback-write"},
      [&](bool write_completed) {
        completed = write_completed;
        ++callback_count;
      }));

  std::string received;
  no_zero::enlarge_to(received, 32);
  EXPECT_TRUE(b.read(received));
  EXPECT_EQ(received, "callback-write");
  EXPECT_TRUE(completed);
  EXPECT_EQ(callback_count, 1);
}

void StreamConn_AsyncCbWrite_Failure() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool completed = true;
  bool closed = false;
  stream_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
  loop.run_once(0); // process posted register_with_loop

  b.close();

  EXPECT_TRUE(conn.async_cb_write(std::string{"boom"},
      [&](bool write_completed) { completed = write_completed; }));

  EXPECT_FALSE(completed);
  EXPECT_TRUE(conn.is_open());
  EXPECT_TRUE(conn.can_read());
  EXPECT_FALSE(conn.can_write());

  loop.run_once(0); // process peer-close notification
  EXPECT_TRUE(closed);
  EXPECT_FALSE(conn.is_open());
}

void StreamConn_ShutdownWrite() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  stream_conn conn{loop, std::move(a), {},
      {.on_data = [&](std::string& d) { received = std::move(d); }}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.can_read());
  EXPECT_TRUE(conn.can_write());
  EXPECT_TRUE(conn.shutdown_write());
  EXPECT_TRUE(conn.is_open());
  EXPECT_TRUE(conn.can_read());
  EXPECT_FALSE(conn.can_write());
  EXPECT_FALSE(conn.async_cb_write(std::string{"nope"}, [&](bool) {}));

  const std::string msg{"inbound"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());
  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(received, msg);
}

void StreamConn_ShutdownRead() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  int data_count = 0;
  stream_conn conn{loop, std::move(a), {},
      {.on_data = [&](std::string&) { ++data_count; }}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.can_read());
  EXPECT_TRUE(conn.can_write());
  EXPECT_TRUE(conn.shutdown_read());
  EXPECT_TRUE(conn.is_open());
  EXPECT_FALSE(conn.can_read());
  EXPECT_TRUE(conn.can_write());
  EXPECT_FALSE(conn.async_cb_read([&](std::string&) { ++data_count; }));

  conn.send(std::string{"outbound"});
  loop.run_once(0);

  std::string buf;
  no_zero::enlarge_to(buf, 32);
  EXPECT_TRUE(b.read(buf));
  EXPECT_EQ(buf, "outbound");
  EXPECT_EQ(data_count, 0);
}

void StreamConn_ShutdownBothCloses() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.shutdown_write());
  EXPECT_TRUE(conn.is_open());
  EXPECT_FALSE(conn.can_write());
  EXPECT_TRUE(conn.can_read());

  EXPECT_TRUE(conn.shutdown_read());
  EXPECT_FALSE(conn.is_open());
  EXPECT_FALSE(conn.can_read());
  EXPECT_FALSE(conn.can_write());
}

void StreamConn_AsyncCbWrite_DuplicateRejected() {
  epoll_loop loop;
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  a.set_send_buffer_size(small_buf);

  stream_conn conn{loop, std::move(a), {}, {}};
  std::thread loop_thread{[&] { loop.run(10); }};
  EXPECT_TRUE(loop.wait_until_running(1000));

  const std::string payload(256ULL * 1024ULL, 'w');
  std::atomic<int> accepted{0};
  std::atomic<int> rejected{0};
  std::atomic<int> completions{0};
  notifiable<bool> completion{false};

  auto try_register = [&] {
    if (conn.async_cb_write(std::string{payload}, [&](bool) {
          ++completions;
          completion.notify_one(true);
        }))
      ++accepted;
    else
      ++rejected;
  };

  std::thread t1{try_register};
  std::thread t2{try_register};
  t1.join();
  t2.join();

  EXPECT_EQ(accepted, 1);
  EXPECT_EQ(rejected, 1);

  b.close();
  EXPECT_TRUE(completion.wait_for_value(std::chrono::seconds{1}, true));

  EXPECT_EQ(completions, 1);

  loop.stop();
  loop_thread.join();
}

void StreamConn_GracefulClose() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  a.set_send_buffer_size(small_buf);

  const std::string payload(64ULL * 1024ULL, 'z');

  bool closed = false;
  stream_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
  loop.run_once(0); // process posted do_open_()

  // Queue data then immediately request a close; the close must be deferred
  // until the send queue drains.
  conn.send(std::string{payload}); // copy payload into send
  conn.close();

  // Drain all data from `b` while running the loop.
  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  while (!closed) {
    loop.run_once(0);
    no_zero::enlarge_to(tmp, 4096);
    while (b.read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero::enlarge_to(tmp, 4096);
    }
  }

  EXPECT_EQ(received.size(), payload.size());
  EXPECT_EQ(received, payload);
  EXPECT_TRUE(closed);
  EXPECT_FALSE(conn.is_open());
}

void StreamConn_CloseThenDestructStaysGraceful() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  a.set_send_buffer_size(small_buf);

  const std::string payload(64ULL * 1024ULL, 'g');

  bool closed = false;
  {
    stream_conn conn{loop, std::move(a), {},
        {.on_close = [&] { closed = true; }}};
    loop.run_once(0); // process posted register_with_loop

    conn.send(std::string{payload});
    conn.close();
  }

  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  for (int i = 0; i < 512 && !closed; ++i) {
    loop.run_once(0);
    no_zero::enlarge_to(tmp, 4096);
    while (b.read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero::enlarge_to(tmp, 4096);
    }
  }

  EXPECT_TRUE(closed);
  EXPECT_EQ(received.size(), payload.size());
  EXPECT_EQ(received, payload);
  no_zero::enlarge_to(tmp, 1);
  EXPECT_FALSE(b.read(tmp));
}

void StreamConn_DestructorHangsUp() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  a.set_send_buffer_size(small_buf);

  const std::string payload(256ULL * 1024ULL, 'q');

  bool closed = false;
  {
    stream_conn conn{loop, std::move(a), {},
        {.on_close = [&] { closed = true; }}};
    loop.run_once(0); // process posted register_with_loop
    conn.send(std::string{payload});
  }

  loop.run_once(0); // drain posted enqueue_send() then posted hangup()

  std::string received;
  std::string tmp;
  no_zero::enlarge_to(tmp, 4096);
  while (b.read(tmp) && !tmp.empty()) {
    received.append(tmp);
    no_zero::enlarge_to(tmp, 4096);
  }

  EXPECT_TRUE(closed);
  EXPECT_LT(received.size(), payload.size());
}

// Coroutine tests.

// Verify that a `loop_task` coroutine body executes eagerly and that the
// frame is self-destroyed (i.e., no handle is needed to drive it).
void LoopTask_FireAndForget() {
  int counter = 0;
  auto coro = [&]() -> loop_task {
    ++counter;
    co_return;
  };
  coro(); // starts and finishes synchronously; frame self-destructs
  EXPECT_EQ(counter, 1);
}

// Verify that `async_read` delivers data to a coroutine.
void StreamConn_AsyncRead() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  bool done = false;

  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  auto coro = [&]() -> loop_task {
    received = co_await conn.async_read();
    done = true;
  };
  coro(); // starts eagerly; suspends at async_read (no data yet)

  const std::string msg{"hello"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  loop.run_once(0); // dispatch EPOLLIN -> posts resume
  loop.run_once(0); // drain post queue -> coroutine resumes

  EXPECT_TRUE(done);
  EXPECT_EQ(received, msg);
}

void StreamConn_AsyncRead_PreservesEarlyData() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  bool done = false;

  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  const std::string msg{"early-coroutine-read"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_EQ(loop.run_once(0),
      0); // data remains in kernel until a read is armed

  auto coro = [&]() -> loop_task {
    received = co_await conn.async_read();
    done = true;
  };
  coro();

  loop.run_once(0); // dispatch buffered EPOLLIN
  loop.run_once(0); // drain posted resume

  EXPECT_TRUE(done);
  EXPECT_EQ(received, msg);
}

void StreamConn_AsyncRead_StopsBetweenCalls() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string first;
  std::string second;
  bool first_done = false;
  bool second_done = false;

  stream_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  auto first_coro = [&]() -> loop_task {
    first = co_await conn.async_read();
    first_done = true;
  };
  first_coro();

  auto first_msg = std::string_view{"first"};
  EXPECT_TRUE(b.send(first_msg) && first_msg.empty());

  loop.run_once(0); // deliver first read
  loop.run_once(0); // resume first coroutine

  EXPECT_TRUE(first_done);
  EXPECT_EQ(first, "first");

  auto second_msg = std::string_view{"second"};
  EXPECT_TRUE(b.send(second_msg) && second_msg.empty());

  EXPECT_EQ(loop.run_once(0), 0); // no waiter, so second chunk stays in kernel
  EXPECT_FALSE(second_done);

  auto second_coro = [&]() -> loop_task {
    second = co_await conn.async_read();
    second_done = true;
  };
  second_coro();

  loop.run_once(0); // buffered kernel data is now delivered
  loop.run_once(0); // resume second coroutine

  EXPECT_TRUE(second_done);
  EXPECT_EQ(second, "second");
}

// Verify that `async_read` returns an empty string when the peer closes
// the connection before data arrives.
void StreamConn_AsyncRead_PeerClose() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received{"sentinel"};
  bool done = false;

  stream_conn conn{loop, std::move(a), {}};
  loop.run_once(0); // process posted register_with_loop

  auto coro = [&]() -> loop_task {
    received = co_await conn.async_read();
    done = true;
  };
  coro(); // starts eagerly; suspends at async_read

  b.close(); // trigger EPOLLHUP/EOF on `a`

  loop.run_once(0); // dispatch EPOLLHUP -> do_close_now -> posts resume
  loop.run_once(0); // drain post queue -> coroutine resumes

  EXPECT_TRUE(done);
  EXPECT_TRUE(received.empty()); // close delivers empty data
  EXPECT_TRUE(conn.is_open());
  EXPECT_FALSE(conn.can_read());
  EXPECT_TRUE(conn.can_write());

  conn.close();
  loop.run_once(0);
  EXPECT_FALSE(conn.is_open());
}

// Verify that `async_send` delivers bytes to the peer and suspends until
// the queue drains.
void StreamConn_AsyncSend() {
  epoll_loop loop;
  auto loop_scope = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool sent = false;

  stream_conn conn{loop, std::move(a), {}};
  loop.run_once(0); // process posted register_with_loop

  const std::string msg{"world"};

  auto coro = [&]() -> loop_task {
    co_await conn.async_send(std::string{msg});
    sent = true;
  };
  coro(); // starts eagerly; write likely completes synchronously

  // If the write was synchronous, `sent` is already true after one
  // run_once (for the register post). Otherwise pump the loop to drain.
  for (int i = 0; i < 4 && !sent; ++i) loop.run_once(0);

  EXPECT_TRUE(sent);

  std::string buf;
  no_zero::enlarge_to(buf, 16);
  EXPECT_TRUE(b.read(buf));
  EXPECT_EQ(buf, msg);
}

MAKE_TEST_LIST(Ipv4Addr_Construction, Ipv4Addr_Parse, Ipv4Addr_Classification,
    Ipv4Addr_Comparison, Ipv4Addr_Formatting, Ipv4Addr_PosixInterop,
    Ipv6Addr_Construction, Ipv6Addr_Parse, Ipv6Addr_Classification,
    Ipv6Addr_Comparison, Ipv6Addr_Formatting, Ipv6Addr_PosixInterop,
    NetEndpoint_Construction, NetEndpoint_Parse, NetEndpoint_Comparison,
    NetEndpoint_Formatting, NetEndpoint_PosixInterop, DnsResolve_NumericIPv4,
    DnsResolve_NumericIPv6, DnsResolve_Localhost, DnsResolve_FamilyFilter,
    DnsResolve_InvalidHost, DnsResolveOne_Success, DnsResolveOne_Failure,
    IoLoop_Lifecycle, IoLoop_Post, IoLoop_PreStartWorkIsQueued,
    IoLoop_RegisterUnregister, IoLoop_SetWritable, IoLoop_SetReadable,
    IoLoop_ErrorSkipsWritable, IoLoop_DefaultOnError,
    IoLoop_IsLoopThreadIsPerLoop, IoLoop_WaitUntilRunning,
    IoLoop_WaitUntilRunning_TimesOut, IoLoop_PostAndWait_StopRace,
    StreamConn_Lifecycle, StreamConn_Receive, StreamConn_SetRecvBufSize,
    StreamConn_PeerClose, StreamConn_Send, StreamConn_ManualClose,
    StreamConn_DrainAfterBufferedSend, StreamConn_DrainAfterImmediateSend,
    StreamConn_AsyncCbRead, StreamConn_AsyncCbRead_PreservesEarlyData,
    StreamConn_AsyncCbRead_DuplicateRejected, StreamConn_AsyncCbRead_PeerClose,
    StreamConn_AsyncCbWrite, StreamConn_AsyncCbWrite_Failure,
    StreamConn_AsyncCbWrite_DuplicateRejected, StreamConn_ShutdownWrite,
    StreamConn_ShutdownRead, StreamConn_ShutdownBothCloses, StreamConn_GracefulClose,
    StreamConn_CloseThenDestructStaysGraceful, StreamConn_DestructorHangsUp,
    LoopTask_FireAndForget, StreamConn_AsyncRead,
    StreamConn_AsyncRead_PreservesEarlyData, StreamConn_AsyncRead_StopsBetweenCalls,
    StreamConn_AsyncRead_PeerClose, StreamConn_AsyncSend);

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
