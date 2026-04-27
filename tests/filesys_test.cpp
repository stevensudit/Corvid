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

#include "../corvid/filesys.h"
#include "../corvid/proto/net_endpoint.h"
#include "../corvid/strings/enum_conversion.h"
#include "minitest.h"

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
    EXPECT_TRUE(bitmask::has(*flags, o_flags::nonblock));

    EXPECT_TRUE(reader.set_nonblocking(false));
    flags = reader.get_flags();
    EXPECT_TRUE(flags.has_value());
    EXPECT_FALSE(bitmask::has(*flags, o_flags::nonblock));

    EXPECT_TRUE(reader.set_nonblocking(true));
    flags = reader.get_flags();
    EXPECT_TRUE(flags.has_value());
    EXPECT_TRUE(bitmask::has(*flags, o_flags::nonblock));
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

void NetSocket_Lifecycle() {
  if (is_codex()) return;

  // Default-constructed socket is invalid.
  if (true) {
    net_socket s;
    EXPECT_FALSE(s.is_open());
    EXPECT_FALSE(static_cast<bool>(s));
    EXPECT_EQ(s.handle(), net_socket::invalid_handle);
    EXPECT_FALSE(s.close());
  }

  // A real socket is open; closing it twice is idempotent.
  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};
    EXPECT_TRUE(s.is_open());
    EXPECT_TRUE(static_cast<bool>(s));
    EXPECT_NE(s.handle(), net_socket::invalid_handle);
    EXPECT_TRUE(s.close());
    EXPECT_FALSE(s.is_open());
    EXPECT_FALSE(s.close());
  }

  // Destructor closes an open socket (no crash or leak).
  if (true) { net_socket s{address_family::inet, socket_type::stream, {}}; }
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

void Epoll_Create() {
  // Default create() produces an open, functional epoll instance.
  if (true) {
    auto p = epoll::create();
    EXPECT_TRUE(p.is_open());
    EXPECT_NE(p.handle(), epoll::invalid_handle);
  }

  // Explicit flags are forwarded correctly (flags=0 is also valid).
  if (true) {
    auto p = epoll::create(0);
    EXPECT_TRUE(p.is_open());
  }

  // A created instance is functional: it can register an eventfd and see
  // its notification.
  if (true) {
    auto p = epoll::create();
    event_fd e{0};

    epoll_event ev{.events = EPOLLIN, .data = {.fd = e.handle()}};
    EXPECT_TRUE(p.add(e.handle(), ev));
    EXPECT_TRUE(e.notify(1));

    epoll_event ready[1]{};
    ASSERT_EQ(p.wait(ready, 0).value_or(-1), 1);
    EXPECT_EQ(ready[0].data.fd, e.handle());
    EXPECT_TRUE(ready[0].events & EPOLLIN);
  }
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
  ASSERT_EQ(p.wait(events, 1, 0).value_or(-1), 1);
  EXPECT_EQ(events[0].data.fd, e.handle());
  EXPECT_TRUE(events[0].events & EPOLLIN);

  auto value = e.read();
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, 3U);

  epoll_event mod_ev{.events = EPOLLOUT,
      .data = epoll_data_t{.fd = e.handle()}};
  EXPECT_TRUE(p.modify(e.handle(), mod_ev));
  EXPECT_TRUE(p.remove(e.handle()));
  EXPECT_EQ(p.wait(events, 1, 0).value_or(-1), 0);
}

