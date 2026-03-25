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
#include "../corvid/proto.h"
#include "../corvid/proto/iouring_loop.h"
#include "../corvid/proto/iou_stream_conn.h"

#include <type_traits>
#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;
using namespace std::chrono_literals;

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
    ASSERT_TRUE(a.has_value());
    EXPECT_TRUE(a->is_any());

    auto b = ipv4_addr::parse("127.0.0.1");
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(b->is_loopback());

    auto c = ipv4_addr::parse("255.255.255.255");
    ASSERT_TRUE(c.has_value());
    EXPECT_TRUE(c->is_broadcast());

    auto d = ipv4_addr::parse("192.168.1.100");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->to_uint32(), 0xc0a80164U);

    // Single-digit octets.
    auto e = ipv4_addr::parse("1.2.3.4");
    ASSERT_TRUE(e.has_value());
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
  ASSERT_TRUE(addr.has_value());
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
    ASSERT_TRUE(a.has_value());
    EXPECT_TRUE(a->is_any());

    auto b = ipv6_addr::parse("::1");
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(b->is_loopback());

    auto c = ipv6_addr::parse("2001:db8::1");
    ASSERT_TRUE(c.has_value());
    EXPECT_EQ(c->to_string(), "2001:db8::1");

    auto d = ipv6_addr::parse("2001:0DB8:0000:0000:0000:0000:0000:0001");
    ASSERT_TRUE(d.has_value());
    EXPECT_EQ(d->to_string(), "2001:db8::1");

    auto e = ipv6_addr::parse("fe80::1234:5678");
    ASSERT_TRUE(e.has_value());
    EXPECT_TRUE(e->is_link_local());

    auto f = ipv6_addr::parse("::ffff:192.168.1.1");
    ASSERT_TRUE(f.has_value());
    auto fw = f->words();
    EXPECT_EQ(fw[4], 0U);
    EXPECT_EQ(fw[5], 0xFFFFU);
    EXPECT_EQ(fw[6], 0xC0A8U);
    EXPECT_EQ(fw[7], 0x0101U);
    auto fb = f->bytes();
    EXPECT_EQ(fb[14], 1U);
    EXPECT_EQ(fb[15], 1U);

    auto g = ipv6_addr::parse("2001:db8::192.0.2.33");
    ASSERT_TRUE(g.has_value());
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
  ASSERT_TRUE(addr.has_value());
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
    ASSERT_TRUE(ep.is_v4());
    EXPECT_EQ(ep.port(), 80U);
    EXPECT_EQ(ep.v4()->to_string(), "127.0.0.1");
  }

  if (true) {
    net_endpoint ep{ipv6_addr::loopback, 443};
    ASSERT_TRUE(ep.is_v6());
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
    // `uds_path` skips the leading '\0' and returns the full 107-byte
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
    ASSERT_TRUE(a.is_v4());
    EXPECT_EQ(a.port(), 8080U);
    EXPECT_EQ(a.v4()->to_string(), "192.168.1.10");

    net_endpoint b{"[2001:db8::1]:443"};
    EXPECT_TRUE(!b.empty());
    ASSERT_TRUE(b.is_v6());
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
  // as `AF_INET6` with no unwrapping. `to_string` formats the address in
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

  // UDS: roundtrip through `as_sockaddr_un` and back.
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
  if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) != 0) {
    const int err = errno;
    std::cerr << "socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK) failed: "
              << std::strerror(err) << " (" << err << ")\n";
    ASSERT_TRUE(false);
    return {};
  }
  return {net_socket{os_file{fds[0]}}, net_socket{os_file{fds[1]}}};
}

// Minimal `io_conn` that counts how many times each virtual is called.
struct counting_conn: io_conn {
  using io_conn::io_conn;

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

void IoLoop_Lifecycle() {
  // Construction succeeds; an empty poll returns 0 events.
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  EXPECT_EQ(loop->run_once(0), 0);
}

void IoLoop_Post() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();

  int fired = 0;
  EXPECT_TRUE(loop->post([&] {
    ++fired;
    return true;
  }));

  // post() callback runs at the top of the next run_once(), even with no
  // I/O events.
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(fired, 1);
}

void IoLoop_PreStartWorkIsQueued() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  EXPECT_TRUE(loop->is_loop_thread());

  auto conn = std::make_shared<counting_conn>(std::move(a));
  ASSERT_TRUE(loop->register_socket(conn, false, false));
  EXPECT_FALSE(loop->register_socket(conn, false, false));

  auto msg_view = std::string_view{"hi"};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());
  EXPECT_EQ(conn->readable, 0);

  // The first pump drains the queued registration work, but read interest is
  // still disabled so the buffered data should remain undispatched.
  EXPECT_EQ(loop->run_once(0), 0);
  EXPECT_EQ(conn->readable, 0);

  ASSERT_TRUE(loop->enable_reads(*conn, true));
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
}

// `register_socket` dispatches `on_readable` via virtual `io_conn` override;
// `unregister_socket` stops further dispatch. Double-register and
// double-unregister both return false.
void IoLoop_RegisterUnregister() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  ASSERT_TRUE(loop->register_socket(conn));

  auto msg_view = std::string_view{"hi"};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_EQ(conn->writable, 0);
  EXPECT_EQ(conn->error, 0);

  // Drain the data from the registered socket so the fd is no longer readable.
  std::string buf(8, '\0');
  (void)conn->sock().read(buf);

  ASSERT_TRUE(loop->unregister_socket(conn->sock()));
  EXPECT_FALSE(loop->unregister_socket(conn->sock())); // already removed

  // No events after unregistering.
  EXPECT_EQ(loop->run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
}

// `enable_writes(true)` arms `EPOLLOUT`; the kernel fires it when the kernel
// send buffer has space, which it does immediately on a fresh socketpair.
void IoLoop_SetWritable() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  ASSERT_TRUE(loop->register_socket(conn));

  // `EPOLLOUT` is not initially armed; no writable event.
  EXPECT_EQ(loop->run_once(0), 0);
  EXPECT_EQ(conn->writable, 0);

  ASSERT_TRUE(loop->enable_writes(*conn, true));
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_GE(conn->writable, 1);

  // Disarm; no further writable events.
  ASSERT_TRUE(loop->enable_writes(*conn, false));
  const int w = conn->writable;
  EXPECT_EQ(loop->run_once(0), 0);
  EXPECT_EQ(conn->writable, w);
}

void IoLoop_SetReadable() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  ASSERT_TRUE(loop->register_socket(conn, false, false));

  auto first = std::string_view{"hi"};
  ASSERT_TRUE(b.send(first) && first.empty());

  EXPECT_EQ(loop->run_once(0), 0);
  EXPECT_EQ(conn->readable, 0);
  EXPECT_EQ(conn->writable, 0);

  ASSERT_TRUE(loop->enable_reads(*conn, true));
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_EQ(conn->writable, 0);

  std::string buf(8, '\0');
  (void)conn->sock().read(buf);

  ASSERT_TRUE(loop->enable_reads(*conn, false));
  ASSERT_TRUE(loop->enable_writes(*conn, true));
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_GE(conn->writable, 1);
}

// When `EPOLLERR` or `EPOLLHUP` fires together with `EPOLLOUT`, `on_error` is
// called but `on_writable` is skipped (the early-return path in
// `dispatch_event`).
void IoLoop_ErrorSkipsWritable() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  ASSERT_TRUE(loop->register_socket(conn));
  ASSERT_TRUE(loop->enable_writes(*conn, true)); // arm EPOLLOUT

  (void)b.close(); // triggers EPOLLHUP on `a`

  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(conn->error, 1);
  EXPECT_EQ(conn->writable, 0); // must not fire when error/hup is reported
}

// The default `io_conn::on_error` implementation falls through to
// `on_readable`. Verify this by registering a subclass that overrides only
// `on_readable` and confirming it is called when the peer closes.
void IoLoop_DefaultOnError() {
  struct readable_only_conn: io_conn {
    using io_conn::io_conn;
    int readable = 0;
    bool on_readable() override {
      ++readable;
      return true;
    }
    // on_error not overridden; default calls on_readable()
  };

  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<readable_only_conn>(std::move(a));
  ASSERT_TRUE(loop->register_socket(conn));

  (void)b.close(); // EPOLLHUP -> default on_error() -> on_readable()
  EXPECT_GE(loop->run_once(0), 0);

  EXPECT_GE(conn->readable, 1);
}

void IoLoop_IsLoopThreadIsPerLoop() {
  auto loop_a = epoll_loop::make();
  auto loop_b = epoll_loop::make();
  auto loop_b_scope = loop_b->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();
  auto conn = std::make_shared<counting_conn>(std::move(a));

  std::atomic_bool first_result{false};
  std::atomic_bool second_result{false};

  std::thread loop_thread{[&] { (void)loop_a->run(10); }};
  ASSERT_TRUE(loop_a->wait_until_running(1000));

  EXPECT_TRUE(loop_a->post([&] {
    first_result = loop_b->register_socket(conn, false, false);
    second_result = loop_b->register_socket(conn, false, false);
    return loop_a->stop();
  }));
  loop_thread.join();

  ASSERT_TRUE(first_result);
  ASSERT_TRUE(second_result);

  EXPECT_EQ(loop_b->run_once(0), 0);

  auto msg_view = std::string_view{"cross-loop"};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());
  EXPECT_EQ(loop_b->run_once(0), 0);

  ASSERT_TRUE(loop_b->enable_reads(*conn, true));
  EXPECT_EQ(loop_b->run_once(0), 1);
  EXPECT_EQ(conn->readable, 1);
}

