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
#include <atomic>
#include <chrono>
#include <thread>

using namespace corvid;

// Exposes `ip_socket`'s protected constructors for testing.
struct test_socket: ip_socket {
  test_socket() = default;
  explicit test_socket(handle_t h) : ip_socket(h) {}
  test_socket(int domain, int type, int protocol)
      : ip_socket(ip_socket::make_ip_socket(domain, type, protocol)) {}
};

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

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
    EXPECT_TRUE(s.file().get_flags().value_or(0) & O_NONBLOCK);

    EXPECT_TRUE(s.file().set_nonblocking(false));
    EXPECT_FALSE(s.file().get_flags().value_or(0) & O_NONBLOCK);
  }
#endif
}

void IpSocket_BindListenAccept() {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Bind a listening socket to a free loopback port.
  test_socket listener{AF_INET, SOCK_STREAM, 0};
  EXPECT_TRUE(listener.is_open());
  EXPECT_TRUE(listener.set_reuse_addr());
  EXPECT_TRUE(listener.bind(ip_endpoint{ipv4_addr::loopback(), 0}));
  EXPECT_TRUE(listener.listen());

  // Retrieve the OS-assigned port via `getsockname`.
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  EXPECT_EQ(::getsockname(listener.file().handle(),
                reinterpret_cast<sockaddr*>(&bound), &bound_len),
      0);
  const uint16_t port = ntohs(bound.sin_port);
  EXPECT_NE(port, 0U);

  // Connect a client to the listening socket.
  test_socket client{AF_INET, SOCK_STREAM, 0};
  EXPECT_TRUE(client.is_open());
  EXPECT_TRUE(client.connect(ip_endpoint{ipv4_addr::loopback(), port}));

  // Accept the connection on the listener side.
  auto result = listener.accept();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result->first.is_open());
  EXPECT_TRUE(result->second.is_v4());
  EXPECT_TRUE(result->second.v4()->is_loopback());
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
// Plain struct (not `std::pair`) so structured bindings use direct member
// access rather than `std::tuple_element<>::type`.
#ifdef __linux__
struct sockpair_t {
  test_socket a;
  test_socket b;
};
// Make a pair of connected sockets, in non-blocking mode.
static sockpair_t make_nb_sockpair() {
  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) != 0)
    return {};
  return {test_socket{fds[0]}, test_socket{fds[1]}};
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
#endif

void IoLoop_Lifecycle() {
#ifdef __linux__
  // Construction succeeds; an empty poll returns 0 events.
  io_loop loop;
  EXPECT_EQ(loop.run_once(0), 0);
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

// `register_socket` dispatches `on_readable` via virtual `io_conn` override;
// `unregister_socket` stops further dispatch. Double-register and
// double-unregister both return false.
void IoLoop_RegisterUnregister() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn));

  auto msg_view = std::string_view{"hi"};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_EQ(loop.run_once(0), 1);
  EXPECT_EQ(conn->readable, 1);
  EXPECT_EQ(conn->writable, 0);
  EXPECT_EQ(conn->error, 0);

  // Drain the data so the fd is no longer readable.
  std::string buf(8, '\0');
  (void)a.file().read(buf);

  EXPECT_TRUE(loop.unregister_socket(conn->sock()));
  EXPECT_FALSE(loop.unregister_socket(conn->sock())); // already removed

  // No events after unregistering.
  EXPECT_EQ(loop.run_once(0), 0);
  EXPECT_EQ(conn->readable, 1);
#endif
}

// `set_writable(true)` arms `EPOLLOUT`; the kernel fires it when the kernel
// send buffer has space, which it does immediately on a fresh socketpair.
void IoLoop_SetWritable() {
#ifdef __linux__
  io_loop loop;
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
#endif
}

// When `EPOLLERR` or `EPOLLHUP` fires together with `EPOLLOUT`, `on_error` is
// called but `on_writable` is skipped (the early-return path in
// `dispatch_event`).
void IoLoop_ErrorSkipsWritable() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<counting_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn));
  loop.set_writable(conn->sock(), true); // arm EPOLLOUT

  b.close(); // triggers EPOLLHUP on `a`

  loop.run_once(0);
  EXPECT_EQ(conn->error, 1);
  EXPECT_EQ(conn->writable, 0); // must not fire when error/hup is reported
#endif
}