void Epoll_WaitArray() {
  event_fd e{0};
  epoll p{epoll::default_flags};

  epoll_event add_ev{.events = EPOLLIN,
      .data = epoll_data_t{.fd = e.handle()}};
  EXPECT_TRUE(p.add(e.handle(), add_ev));
  EXPECT_TRUE(e.notify(1));

  // Array overload: no `maxevents` arg; the single-argument call unambiguously
  // selects the template, which deduces `maxevents=4` from the array. The
  // event is already ready so this returns without blocking.
  epoll_event events[4]{};
  ASSERT_EQ(p.wait(events, 1000).value_or(-1), 1);
  EXPECT_EQ(events[0].data.fd, e.handle());
  EXPECT_TRUE(events[0].events & EPOLLIN);
  (void)e.read();

  // A second pending event also comes through the array overload correctly.
  EXPECT_TRUE(e.notify(7));
  ASSERT_EQ(p.wait(events, 1000).value_or(-1), 1);
  EXPECT_TRUE(events[0].events & EPOLLIN);
  (void)e.read();
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

void EventFd_Create() {
  using namespace bool_enums;

  // Default: non-blocking counter mode, initial value 0.
  if (true) {
    auto e = event_fd::create();
    EXPECT_TRUE(e.is_open());
    EXPECT_TRUE(
        bitmask::has(e.get_flags().value_or(o_flags{}), o_flags::nonblock));
    // Counter starts at 0, so an immediate read returns nullopt (EAGAIN).
    EXPECT_FALSE(e.read().has_value());
    EXPECT_EQ(errno, EAGAIN);
  }

  // Non-zero initial value is readable immediately.
  if (true) {
    auto e = event_fd::create(5);
    EXPECT_TRUE(e.is_open());
    auto v = e.read();
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, 5U);
  }

  // Blocking mode: O_NONBLOCK is absent.
  if (true) {
    auto e = event_fd::create(0, event_mode::counter, execution::blocking);
    EXPECT_TRUE(e.is_open());
    EXPECT_FALSE(
        bitmask::has(e.get_flags().value_or(o_flags{}), o_flags::nonblock));
  }
}

void EventFd_SemaphoreMode() {
  using namespace bool_enums;

  // With initial value 3, each read consumes exactly 1 token and returns 1.
  if (true) {
    auto e = event_fd::create(3, event_mode::semaphore);
    EXPECT_TRUE(e.is_open());

    auto v = e.read();
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, 1U);

    v = e.read();
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, 1U);

    v = e.read();
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, 1U);

    // Counter exhausted: next read would block (EAGAIN).
    EXPECT_FALSE(e.read().has_value());
    EXPECT_EQ(errno, EAGAIN);
  }

  // notify(n) posts n tokens; each read still consumes exactly 1.
  if (true) {
    auto e = event_fd::create(0, event_mode::semaphore);
    EXPECT_TRUE(e.notify(2));

    event_fd::counter_t val = 0;
    EXPECT_TRUE(e.read(val));
    EXPECT_EQ(val, 1U);

    EXPECT_TRUE(e.read(val));
    EXPECT_EQ(val, 1U);

    EXPECT_FALSE(e.read(val));
    EXPECT_EQ(errno, EAGAIN);
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

void NetSocket_Move() {
  if (is_codex()) return;

  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    net_socket a{address_family::inet, socket_type::stream, {}};
    const auto h = a.handle();
    net_socket b{std::move(a)};
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    net_socket a{address_family::inet, socket_type::stream, {}};
    net_socket b{address_family::inet, socket_type::stream, {}};
    const auto h = a.handle();
    b = std::move(a);
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Self-assignment is a no-op.
  if (true) {
    net_socket a{address_family::inet, socket_type::stream, {}};
    const auto h = a.handle();
    // Route through a pointer to defeat -Wself-move while still exercising
    // the self-assignment path.
    auto* p = &a;
    a = std::move(*p);
    EXPECT_TRUE(a.is_open());
    EXPECT_EQ(a.handle(), h);
  }
}

void NetSocket_Release() {
  if (is_codex()) return;

  // `release()` yields the handle without closing it; socket becomes invalid.
  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};
    const auto h = s.release();
    EXPECT_NE(h, net_socket::invalid_handle);
    EXPECT_FALSE(s.is_open());
    ::close(h);
  }
}

