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
#include <type_traits>
#include <atomic>
#include <chrono>
#include <thread>

using namespace corvid;

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
  ipv6_addr a{0x2001, 0xdb8, 0, 0, 0, 0, 0, 1};

  in6_addr raw = a.to_in6_addr();
  ipv6_addr b{raw};
  EXPECT_EQ(a, b);
  EXPECT_EQ(raw.s6_addr[0], 0x20U);
  EXPECT_EQ(raw.s6_addr[1], 0x01U);
  EXPECT_EQ(raw.s6_addr[15], 0x01U);
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
  if (true) {
    ip_endpoint ep{ipv4_addr(192, 168, 1, 2), 1234};
    auto raw = ep.as_sockaddr_in();
    ip_endpoint roundtrip{raw};
    EXPECT_EQ(roundtrip, ep);

    auto storage = ep.as_sockaddr_storage();
    auto* as_v4 = reinterpret_cast<const sockaddr_in*>(&storage);
    EXPECT_EQ(as_v4->sin_family, AF_INET);
    EXPECT_EQ(ntohs(as_v4->sin_port), 1234U);
  }

  if (true) {
    ip_endpoint ep{ipv6_addr(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1), 4321};
    auto raw = ep.as_sockaddr_in6();
    ip_endpoint roundtrip{raw};
    EXPECT_EQ(roundtrip, ep);

    auto storage = ep.as_sockaddr_storage();
    auto* as_v6 = reinterpret_cast<const sockaddr_in6*>(&storage);
    EXPECT_EQ(as_v6->sin6_family, AF_INET6);
    EXPECT_EQ(ntohs(as_v6->sin6_port), 4321U);
  }
}

// Helper: create a non-blocking pipe and wrap each end in an `os_file`.
std::pair<os_file, os_file> make_nb_pipe() {
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
    throw std::system_error(errno, std::generic_category(), "pipe2");
  return {os_file{fds[0]}, os_file{fds[1]}};
}

void OsFile_Lifecycle() {
  // Default-constructed file is invalid.
  if (true) {
    os_file f;
    EXPECT_FALSE(f.is_open());
    EXPECT_FALSE(static_cast<bool>(f));
    EXPECT_EQ(f.handle(), os_file::invalid_file_handle);
    EXPECT_FALSE(f.close());
  }

  // An adopted file handle is open; closing it twice is idempotent.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    EXPECT_TRUE(reader.is_open());
    EXPECT_TRUE(static_cast<bool>(reader));
    EXPECT_NE(reader.handle(), os_file::invalid_file_handle);
    EXPECT_TRUE(reader.close());
    EXPECT_FALSE(reader.is_open());
    EXPECT_FALSE(reader.close());
  }

  // Destructor closes an open file (no crash or leak).
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    (void)writer;
  }
}

void OsFile_Move() {
  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.handle();
    os_file moved{std::move(reader)};
    EXPECT_FALSE(reader.is_open());
    EXPECT_TRUE(moved.is_open());
    EXPECT_EQ(moved.handle(), h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    auto [reader_a, writer_a] = make_nb_pipe();
    auto [reader_b, writer_b] = make_nb_pipe();
    const auto h = reader_a.handle();
    reader_b = std::move(reader_a);
    EXPECT_FALSE(reader_a.is_open());
    EXPECT_TRUE(reader_b.is_open());
    EXPECT_EQ(reader_b.handle(), h);
  }

  // Self-assignment is a no-op.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.handle();
    auto* p = &reader;
    reader = std::move(*p);
    EXPECT_TRUE(reader.is_open());
    EXPECT_EQ(reader.handle(), h);
  }
}