void IoLoop_PostAndWait_StopRace() {
  constexpr int iterations = 64;
  std::atomic_int waiter_returns{0};
  std::atomic_int callback_runs{0};

  for (int i = 0; i < iterations; ++i) {
    epoll_loop_runner loop{std::chrono::milliseconds{5}};
    notifiable<bool> release_blocker{false};
    std::atomic_bool blocker_entered{false};
    std::atomic_bool waiter_started{false};

    EXPECT_TRUE(loop->post([&] {
      blocker_entered = true;
      release_blocker.wait_until_value(true);
      return true;
    }));

    while (!blocker_entered.load(std::memory_order::relaxed))
      std::this_thread::yield();

    std::thread waiter{[&] {
      waiter_started = true;
      const bool result = loop->post_and_wait([&] {
        ++callback_runs;
        return true;
      });
      if (result) ++waiter_returns;
    }};

    while (!waiter_started.load(std::memory_order::relaxed))
      std::this_thread::yield();

    EXPECT_TRUE(loop->stop());
    release_blocker.notify_one(true);

    waiter.join();
  }

  EXPECT_LE(waiter_returns.load(), iterations);
  EXPECT_LE(callback_runs.load(), iterations);
}

// Helper: allocate `cap` bytes into `rb.buffer`, set `begin`/`end`, and
// default `min_capacity` to the actual post-allocation capacity so that
// resize logic does not fire unexpectedly in tests that do not set it.
// Fills active region [b..e) with `ch` so moves can be verified.
static void
setup_rb(recv_buffer& rb, size_t cap, size_t b, size_t e, char ch = 'X') {
  no_zero::enlarge_to(rb.buffer, cap);
  rb.min_capacity = rb.buffer.capacity();
  if (b < e) std::fill(rb.buffer.data() + b, rb.buffer.data() + e, ch);
  rb.begin.store(b, std::memory_order::relaxed);
  rb.end.store(e, std::memory_order::relaxed);
}

// When there are no active bytes, compact always resets begin and end to 0.
void RecvBuffer_Compact_NoActiveBytes() {
  if (true) {
    // begin == end == 0: already at front, cheap reset is a no-op.
    recv_buffer rb;
    setup_rb(rb, 64, 0, 0);
    rb.compact();
    EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
    EXPECT_EQ(rb.end.load(std::memory_order::relaxed), 0U);
    EXPECT_EQ(rb.write_space(), rb.buffer.capacity());
  }
  if (true) {
    // begin == end > 0: cheap reset reclaims all space.
    recv_buffer rb;
    setup_rb(rb, 64, 40, 40);
    rb.compact();
    EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
    EXPECT_EQ(rb.end.load(std::memory_order::relaxed), 0U);
    EXPECT_EQ(rb.write_space(), rb.buffer.capacity());
  }
}

// When write space is exhausted (end == capacity) but begin > 0, compact
// must memmove to reclaim space before the active region.
void RecvBuffer_Compact_MustCompact() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  const size_t b = cap / 4; // begin at 1/4 mark (not past it: worth_it false)
  setup_rb(rb, cap, b, cap, 'A'); // end == capacity: must compact
  rb.compact();
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
  EXPECT_EQ(rb.end.load(std::memory_order::relaxed), cap - b);
  EXPECT_EQ(rb.buffer[0], 'A');
  EXPECT_GT(rb.write_space(), 0U);
}

// When begin is past the 1/4 mark and end is past the 3/4 mark, compact
// proactively moves bytes to avoid a short recv on the next call.
void RecvBuffer_Compact_WorthIt() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  const size_t b = (cap / 4) + 1;     // just past 1/4 mark
  const size_t e = (cap / 4 * 3) + 1; // just past 3/4 mark
  setup_rb(rb, cap, b, e, 'B');
  rb.compact();
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
  EXPECT_EQ(rb.end.load(std::memory_order::relaxed), e - b);
  EXPECT_EQ(rb.buffer[0], 'B');
}

// When neither "must" nor "worth it" applies, begin and end are left
// unchanged to avoid a pointless memmove.
void RecvBuffer_Compact_SkipsUnnecessaryMove() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  const size_t b = cap / 4; // at the 1/4 mark, not past it: worth_it false
  const size_t e = cap / 2; // end well before 3/4 mark, write_space > 0
  setup_rb(rb, cap, b, e);
  rb.compact();
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), b);
  EXPECT_EQ(rb.end.load(std::memory_order::relaxed), e);
}

// When target exceeds current capacity, the buffer is grown and active bytes
// are copied to the front of the new buffer.
void RecvBuffer_Compact_GrowOnRequest() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  setup_rb(rb, cap, 10, 30, 'C');
  rb.compact(cap * 2);
  EXPECT_GE(rb.buffer.capacity(), cap * 2);
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
  EXPECT_EQ(rb.end.load(std::memory_order::relaxed), 20U);
  EXPECT_EQ(rb.buffer[0], 'C');
}

// When current capacity is below min_capacity, compact grows the buffer
// and syncs min_capacity to the actual post-resize capacity.
void RecvBuffer_Compact_GrowToMinCapacity() {
  recv_buffer rb;
  setup_rb(rb, 32, 0, 10, 'D');
  const size_t configured = rb.buffer.capacity() * 2;
  rb.min_capacity = configured;
  rb.compact();
  EXPECT_GE(rb.buffer.capacity(), configured);
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
  EXPECT_EQ(rb.end.load(std::memory_order::relaxed), 10U);
  EXPECT_EQ(rb.buffer[0], 'D');
  // min_capacity is synced to the actual post-resize capacity.
  EXPECT_EQ(size_t(rb.min_capacity), rb.buffer.capacity());
}

// When the buffer has bloated beyond 2x min_capacity and all active data
// fits in min_capacity, compact shrinks back to min_capacity.
void RecvBuffer_Compact_Shrink() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t bloated_cap = rb.buffer.capacity();
  // configured = cap/4; current = cap = 4*configured > 2*configured: bloated.
  const size_t configured = bloated_cap / 4;
  // Fill active region [4..4+configured) then override min_capacity.
  setup_rb(rb, bloated_cap, 4, 4 + configured,
      'E');                     // active_len == configured
  rb.min_capacity = configured; // set after setup_rb, which resets it
  rb.compact();
  EXPECT_LT(rb.buffer.capacity(), bloated_cap);
  EXPECT_GE(rb.buffer.capacity(), configured);
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
  EXPECT_EQ(rb.end.load(std::memory_order::relaxed), configured);
  EXPECT_EQ(rb.buffer[0], 'E');
}

// Shrink is skipped when active_len exceeds min_capacity; the shrunken
// buffer would not fit the active data.
void RecvBuffer_Compact_ShrinkSkippedIfActiveWontFit() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  // active_len = cap/2 > configured = cap/4: shrink condition fails.
  setup_rb(rb, cap, 0, cap / 2);
  rb.min_capacity = cap / 4; // set after setup_rb, which resets it
  rb.compact();
  EXPECT_EQ(rb.buffer.capacity(), cap); // no resize
  // begin=0, end unchanged (no memmove: begin not past 1/4 mark).
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
  EXPECT_EQ(rb.end.load(std::memory_order::relaxed), cap / 2);
}

// When target > 0 but does not exceed current capacity, no resize occurs.
void RecvBuffer_Compact_NoResizeWhenTargetFits() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  rb.compact(cap / 2); // target <= current: no resize
  EXPECT_EQ(rb.buffer.capacity(), cap);
}

// `update_active_view` advances `begin` by the number of bytes the parser
// moved past in an `active_view` snapshot. Calling it in stages is
// equivalent to a single `consume` for the total consumed bytes.
void RecvBufferView_UpdateActiveView() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 5, 'X');

  recv_buffer_view v{rb, [](size_t, size_t) {}};

  // First look: 5 bytes available.
  std::string_view sv = v;
  EXPECT_EQ(sv.size(), 5U);

  // Advance 2 bytes into the snapshot, then inform the view.
  sv.remove_prefix(2);
  v.update_active_view(sv);
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 2U);

  // Second look: 3 bytes remain at the new `begin`.
  sv = v;
  EXPECT_EQ(sv.size(), 3U);
  EXPECT_EQ(sv[0], 'X');

  // Consume the rest.
  sv.remove_prefix(3);
  v.update_active_view(sv);
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 5U);
}

// The moved-from view must not fire the resume callback; only the final
// owner fires it, exactly once. `last_seen_end_` and `new_buffer_size_` are
// carried over by the move so the callback receives correct values.
void RecvBufferView_MoveSemantics() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 10, 'X');

  if (true) {
    // Move construction: moved-from is null; moved-to fires resume.
    size_t fired_new_size{};
    size_t fired_lse{};
    {
      recv_buffer_view v1{rb,
          [&](size_t n, size_t lse) {
            fired_new_size = n;
            fired_lse = lse;
          }};
      recv_buffer_view v2{std::move(v1)}; // v1 now null

      // v2 retains full buffer access.
      EXPECT_EQ(v2.active_view().size(), 10U); // also sets last_seen_end_ = 10
      v2.expand_to(128);
      // v1 destructs silently; v2 destructs and fires resume(128, 10).
    }
    EXPECT_EQ(fired_new_size, 128U);
    EXPECT_EQ(fired_lse, 10U);
  }
}