void NetSocket_Options() {
  if (is_codex()) return;

  // Named option helpers round-trip through `get_option`.
  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};

    EXPECT_TRUE(s.set_reuse_addr(true));
    auto v = s.get_option<int>(socket_option::reuse_addr);
    EXPECT_TRUE(v.has_value());
    EXPECT_NE(*v, 0);

    EXPECT_TRUE(s.set_reuse_addr(false));
    v = s.get_option<int>(socket_option::reuse_addr);
    EXPECT_TRUE(v.has_value());
    EXPECT_EQ(*v, 0);

    EXPECT_TRUE(s.set_reuse_port(true));
    EXPECT_TRUE(s.set_keepalive(true));
    EXPECT_TRUE(s.set_nodelay(true));
  }

  // Buffer size helpers: kernel may round up, so just verify >= requested.
  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};
    EXPECT_TRUE(s.set_recv_buffer_size(65536));
    EXPECT_TRUE(s.set_send_buffer_size(65536));
    auto r = s.get_option<int>(socket_option::rcvbuf);
    EXPECT_TRUE(r.has_value());
    EXPECT_GE(*r, 65536);
    auto t = s.get_option<int>(socket_option::sndbuf);
    EXPECT_TRUE(t.has_value());
    EXPECT_GE(*t, 65536);
  }
}

void NetSocket_Nonblocking() {
  if (is_codex()) return;

  if (true) {
    net_socket s{address_family::inet, socket_type::stream, {}};

    EXPECT_TRUE(s.set_nonblocking(true));
    EXPECT_TRUE(
        bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));

    EXPECT_TRUE(s.set_nonblocking(false));
    EXPECT_FALSE(
        bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
  }
}

void NetSocket_SendRecv() {
  int fds[2];
  ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  net_socket a{os_file{fds[0]}};
  net_socket b{os_file{fds[1]}};

  auto msg = std::string_view{"hello"};
  EXPECT_TRUE(a.send(msg));
  EXPECT_TRUE(msg.empty());

  std::string buf(16, '\0');
  EXPECT_TRUE(b.recv(buf));
  EXPECT_EQ(buf, "hello");

  constexpr char raw_msg[] = "raw";
  EXPECT_EQ(a.send(raw_msg, sizeof(raw_msg) - 1), 3);

  char raw_buf[8]{};
  EXPECT_EQ(b.recv(raw_buf, sizeof(raw_buf), {}), 3);
  const auto raw_view = std::string_view{raw_buf, 3};
  EXPECT_EQ(raw_view, "raw");
}

void NetSocket_RecvAtContract() {
  auto [reader, writer] = net_socket::create_pair();
  ASSERT_TRUE(reader);
  ASSERT_TRUE(writer);
  EXPECT_TRUE(reader.set_nonblocking(true));

  // Soft errors (EAGAIN on a non-blocking empty socket) trim back to the
  // supplied offset and succeed.
  if (true) {
    std::string buf(8, '\0');
    EXPECT_TRUE(reader.recv_at(buf, 5));
    EXPECT_EQ(buf.size(), 5U);
  }

  // EOF leaves the caller's string unchanged and reports failure.
  if (true) {
    EXPECT_TRUE(writer.close());
    std::string buf(8, '\0');
    EXPECT_FALSE(reader.recv_at(buf, 3));
    EXPECT_EQ(buf.size(), 8U);
  }

  // `recv()` still clears on hard failure.
  if (true) {
    EXPECT_TRUE(reader.close());
    std::string buf(8, '\0');
    errno = 0;
    EXPECT_FALSE(reader.recv(buf));
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(errno, EBADF);
  }
}