void OsFile_ReleaseFlags() {
  // `release()` yields the handle without closing it; file becomes invalid.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.release();
    EXPECT_NE(h, os_file::invalid_file_handle);
    EXPECT_FALSE(reader.is_open());
    ::close(h);
  }

  // Flag helpers round-trip non-blocking mode through `fcntl`.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    auto flags = reader.get_flags();
    EXPECT_TRUE(flags.has_value());
    EXPECT_TRUE(*flags & O_NONBLOCK);

    EXPECT_TRUE(reader.set_nonblocking(false));
    flags = reader.get_flags();
    EXPECT_TRUE(flags.has_value());
    EXPECT_FALSE(*flags & O_NONBLOCK);

    EXPECT_TRUE(reader.set_nonblocking(true));
    flags = reader.get_flags();
    EXPECT_TRUE(flags.has_value());
    EXPECT_TRUE(*flags & O_NONBLOCK);
  }
}

void OsFile_WriteRead() {
  auto [reader, writer] = make_nb_pipe();

  // A small write drains fully and the read side sees the same bytes.
  auto msg = std::string_view{"hello"};
  EXPECT_TRUE(writer.write(msg));
  EXPECT_TRUE(msg.empty());

  std::string buf;
  no_zero::enlarge_to(buf, 16);
  EXPECT_TRUE(reader.read(buf));
  EXPECT_EQ(buf, "hello");

  // An empty non-blocking read is a soft failure: success with no bytes read.
  no_zero::enlarge_to(buf, 16);
  EXPECT_TRUE(reader.read(buf));
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(errno, EAGAIN);

  // EOF leaves the caller's buffer unchanged and returns false.
  EXPECT_TRUE(writer.close());
  buf = "sentinel";
  EXPECT_FALSE(reader.read(buf));
  EXPECT_EQ(buf, "sentinel");
}

void IpSocket_Lifecycle() {
  // Default-constructed socket is invalid.
  if (true) {
    ip_socket s;
    EXPECT_FALSE(s.is_open());
    EXPECT_FALSE(static_cast<bool>(s));
    EXPECT_EQ(s.handle(), ip_socket::invalid_handle);
    EXPECT_FALSE(s.close());
  }

  // A real socket is open; closing it twice is idempotent.
  if (true) {
    ip_socket s{AF_INET, SOCK_STREAM, 0};
    EXPECT_TRUE(s.is_open());
    EXPECT_TRUE(static_cast<bool>(s));
    EXPECT_NE(s.handle(), ip_socket::invalid_handle);
    EXPECT_TRUE(s.close());
    EXPECT_FALSE(s.is_open());
    EXPECT_FALSE(s.close());
  }

  // Destructor closes an open socket (no crash or leak).
  if (true) { ip_socket s{AF_INET, SOCK_STREAM, 0}; }
}

void EventFd_Lifecycle() {
  // Default-constructed eventfd is invalid.
  if (true) {
    event_fd e;
    EXPECT_FALSE(e.is_open());
    EXPECT_FALSE(static_cast<bool>(e));
    EXPECT_EQ(e.handle(), event_fd::invalid_handle);
    EXPECT_FALSE(e.close());
  }

  // A real eventfd is open; closing it twice is idempotent.
  if (true) {
    event_fd e{0};
    EXPECT_TRUE(e.is_open());
    EXPECT_TRUE(static_cast<bool>(e));
    EXPECT_NE(e.handle(), event_fd::invalid_handle);
    EXPECT_TRUE(e.close());
    EXPECT_FALSE(e.is_open());
    EXPECT_FALSE(e.close());
  }

  // Destructor closes an open eventfd (no crash or leak).
  if (true) { event_fd e{0}; }
}

void Epoll_Lifecycle() {
  // Default-constructed epoll handle is invalid.
  if (true) {
    epoll p;
    EXPECT_FALSE(p.is_open());
    EXPECT_FALSE(static_cast<bool>(p));
    EXPECT_EQ(p.handle(), epoll::invalid_handle);
    EXPECT_FALSE(p.close());
  }

  // A real epoll instance is open; closing it twice is idempotent.
  if (true) {
    epoll p{epoll::default_flags};
    EXPECT_TRUE(p.is_open());
    EXPECT_TRUE(static_cast<bool>(p));
    EXPECT_NE(p.handle(), epoll::invalid_handle);
    EXPECT_TRUE(p.close());
    EXPECT_FALSE(p.is_open());
    EXPECT_FALSE(p.close());
  }

  // Destructor closes an open epoll fd (no crash or leak).
  if (true) { epoll p{epoll::default_flags}; }
}

