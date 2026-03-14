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
#include "minitest.h"

using namespace corvid;

// Exposes `ip_socket`'s protected constructors for testing.
struct test_socket: ip_socket {
  test_socket() = default;
  explicit test_socket(handle_t h) : ip_socket(h) {}
  test_socket(int domain, int type, int protocol)
      : ip_socket(ip_socket::make_ip_socket(domain, type, protocol)) {}
};

// NOLINTBEGIN(readability-function-cognitive-complexity)

void Ipv4Addr_Construction() {
  // Default construction yields the "any" address.
  if (true) {
    ipv4_addr a;
    EXPECT_TRUE(a.is_any());
    EXPECT_EQ(a.to_uint32(), 0U);
  }

  // Named factory: any().
  if (true) {
    auto a = ipv4_addr::any();
    EXPECT_TRUE(a.is_any());
    EXPECT_EQ(a.to_uint32(), 0U);
  }

  // Named factory: loopback().
  if (true) {
    auto a = ipv4_addr::loopback();
    EXPECT_TRUE(a.is_loopback());
    EXPECT_EQ(a.to_uint32(), 0x7f000001U);
  }

  // Named factory: broadcast().
  if (true) {
    auto a = ipv4_addr::broadcast();
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
    EXPECT_TRUE(ipv4_addr::broadcast().is_broadcast());
    EXPECT_FALSE(ipv4_addr(255, 255, 255, 254).is_broadcast());
  }

  // is_any().
  if (true) {
    EXPECT_TRUE(ipv4_addr::any().is_any());
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
  EXPECT_EQ(ipv4_addr::any().to_string(), "0.0.0.0");
  EXPECT_EQ(ipv4_addr::loopback().to_string(), "127.0.0.1");
  EXPECT_EQ(ipv4_addr::broadcast().to_string(), "255.255.255.255");
  EXPECT_EQ(ipv4_addr(192, 168, 1, 100).to_string(), "192.168.1.100");

  // Round-trip: parse then format.
  auto addr = ipv4_addr::parse("10.20.30.40");
  EXPECT_TRUE(addr.has_value());
  EXPECT_EQ(addr->to_string(), "10.20.30.40");
}

void Ipv4Addr_PosixInterop() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  ipv4_addr a{192, 168, 1, 1};

  // Convert to `in_addr` and back.
  in_addr raw = a.to_in_addr();
  ipv4_addr b{raw};
  EXPECT_EQ(a, b);

  // Verify network byte order: 192.168.1.1 = 0xc0a80101 in host order,
  // so `s_addr` should be 0x0101a8c0 on a little-endian host.
  EXPECT_EQ(ntohl(raw.s_addr), a.to_uint32());
#endif
}

void Ipv6Addr_Construction() {
  if (true) {
    ipv6_addr a;
    EXPECT_TRUE(a.is_any());
    EXPECT_EQ(a.to_string(), "::");
  }

  if (true) {
    auto a = ipv6_addr::any();
    EXPECT_TRUE(a.is_any());
  }

  if (true) {
    auto a = ipv6_addr::loopback();
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
    EXPECT_TRUE(f.has_value());
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
    EXPECT_TRUE(ipv6_addr::loopback().is_loopback());
    EXPECT_FALSE(ipv6_addr::any().is_loopback());
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
  EXPECT_EQ(ipv6_addr::any().to_string(), "::");
  EXPECT_EQ(ipv6_addr::loopback().to_string(), "::1");
  EXPECT_EQ(ipv6_addr(0x2001, 0xdb8, 0, 0, 1, 0, 0, 1).to_string(),
      "2001:db8::1:0:0:1");
  EXPECT_EQ(ipv6_addr(0x2001, 0xdb8, 0, 1, 0, 0, 0, 1).to_string(),
      "2001:db8:0:1::1");

  auto addr = ipv6_addr::parse("2001:db8::abcd");
  EXPECT_TRUE(addr.has_value());
  EXPECT_EQ(addr->to_string(), "2001:db8::abcd");
}

void Ipv6Addr_PosixInterop() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  ipv6_addr a{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1};

  in6_addr raw = a.to_in6_addr();
  ipv6_addr b{raw};
  EXPECT_EQ(a, b);
  EXPECT_EQ(raw.s6_addr[0], 0x20U);
  EXPECT_EQ(raw.s6_addr[1], 0x01U);
  EXPECT_EQ(raw.s6_addr[15], 0x01U);
#endif
}