// try_take_full returns false and leaves everything unchanged when `end` is
// not at the physical end of the buffer.
void RecvBufferView_TryTakeFull_Fail() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 5, 'X');
  const size_t cap = rb.buffer.capacity();

  recv_buffer_view v{rb, [](size_t, size_t) {}};
  std::string out;
  std::string_view sv;
  EXPECT_FALSE(v.try_take_full(out, sv));
  EXPECT_TRUE(out.empty());
  EXPECT_TRUE(sv.empty());
  EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
  EXPECT_EQ(rb.end.load(std::memory_order::relaxed), 5U);
  EXPECT_EQ(rb.buffer.capacity(), cap);
}

// try_take_full succeeds when `end == capacity`. The backing buffer is
// swapped into `out`, `view` covers the active region inside `out`, and
// indices are reset. The destructor callback receives `lse == 0`.
void RecvBufferView_TryTakeFull_Success() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  // Active region starts mid-buffer so the view offset is verified.
  setup_rb(rb, cap, 10, cap, 'A');

  size_t cb_lse{1}; // non-zero sentinel; confirmed reset to 0 by destructor
  {
    recv_buffer_view v{rb, [&](size_t, size_t lse) { cb_lse = lse; }};
    std::string out;
    std::string_view sv;
    EXPECT_TRUE(v.try_take_full(out, sv));

    // `out` holds the full old buffer.
    EXPECT_EQ(out.size(), cap);
    // `sv` covers the active portion inside `out`.
    EXPECT_EQ(sv.data(), out.data() + 10);
    EXPECT_EQ(sv.size(), cap - 10U);
    EXPECT_EQ(sv[0], 'A');
    // Backing buffer maintains the `size == capacity` invariant.
    EXPECT_EQ(rb.buffer.size(), rb.buffer.capacity());
    EXPECT_EQ(rb.begin.load(std::memory_order::relaxed), 0U);
    EXPECT_EQ(rb.end.load(std::memory_order::relaxed), 0U);
  } // destructor fires with lse == 0
  EXPECT_EQ(cb_lse, 0U);
}

// Pre-loading `out` with a large allocation causes try_take_full to steal it
// for the backing buffer, avoiding a reallocation on the next cycle.
void RecvBufferView_TryTakeFull_StealAllocation() {
  recv_buffer rb;
  setup_rb(rb, 64, 0, 0);
  const size_t cap = rb.buffer.capacity();
  setup_rb(rb, cap, 0, cap, 'B');

  // Give `out` a large allocation to steal.
  std::string out;
  no_zero::enlarge_to(out, 512);
  const size_t big_cap = out.capacity();
  EXPECT_GE(big_cap, 512U);

  recv_buffer_view v{rb, [](size_t, size_t) {}};
  std::string_view sv;
  EXPECT_TRUE(v.try_take_full(out, sv));

  // The internal buffer now holds the stolen large allocation.
  EXPECT_GE(rb.buffer.capacity(), big_cap);
  // `out` holds the data that was in the buffer.
  EXPECT_EQ(sv.size(), cap);
  EXPECT_EQ(sv[0], 'B');
}

void StreamConn_Lifecycle() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  const net_endpoint remote{ipv4_addr::loopback, 9999};
  {
    auto conn = stream_conn_ptr::adopt(loop, std::move(a), remote, {});
    // open_ is set in the state constructor before the post fires.
    EXPECT_TRUE(conn->is_open());
    EXPECT_EQ(conn->remote_endpoint(), remote);
    EXPECT_GE(loop->run_once(0), 0); // process posted do_open()
  }
  // destructor posted do_hangup(); process it, then verify loop is clean.
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(loop->run_once(0), 0);
}

void StreamConn_Receive() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data = [&](stream_conn&, recv_buffer_view v) {
        std::string_view av = v;
        received.assign(av);
        v.consume(av.size());
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // process posted do_open()

  const std::string msg{"hello"};
  auto msg_view = std::string_view{msg};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_GE(loop->run_once(0), 0); // dispatch EPOLLIN
  EXPECT_EQ(received, msg);
}

void StreamConn_SetRecvBufSize() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  // `recv_buf_size` is a target for future compactions; the actual per-read
  // limit may be larger when the allocator (or SSO) provides extra capacity.
  // Test that the getter/setter works and that all data arrives correctly.
  std::string received;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data =
              [&](stream_conn&, recv_buffer_view v) {
                std::string_view av = v;
                received.append(av);
                v.consume(av.size());
                return true;
              }},
      4);
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  EXPECT_EQ(conn->recv_buf_size(), 4U);

  auto first = std::string_view{"abcd1234"};
  ASSERT_TRUE(b.send(first) && first.empty());
  for (int i = 0; i < 4 && received.size() < 8; ++i)
    EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(received, "abcd1234");

  ASSERT_TRUE(conn->set_recv_buf_size(8));
  EXPECT_EQ(conn->recv_buf_size(), 8U);

  auto second = std::string_view{"ABCDEFGHijkl"};
  ASSERT_TRUE(b.send(second) && second.empty());
  for (int i = 0; i < 4 && received.size() < 20; ++i)
    EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(received, "abcd1234ABCDEFGHijkl");
}

void StreamConn_PeerClose() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool closed = false;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_close = [&](stream_conn&) {
        closed = true;
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // process posted do_open()

  ASSERT_TRUE(b.shutdown(SHUT_WR));

  EXPECT_GE(loop->run_once(0), 0); // dispatch readable event with EOF (HUP)

  EXPECT_TRUE(closed);

  EXPECT_TRUE(conn->is_open());
  EXPECT_FALSE(conn->can_read());
  EXPECT_TRUE(conn->can_write());

  EXPECT_TRUE(conn->send(std::string{"still-open"}));
  EXPECT_GE(loop->run_once(0), 0);
  std::string buf;
  no_zero::enlarge_to(buf, 32);
  ASSERT_TRUE(b.read(buf));
  EXPECT_EQ(buf, "still-open");

  EXPECT_TRUE(conn->close());
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_FALSE(conn->is_open());
}

// When the peer sends data and then half-closes before the receiver reads,
// the first `handle_readable` delivers the data via `on_data`. The subsequent
// EOF event enters `handle_read_eof` with a non-empty buffer and no live view,
// so it must dispatch `on_data` once more with the residual bytes (at which
// point `can_read()` is already false) before firing `on_close`.
void StreamConn_PeerClose_WithBufferedData() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  int data_count = 0;
  bool read_open_at_eof_dispatch = true;
  std::string received;
  bool closed = false;

  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data =
              [&](stream_conn& c, recv_buffer_view v) {
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
              [&](stream_conn&) {
                closed = true;
                return true;
              }});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  const std::string msg{"hello"};
  auto msg_view = std::string_view{msg};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());
  ASSERT_TRUE(b.shutdown(SHUT_WR)); // send EOF after data

  // First iteration: reads "hello", dispatches on_data (consumes "he").
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(data_count, 1);
  EXPECT_EQ(received, "he");
  EXPECT_FALSE(closed);

  // Second iteration: EOF arrives; `handle_read_eof` finds "llo" in the
  // buffer, dispatches on_data with residual bytes, then fires on_close.
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(data_count, 2);
  EXPECT_EQ(received, "hello");
  EXPECT_FALSE(read_open_at_eof_dispatch); // read side already closed
  EXPECT_TRUE(closed);
}

void StreamConn_Send() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted do_open()

  EXPECT_TRUE(conn->send(std::string{"world"}));
  // process posted enqueue() -> immediate ::write
  EXPECT_GE(loop->run_once(0), 0);

  // Data written by enqueue() is now in the kernel buffer.
  std::string buf;
  no_zero::enlarge_to(buf, 16);
  ASSERT_TRUE(b.read(buf));
  EXPECT_EQ(buf.size(), 5U);
  EXPECT_EQ(buf, "world");
}

void StreamConn_ManualClose() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool closed = false;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_close = [&](stream_conn&) {
        closed = true;
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // process posted do_open()

  EXPECT_TRUE(conn.close());
  EXPECT_GE(loop->run_once(0), 0); // process posted do_close() -> close_now()

  EXPECT_FALSE(conn->is_open());
  EXPECT_TRUE(closed);

  // Destructor posts a hangup; it must be idempotent after close().
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(loop->run_once(0), 0);
}