void NetSocket_BindListenAccept() {
  if (is_codex()) return;

  // Bind a listening socket to a free loopback port.
  net_socket listener{address_family::inet, socket_type::stream, {}};
  EXPECT_TRUE(listener.is_open());
  EXPECT_TRUE(listener.set_reuse_addr());
  EXPECT_TRUE(listener.bind(net_endpoint{ipv4_addr::loopback, 0}));
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
  net_socket client{address_family::inet, socket_type::stream, {}};
  EXPECT_TRUE(client.is_open());
  EXPECT_TRUE(
      client.connect(net_endpoint{ipv4_addr::loopback, port}).value_or(false));

  // Accept the connection on the listener side.
  auto result = listener.accept();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result->first.is_open());
  const auto peer = net_endpoint{result->second};
  EXPECT_TRUE(peer.is_v4());
  EXPECT_TRUE(peer.v4()->is_loopback());
}

void NetSocket_FactoryMethods() {
  using namespace bool_enums;

  // create_ipv4 defaults to non-blocking TCP.
  if (true) {
    if (!is_codex()) {
      auto s = net_socket::create_ipv4();
      EXPECT_TRUE(s.is_open());
      EXPECT_TRUE(
          bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
      auto dom = s.get_option<int>(socket_option::domain);
      EXPECT_TRUE(dom.has_value());
      EXPECT_EQ(*dom, AF_INET);
      auto type = s.get_option<int>(socket_option::type);
      EXPECT_TRUE(type.has_value());
      EXPECT_EQ(*type, SOCK_STREAM);
    }
  }

  // create_ipv4 with blocking + datagram gives a blocking UDP socket.
  if (true) {
    if (!is_codex()) {
      auto s = net_socket::create_ipv4(execution::blocking,
          message_style::datagram);
      EXPECT_TRUE(s.is_open());
      EXPECT_FALSE(
          bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
      auto dom = s.get_option<int>(socket_option::domain);
      EXPECT_EQ(*dom, AF_INET);
      auto type = s.get_option<int>(socket_option::type);
      EXPECT_TRUE(type.has_value());
      EXPECT_EQ(*type, SOCK_DGRAM);
    }
  }

  // create_ipv6 defaults to non-blocking TCP.
  if (true) {
    if (!is_codex()) {
      auto s = net_socket::create_ipv6();
      EXPECT_TRUE(s.is_open());
      EXPECT_TRUE(
          bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
      auto dom = s.get_option<int>(socket_option::domain);
      EXPECT_TRUE(dom.has_value());
      EXPECT_EQ(*dom, AF_INET6);
      auto type = s.get_option<int>(socket_option::type);
      EXPECT_TRUE(type.has_value());
      EXPECT_EQ(*type, SOCK_STREAM);
    }
  }

  // create_uds defaults to non-blocking stream.
  if (true) {
    auto s = net_socket::create_uds();
    EXPECT_TRUE(s.is_open());
    EXPECT_TRUE(
        bitmask::has(s.get_flags().value_or(o_flags{}), o_flags::nonblock));
    auto dom = s.get_option<int>(socket_option::domain);
    EXPECT_TRUE(dom.has_value());
    EXPECT_EQ(*dom, AF_UNIX);
    auto type = s.get_option<int>(socket_option::type);
    EXPECT_TRUE(type.has_value());
    EXPECT_EQ(*type, SOCK_STREAM);
  }

  // create_uds with datagram style gives a SOCK_DGRAM UDS.
  if (true) {
    auto s = net_socket::create_uds(execution::nonblocking,
        message_style::datagram);
    EXPECT_TRUE(s.is_open());
    auto type = s.get_option<int>(socket_option::type);
    EXPECT_TRUE(type.has_value());
    EXPECT_EQ(*type, SOCK_DGRAM);
  }
}

void OsFile_WriteAllReadExact() {
  // write_all sends all bytes; read_exact receives exactly that many.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    const std::string_view msg = "hello, world";
    EXPECT_TRUE(writer.write_all(msg));

    std::string buf(msg.size(), '\0');
    EXPECT_TRUE(reader.read_exact(buf));
    EXPECT_EQ(buf, msg);
  }

  // read_exact on EOF before the buffer is filled trims `data` to the
  // bytes received and returns false.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    EXPECT_TRUE(writer.write_all(std::string_view{"hi"}));
    EXPECT_TRUE(writer.close());

    std::string buf(8, '\0');
    EXPECT_FALSE(reader.read_exact(buf));
    EXPECT_EQ(buf, "hi");
  }

  // Empty write_all and read_exact are no-ops that return true.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    EXPECT_TRUE(writer.write_all(std::string_view{}));
    std::string buf;
    EXPECT_TRUE(reader.read_exact(buf));
  }
}