void IpEndpoint_Construction() {
  if (true) {
    ip_endpoint ep;
    EXPECT_FALSE(ep.is_valid());
    EXPECT_FALSE(ep.is_v4());
    EXPECT_FALSE(ep.is_v6());
    EXPECT_EQ(ep.to_string(), "(invalid)");
  }

  if (true) {
    ip_endpoint ep{ipv4_addr(127, 0, 0, 1), 80};
    EXPECT_TRUE(ep.is_v4());
    EXPECT_EQ(ep.port(), 80U);
    EXPECT_EQ(ep.v4()->to_string(), "127.0.0.1");
  }

  if (true) {
    ip_endpoint ep{ipv6_addr::loopback(), 443};
    EXPECT_TRUE(ep.is_v6());
    EXPECT_EQ(ep.port(), 443U);
    EXPECT_EQ(ep.v6()->to_string(), "::1");
  }
}

void IpEndpoint_Parse() {
  if (true) {
    auto a = ip_endpoint::parse("192.168.1.10:8080");
    EXPECT_TRUE(a.has_value());
    EXPECT_TRUE(a->is_v4());
    EXPECT_EQ(a->port(), 8080U);
    EXPECT_EQ(a->v4()->to_string(), "192.168.1.10");

    auto b = ip_endpoint::parse("[2001:db8::1]:443");
    EXPECT_TRUE(b.has_value());
    EXPECT_TRUE(b->is_v6());
    EXPECT_EQ(b->port(), 443U);
    EXPECT_EQ(b->v6()->to_string(), "2001:db8::1");
  }

  if (true) {
    EXPECT_FALSE(ip_endpoint::parse("").has_value());
    EXPECT_FALSE(ip_endpoint::parse("127.0.0.1").has_value());
    EXPECT_FALSE(ip_endpoint::parse("127.0.0.1:").has_value());
    EXPECT_FALSE(ip_endpoint::parse("127.0.0.1:99999").has_value());
    EXPECT_FALSE(ip_endpoint::parse("2001:db8::1:443").has_value());
    EXPECT_FALSE(ip_endpoint::parse("[2001:db8::1]").has_value());
    EXPECT_FALSE(ip_endpoint::parse("[2001:db8::1]:").has_value());
    EXPECT_FALSE(ip_endpoint::parse("[2001:db8::1]:70000").has_value());
  }
}

void IpEndpoint_Comparison() {
  ip_endpoint a{ipv4_addr(10, 0, 0, 1), 80};
  ip_endpoint b{ipv4_addr(10, 0, 0, 1), 81};
  ip_endpoint c{ipv4_addr(10, 0, 0, 1), 80};

  EXPECT_TRUE(a == c);
  EXPECT_FALSE(a == b);
  EXPECT_TRUE(a < b);
}

void IpEndpoint_Formatting() {
  auto v4 = ip_endpoint{ipv4_addr(127, 0, 0, 1), 80};
  auto v6 = ip_endpoint{ipv6_addr::loopback(), 443};
  EXPECT_EQ(v4.to_string(), "127.0.0.1:80");
  EXPECT_EQ(v6.to_string(), "[::1]:443");
}

void IpEndpoint_PosixInterop() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  if (true) {
    ip_endpoint ep{ipv4_addr(192, 168, 1, 2), 1234};
    auto raw = ep.to_sockaddr_in();
    ip_endpoint roundtrip{raw};
    EXPECT_EQ(roundtrip, ep);

    auto storage = ep.to_sockaddr_storage();
    auto* as_v4 = reinterpret_cast<const sockaddr_in*>(&storage);
    EXPECT_EQ(as_v4->sin_family, AF_INET);
    EXPECT_EQ(ntohs(as_v4->sin_port), 1234U);
  }

  if (true) {
    ip_endpoint ep{ipv6_addr(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1), 4321};
    auto raw = ep.to_sockaddr_in6();
    ip_endpoint roundtrip{raw};
    EXPECT_EQ(roundtrip, ep);

    auto storage = ep.to_sockaddr_storage();
    auto* as_v6 = reinterpret_cast<const sockaddr_in6*>(&storage);
    EXPECT_EQ(as_v6->sin6_family, AF_INET6);
    EXPECT_EQ(ntohs(as_v6->sin6_port), 4321U);
  }
#endif
}