void StreamConn_DrainAfterBufferedSend() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  // Restrict the kernel send buffer so that a large write is partial.
  // The kernel may round up, but typically honors a small value closely
  // enough to force at least one EAGAIN before the full payload drains.
  constexpr int small_buf = 4096;
  EXPECT_TRUE(a.set_send_buffer_size(small_buf));

  // Payload larger than the send buffer to reliably exercise the EPOLLOUT
  // path. 256 KB is large enough on typical Linux systems.
  const std::string payload(256ULL * 1024ULL, 'x');

  int drain_count = 0;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_drain = [&](stream_conn&) {
        ++drain_count;
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // process posted do_open()

  EXPECT_TRUE(conn->send(std::string{payload})); // copy payload into send
  // Drain by reading from `b` and running the loop until all data arrives.
  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  while (received.size() < payload.size()) {
    EXPECT_GE(loop->run_once(0), 0);
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
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  int drain_count = 0;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_drain = [&](stream_conn&) {
        ++drain_count;
        return true;
      }});
  // `register_with_loop` arms `EPOLLOUT` for all non-listening sockets, so
  // the first writable event fires `on_drain` even with an empty send queue.
  EXPECT_GE(loop->run_once(0), 0); // register_with_loop + initial EPOLLOUT
  EXPECT_EQ(drain_count, 1);       // initial drain fired

  EXPECT_TRUE(conn->send(std::string{"hello"}));
  EXPECT_GE(loop->run_once(0), 0); // enqueue_send() + EPOLLOUT drain
  EXPECT_EQ(drain_count, 2);       // second drain after send queue empties

  std::string received;
  no_zero::enlarge_to(received, 16);
  ASSERT_TRUE(b.read(received));
  EXPECT_EQ(received, "hello");
}

void StreamConn_AsyncCbRead() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  stream_async_cb cb{conn.pointer()};
  ASSERT_TRUE(cb.read([&](recv_buffer_view v) {
    std::string_view av = v;
    received.assign(av);
    v.consume(av.size());
    return true;
  }));

  const std::string msg{"callback-read"};
  auto msg_view = std::string_view{msg};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_GE(loop->run_once(0), 0); // dispatch EPOLLIN -> inline callback

  EXPECT_EQ(received, msg);
}

void StreamConn_AsyncCbRead_PreservesEarlyData() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  const std::string msg{"early-callback-read"};
  auto msg_view = std::string_view{msg};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_EQ(loop->run_once(0),
      0); // read interest is disabled; data stays queued

  stream_async_cb cb{conn.pointer()};
  ASSERT_TRUE(cb.read([&](recv_buffer_view v) {
    std::string_view av = v;
    received.assign(av);
    v.consume(av.size());
    return true;
  }));

  EXPECT_GE(loop->run_once(0), 0); // enabling EPOLLIN surfaces buffered data
  EXPECT_EQ(received, msg);
}

void StreamConn_AsyncCbRead_DuplicateRejected() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  int callback_count = 0;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  stream_async_cb cb{conn.pointer()};
  ASSERT_TRUE(cb.read([&](recv_buffer_view v) {
    v.consume(std::string_view{v}.size());
    ++callback_count;
    return true;
  }));
  EXPECT_FALSE(cb.read([&](recv_buffer_view v) {
    v.consume(std::string_view{v}.size());
    ++callback_count;
    return true;
  }));

  const std::string msg{"one"};
  auto msg_view = std::string_view{msg};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_GE(loop->run_once(0), 0); // dispatch EPOLLIN -> one callback

  EXPECT_EQ(callback_count, 1);
}

void StreamConn_AsyncCbRead_PeerClose() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  // `stream_async_cb` fully takes over the handlers, so the persistent
  // `on_close` on `own_handlers_` does not fire while the `stream_async_cb`
  // is active. Close notification arrives via `stream_async_cb::on_close`,
  // which fires the pending read callback with an empty `recv_buffer_view`
  // (signaling EOF).
  std::string received{"sentinel"};
  int callback_count = 0;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  stream_async_cb cb{conn.pointer()};
  ASSERT_TRUE(cb.read([&](recv_buffer_view v) {
    std::string_view av = v;
    received.assign(av);
    v.consume(av.size());
    ++callback_count;
    return true;
  }));

  (void)b.close();
  // dispatch peer close -> on_close -> cb fires with ""
  EXPECT_GE(loop->run_once(0), 0);

  EXPECT_EQ(callback_count, 1);
  EXPECT_TRUE(received.empty());
  EXPECT_TRUE(cb.is_open());
  EXPECT_FALSE(cb.can_read());
  EXPECT_TRUE(cb.can_write());

  EXPECT_TRUE(conn.close());
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_FALSE(cb.is_open());
}

void StreamConn_AsyncCbWrite() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool completed{false};
  int callback_count = 0;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  stream_async_cb cb{conn.pointer()};
  ASSERT_TRUE(
      cb.write(std::string{"callback-write"}, [&](bool write_completed) {
        completed = write_completed;
        ++callback_count;
        return true;
      }));

  std::string received;
  no_zero::enlarge_to(received, 32);
  ASSERT_TRUE(b.read(received));
  EXPECT_EQ(received, "callback-write");
  EXPECT_TRUE(completed);
  EXPECT_EQ(callback_count, 1);
}

void StreamConn_AsyncCbWrite_Failure() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  // `stream_async_cb` fully takes over the handlers; the persistent `on_close`
  // on `own_handlers_` is silenced while it is active. The write-failure
  // callback fires synchronously when `enqueue_send` fails.
  bool completed = true;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  (void)b.close();

  stream_async_cb cb{conn.pointer()};
  ASSERT_FALSE(cb.write(std::string{"boom"}, [&](bool write_completed) {
    completed = write_completed;
    return completed;
  }));

  EXPECT_FALSE(completed);
  EXPECT_TRUE(cb.is_open());
  EXPECT_TRUE(cb.can_read());
  EXPECT_FALSE(cb.can_write());

  // process peer-close notification -> full close
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_FALSE(cb.is_open());
}

void StreamConn_ShutdownWrite() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data = [&](stream_conn&, recv_buffer_view v) {
        std::string_view av = v;
        received.assign(av);
        v.consume(av.size());
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  EXPECT_TRUE(conn->can_read());
  EXPECT_TRUE(conn->can_write());
  EXPECT_TRUE(conn->shutdown_write());
  EXPECT_TRUE(conn->is_open());
  EXPECT_TRUE(conn->can_read());
  EXPECT_FALSE(conn->can_write());
  {
    stream_async_cb cb{conn.pointer()};
    EXPECT_FALSE(cb.write(std::string{"nope"}, [&](bool) { return true; }));
  }

  const std::string msg{"inbound"};
  auto msg_view = std::string_view{msg};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_EQ(received, msg);
}

void StreamConn_ShutdownRead() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  int data_count = 0;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_data = [&](stream_conn&, recv_buffer_view v) {
        v.consume(std::string_view{v}.size());
        ++data_count;
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  EXPECT_TRUE(conn->can_read());
  EXPECT_TRUE(conn->can_write());
  ASSERT_TRUE(conn->shutdown_read());
  EXPECT_TRUE(conn->is_open());
  EXPECT_FALSE(conn->can_read());
  EXPECT_TRUE(conn->can_write());
  {
    stream_async_cb cb{conn.pointer()};
    EXPECT_FALSE(cb.read([&](recv_buffer_view) {
      ++data_count;
      return true;
    }));
  }

  EXPECT_TRUE(conn->send(std::string{"outbound"}));
  EXPECT_GE(loop->run_once(0), 0);

  std::string buf;
  no_zero::enlarge_to(buf, 32);
  ASSERT_TRUE(b.read(buf));
  EXPECT_EQ(buf, "outbound");
  EXPECT_EQ(data_count, 0);
}

void StreamConn_ShutdownBothCloses() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  ASSERT_TRUE(conn->shutdown_write());
  EXPECT_TRUE(conn->is_open());
  EXPECT_FALSE(conn->can_write());
  EXPECT_TRUE(conn->can_read());

  ASSERT_TRUE(conn->shutdown_read());
  EXPECT_FALSE(conn->is_open());
  EXPECT_FALSE(conn->can_read());
  EXPECT_FALSE(conn->can_write());
}