void OsFile_MsgFlagsString() {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  // `none` (value 0) has no bit name and prints as "0x00000000".
  using namespace corvid::strings;
  using F = msg_flags;
  if (true) {
    EXPECT_EQ(enum_as_string(F{}), "0x00000000");
    EXPECT_EQ(enum_as_string(F::oob), "oob");
    EXPECT_EQ(enum_as_string(F::peek), "peek");
    EXPECT_EQ(enum_as_string(F::nosignal), "nosignal");
    EXPECT_EQ(enum_as_string(F::cloexec), "cloexec");
  }
  if (true) {
    // Higher bits print first.
    EXPECT_EQ(enum_as_string(F::dontwait | F::peek), "dontwait + peek");
  }
  if (true) {
    constexpr F bad{0x00200000}; // bit 21, above all named flags
    EXPECT_EQ(parse_enum("oob", bad), F::oob);
    EXPECT_EQ(parse_enum("nosignal", bad), F::nosignal);
    EXPECT_EQ(parse_enum("cloexec", bad), F::cloexec);
    EXPECT_EQ(parse_enum("dontwait + peek", bad), F::dontwait | F::peek);
  }
}

void NetSocket_SocketTypeString() {
  // Sequence enum names round-trip correctly starting from `stream` = 1.
  using namespace corvid::strings;
  using T = socket_type;
  if (true) {
    EXPECT_EQ(enum_as_string(T::stream), "stream");
    EXPECT_EQ(enum_as_string(T::datagram), "datagram");
    EXPECT_EQ(enum_as_string(T::raw), "raw");
    EXPECT_EQ(enum_as_string(T::seqpacket), "seqpacket");
    EXPECT_EQ(enum_as_string(T::packet), "packet");
  }
  if (true) {
    constexpr T bad{0};
    EXPECT_EQ(parse_enum("stream", bad), T::stream);
    EXPECT_EQ(parse_enum("datagram", bad), T::datagram);
    EXPECT_EQ(parse_enum("packet", bad), T::packet);
  }
}

void NetSocket_AddressFamilyString() {
  // Sequence enum names starting from `unspecified` = 0.
  using namespace corvid::strings;
  using AF = address_family;
  if (true) {
    EXPECT_EQ(enum_as_string(AF::unspecified), "unspecified");
    EXPECT_EQ(enum_as_string(AF::local), "local");
    EXPECT_EQ(enum_as_string(AF::inet), "inet");
    EXPECT_EQ(enum_as_string(AF::inet6), "inet6");
    // `unix` and `file` are aliases for `local`; they share value 1.
    EXPECT_EQ(enum_as_string(AF::unix), "local");
  }
  if (true) {
    constexpr AF bad{-1};
    EXPECT_EQ(parse_enum("unspecified", bad), AF::unspecified);
    EXPECT_EQ(parse_enum("inet", bad), AF::inet);
    EXPECT_EQ(parse_enum("inet6", bad), AF::inet6);
  }
}