void IpSocket_Lifecycle() {
  // Default-constructed socket is invalid.
  if (true) {
    test_socket s;
    EXPECT_FALSE(s.is_open());
    EXPECT_FALSE(static_cast<bool>(s));
    EXPECT_EQ(s.file().handle(), ip_socket::invalid_handle);
    EXPECT_FALSE(s.close());
  }

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // A real socket is open; closing it twice is idempotent.
  if (true) {
    test_socket s{AF_INET, SOCK_STREAM, 0};
    EXPECT_TRUE(s.is_open());
    EXPECT_TRUE(static_cast<bool>(s));
    EXPECT_NE(s.file().handle(), ip_socket::invalid_handle);
    EXPECT_TRUE(s.close());
    EXPECT_FALSE(s.is_open());
    EXPECT_FALSE(s.close());
  }

  // Destructor closes an open socket (no crash or leak).
  if (true) { test_socket s{AF_INET, SOCK_STREAM, 0}; }
#endif
}

void IpSocket_Move() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    test_socket a{AF_INET, SOCK_STREAM, 0};
    const auto h = a.file().handle();
    test_socket b{std::move(a)};
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.file().handle(), h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    test_socket a{AF_INET, SOCK_STREAM, 0};
    test_socket b{AF_INET, SOCK_STREAM, 0};
    const auto h = a.file().handle();
    b = std::move(a);
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.file().handle(), h);
  }

  // Self-assignment is a no-op.
  if (true) {
    test_socket a{AF_INET, SOCK_STREAM, 0};
    const auto h = a.file().handle();
    // Route through a pointer to defeat -Wself-move while still exercising
    // the self-assignment path.
    auto* p = &a;
    a = std::move(*p);
    EXPECT_TRUE(a.is_open());
    EXPECT_EQ(a.file().handle(), h);
  }
#endif
}

void IpSocket_Release() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // `release()` yields the handle without closing it; socket becomes invalid.
  if (true) {
    test_socket s{AF_INET, SOCK_STREAM, 0};
    const auto h = s.file().release();
    EXPECT_NE(h, ip_socket::invalid_handle);
    EXPECT_FALSE(s.is_open());
    ::close(h);
  }
#endif
}

void IpSocket_Options() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Named option helpers round-trip through `get_option`.
  if (true) {
    test_socket s{AF_INET, SOCK_STREAM, 0};

    EXPECT_TRUE(s.set_reuse_addr(true));
    auto v = s.get_option<int>(SOL_SOCKET, SO_REUSEADDR);
    EXPECT_TRUE(v.has_value());
    EXPECT_NE(*v, 0);

    EXPECT_TRUE(s.set_reuse_addr(false));
    v = s.get_option<int>(SOL_SOCKET, SO_REUSEADDR);
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, 0);

    EXPECT_TRUE(s.set_reuse_port(true));
    EXPECT_TRUE(s.set_keepalive(true));
    EXPECT_TRUE(s.set_nodelay(true));
  }

  // Buffer size helpers: kernel may round up, so just verify >= requested.
  if (true) {
    test_socket s{AF_INET, SOCK_STREAM, 0};
    EXPECT_TRUE(s.set_recv_buffer_size(65536));
    EXPECT_TRUE(s.set_send_buffer_size(65536));
    auto r = s.get_option<int>(SOL_SOCKET, SO_RCVBUF);
    EXPECT_TRUE(r.has_value());
    EXPECT_GE(*r, 65536);
    auto t = s.get_option<int>(SOL_SOCKET, SO_SNDBUF);
    EXPECT_TRUE(t.has_value());
    EXPECT_GE(*t, 65536);
  }
#endif
}

void IpSocket_Nonblocking() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  if (true) {
    test_socket s{AF_INET, SOCK_STREAM, 0};

    EXPECT_TRUE(s.file().set_nonblocking(true));
    EXPECT_TRUE(s.file().get_flags() & O_NONBLOCK);

    EXPECT_TRUE(s.file().set_nonblocking(false));
    EXPECT_FALSE(s.file().get_flags() & O_NONBLOCK);
  }
#endif
}

void DnsResolve_NumericIPv4() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Numeric IPv4 addresses are resolved without a DNS lookup.
  auto result = dns_resolver::find_all("127.0.0.1", 80);
  EXPECT_FALSE(result.empty());
  bool found = false;
  for (const auto& ep : result) {
    if (ep.is_v4() && ep.v4()->is_loopback() && ep.port() == 80) found = true;
  }
  EXPECT_TRUE(found);
#endif
}

void DnsResolve_NumericIPv6() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Numeric IPv6 addresses are resolved without a DNS lookup.
  auto result = dns_resolver::find_all("::1", 443, AF_INET6);
  EXPECT_FALSE(result.empty());
  bool found = false;
  for (const auto& ep : result) {
    if (ep.is_v6() && ep.v6()->is_loopback() && ep.port() == 443) found = true;
  }
  EXPECT_TRUE(found);
#endif
}

void DnsResolve_Localhost() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
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
#endif
}