void Epoll_Move() {
  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    epoll a{epoll::default_flags};
    const auto h = a.handle();
    epoll b{std::move(a)};
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    epoll a{epoll::default_flags};
    epoll b{epoll::default_flags};
    const auto h = a.handle();
    b = std::move(a);
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Self-assignment is a no-op.
  if (true) {
    epoll a{epoll::default_flags};
    const auto h = a.handle();
    auto* p = &a;
    a = std::move(*p);
    EXPECT_TRUE(a.is_open());
    EXPECT_EQ(a.handle(), h);
  }
}

void Epoll_Release() {
  // `release()` yields the handle without closing it; epoll becomes invalid.
  if (true) {
    epoll p{epoll::default_flags};
    const auto h = p.release();
    EXPECT_NE(h, epoll::invalid_handle);
    EXPECT_FALSE(p.is_open());
    ::close(h);
  }
}

void Epoll_ControlWait() {
  event_fd e{0};
  epoll p{epoll::default_flags};

  epoll_event add_ev{.events = EPOLLIN,
      .data = epoll_data_t{.fd = e.handle()}};
  EXPECT_TRUE(p.add(e.handle(), add_ev));

  EXPECT_TRUE(e.notify(3));

  epoll_event events[1]{};
  ASSERT_EQ(p.wait(events, 1, 0), 1);
  EXPECT_EQ(events[0].data.fd, e.handle());
  EXPECT_TRUE(events[0].events & EPOLLIN);

  auto value = e.read();
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 3U);

  epoll_event mod_ev{.events = EPOLLOUT,
      .data = epoll_data_t{.fd = e.handle()}};
  EXPECT_TRUE(p.modify(e.handle(), mod_ev));
  EXPECT_TRUE(p.remove(e.handle()));
  EXPECT_EQ(p.wait(events, 1, 0), 0);
}

void EventFd_Move() {
  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    event_fd a{0};
    const auto h = a.handle();
    event_fd b{std::move(a)};
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    event_fd a{0};
    event_fd b{0};
    const auto h = a.handle();
    b = std::move(a);
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Self-assignment is a no-op.
  if (true) {
    event_fd a{0};
    const auto h = a.handle();
    auto* p = &a;
    a = std::move(*p);
    EXPECT_TRUE(a.is_open());
    EXPECT_EQ(a.handle(), h);
  }
}

void EventFd_Release() {
  // `release()` yields the handle without closing it; eventfd becomes invalid.
  if (true) {
    event_fd e{0};
    const auto h = e.release();
    EXPECT_NE(h, event_fd::invalid_handle);
    EXPECT_FALSE(e.is_open());
    ::close(h);
  }
}

void EventFd_NotifyRead() {
  // Writes accumulate and a read returns the total while resetting to zero.
  if (true) {
    event_fd e{0};
    EXPECT_TRUE(e.notify());
    EXPECT_TRUE(e.notify(4));

    auto value = e.read();
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, 5U);
  }

  // The out-parameter overload returns the current counter value.
  if (true) {
    event_fd e{7};
    event_fd::counter_t value = 0;
    EXPECT_TRUE(e.read(value));
    EXPECT_EQ(value, 7U);
  }
}

void EventFd_NonblockingEmptyRead() {
  // Default-created eventfds are non-blocking, so an empty read returns
  // nullopt.
  event_fd e{0};
  auto value = e.read();
  EXPECT_FALSE(value.has_value());
  EXPECT_EQ(errno, EAGAIN);
}