void NetSocket_ProtocolTypeString() {
  // Each named protocol round-trips. (`tp` is unnamed in the spec and prints
  // as "U29", so it is intentionally excluded.)
  using namespace corvid::strings;
  using P = protocol_type;
  if (true) {
    EXPECT_EQ(enum_as_string(P::ip), "ip");
    EXPECT_EQ(enum_as_string(P::icmp), "icmp");
    EXPECT_EQ(enum_as_string(P::igmp), "igmp");
    EXPECT_EQ(enum_as_string(P::ipip), "ipip");
    EXPECT_EQ(enum_as_string(P::tcp), "tcp");
    EXPECT_EQ(enum_as_string(P::egp), "egp");
    EXPECT_EQ(enum_as_string(P::pup), "pup");
    EXPECT_EQ(enum_as_string(P::udp), "udp");
    EXPECT_EQ(enum_as_string(P::idp), "idp");
    EXPECT_EQ(enum_as_string(P::dccp), "dccp");
    EXPECT_EQ(enum_as_string(P::ipv6), "ipv6");
    EXPECT_EQ(enum_as_string(P::routing), "routing");
    EXPECT_EQ(enum_as_string(P::fragment), "fragment");
    EXPECT_EQ(enum_as_string(P::rsvp), "rsvp");
    EXPECT_EQ(enum_as_string(P::gre), "gre");
    EXPECT_EQ(enum_as_string(P::esp), "esp");
    EXPECT_EQ(enum_as_string(P::ah), "ah");
    EXPECT_EQ(enum_as_string(P::icmpv6), "icmpv6");
    EXPECT_EQ(enum_as_string(P::none), "none");
    EXPECT_EQ(enum_as_string(P::dstopts), "dstopts");
    EXPECT_EQ(enum_as_string(P::mtp), "mtp");
    EXPECT_EQ(enum_as_string(P::beetph), "beetph");
    EXPECT_EQ(enum_as_string(P::encap), "encap");
    EXPECT_EQ(enum_as_string(P::pim), "pim");
    EXPECT_EQ(enum_as_string(P::comp), "comp");
    EXPECT_EQ(enum_as_string(P::l2tp), "l2tp");
    EXPECT_EQ(enum_as_string(P::sctp), "sctp");
    EXPECT_EQ(enum_as_string(P::mh), "mh");
    EXPECT_EQ(enum_as_string(P::udplite), "udplite");
    EXPECT_EQ(enum_as_string(P::mpls), "mpls");
    EXPECT_EQ(enum_as_string(P::ethernet), "ethernet");
    EXPECT_EQ(enum_as_string(P::raw), "raw");
  }
  if (true) {
    constexpr P bad{-1};
    EXPECT_EQ(parse_enum("ip", bad), P::ip);
    EXPECT_EQ(parse_enum("tcp", bad), P::tcp);
    EXPECT_EQ(parse_enum("udp", bad), P::udp);
  }
}

void OsFile_ErrnoCodeString() {
  // Sequence enum: named values 0 ("ok") through 133 ("hwpoison").
  using namespace corvid::strings;
  using EC = filesys::errno_code;
  if (true) {
    EXPECT_EQ(enum_as_string(EC::ok), "ok");
    EXPECT_EQ(enum_as_string(EC::noent), "noent");
    EXPECT_EQ(enum_as_string(EC::again), "again");
    // `wouldblock` is an alias for `again` (both have value 11); only one
    // name.
    EXPECT_EQ(enum_as_string(EC::wouldblock), "again");
    EXPECT_EQ(enum_as_string(EC::hwpoison), "hwpoison");
  }
  if (true) {
    // Out-of-range values print as their numeric value.
    EXPECT_EQ(enum_as_string(EC{-1}), "-1");
    EXPECT_EQ(enum_as_string(EC{134}), "134");
  }
  if (true) {
    // `enum_as_view` returns "(unknown)" for out-of-range or unnamed values.
    EXPECT_EQ(enums::sequence::enum_as_view(EC::ok), "ok");
    EXPECT_EQ(enums::sequence::enum_as_view(EC::noent), "noent");
    EXPECT_EQ(enums::sequence::enum_as_view(EC{-1}), "(unknown)");
    EXPECT_EQ(enums::sequence::enum_as_view(EC{134}), "(unknown)");
  }
  if (true) {
    constexpr EC bad{-1};
    EXPECT_EQ(parse_enum("ok", bad), EC::ok);
    EXPECT_EQ(parse_enum("noent", bad), EC::noent);
    EXPECT_EQ(parse_enum("again", bad), EC::again);
    EXPECT_EQ(parse_enum("hwpoison", bad), EC::hwpoison);
  }
}