void StreamConn_AsyncCbWrite_DuplicateRejected() {
  epoll_loop_runner loop;
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  EXPECT_TRUE(a.set_send_buffer_size(small_buf));

  auto conn = stream_conn_ptr::adopt(loop.loop(), std::move(a), {}, {});
  stream_async_cb cb{conn.pointer()};

  const std::string payload(256ULL * 1024ULL, 'w');
  std::atomic<int> accepted{0};
  std::atomic<int> rejected{0};
  std::atomic<int> completions{0};
  notifiable<bool> completion{false};

  auto try_register = [&] {
    if (cb.write(std::string{payload}, [&](bool) {
          ++completions;
          completion.notify_one(true);
          return true;
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

  (void)b.close();
  ASSERT_TRUE(completion.wait_for_value(std::chrono::seconds{1}, true));

  EXPECT_EQ(completions, 1);
}

void StreamConn_GracefulClose() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  EXPECT_TRUE(a.set_send_buffer_size(small_buf));

  const std::string payload(64ULL * 1024ULL, 'z');

  bool closed = false;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_close = [&](stream_conn&) {
        closed = true;
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // process posted do_open()

  // Queue data then immediately request a close; the close must be deferred
  // until the send queue drains.
  EXPECT_TRUE(conn->send(std::string{payload})); // copy payload into send
  EXPECT_TRUE(conn->close());

  // Drain all data from `b` while running the loop.
  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  while (!closed) {
    EXPECT_GE(loop->run_once(0), 0);
    no_zero::enlarge_to(tmp, 4096);
    while (b.read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero::enlarge_to(tmp, 4096);
    }
  }

  EXPECT_EQ(received.size(), payload.size());
  EXPECT_EQ(received, payload);
  EXPECT_TRUE(closed);
  EXPECT_FALSE(conn->is_open());
}

void StreamConn_CloseThenDestructStaysGraceful() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  EXPECT_TRUE(a.set_send_buffer_size(small_buf));

  const std::string payload(64ULL * 1024ULL, 'g');

  bool closed = false;
  {
    auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
        {.on_close = [&](stream_conn&) {
          closed = true;
          return true;
        }});
    EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

    EXPECT_TRUE(conn->send(std::string{payload}));
    EXPECT_TRUE(conn->close());
  }

  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  for (int i = 0; i < 512 && !closed; ++i) {
    EXPECT_GE(loop->run_once(0), 0);
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

// With `coordination_policy::bilateral` set, `close()` shuts down writes after
// the send queue drains, then waits for the peer to close before firing
// `on_close`. Incoming data arriving during the drain is silently discarded.
void StreamConn_MutualClose() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool closed = false;
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
      {.on_close = [&](stream_conn&) {
        closed = true;
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  conn->set_coordination();
  EXPECT_TRUE(conn->coordination() == coordination_policy::bilateral);
  EXPECT_TRUE(conn->close());
  EXPECT_GE(loop->run_once(0), 0); // process do_close() -> do_finish_close()

  // After close() with bilateral coordination, conn shuts down its write side
  // but stays open waiting for the peer to close.
  EXPECT_TRUE(conn->is_open());
  EXPECT_FALSE(conn->can_write());
  EXPECT_FALSE(closed);

  // Send data from the peer; handle_drain_reads discards it.
  auto msg = std::string_view{"some data"};
  ASSERT_TRUE(b.send(msg) && msg.empty());
  EXPECT_GE(loop->run_once(0), 0); // handle_drain_reads discards data
  EXPECT_FALSE(closed);            // still waiting for peer to close

  // Peer closes: handle_drain_reads sees EOF and fires do_close_now ->
  // on_close.
  ASSERT_TRUE(b.close());
  EXPECT_GE(loop->run_once(0), 0);

  EXPECT_TRUE(closed);
  EXPECT_FALSE(conn->is_open());
}

// Verify that a listener created with `coordination_policy::bilateral`
// propagates the policy to every accepted connection, and that the accepted
// connection's bilateral handshake completes correctly end to end.
//
// The critical invariant under test: with `bilateral` coordination, the
// server's `on_close` does NOT fire after `conn.close()` alone -- it fires
// only after the peer (client) has also closed. Since the client is still open
// when we check, the absence of `on_close` is logically guaranteed, not
// timing-based.
void StreamConn_Listen_MutualClose() {
  epoll_loop_runner loop;

  // Set in `on_data` after the server calls `conn.close()`, so the test
  // thread knows the server has initiated its half-close.
  notifiable<bool> close_initiated{false};
  // Set in `on_close` once the server connection fully closes.
  notifiable<bool> server_closed{false};
  // The `coordination_policy` as seen on the accepted connection inside
  // `on_data` -- confirms the policy was copied from the listener.
  notifiable<coordination_policy> accepted_policy{
      coordination_policy::unlateral};

  auto listener = stream_conn_ptr::listen(loop.loop(),
      net_endpoint{ipv4_addr::loopback, 0},
      {.on_data =
              [&](stream_conn& conn, recv_buffer_view v) {
                accepted_policy.notify_one(conn.coordination());
                std::string_view av = v;
                v.consume(av.size());
                bool ok = conn.close();
                close_initiated.notify_one(true);
                return ok;
              },
          .on_close =
              [&](stream_conn&) {
                server_closed.notify_one(true);
                return true;
              }},
      coordination_policy::bilateral);
  ASSERT_TRUE(listener);

  // The listener itself must carry the policy.
  EXPECT_TRUE(listener->coordination() == coordination_policy::bilateral);

  const net_endpoint server_ep = listener->local_endpoint();
  ASSERT_TRUE(server_ep);

  // Connect and send a message to trigger `on_data` on the accepted
  // connection.
  auto client = stream_conn_ptr::connect(loop.loop(), server_ep, {});
  ASSERT_TRUE(client);
  ASSERT_TRUE(client->send(std::string{"ping"}));

  // Wait until the server has received data and called `conn.close()`.
  ASSERT_TRUE(close_initiated.wait_for_value(std::chrono::seconds{5}, true));

  // The accepted connection must have inherited the policy from the listener.
  EXPECT_TRUE(accepted_policy.get() == coordination_policy::bilateral);

  // The server shut down its write side but is waiting for the client to
  // close. The client has not closed yet, so `on_close` cannot have fired.
  EXPECT_FALSE(server_closed.get());

  // Client closes, unblocking the bilateral close. Server's `on_close` fires.
  ASSERT_TRUE(client->close());
  ASSERT_TRUE(server_closed.wait_for_value(std::chrono::seconds{5}, true));
}

void StreamConn_DestructorHangsUp() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  EXPECT_TRUE(a.set_send_buffer_size(small_buf));

  const std::string payload(256ULL * 1024ULL, 'q');

  bool closed = false;
  {
    auto conn = stream_conn_ptr::adopt(loop, std::move(a), {},
        {.on_close = [&](stream_conn&) {
          closed = true;
          return true;
        }});
    EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop
    EXPECT_TRUE(conn->send(std::string{payload}));
  }

  // drain posted enqueue_send() then posted hangup()
  EXPECT_GE(loop->run_once(0), 0);

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
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  bool done = false;

  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  stream_async_coro coro_conn{conn.pointer()};
  auto coro = [&]() -> loop_task {
    received = co_await coro_conn.read();
    done = true;
  };
  coro(); // starts eagerly; suspends at async_read (no data yet)

  const std::string msg{"hello"};
  auto msg_view = std::string_view{msg};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_GE(loop->run_once(0), 0); // dispatch EPOLLIN -> posts resume
  EXPECT_GE(loop->run_once(0), 0); // drain post queue -> coroutine resumes

  EXPECT_TRUE(done);
  EXPECT_EQ(received, msg);
}

void StreamConn_AsyncRead_PreservesEarlyData() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received;
  bool done = false;

  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  const std::string msg{"early-coroutine-read"};
  auto msg_view = std::string_view{msg};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_EQ(loop->run_once(0),
      0); // data remains in kernel until a read is armed

  stream_async_coro coro_conn{conn.pointer()};
  auto coro = [&]() -> loop_task {
    received = co_await coro_conn.read();
    done = true;
  };
  coro();

  EXPECT_GE(loop->run_once(0), 0); // dispatch buffered EPOLLIN
  EXPECT_GE(loop->run_once(0), 0); // drain posted resume

  EXPECT_TRUE(done);
  EXPECT_EQ(received, msg);
}

void StreamConn_AsyncRead_StopsBetweenCalls() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string first;
  std::string second;
  bool first_done = false;
  bool second_done = false;

  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  stream_async_coro coro_conn{conn.pointer()};

  auto first_coro = [&]() -> loop_task {
    first = co_await coro_conn.read();
    first_done = true;
  };
  first_coro();

  auto first_msg = std::string_view{"first"};
  ASSERT_TRUE(b.send(first_msg) && first_msg.empty());

  EXPECT_GE(loop->run_once(0), 0); // deliver first read
  EXPECT_GE(loop->run_once(0), 0); // resume first coroutine

  EXPECT_TRUE(first_done);
  EXPECT_EQ(first, "first");

  auto second_msg = std::string_view{"second"};
  ASSERT_TRUE(b.send(second_msg) && second_msg.empty());

  EXPECT_EQ(loop->run_once(0),
      0); // no waiter, so second chunk stays in kernel
  EXPECT_FALSE(second_done);

  auto second_coro = [&]() -> loop_task {
    second = co_await coro_conn.read();
    second_done = true;
  };
  second_coro();

  EXPECT_GE(loop->run_once(0), 0); // buffered kernel data is now delivered
  EXPECT_GE(loop->run_once(0), 0); // resume second coroutine

  EXPECT_TRUE(second_done);
  EXPECT_EQ(second, "second");
}

// Verify that `async_read` returns an empty string when the peer closes
// the connection before data arrives.
void StreamConn_AsyncRead_PeerClose() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  std::string received{"sentinel"};
  bool done = false;

  // `stream_async_coro` installs an `on_close` handler that replicates the
  // auto-graceful-close that `handle_read_eof` would otherwise initiate.
  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {}, {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  stream_async_coro coro_conn{conn.pointer()};
  auto coro = [&]() -> loop_task {
    received = co_await coro_conn.read();
    done = true;
  };
  coro(); // starts eagerly; suspends at async_read

  (void)b.close(); // trigger EPOLLHUP/EOF on `a`

  // dispatch EPOLLHUP -> notify_read_closed/on_close -> posts resume +
  // do_close
  EXPECT_GE(loop->run_once(0), 0);
  // drain post queue -> coroutine resumes, do_close runs
  EXPECT_GE(loop->run_once(0), 0);

  EXPECT_TRUE(done);
  EXPECT_TRUE(received.empty()); // close delivers empty data
  EXPECT_FALSE(conn->is_open()); // on_close initiated a graceful close
}

// Verify that `async_send` delivers bytes to the peer and suspends until
// the queue drains.
void StreamConn_AsyncSend() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  bool sent = false;

  auto conn = stream_conn_ptr::adopt(loop, std::move(a), {});
  EXPECT_GE(loop->run_once(0), 0); // process posted register_with_loop

  const std::string msg{"world"};

  stream_async_coro coro_conn{conn.pointer()};
  auto coro = [&]() -> loop_task {
    co_await coro_conn.write(std::string{msg});
    sent = true;
  };
  coro(); // starts eagerly; write completes via posted drain

  // If the write was synchronous, `sent` is already true after one
  // run_once (for the register post). Otherwise pump the loop to drain.
  for (int i = 0; i < 4 && !sent; ++i) EXPECT_GE(loop->run_once(0), 0);

  ASSERT_TRUE(sent);

  std::string buf;
  no_zero::enlarge_to(buf, 16);
  ASSERT_TRUE(b.read(buf));
  EXPECT_EQ(buf, msg);
}

// Verify that a client can connect to a local loopback listener and that the
// server echoes back whatever it receives, using only persistent callbacks.
void StreamConn_EchoServer() {
  epoll_loop_runner loop;

  // Bind a non-blocking listener to an OS-assigned loopback port.
  // Each accepted connection is self-owning and gets a copy of the listener's
  // handlers, so no external handle is needed.
  auto listener = stream_conn_ptr::listen(loop.loop(),
      net_endpoint{ipv4_addr::loopback, 0},
      {.on_data = [](stream_conn& conn, recv_buffer_view v) {
        std::string_view av = v;
        bool ok = conn.send(std::string{av});
        v.consume(av.size());
        return ok;
      }});
  ASSERT_TRUE(listener);

  // Sniff out the port from the listener socket so we can connect to it.
  const net_endpoint server_ep = listener->local_endpoint();
  ASSERT_TRUE(server_ep);

  // Connect to the server, send a message once the connection is established,
  // and accumulate the echo in `received`.
  constexpr std::string_view msg{"hello echo"};
  std::string received;
  notifiable<bool> done{false};
  stream_conn_ptr client_conn;

  client_conn = stream_conn_ptr::connect(loop.loop(), server_ep,
      {.on_data =
              [&](stream_conn&, recv_buffer_view v) {
                std::string_view av = v;
                received.append(av);
                v.consume(av.size());
                if (received.size() >= msg.size()) done.notify_one(true);
                return true;
              },
          .on_drain =
              [&, sent = false](stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return false;
                return conn.send(std::string{msg});
              }});
  ASSERT_TRUE(client_conn);

  ASSERT_TRUE(done.wait_for_value(std::chrono::seconds{5}, true));
  EXPECT_EQ(received, std::string{msg});
}

// `stream_conn_with_state` tests.

// Verify that `adopt()` creates a typed handle, that state is
// zero-initialized, and that it is mutable via the typed pointer.
void StreamConnWithState_Adopt() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  using conn_t = stream_conn_with_state<int>;
  auto conn = stream_conn_ptr_with<conn_t>::adopt(loop, std::move(a), {});
  ASSERT_TRUE(conn);
  EXPECT_EQ(conn->state(), 0);
  conn->state() = 42;
  EXPECT_EQ(conn->state(), 42);
}