// The default `io_conn::on_error()` implementation falls through to
// `on_readable()`. Verify this by registering a subclass that overrides only
// `on_readable` and confirming it is called when the peer closes.
void IoLoop_DefaultOnError() {
#ifdef __linux__
  struct readable_only_conn: io_conn {
    using io_conn::io_conn;
    int readable = 0;
    void on_readable() override { ++readable; }
    // on_error not overridden; default calls on_readable()
  };

  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<readable_only_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn));

  b.close(); // EPOLLHUP -> default on_error() -> on_readable()
  loop.run_once(0);

  EXPECT_GE(conn->readable, 1);
#endif
}

void TcpConn_Lifecycle() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  const ip_endpoint remote{ipv4_addr::loopback(), 9999};
  {
    tcp_conn conn{loop, std::move(a), remote, {}};
    // open_ is set in the state constructor before the post fires.
    EXPECT_TRUE(conn.is_open());
    EXPECT_EQ(conn.remote_endpoint(), remote);
    loop.run_once(0); // process posted do_open_()
  }
  // destructor posted do_close_(); process it, then verify loop is clean.
  loop.run_once(0);
  EXPECT_EQ(loop.run_once(0), 0);
#endif
}

void TcpConn_Receive() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string received;
  tcp_conn conn{loop, std::move(a), {},
      {.on_data = [&](std::string& d) { received = std::move(d); }}};
  loop.run_once(0); // process posted do_open_()

  const std::string msg{"hello"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  EXPECT_EQ(loop.run_once(0), 1); // dispatch EPOLLIN
  EXPECT_EQ(received, msg);
#endif
}

void TcpConn_SetRecvBufSize() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::vector<size_t> chunk_sizes;
  tcp_conn conn{loop, std::move(a), {},
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
  EXPECT_EQ(loop.run_once(0), 1); // next read should use updated cap
  EXPECT_EQ(chunk_sizes.size(), 3U);
  EXPECT_EQ(chunk_sizes[2], 8U);
#endif
}

void TcpConn_PeerClose() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  bool closed = false;
  tcp_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
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
  EXPECT_TRUE(b.file().read(buf));
  EXPECT_EQ(buf, "still-open");

  conn.close();
  loop.run_once(0);
  EXPECT_FALSE(conn.is_open());
#endif
}

void TcpConn_Send() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  tcp_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted do_open_()

  conn.send(std::string{"world"});
  loop.run_once(0); // process posted enqueue() -> immediate ::write

  // Data written by enqueue() is now in the kernel buffer.
  std::string buf;
  no_zero::enlarge_to(buf, 16);
  EXPECT_TRUE(b.file().read(buf));
  EXPECT_EQ(buf.size(), 5U);
  EXPECT_EQ(buf, "world");
#endif
}

void TcpConn_ManualClose() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  bool closed = false;
  tcp_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
  loop.run_once(0); // process posted do_open_()

  conn.close();
  loop.run_once(0); // process posted do_close_() -> close_now_()

  EXPECT_FALSE(conn.is_open());
  EXPECT_TRUE(closed);

  // Destructor posts a hangup; it must be idempotent after close().
  loop.run_once(0);
  EXPECT_EQ(loop.run_once(0), 0);
#endif
}

void TcpConn_DrainAfterBufferedSend() {
#ifdef __linux__
  io_loop loop;
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
  tcp_conn conn{loop, std::move(a), {}, {.on_drain = [&] { ++drain_count; }}};
  loop.run_once(0); // process posted do_open_()

  conn.send(std::string{payload}); // copy payload into send
  // Drain by reading from `b` and running the loop until all data arrives.
  std::string received;
  received.reserve(payload.size());
  std::string tmp;
  while (received.size() < payload.size()) {
    loop.run_once(0);
    no_zero::enlarge_to(tmp, 4096);
    while (b.file().read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero::enlarge_to(tmp, 4096);
    }
  }

  // All bytes must arrive intact.
  EXPECT_EQ(received.size(), payload.size());
  EXPECT_EQ(received, payload);

  // `on_drain` must have fired at least once (via the EPOLLOUT path).
  EXPECT_GE(drain_count, 1);
#endif
}