void OsFile_FcntlOpsString() {
  // Sequence enum: named values 0 ("dupfd") through 16 ("getownex").
  using namespace corvid::strings;
  using FO = filesys::fcntl_ops;
  if (true) {
    EXPECT_EQ(enum_as_string(FO::dupfd), "dupfd");
    EXPECT_EQ(enum_as_string(FO::getfd), "getfd");
    EXPECT_EQ(enum_as_string(FO::setfl), "setfl");
    EXPECT_EQ(enum_as_string(FO::getownex), "getownex");
  }
  if (true) {
    // Out-of-range values (including the non-contiguous `dupfd_cloexec`) print
    // as their numeric value.
    EXPECT_EQ(enum_as_string(FO{-1}), "-1");
    EXPECT_EQ(enum_as_string(FO{17}), "17");
    EXPECT_EQ(enum_as_string(FO::dupfd_cloexec), "1030");
  }
  if (true) {
    constexpr FO bad{-1};
    EXPECT_EQ(parse_enum("dupfd", bad), FO::dupfd);
    EXPECT_EQ(parse_enum("setfl", bad), FO::setfl);
    EXPECT_EQ(parse_enum("getownex", bad), FO::getownex);
  }
}

void NetSocket_SocketOptionString() {
  // Sequence enum: named values 1 ("debug") through 77 ("peerpidfd").
  // Aliases (`get_filter`=26, `detach_bpf`=27, `scm_txtime`=61) share values
  // with primary names and are omitted from the spec; the primary name wins.
  using namespace corvid::strings;
  using SO = socket_option;
  if (true) {
    EXPECT_EQ(enum_as_string(SO::debug), "debug");
    EXPECT_EQ(enum_as_string(SO::reuse_addr), "reuse_addr");
    EXPECT_EQ(enum_as_string(SO::type), "type");
    EXPECT_EQ(enum_as_string(SO::sndbuf), "sndbuf");
    EXPECT_EQ(enum_as_string(SO::keep_alive), "keep_alive");
    EXPECT_EQ(enum_as_string(SO::linger), "linger");
    EXPECT_EQ(enum_as_string(SO::reuse_port), "reuse_port");
    EXPECT_EQ(enum_as_string(SO::attach_filter), "attach_filter");
    EXPECT_EQ(enum_as_string(SO::detach_filter), "detach_filter");
    EXPECT_EQ(enum_as_string(SO::peername), "peername");
    EXPECT_EQ(enum_as_string(SO::protocol), "protocol");
    EXPECT_EQ(enum_as_string(SO::domain), "domain");
    EXPECT_EQ(enum_as_string(SO::attach_bpf), "attach_bpf");
    EXPECT_EQ(enum_as_string(SO::cookie), "cookie");
    EXPECT_EQ(enum_as_string(SO::txtime), "txtime");
    EXPECT_EQ(enum_as_string(SO::bind_to_ifindex), "bind_to_ifindex");
    EXPECT_EQ(enum_as_string(SO::peerpidfd), "peerpidfd");
    // Aliases: primary name wins for the shared value.
    EXPECT_EQ(enum_as_string(SO::get_filter), "attach_filter");
    EXPECT_EQ(enum_as_string(SO::detach_bpf), "detach_filter");
    EXPECT_EQ(enum_as_string(SO::scm_txtime), "txtime");
  }
  if (true) {
    // Out-of-range values print as their numeric value.
    EXPECT_EQ(enum_as_string(SO{0}), "0");
    EXPECT_EQ(enum_as_string(SO{78}), "78");
  }
  if (true) {
    constexpr SO bad{0};
    EXPECT_EQ(parse_enum("debug", bad), SO::debug);
    EXPECT_EQ(parse_enum("reuse_addr", bad), SO::reuse_addr);
    EXPECT_EQ(parse_enum("detach_filter", bad), SO::detach_filter);
    EXPECT_EQ(parse_enum("domain", bad), SO::domain);
    EXPECT_EQ(parse_enum("txtime", bad), SO::txtime);
    EXPECT_EQ(parse_enum("peerpidfd", bad), SO::peerpidfd);
  }
}