// Verify that `from` correctly downcasts a `stream_conn&` inside a callback.
void StreamConnWithState_From() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  using conn_t = stream_conn_with_state<int>;
  int seen = -1;
  auto conn = stream_conn_ptr_with<conn_t>::adopt(loop, std::move(a), {},
      {.on_data = [&](stream_conn& c, recv_buffer_view v) {
        auto& typed = conn_t::from(c);
        typed.state() += 1;
        seen = typed.state();
        std::string_view av = v;
        v.consume(av.size());
        return true;
      }});
  EXPECT_GE(loop->run_once(0), 0); // register_with_loop

  conn->state() = 9;

  const std::string msg{"hi"};
  auto sv = std::string_view{msg};
  ASSERT_TRUE(b.send(sv) && sv.empty());
  EXPECT_GE(loop->run_once(0), 0); // EPOLLIN -> on_data

  EXPECT_EQ(seen, 10); // state was 9, incremented to 10 inside the callback
}

// Verify that connections accepted via listen() have the right concrete type
// and that each has an independent `TState`.
void StreamConnWithState_Listen() {
  epoll_loop_runner loop;

  using conn_t = stream_conn_with_state<int>;
  notifiable<int> received_state{-1};

  auto listener = stream_conn_ptr_with<conn_t>::listen(loop.loop(),
      net_endpoint{ipv4_addr::loopback, 0},
      {.on_data = [&](stream_conn& c, recv_buffer_view v) {
        auto& typed = conn_t::from(c);
        typed.state() += 1;
        std::string_view av = v;
        v.consume(av.size());
        received_state.notify_one(typed.state());
        return true;
      }});
  ASSERT_TRUE(listener);

  const net_endpoint server_ep = listener->local_endpoint();
  ASSERT_TRUE(server_ep);

  auto client = stream_conn_ptr::connect(loop.loop(), server_ep, {});
  ASSERT_TRUE(client);

  const std::string msg{"ping"};
  ASSERT_TRUE(client->send(std::string{msg}));

  // State starts at 0 and is incremented to 1 in the first on_data call.
  ASSERT_TRUE(received_state.wait_for_value(std::chrono::seconds{5}, 1));
}

// Verify that a stream_conn_ptr_with<Derived> is implicitly convertible to
// the untyped stream_conn_ptr and the resulting handle is usable.
void StreamConnPtr_Covariance() {
  auto loop = epoll_loop::make();
  auto this_is_the_loop_thread = loop->poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  using conn_t = stream_conn_with_state<int>;
  bool closed = false;
  stream_conn_ptr_with<conn_t> typed = stream_conn_ptr_with<conn_t>::adopt(
      loop, std::move(a), {}, {.on_close = [&](stream_conn&) {
        closed = true;
        return true;
      }});
  ASSERT_TRUE(typed);

  // Implicit upcast.
  stream_conn_ptr base = std::move(typed);
  ASSERT_TRUE(base);
  EXPECT_FALSE(typed); // ownership transferred

  EXPECT_GE(loop->run_once(0), 0); // register_with_loop
  EXPECT_TRUE(base->close());
  (void)b.close();
  EXPECT_GE(loop->run_once(0), 0);
  EXPECT_TRUE(closed);
}

// Verify that overriding accept_clone to return nullptr causes handle_listen
// to silently skip the connection (no on_data fired, drain loop continues).
void StreamConnWithState_AcceptClone_Nullptr() {
  struct rejecting_conn: stream_conn_with_state<int> {
    using stream_conn_with_state<int>::stream_conn_with_state;

  protected:
    [[nodiscard]] std::shared_ptr<stream_conn> accept_clone(net_socket&&,
        const net_endpoint&, stream_conn_handlers) const override {
      return nullptr;
    }
  };

  epoll_loop_runner loop;

  int data_calls = 0;
  auto listener = stream_conn_ptr_with<rejecting_conn>::listen(loop.loop(),
      net_endpoint{ipv4_addr::loopback, 0},
      {.on_data = [&](stream_conn&, recv_buffer_view v) {
        ++data_calls;
        std::string_view av = v;
        v.consume(av.size());
        return true;
      }});
  ASSERT_TRUE(listener);

  const net_endpoint server_ep = listener->local_endpoint();
  ASSERT_TRUE(server_ep);

  // Connect; the server drops the accepted socket immediately. Blocking on
  // `recv` provides reliable synchronization: once it returns empty (EOF),
  // the server has already processed and discarded the connection.
  auto client = stream_sync::connect(server_ep, std::chrono::seconds{5});
  ASSERT_TRUE(client);
  EXPECT_TRUE(client.recv().empty());

  EXPECT_EQ(data_calls, 0);
}

// Single complete frame delivered in one call.
void TerminatedTextParser_CompleteLine() {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv{"text\r\n"};
  std::string_view text;
  EXPECT_TRUE(p.parse(sv, text) == true);
  EXPECT_EQ(text, "text");
  EXPECT_TRUE(sv.empty()); // `sv` advanced past the frame
}

// Empty view: incomplete with zero bytes scanned.
void TerminatedTextParser_IncompleteEmpty() {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv;
  std::string_view text;
  EXPECT_TRUE(p.parse(sv, text) == std::nullopt);
  EXPECT_EQ(p.bytes_scanned(), 0U);
}

// Partial frame with no sentinel: incomplete, bytes_scanned updated.
void TerminatedTextParser_IncompletePartial() {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv{"text"};
  std::string_view text;
  EXPECT_TRUE(p.parse(sv, text) == std::nullopt);
  EXPECT_EQ(p.bytes_scanned(), 4U);
}

// Sentinel split across two calls: "\r" arrives first, "\n" in the next view.
void TerminatedTextParser_SplitSentinel() {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view text;

  // First call: only "\r" present -- not a complete sentinel.
  std::string_view sv1{"text\r"};
  EXPECT_TRUE(p.parse(sv1, text) == std::nullopt);
  EXPECT_EQ(p.bytes_scanned(), 5U);

  // Second call: the same bytes now extended with "\n".
  std::string_view sv2{"text\r\n"};
  EXPECT_TRUE(p.parse(sv2, text) == true);
  EXPECT_EQ(text, "text");
  EXPECT_TRUE(sv2.empty());
}