void TcpConn_DrainAfterImmediateSend() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  int drain_count = 0;
  tcp_conn conn{loop, std::move(a), {}, {.on_drain = [&] { ++drain_count; }}};
  loop.run_once(0); // process posted register_with_loop

  conn.send(std::string{"hello"});
  loop.run_once(0); // process posted enqueue_send()

  std::string received;
  no_zero::enlarge_to(received, 16);
  EXPECT_TRUE(b.file().read(received));
  EXPECT_EQ(received, "hello");
  EXPECT_EQ(drain_count, 1);
#endif
}

void TcpConn_AsyncCbRead() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string received;
  tcp_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.async_cb_read([&](std::string& data) {
    received = std::move(data);
  }));

  const std::string msg{"callback-read"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  loop.run_once(0); // dispatch EPOLLIN -> inline callback

  EXPECT_EQ(received, msg);
#endif
}

void TcpConn_AsyncCbRead_DuplicateRejected() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  int callback_count = 0;
  tcp_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.async_cb_read([&](std::string&) { ++callback_count; }));
  EXPECT_FALSE(conn.async_cb_read([&](std::string&) { ++callback_count; }));

  const std::string msg{"one"};
  auto msg_view = std::string_view{msg};
  EXPECT_TRUE(b.send(msg_view) && msg_view.empty());

  loop.run_once(0); // dispatch EPOLLIN -> one callback

  EXPECT_EQ(callback_count, 1);
#endif
}

void TcpConn_AsyncCbRead_PeerClose() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string received{"sentinel"};
  int callback_count = 0;
  bool closed = false;
  tcp_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
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
#endif
}

void TcpConn_AsyncCbWrite() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  bool completed = false;
  int callback_count = 0;
  tcp_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.async_cb_write(std::string{"callback-write"},
      [&](bool write_completed) {
        completed = write_completed;
        ++callback_count;
      }));

  std::string received;
  no_zero::enlarge_to(received, 32);
  EXPECT_TRUE(b.file().read(received));
  EXPECT_EQ(received, "callback-write");
  EXPECT_TRUE(completed);
  EXPECT_EQ(callback_count, 1);
#endif
}

void TcpConn_AsyncCbWrite_Failure() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  bool completed = true;
  bool closed = false;
  tcp_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
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
#endif
}

void TcpConn_ShutdownWrite() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string received;
  tcp_conn conn{loop, std::move(a), {},
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
#endif
}

void TcpConn_ShutdownRead() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  int data_count = 0;
  tcp_conn conn{loop, std::move(a), {},
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
  EXPECT_TRUE(b.file().read(buf));
  EXPECT_EQ(buf, "outbound");
  EXPECT_EQ(data_count, 0);
#endif
}

void TcpConn_ShutdownBothCloses() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  tcp_conn conn{loop, std::move(a), {}, {}};
  loop.run_once(0); // process posted register_with_loop

  EXPECT_TRUE(conn.shutdown_write());
  EXPECT_TRUE(conn.is_open());
  EXPECT_FALSE(conn.can_write());
  EXPECT_TRUE(conn.can_read());

  EXPECT_TRUE(conn.shutdown_read());
  EXPECT_FALSE(conn.is_open());
  EXPECT_FALSE(conn.can_read());
  EXPECT_FALSE(conn.can_write());
#endif
}

void TcpConn_AsyncCbWrite_DuplicateRejected() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  a.set_send_buffer_size(small_buf);

  tcp_conn conn{loop, std::move(a), {}, {}};
  std::thread loop_thread{[&] { loop.run(10); }};

  const std::string payload(256ULL * 1024ULL, 'w');
  std::atomic<int> accepted{0};
  std::atomic<int> rejected{0};
  std::atomic<int> completions{0};

  auto try_register = [&] {
    if (conn.async_cb_write(std::string{payload}, [&](bool) {
          ++completions;
        }))
      ++accepted;
    else
      ++rejected;
  };

  std::thread t1{try_register};
  std::thread t2{try_register};
  t1.join();
  t2.join();

  EXPECT_EQ(accepted.load(), 1);
  EXPECT_EQ(rejected.load(), 1);

  b.close();
  for (int i = 0; i < 100 && completions.load() == 0; ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds{10});

  EXPECT_EQ(completions.load(), 1);

  loop.stop();
  loop_thread.join();
#endif
}