void NetSocket_TcpOptionString() {
  // Sequence enum: named values 1 ("nodelay") through 37 ("tx_delay").
  using namespace corvid::strings;
  using TO = tcp_option;
  if (true) {
    EXPECT_EQ(enum_as_string(TO::nodelay), "nodelay");
    EXPECT_EQ(enum_as_string(TO::maxseg), "maxseg");
    EXPECT_EQ(enum_as_string(TO::cork), "cork");
    EXPECT_EQ(enum_as_string(TO::keep_idle), "keep_idle");
    EXPECT_EQ(enum_as_string(TO::syncnt), "syncnt");
    EXPECT_EQ(enum_as_string(TO::linger2), "linger2");
    EXPECT_EQ(enum_as_string(TO::defer_accept), "defer_accept");
    EXPECT_EQ(enum_as_string(TO::info), "info");
    EXPECT_EQ(enum_as_string(TO::quickack), "quickack");
    EXPECT_EQ(enum_as_string(TO::fastopen), "fastopen");
    EXPECT_EQ(enum_as_string(TO::notsent_lowat), "notsent_lowat");
    EXPECT_EQ(enum_as_string(TO::save_syn), "save_syn");
    EXPECT_EQ(enum_as_string(TO::saved_syn), "saved_syn");
    EXPECT_EQ(enum_as_string(TO::fastopen_connect), "fastopen_connect");
    EXPECT_EQ(enum_as_string(TO::zerocopy_receive), "zerocopy_receive");
    EXPECT_EQ(enum_as_string(TO::inq), "inq");
    EXPECT_EQ(enum_as_string(TO::tx_delay), "tx_delay");
  }
  if (true) {
    // Out-of-range values print as their numeric value.
    EXPECT_EQ(enum_as_string(TO{0}), "0");
    EXPECT_EQ(enum_as_string(TO{38}), "38");
  }
  if (true) {
    constexpr TO bad{0};
    EXPECT_EQ(parse_enum("nodelay", bad), TO::nodelay);
    EXPECT_EQ(parse_enum("linger2", bad), TO::linger2);
    EXPECT_EQ(parse_enum("fastopen", bad), TO::fastopen);
    EXPECT_EQ(parse_enum("saved_syn", bad), TO::saved_syn);
    EXPECT_EQ(parse_enum("tx_delay", bad), TO::tx_delay);
  }
}

MAKE_TEST_LIST(OsFile_Lifecycle, OsFile_Move, OsFile_ReleaseFlags,
    OsFile_WriteRead, OsFile_WriteAllReadExact, NetSocket_Lifecycle,
    EventFd_Lifecycle, Epoll_Lifecycle, Epoll_Move, Epoll_Release,
    Epoll_Create, Epoll_ControlWait, Epoll_WaitArray, EventFd_Move,
    EventFd_Release, EventFd_NotifyRead, EventFd_NonblockingEmptyRead,
    EventFd_Create, EventFd_SemaphoreMode, NetSocket_Move, NetSocket_Release,
    NetSocket_Options, NetSocket_Nonblocking, NetSocket_SendRecv,
    NetSocket_RecvAtContract, NetSocket_BindListenAccept,
    NetSocket_FactoryMethods, OsFile_MsgFlagsString, OsFile_ErrnoCodeString,
    OsFile_FcntlOpsString, NetSocket_SocketTypeString,
    NetSocket_AddressFamilyString, NetSocket_ProtocolTypeString,
    NetSocket_SocketOptionString, NetSocket_TcpOptionString);

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