void IpSocket_Move() {
  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    ip_socket a{AF_INET, SOCK_STREAM, 0};
    const auto h = a.handle();
    ip_socket b{std::move(a)};
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    ip_socket a{AF_INET, SOCK_STREAM, 0};
    ip_socket b{AF_INET, SOCK_STREAM, 0};
    const auto h = a.handle();
    b = std::move(a);
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Self-assignment is a no-op.
  if (true) {
    ip_socket a{AF_INET, SOCK_STREAM, 0};
    const auto h = a.handle();
    // Route through a pointer to defeat -Wself-move while still exercising
    // the self-assignment path.
    auto* p = &a;
    a = std::move(*p);
    EXPECT_TRUE(a.is_open());
    EXPECT_EQ(a.handle(), h);
  }
}

void IpSocket_Release() {
  // `release()` yields the handle without closing it; socket becomes invalid.
  if (true) {
    ip_socket s{AF_INET, SOCK_STREAM, 0};
    const auto h = s.release();
    EXPECT_NE(h, ip_socket::invalid_handle);
    EXPECT_FALSE(s.is_open());
    ::close(h);
  }
}

void IpSocket_Options() {
  // Named option helpers round-trip through `get_option`.
  if (true) {
    ip_socket s{AF_INET, SOCK_STREAM, 0};

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
    ip_socket s{AF_INET, SOCK_STREAM, 0};
    EXPECT_TRUE(s.set_recv_buffer_size(65536));
    EXPECT_TRUE(s.set_send_buffer_size(65536));
    auto r = s.get_option<int>(SOL_SOCKET, SO_RCVBUF);
    EXPECT_TRUE(r.has_value());
    EXPECT_GE(*r, 65536);
    auto t = s.get_option<int>(SOL_SOCKET, SO_SNDBUF);
    EXPECT_TRUE(t.has_value());
    EXPECT_GE(*t, 65536);
  }
}

void IpSocket_Nonblocking() {
  if (true) {
    ip_socket s{AF_INET, SOCK_STREAM, 0};

    EXPECT_TRUE(s.set_nonblocking(true));
    EXPECT_TRUE(s.get_flags().value_or(0) & O_NONBLOCK);

    EXPECT_TRUE(s.set_nonblocking(false));
    EXPECT_FALSE(s.get_flags().value_or(0) & O_NONBLOCK);
  }
}

void IpSocket_SendRecv() {
  int fds[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  ip_socket a{os_file{fds[0]}};
  ip_socket b{os_file{fds[1]}};

  auto msg = std::string_view{"hello"};
  EXPECT_TRUE(a.send(msg));
  EXPECT_TRUE(msg.empty());

  std::string buf(16, '\0');
  EXPECT_TRUE(b.recv(buf));
  EXPECT_EQ(buf, "hello");

  constexpr char raw_msg[] = "raw";
  EXPECT_EQ(a.send(raw_msg, sizeof(raw_msg) - 1), 3);

  char raw_buf[8]{};
  EXPECT_EQ(b.recv(raw_buf, sizeof(raw_buf), 0), 3);
  const auto raw_view = std::string_view{raw_buf, 3};
  EXPECT_EQ(raw_view, "raw");
}

void IpSocket_BindListenAccept() {
  // Bind a listening socket to a free loopback port.
  ip_socket listener{AF_INET, SOCK_STREAM, 0};
  EXPECT_TRUE(listener.is_open());
  EXPECT_TRUE(listener.set_reuse_addr());
  EXPECT_TRUE(listener.bind(ip_endpoint{ipv4_addr::loopback(), 0}));
  EXPECT_TRUE(listener.listen());

  // Retrieve the OS-assigned port via `getsockname`.
  sockaddr_in bound{};
  socklen_t bound_len = sizeof(bound);
  EXPECT_EQ(::getsockname(listener.handle(),
                reinterpret_cast<sockaddr*>(&bound), &bound_len),
      0);
  const uint16_t port = ntohs(bound.sin_port);
  EXPECT_NE(port, 0U);

  // Connect a client to the listening socket.
  ip_socket client{AF_INET, SOCK_STREAM, 0};
  EXPECT_TRUE(client.is_open());
  EXPECT_TRUE(client.connect(ip_endpoint{ipv4_addr::loopback(), port}));

  // Accept the connection on the listener side.
  auto result = listener.accept();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result->first.is_open());
  EXPECT_TRUE(result->second.is_v4());
  EXPECT_TRUE(result->second.v4()->is_loopback());
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
  EXPECT_EQ(ep, ip_endpoint{});
}