void TcpConn_GracefulClose() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  a.set_send_buffer_size(small_buf);

  const std::string payload(64ULL * 1024ULL, 'z');

  bool closed = false;
  tcp_conn conn{loop, std::move(a), {}, {.on_close = [&] { closed = true; }}};
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
    while (b.file().read(tmp) && !tmp.empty()) {
      received.append(tmp);
      no_zero::enlarge_to(tmp, 4096);
    }
  }

  EXPECT_EQ(received.size(), payload.size());
  EXPECT_EQ(received, payload);
  EXPECT_TRUE(closed);
  EXPECT_FALSE(conn.is_open());
#endif
}

void TcpConn_DestructorHangsUp() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  constexpr int small_buf = 4096;
  a.set_send_buffer_size(small_buf);

  const std::string payload(256ULL * 1024ULL, 'q');

  bool closed = false;
  {
    tcp_conn conn{loop, std::move(a), {},
        {.on_close = [&] { closed = true; }}};
    loop.run_once(0); // process posted register_with_loop
    conn.send(std::string{payload});
  }

  loop.run_once(0); // drain posted enqueue_send() then posted hangup()

  std::string received;
  std::string tmp;
  no_zero::enlarge_to(tmp, 4096);
  while (b.file().read(tmp) && !tmp.empty()) {
    received.append(tmp);
    no_zero::enlarge_to(tmp, 4096);
  }

  EXPECT_TRUE(closed);
  EXPECT_LT(received.size(), payload.size());
#endif
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
void TcpConn_AsyncRead() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string received;
  bool done = false;

  tcp_conn conn{loop, std::move(a), {}, {}};
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
#endif
}

// Verify that `async_read` returns an empty string when the peer closes
// the connection before data arrives.
void TcpConn_AsyncRead_PeerClose() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string received{"sentinel"};
  bool done = false;

  tcp_conn conn{loop, std::move(a), {}};
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
#endif
}

// Verify that `async_send` delivers bytes to the peer and suspends until
// the queue drains.
void TcpConn_AsyncSend() {
#ifdef __linux__
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  bool sent = false;

  tcp_conn conn{loop, std::move(a), {}};
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
  EXPECT_TRUE(b.file().read(buf));
  EXPECT_EQ(buf, msg);
#endif
}

MAKE_TEST_LIST(Ipv4Addr_Construction, Ipv4Addr_Parse, Ipv4Addr_Classification,
    Ipv4Addr_Comparison, Ipv4Addr_Formatting, Ipv4Addr_PosixInterop,
    Ipv6Addr_Construction, Ipv6Addr_Parse, Ipv6Addr_Classification,
    Ipv6Addr_Comparison, Ipv6Addr_Formatting, Ipv6Addr_PosixInterop,
    IpEndpoint_Construction, IpEndpoint_Parse, IpEndpoint_Comparison,
    IpEndpoint_Formatting, IpEndpoint_PosixInterop, IpSocket_Lifecycle,
    IpSocket_Move, IpSocket_Release, IpSocket_Options, IpSocket_Nonblocking,
    IpSocket_BindListenAccept, DnsResolve_NumericIPv4, DnsResolve_NumericIPv6,
    DnsResolve_Localhost, DnsResolve_FamilyFilter, DnsResolve_InvalidHost,
    DnsResolveOne_Success, DnsResolveOne_Failure, IoLoop_Lifecycle,
    IoLoop_Post, IoLoop_RegisterUnregister, IoLoop_SetWritable,
    IoLoop_ErrorSkipsWritable, IoLoop_DefaultOnError, TcpConn_Lifecycle,
    TcpConn_Receive, TcpConn_SetRecvBufSize, TcpConn_PeerClose, TcpConn_Send,
    TcpConn_ManualClose, TcpConn_DrainAfterBufferedSend,
    TcpConn_DrainAfterImmediateSend, TcpConn_AsyncCbRead,
    TcpConn_AsyncCbRead_DuplicateRejected, TcpConn_AsyncCbRead_PeerClose,
    TcpConn_AsyncCbWrite, TcpConn_AsyncCbWrite_Failure,
    TcpConn_AsyncCbWrite_DuplicateRejected, TcpConn_ShutdownWrite,
    TcpConn_ShutdownRead, TcpConn_ShutdownBothCloses, TcpConn_GracefulClose,
    TcpConn_DestructorHangsUp, LoopTask_FireAndForget, TcpConn_AsyncRead,
    TcpConn_AsyncRead_PeerClose, TcpConn_AsyncSend);

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