// Two frames in the same view: parse twice with reset() between.
void TerminatedTextParser_MultipleFrames() {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv{"line1\r\nline2\r\n"};
  std::string_view text;

  EXPECT_TRUE(p.parse(sv, text) == true);
  EXPECT_EQ(text, "line1");
  p.reset();

  EXPECT_TRUE(p.parse(sv, text) == true);
  EXPECT_EQ(text, "line2");
  EXPECT_TRUE(sv.empty());
}

// Bare sentinel with no preceding text: complete with empty text.
void TerminatedTextParser_EmptyLine() {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view sv{"\r\n"};
  std::string_view text;
  EXPECT_TRUE(p.parse(sv, text) == true);
  EXPECT_TRUE(text.empty());
  EXPECT_TRUE(sv.empty());
}

// Exceeding max_length with no sentinel present returns false.
void TerminatedTextParser_TooLong() {
  terminated_text_parser::state s{"\r\n", 8};
  terminated_text_parser p{s};
  std::string_view sv{"123456789"}; // 9 bytes, no sentinel
  std::string_view text;
  EXPECT_TRUE(p.parse(sv, text) == false);
}

// Sentinel found after max_length bytes returns false; input is not modified.
// A frame of exactly max_length bytes succeeds.
void TerminatedTextParser_TooLong_WithSentinel() {
  // 9 bytes before "\r\n": over the limit of 8.
  {
    terminated_text_parser::state s{"\r\n", 8};
    terminated_text_parser p{s};
    std::string_view sv{"123456789\r\n"};
    std::string_view text;
    EXPECT_TRUE(p.parse(sv, text) == false);
    EXPECT_EQ(sv, "123456789\r\n"); // input not modified
  }
  // Exactly 8 bytes before "\r\n": at the limit, succeeds.
  {
    terminated_text_parser::state s{"\r\n", 8};
    terminated_text_parser p{s};
    std::string_view sv{"12345678\r\n"};
    std::string_view text;
    EXPECT_TRUE(p.parse(sv, text) == true);
    EXPECT_EQ(text, "12345678");
  }
}

// With max_length == 0, the same input returns incomplete (no limit enforced).
void TerminatedTextParser_NoLimit() {
  terminated_text_parser::state s{"\r\n", 0};
  terminated_text_parser p{s};
  std::string_view sv{"123456789"};
  std::string_view text;
  EXPECT_TRUE(p.parse(sv, text) == std::nullopt);
}

// Custom sentinel ":" extracts the text up to the first colon.
void TerminatedTextParser_CustomSentinel() {
  terminated_text_parser::state s{":"};
  terminated_text_parser p{s};
  std::string_view sv{"Content-Type: text/html"};
  std::string_view text;
  EXPECT_TRUE(p.parse(sv, text) == true);
  EXPECT_EQ(text, "Content-Type");
  EXPECT_EQ(sv, " text/html");
}

// After complete + reset(), the parser correctly handles a second frame.
void TerminatedTextParser_Reset() {
  terminated_text_parser::state s{"\r\n"};
  terminated_text_parser p{s};
  std::string_view text;

  std::string_view sv1{"first\r\n"};
  EXPECT_TRUE(p.parse(sv1, text) == true);
  EXPECT_EQ(text, "first");
  p.reset();
  EXPECT_EQ(p.bytes_scanned(), 0U);

  // Confirm state is clean: a fresh incomplete call should update
  // bytes_scanned.
  std::string_view sv2{"second"};
  EXPECT_TRUE(p.parse(sv2, text) == std::nullopt);
  EXPECT_EQ(p.bytes_scanned(), 6U);
}

// `stream_sync` tests.

// Verify that connecting to an invalid endpoint returns a falsy `stream_sync`.
void StreamSync_ConnectFail() {
  // An empty endpoint has ss_family == AF_UNSPEC; `socket(2)` will fail and
  // the returned connection will be closed.
  auto conn = stream_sync::connect(net_endpoint{});
  EXPECT_FALSE(conn);
  EXPECT_FALSE(conn.is_open());
}

// Helper: start a loopback echo server on an OS-assigned port and return its
// endpoint. The caller must keep `listener` alive for the test duration.
static net_endpoint
start_echo_server(epoll_loop_runner& loop, stream_conn_ptr& listener) {
  listener = stream_conn_ptr::listen(loop.loop(),
      net_endpoint{ipv4_addr::loopback, 0},
      {.on_data = [](stream_conn& conn, recv_buffer_view v) {
        std::string_view av = v;
        bool ok = conn.send(std::string{av});
        v.consume(av.size());
        return ok;
      }});
  if (!listener) return {};
  return listener->local_endpoint();
}

// Verify basic send and recv_exact against an echo server.
void StreamSync_SendRecv() {
  epoll_loop_runner loop;
  stream_conn_ptr listener;
  const auto ep = start_echo_server(loop, listener);
  ASSERT_TRUE(ep);

  auto conn = stream_sync::connect(ep, std::chrono::seconds{5});
  ASSERT_TRUE(conn);
  EXPECT_TRUE(conn.send("hello"));
  auto got = conn.recv_exact(5);
  EXPECT_EQ(got, "hello");
}

// Verify that recv_until returns through the delimiter and leaves trailing
// bytes in the internal buffer for the next recv call.
void StreamSync_RecvUntil() {
  epoll_loop_runner loop;
  stream_conn_ptr listener;
  const auto ep = start_echo_server(loop, listener);
  ASSERT_TRUE(ep);

  auto conn = stream_sync::connect(ep, std::chrono::seconds{5});
  ASSERT_TRUE(conn);
  EXPECT_TRUE(conn.send("line1\r\nextra"));

  auto line = conn.recv_until("\r\n");
  EXPECT_EQ(line, "line1\r\n");

  // "extra" was already buffered; recv_exact should not block.
  auto tail = conn.recv_exact(5);
  EXPECT_EQ(tail, "extra");
}

// Verify that recv returns nullopt when the peer closes the connection.
void StreamSync_PeerClose() {
  epoll_loop_runner loop;
  stream_conn_ptr listener;
  // Server echoes nothing; it closes as soon as data arrives.
  listener = stream_conn_ptr::listen(loop.loop(),
      net_endpoint{ipv4_addr::loopback, 0},
      {.on_data = [](stream_conn& conn, recv_buffer_view v) {
        v.consume(std::string_view{v}.size());
        return conn.close();
      }});
  ASSERT_TRUE(listener);
  const auto ep = listener->local_endpoint();

  auto conn = stream_sync::connect(ep, std::chrono::seconds{5});
  ASSERT_TRUE(conn);
  EXPECT_TRUE(conn.send("bye"));
  // Server closes; recv should return empty once EOF is detected.
  EXPECT_TRUE(conn.recv().empty());
}

// ---------------------------------------------------------------------------
// io_uring loop and stream connection tests
// ---------------------------------------------------------------------------

// Minimal `iou_io_conn` that counts how many times each virtual is called.
struct iou_counting_conn: iou_io_conn {
  using iou_io_conn::iou_io_conn;

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

void IouLoop_Lifecycle() {
  iouring_loop loop;
  auto this_is_the_loop_thread = loop.poll_thread_scope();
  EXPECT_EQ(loop.run_once(0), 0);
}

void IouLoop_Post() {
  iouring_loop loop;
  auto this_is_the_loop_thread = loop.poll_thread_scope();

  int fired = 0;
  EXPECT_TRUE(loop.post([&] {
    ++fired;
    return true;
  }));

  // Post callbacks run at the top of the next `run_once`, before the ring
  // wait, even with no I/O events.
  EXPECT_GE(loop.run_once(0), 0);
  EXPECT_EQ(fired, 1);
}

// `register_socket` dispatches `on_readable` via virtual `iou_io_conn`
// override; `unregister_socket` stops further dispatch. Double-register and
// double-unregister both return false.
void IouLoop_RegisterUnregister() {
  iouring_loop loop;
  auto this_is_the_loop_thread = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<iou_counting_conn>(std::move(a));
  ASSERT_TRUE(loop.register_socket(conn));

  // Double-register must fail.
  EXPECT_FALSE(loop.register_socket(conn));

  auto msg_view = std::string_view{"hi"};
  ASSERT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_GE(loop.run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_EQ(conn->writable, 0);
  EXPECT_EQ(conn->error, 0);

  // Drain the data so the fd is no longer readable.
  std::string buf(8, '\0');
  (void)conn->sock().read(buf);

  ASSERT_TRUE(loop.unregister_socket(conn->sock()));
  EXPECT_FALSE(loop.unregister_socket(conn->sock())); // already removed

  // No events after unregistering.
  (void)loop.run_once(0); // flush the cancel SQE
  EXPECT_EQ(conn->readable, 1);
}

// `enable_writes(true)` arms `EPOLLOUT`; on a fresh socketpair the kernel
// fires it immediately because the send buffer has space.
void IouLoop_SetWritable() {
  iouring_loop loop;
  auto this_is_the_loop_thread = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<iou_counting_conn>(std::move(a));
  ASSERT_TRUE(loop.register_socket(conn, /*readable=*/true,
      /*writable=*/false));

  // `EPOLLOUT` not armed; no writable event.
  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->writable, 0);

  ASSERT_TRUE(loop.enable_writes(*conn, true));
  EXPECT_GE(loop.run_once(0), 0);
  EXPECT_GE(conn->writable, 1);

  // Disarm and confirm no further writable events.
  ASSERT_TRUE(loop.enable_writes(*conn, false));
  const int w = conn->writable;
  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->writable, w);
}