// Helper: create a connected socketpair and wrap each end in an `ip_socket`.
// Caller must close both sockets when done (RAII via `ip_socket` destructor).
// Plain struct (not `std::pair`) so structured bindings use direct member
// access rather than `std::tuple_element<>::type`.
struct sockpair_t {
  ip_socket a;
  ip_socket b;
};
// Make a pair of connected sockets, in non-blocking mode.
static sockpair_t make_nb_sockpair() {
  int fds[2];
  if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, fds) != 0)
    return {};
  return {ip_socket{os_file{fds[0]}}, ip_socket{os_file{fds[1]}}};
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
  io_loop loop;
  EXPECT_EQ(loop.run_once(0), 0);
}

void IoLoop_Post() {
  io_loop loop;

  int fired = 0;
  loop.post([&] { ++fired; });

  // post() callback runs at the top of the next run_once(), even with no
  // I/O events.
  loop.run_once(0);
  EXPECT_EQ(fired, 1);
}

// `register_socket` dispatches `on_readable` via virtual `io_conn` override;
// `unregister_socket` stops further dispatch. Double-register and
// double-unregister both return false.
void IoLoop_RegisterUnregister() {
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
}

void IoLoop_SetReadable() {
  io_loop loop;
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
  io_loop loop;
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

  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  auto conn = std::make_shared<readable_only_conn>(std::move(a));
  EXPECT_TRUE(loop.register_socket(conn));

  b.close(); // EPOLLHUP -> default on_error() -> on_readable()
  loop.run_once(0);

  EXPECT_GE(conn->readable, 1);
}

void TcpConn_Lifecycle() {
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
}

void TcpConn_Receive() {
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
}

void TcpConn_SetRecvBufSize() {
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
}

void TcpConn_PeerClose() {
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
  EXPECT_TRUE(b.read(buf));
  EXPECT_EQ(buf, "still-open");

  conn.close();
  loop.run_once(0);
  EXPECT_FALSE(conn.is_open());
}