void DnsResolve_FamilyFilter() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // With `AF_INET`, every result must be an IPv4 endpoint.
  auto v4 = dns_resolver::find_all("localhost", 80, AF_INET);
  for (const auto& ep : v4) EXPECT_TRUE(ep.is_v4());

  // With `AF_INET6`, every result must be an IPv6 endpoint.
  auto v6 = dns_resolver::find_all("localhost", 80, AF_INET6);
  for (const auto& ep : v6) EXPECT_TRUE(ep.is_v6());
#endif
}

void DnsResolve_InvalidHost() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // The `.invalid` TLD (RFC 2606) must not resolve.
  auto result = dns_resolver::find_all("no-such-host.invalid", 80);
  EXPECT_TRUE(result.empty());
#endif
}

void DnsResolveOne_Success() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Numeric loopback resolves to exactly one endpoint with the right port.
  const auto ep = dns_resolver::find_one("127.0.0.1", 80);
  EXPECT_TRUE(ep.is_v4());
  EXPECT_EQ(ep.port(), 80U);
#endif
}

void DnsResolveOne_Failure() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // An unresolvable host returns a default-constructed (invalid) endpoint.
  const auto ep = dns_resolver::find_one("no-such-host.invalid", 80);
  EXPECT_EQ(ep, ip_endpoint{});
#endif
}

// Helper: create a connected socketpair and wrap each end in a `test_socket`.
// Caller must close both sockets when done (RAII via test_socket destructor).
#ifdef __linux__
static std::pair<test_socket, test_socket> make_sockpair() {
  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return {};
  return {test_socket{fds[0]}, test_socket{fds[1]}};
}
#endif

void IoLoop_Lifecycle() {
#ifdef __linux__
  // Construction succeeds; an empty poll returns 0 events.
  io_loop loop;
  EXPECT_EQ(loop.run_once(0), 0);
#endif
}

void IoLoop_AddRemove() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  EXPECT_TRUE(loop.add(a, {}));

  // Adding the same socket again must fail.
  EXPECT_FALSE(loop.add(a, {}));

  EXPECT_TRUE(loop.remove(a));

  // Removing an unregistered socket must fail.
  EXPECT_FALSE(loop.remove(a));
#endif
}

void IoLoop_ReadableDispatch() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  int fired = 0;
  loop.add(a,
      {.on_readable = [&] { ++fired; }, .on_writable = {}, .on_error = {}});

  const char byte = 'x';
  EXPECT_EQ(::write(b.file().handle(), &byte, 1), 1);

  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(fired, 1);

  loop.remove(a);
#endif
}

void IoLoop_WritableNotFiredByDefault() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  int fired = 0;
  loop.add(a,
      {.on_readable = {}, .on_writable = [&] { ++fired; }, .on_error = {}});

  // EPOLLOUT is not in the initial mask, so on_writable must not fire.
  loop.run_once(0);
  EXPECT_EQ(fired, 0);

  loop.remove(a);
#endif
}

void IoLoop_EnableDisableWritable() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  int fired = 0;
  loop.add(a,
      {.on_readable = {}, .on_writable = [&] { ++fired; }, .on_error = {}});

  // Unix-domain stream sockets are always writable when the buffer is empty.
  EXPECT_TRUE(loop.set_writable(a));
  loop.run_once(0);
  EXPECT_EQ(fired, 1);

  // After disabling, on_writable must not fire again.
  EXPECT_TRUE(loop.set_writable(a, false));
  loop.run_once(0);
  EXPECT_EQ(fired, 1);

  loop.remove(a);
#endif
}

void IoLoop_ErrorFallsThruToReadable() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  int readable_fired = 0;
  // No on_error; EPOLLHUP must fall through to on_readable.
  loop.add(a,
      {.on_readable = [&] { ++readable_fired; },
          .on_writable = {},
          .on_error = {}});

  // Closing the write peer triggers EPOLLHUP on `a`.
  b.close();

  loop.run_once(0);
  EXPECT_EQ(readable_fired, 1);

  loop.remove(a);
#endif
}

void IoLoop_ErrorHandler() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  int error_fired = 0;
  int readable_fired = 0;
  loop.add(a,
      {
          .on_readable = [&] { ++readable_fired; },
          .on_writable = {},
          .on_error = [&] { ++error_fired; },
      });

  // Closing the write peer triggers EPOLLHUP; on_error must fire, not
  // on_readable.
  b.close();

  loop.run_once(0);
  EXPECT_EQ(error_fired, 1);
  EXPECT_EQ(readable_fired, 0);

  loop.remove(a);