void IouLoop_SetReadable() {
  iouring_loop loop;
  auto this_is_the_loop_thread = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  // Register with neither EPOLLIN nor EPOLLOUT initially.
  auto conn = std::make_shared<iou_counting_conn>(std::move(a));
  ASSERT_TRUE(loop.register_socket(conn, /*readable=*/false,
      /*writable=*/false));

  auto first = std::string_view{"hi"};
  ASSERT_TRUE(b.send(first) && first.empty());

  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->readable, 0);
  EXPECT_EQ(conn->writable, 0);

  ASSERT_TRUE(loop.enable_reads(*conn, true));
  EXPECT_GE(loop.run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_EQ(conn->writable, 0);

  std::string buf(8, '\0');
  (void)conn->sock().read(buf);

  ASSERT_TRUE(loop.enable_reads(*conn, false));
  ASSERT_TRUE(loop.enable_writes(*conn, true));
  EXPECT_GE(loop.run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_GE(conn->writable, 1);
}

// When `EPOLLERR`/`EPOLLHUP` fires alongside `EPOLLOUT`, `on_error` is
// called but `on_writable` is skipped (same dispatch ordering as epoll).
void IouLoop_ErrorSkipsWritable() {
  iouring_loop loop;
  auto this_is_the_loop_thread = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<iou_counting_conn>(std::move(a));
  ASSERT_TRUE(loop.register_socket(conn, /*readable=*/true,
      /*writable=*/true)); // arm EPOLLOUT

  (void)b.close(); // triggers EPOLLHUP on `a`

  EXPECT_GE(loop.run_once(0), 0);
  EXPECT_EQ(conn->error, 1);
  EXPECT_EQ(conn->writable, 0); // must not fire alongside error/hup
}

// `iou_stream_conn` echo server test using an ANS socket to avoid
// the EPERM issue with AF_INET sockets in restricted environments.
void IouStreamConn_EchoServer() {
  iouring_loop_runner loop;

  auto listener = iou_stream_conn_ptr::listen(loop,
      net_endpoint{"@corvid_iou_echo_test"},
      {.on_data = [](iou_stream_conn& conn, recv_buffer_view v) {
        std::string_view av = v;
        bool ok = conn.send(std::string{av});
        v.consume(av.size());
        return ok;
      }});
  ASSERT_TRUE(listener);

  const net_endpoint server_ep = listener->local_endpoint();
  ASSERT_TRUE(server_ep);

  constexpr std::string_view msg{"hello iou echo"};
  std::string received;
  notifiable<bool> done{false};
  iou_stream_conn_ptr client_conn;

  client_conn = iou_stream_conn_ptr::connect(loop, server_ep,
      {.on_data =
              [&](iou_stream_conn&, recv_buffer_view v) {
                std::string_view av = v;
                received.append(av);
                v.consume(av.size());
                if (received.size() >= msg.size()) done.notify_one(true);
                return true;
              },
          .on_drain =
              [&, sent = false](iou_stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return false;
                return conn.send(std::string{msg});
              }});
  ASSERT_TRUE(client_conn);
  ASSERT_TRUE(done.wait_for_value(std::chrono::seconds{5}, true));
  EXPECT_EQ(received, std::string{msg});
}

// `iou_stream_conn_with_state` carries per-connection typed state.
void IouStreamConnWithState_Adopt() {
  iouring_loop loop;
  auto this_is_the_loop_thread = loop.poll_thread_scope();
  auto [a, b] = make_nb_sockpair();

  using conn_t = iou_stream_conn_with_state<int>;
  auto conn = iou_stream_conn_ptr_with<conn_t>::adopt(loop, std::move(a), {});
  ASSERT_TRUE(conn);
  EXPECT_EQ(conn->state(), 0);
  conn->state() = 42;
  EXPECT_EQ(conn->state(), 42);
}

// ---------------------------------------------------------------------------
// http_server tests
// ---------------------------------------------------------------------------

// Verify that the write timeout fires when the client stops reading.
//
// The client requests a 10 MB response but never reads, filling the kernel
// receive buffer and stalling the server's send path. The server should hang
// up the connection after the write timeout expires.
void HttpServer_WriteTimeout() {
  // Use a short write timeout so the test completes quickly. The timing
  // wheel has 100 ms precision, so allow generously for scheduling overhead.
  constexpr auto kWriteTimeout = 500ms;

  epoll_loop_runner loop;
  timing_wheel_runner wheel;

  auto server = http_server::create(net_endpoint{ipv4_addr::loopback, 0},
      loop.loop(), wheel.wheel(),
      /*request_timeout=*/30s,
      /*write_timeout=*/kWriteTimeout);
  ASSERT_TRUE(server);

  const auto ep = server->local_endpoint();
  ASSERT_TRUE(ep);

  // Connect a client that sends the request but never reads the response.
  // Without an `on_data` handler, `EPOLLIN` is not armed on the client
  // connection, so incoming bytes accumulate in the kernel receive buffer.
  // Once that buffer fills, TCP flow control prevents the server from
  // writing, stalling the drain and triggering the write timeout.
  notifiable<bool> closed{false};
  auto client = stream_conn_ptr::connect(loop.loop(), ep,
      {.on_drain =
              [sent = false](stream_conn& conn) mutable {
                if (std::exchange(sent, true)) return true;
                return conn.send("GET /10000000\r\n");
              },
          .on_close =
              [&closed](stream_conn&) {
                closed.notify_one(true);
                return true;
              }});
  ASSERT_TRUE(client);

  // Allow 10x the write timeout for timing-wheel jitter and system overhead.
  ASSERT_TRUE(closed.wait_for_value(kWriteTimeout * 10, true));
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
    IoLoop_IsLoopThreadIsPerLoop, IoLoop_PostAndWait_StopRace,
    RecvBuffer_Compact_NoActiveBytes, RecvBuffer_Compact_MustCompact,
    RecvBuffer_Compact_WorthIt, RecvBuffer_Compact_SkipsUnnecessaryMove,
    RecvBuffer_Compact_GrowOnRequest, RecvBuffer_Compact_GrowToMinCapacity,
    RecvBuffer_Compact_Shrink, RecvBuffer_Compact_ShrinkSkippedIfActiveWontFit,
    RecvBuffer_Compact_NoResizeWhenTargetFits, RecvBufferView_UpdateActiveView,
    RecvBufferView_MoveSemantics, RecvBufferView_TryTakeFull_Fail,
    RecvBufferView_TryTakeFull_Success,
    RecvBufferView_TryTakeFull_StealAllocation, StreamConn_Lifecycle,
    StreamConn_Receive, StreamConn_SetRecvBufSize, StreamConn_PeerClose,
    StreamConn_PeerClose_WithBufferedData, StreamConn_Send,
    StreamConn_ManualClose, StreamConn_DrainAfterBufferedSend,
    StreamConn_DrainAfterImmediateSend, StreamConn_AsyncCbRead,
    StreamConn_AsyncCbRead_PreservesEarlyData,
    StreamConn_AsyncCbRead_DuplicateRejected, StreamConn_AsyncCbRead_PeerClose,
    StreamConn_AsyncCbWrite, StreamConn_AsyncCbWrite_Failure,
    StreamConn_AsyncCbWrite_DuplicateRejected, StreamConn_ShutdownWrite,
    StreamConn_ShutdownRead, StreamConn_ShutdownBothCloses,
    StreamConn_GracefulClose, StreamConn_CloseThenDestructStaysGraceful,
    StreamConn_MutualClose, StreamConn_Listen_MutualClose,
    StreamConn_DestructorHangsUp, LoopTask_FireAndForget, StreamConn_AsyncRead,
    StreamConn_AsyncRead_PreservesEarlyData,
    StreamConn_AsyncRead_StopsBetweenCalls, StreamConn_AsyncRead_PeerClose,
    StreamConn_AsyncSend, StreamConn_EchoServer, StreamConnWithState_Adopt,
    StreamConnWithState_From, StreamConnWithState_Listen,
    StreamConnPtr_Covariance, StreamConnWithState_AcceptClone_Nullptr,
    TerminatedTextParser_CompleteLine, TerminatedTextParser_IncompleteEmpty,
    TerminatedTextParser_IncompletePartial, TerminatedTextParser_SplitSentinel,
    TerminatedTextParser_MultipleFrames, TerminatedTextParser_EmptyLine,
    TerminatedTextParser_TooLong, TerminatedTextParser_TooLong_WithSentinel,
    TerminatedTextParser_NoLimit, TerminatedTextParser_CustomSentinel,
    TerminatedTextParser_Reset, StreamSync_ConnectFail, StreamSync_SendRecv,
    StreamSync_RecvUntil, StreamSync_PeerClose, IouLoop_Lifecycle,
    IouLoop_Post, IouLoop_RegisterUnregister, IouLoop_SetWritable,
    IouLoop_SetReadable, IouLoop_ErrorSkipsWritable, IouStreamConn_EchoServer,
    IouStreamConnWithState_Adopt, HttpServer_WriteTimeout);

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