void TcpConn_Send() {
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  tcp_conn conn{loop, std::move(a), {}, {}};
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

void TcpConn_ManualClose() {
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
}

void TcpConn_DrainAfterBufferedSend() {
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

void TcpConn_DrainAfterImmediateSend() {
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  int drain_count = 0;
  tcp_conn conn{loop, std::move(a), {}, {.on_drain = [&] { ++drain_count; }}};
  loop.run_once(0); // process posted register_with_loop

  conn.send(std::string{"hello"});
  loop.run_once(0); // process posted enqueue_send()

  std::string received;
  no_zero::enlarge_to(received, 16);
  EXPECT_TRUE(b.read(received));
  EXPECT_EQ(received, "hello");
  EXPECT_EQ(drain_count, 1);
}

void TcpConn_AsyncCbRead() {
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
}

void TcpConn_AsyncCbRead_PreservesEarlyData() {
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string received;
  tcp_conn conn{loop, std::move(a), {}, {}};
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

void TcpConn_AsyncCbRead_DuplicateRejected() {
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
}

void TcpConn_AsyncCbRead_PeerClose() {
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
}

void TcpConn_AsyncCbWrite() {
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
  EXPECT_TRUE(b.read(received));
  EXPECT_EQ(received, "callback-write");
  EXPECT_TRUE(completed);
  EXPECT_EQ(callback_count, 1);
}

void TcpConn_AsyncCbWrite_Failure() {
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
}

void TcpConn_ShutdownWrite() {
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
}

void TcpConn_ShutdownRead() {
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
  EXPECT_TRUE(b.read(buf));
  EXPECT_EQ(buf, "outbound");
  EXPECT_EQ(data_count, 0);
}

void TcpConn_ShutdownBothCloses() {
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
}

void TcpConn_AsyncCbWrite_DuplicateRejected() {
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
}

void TcpConn_GracefulClose() {
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

void TcpConn_DestructorHangsUp() {
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
void TcpConn_AsyncRead() {
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
}

void TcpConn_AsyncRead_PreservesEarlyData() {
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string received;
  bool done = false;

  tcp_conn conn{loop, std::move(a), {}, {}};
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

void TcpConn_AsyncRead_StopsBetweenCalls() {
  io_loop loop;
  auto [a, b] = make_nb_sockpair();

  std::string first;
  std::string second;
  bool first_done = false;
  bool second_done = false;

  tcp_conn conn{loop, std::move(a), {}, {}};
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
void TcpConn_AsyncRead_PeerClose() {
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
}

// Verify that `async_send` delivers bytes to the peer and suspends until
// the queue drains.
void TcpConn_AsyncSend() {
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
  EXPECT_TRUE(b.read(buf));
  EXPECT_EQ(buf, msg);
}

MAKE_TEST_LIST(Ipv4Addr_Construction, Ipv4Addr_Parse, Ipv4Addr_Classification,
    Ipv4Addr_Comparison, Ipv4Addr_Formatting, Ipv4Addr_PosixInterop,
    Ipv6Addr_Construction, Ipv6Addr_Parse, Ipv6Addr_Classification,
    Ipv6Addr_Comparison, Ipv6Addr_Formatting, Ipv6Addr_PosixInterop,
    IpEndpoint_Construction, IpEndpoint_Parse, IpEndpoint_Comparison,
    IpEndpoint_Formatting, IpEndpoint_PosixInterop, OsFile_Lifecycle,
    OsFile_Move, OsFile_ReleaseFlags, OsFile_WriteRead, IpSocket_Lifecycle,
    EventFd_Lifecycle, Epoll_Lifecycle, Epoll_Move, Epoll_Release,
    Epoll_ControlWait, EventFd_Move, EventFd_Release, EventFd_NotifyRead,
    EventFd_NonblockingEmptyRead, IpSocket_Move, IpSocket_Release,
    IpSocket_Options, IpSocket_Nonblocking, IpSocket_SendRecv,
    IpSocket_BindListenAccept, DnsResolve_NumericIPv4, DnsResolve_NumericIPv6,
    DnsResolve_Localhost, DnsResolve_FamilyFilter, DnsResolve_InvalidHost,
    DnsResolveOne_Success, DnsResolveOne_Failure, IoLoop_Lifecycle,
    IoLoop_Post, IoLoop_RegisterUnregister, IoLoop_SetWritable,
    IoLoop_SetReadable, IoLoop_ErrorSkipsWritable, IoLoop_DefaultOnError,
    TcpConn_Lifecycle, TcpConn_Receive, TcpConn_SetRecvBufSize,
    TcpConn_PeerClose, TcpConn_Send, TcpConn_ManualClose,
    TcpConn_DrainAfterBufferedSend, TcpConn_DrainAfterImmediateSend,
    TcpConn_AsyncCbRead, TcpConn_AsyncCbRead_PreservesEarlyData,
    TcpConn_AsyncCbRead_DuplicateRejected, TcpConn_AsyncCbRead_PeerClose,
    TcpConn_AsyncCbWrite, TcpConn_AsyncCbWrite_Failure,
    TcpConn_AsyncCbWrite_DuplicateRejected, TcpConn_ShutdownWrite,
    TcpConn_ShutdownRead, TcpConn_ShutdownBothCloses, TcpConn_GracefulClose,
    TcpConn_DestructorHangsUp, LoopTask_FireAndForget, TcpConn_AsyncRead,
    TcpConn_AsyncRead_PreservesEarlyData, TcpConn_AsyncRead_StopsBetweenCalls,
    TcpConn_AsyncRead_PeerClose, TcpConn_AsyncSend);

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