#endif
}

void IoLoop_Post() {
#ifdef __linux__
  io_loop loop;

  int fired = 0;
  loop.post([&] { ++fired; });

  // post() callback runs at the top of the next run_once(), even with no
  // I/O events.
  loop.run_once(0);
  EXPECT_EQ(fired, 1);
#endif
}

void IoLoop_PostFromCallback() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  int outer = 0;
  int inner = 0;

  loop.add(a,
      {
          .on_readable =
              [&] {
                ++outer;
                // This post() must NOT fire during the current run_once() --
                // it lands in the new post_queue_ after the swap-and-drain.
                loop.post([&] { ++inner; });
              },
          .on_writable = {},
          .on_error = {},
      });

  const char byte = 'x';
  EXPECT_EQ(::write(b.file().handle(), &byte, 1), 1);

  loop.run_once(0);
  EXPECT_EQ(outer, 1);
  EXPECT_EQ(inner, 0); // posted callback not yet run

  loop.run_once(0);
  EXPECT_EQ(inner, 1); // fires on the following iteration

  loop.remove(a);
#endif
}

void IoLoop_Stop() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  int fired = 0;
  loop.add(a,
      {
          .on_readable =
              [&] {
                ++fired;
                loop.stop();
              },
          .on_writable = {},
          .on_error = {},
      });

  // Write two bytes; the loop should exit after the first dispatch.
  const char buf[2] = {'p', 'q'};
  EXPECT_EQ(::write(b.file().handle(), buf, 2), 2);

  loop.run(0);
  EXPECT_EQ(fired, 1);

  loop.remove(a);
#endif
}

void IoLoop_RemoveFromCallback() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();

  // Removing a *different* fd from within a callback must not crash.
  auto [c, d] = make_sockpair();
  loop.add(c, {});

  loop.add(a,
      {.on_readable = [&] { loop.remove(c); },
          .on_writable = {},
          .on_error = {}});

  const char byte = 'x';
  EXPECT_EQ(::write(b.file().handle(), &byte, 1), 1);
  loop.run_once(0);

  // `c` should no longer be registered.
  EXPECT_FALSE(loop.remove(c));

  loop.remove(a);
#endif
}

void IoLoop_MultipleRegistrations() {
#ifdef __linux__
  io_loop loop;

  auto [a, b] = make_sockpair();
  auto [c, d] = make_sockpair();

  int a_fired = 0;
  int c_fired = 0;
  loop.add(a,
      {.on_readable = [&] { ++a_fired; }, .on_writable = {}, .on_error = {}});
  loop.add(c,
      {.on_readable = [&] { ++c_fired; }, .on_writable = {}, .on_error = {}});

  // Only write to `b`; only `a` should become readable.
  const char byte = 'x';
  EXPECT_EQ(::write(b.file().handle(), &byte, 1), 1);

  loop.run_once(0);
  EXPECT_EQ(a_fired, 1);
  EXPECT_EQ(c_fired, 0);

  loop.remove(a);
  loop.remove(c);
#endif
}

MAKE_TEST_LIST(Ipv4Addr_Construction, Ipv4Addr_Parse, Ipv4Addr_Classification,
    Ipv4Addr_Comparison, Ipv4Addr_Formatting, Ipv4Addr_PosixInterop,
    Ipv6Addr_Construction, Ipv6Addr_Parse, Ipv6Addr_Classification,
    Ipv6Addr_Comparison, Ipv6Addr_Formatting, Ipv6Addr_PosixInterop,
    IpEndpoint_Construction, IpEndpoint_Parse, IpEndpoint_Comparison,
    IpEndpoint_Formatting, IpEndpoint_PosixInterop, IpSocket_Lifecycle,
    IpSocket_Move, IpSocket_Release, IpSocket_Options, IpSocket_Nonblocking,
    DnsResolve_NumericIPv4, DnsResolve_NumericIPv6, DnsResolve_Localhost,
    DnsResolve_FamilyFilter, DnsResolve_InvalidHost, DnsResolveOne_Success,
    DnsResolveOne_Failure, IoLoop_Lifecycle, IoLoop_AddRemove,
    IoLoop_ReadableDispatch, IoLoop_WritableNotFiredByDefault,
    IoLoop_EnableDisableWritable, IoLoop_ErrorFallsThruToReadable,
    IoLoop_ErrorHandler, IoLoop_Post, IoLoop_PostFromCallback, IoLoop_Stop,
    IoLoop_RemoveFromCallback, IoLoop_MultipleRegistrations);

// NOLINTEND(readability-function-cognitive-complexity)
