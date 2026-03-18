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

#include "../corvid/filesys.h"
#include "../corvid/proto/net_endpoint.h"
#include "minitest.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>

using namespace corvid;

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
    net_socket s;
    EXPECT_FALSE(s.is_open());
    EXPECT_FALSE(static_cast<bool>(s));
    EXPECT_EQ(s.handle(), net_socket::invalid_handle);
    EXPECT_FALSE(s.close());
  }

  // A real socket is open; closing it twice is idempotent.
  if (true) {
    net_socket s{AF_INET, SOCK_STREAM, 0};
    EXPECT_TRUE(s.is_open());
    EXPECT_TRUE(static_cast<bool>(s));
    EXPECT_NE(s.handle(), net_socket::invalid_handle);
    EXPECT_TRUE(s.close());
    EXPECT_FALSE(s.is_open());
    EXPECT_FALSE(s.close());
  }

  // Destructor closes an open socket (no crash or leak).
  if (true) { net_socket s{AF_INET, SOCK_STREAM, 0}; }
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
    EXPECT_TRUE(e.get_flags().value_or(0) & O_NONBLOCK);
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
    EXPECT_FALSE(e.get_flags().value_or(0) & O_NONBLOCK);
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

void IpSocket_Move() {
  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    net_socket a{AF_INET, SOCK_STREAM, 0};
    const auto h = a.handle();
    net_socket b{std::move(a)};
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    net_socket a{AF_INET, SOCK_STREAM, 0};
    net_socket b{AF_INET, SOCK_STREAM, 0};
    const auto h = a.handle();
    b = std::move(a);
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.handle(), h);
  }

  // Self-assignment is a no-op.
  if (true) {
    net_socket a{AF_INET, SOCK_STREAM, 0};
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
    net_socket s{AF_INET, SOCK_STREAM, 0};
    const auto h = s.release();
    EXPECT_NE(h, net_socket::invalid_handle);
    EXPECT_FALSE(s.is_open());
    ::close(h);
  }
}

void IpSocket_Options() {
  // Named option helpers round-trip through `get_option`.
  if (true) {
    net_socket s{AF_INET, SOCK_STREAM, 0};

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
    net_socket s{AF_INET, SOCK_STREAM, 0};
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
    net_socket s{AF_INET, SOCK_STREAM, 0};

    EXPECT_TRUE(s.set_nonblocking(true));
    EXPECT_TRUE(s.get_flags().value_or(0) & O_NONBLOCK);

    EXPECT_TRUE(s.set_nonblocking(false));
    EXPECT_FALSE(s.get_flags().value_or(0) & O_NONBLOCK);
  }
}

void IpSocket_SendRecv() {
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
  EXPECT_EQ(b.recv(raw_buf, sizeof(raw_buf), 0), 3);
  const auto raw_view = std::string_view{raw_buf, 3};
  EXPECT_EQ(raw_view, "raw");
}

void IpSocket_BindListenAccept() {
  // Bind a listening socket to a free loopback port.
  net_socket listener{AF_INET, SOCK_STREAM, 0};
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
  net_socket client{AF_INET, SOCK_STREAM, 0};
  EXPECT_TRUE(client.is_open());
  EXPECT_TRUE(client.connect(net_endpoint{ipv4_addr::loopback, port}));

  // Accept the connection on the listener side.
  auto result = listener.accept();
  EXPECT_TRUE(result.has_value());
  EXPECT_TRUE(result->first.is_open());
  const auto peer = net_endpoint{result->second};
  EXPECT_TRUE(peer.is_v4());
  EXPECT_TRUE(peer.v4()->is_loopback());
}

void IpSocket_FactoryMethods() {
  using namespace bool_enums;

  // create_ipv4 defaults to non-blocking TCP.
  if (true) {
    auto s = net_socket::create_ipv4();
    EXPECT_TRUE(s.is_open());
    EXPECT_TRUE(s.get_flags().value_or(0) & O_NONBLOCK);
    auto dom = s.get_option<int>(SOL_SOCKET, SO_DOMAIN);
    EXPECT_TRUE(dom.has_value());
    EXPECT_EQ(*dom, AF_INET);
    auto type = s.get_option<int>(SOL_SOCKET, SO_TYPE);
    EXPECT_TRUE(type.has_value());
    EXPECT_EQ(*type, SOCK_STREAM);
  }

  // create_ipv4 with blocking + datagram gives a blocking UDP socket.
  if (true) {
    auto s =
        net_socket::create_ipv4(execution::blocking, message_style::datagram);
    EXPECT_TRUE(s.is_open());
    EXPECT_FALSE(s.get_flags().value_or(0) & O_NONBLOCK);
    auto dom = s.get_option<int>(SOL_SOCKET, SO_DOMAIN);
    EXPECT_EQ(*dom, AF_INET);
    auto type = s.get_option<int>(SOL_SOCKET, SO_TYPE);
    EXPECT_TRUE(type.has_value());
    EXPECT_EQ(*type, SOCK_DGRAM);
  }

  // create_ipv6 defaults to non-blocking TCP.
  if (true) {
    auto s = net_socket::create_ipv6();
    EXPECT_TRUE(s.is_open());
    EXPECT_TRUE(s.get_flags().value_or(0) & O_NONBLOCK);
    auto dom = s.get_option<int>(SOL_SOCKET, SO_DOMAIN);
    EXPECT_TRUE(dom.has_value());
    EXPECT_EQ(*dom, AF_INET6);
    auto type = s.get_option<int>(SOL_SOCKET, SO_TYPE);
    EXPECT_TRUE(type.has_value());
    EXPECT_EQ(*type, SOCK_STREAM);
  }

  // create_uds defaults to non-blocking stream.
  if (true) {
    auto s = net_socket::create_uds();
    EXPECT_TRUE(s.is_open());
    EXPECT_TRUE(s.get_flags().value_or(0) & O_NONBLOCK);
    auto dom = s.get_option<int>(SOL_SOCKET, SO_DOMAIN);
    EXPECT_TRUE(dom.has_value());
    EXPECT_EQ(*dom, AF_UNIX);
    auto type = s.get_option<int>(SOL_SOCKET, SO_TYPE);
    EXPECT_TRUE(type.has_value());
    EXPECT_EQ(*type, SOCK_STREAM);
  }

  // create_uds with datagram style gives a SOCK_DGRAM UDS.
  if (true) {
    auto s =
        net_socket::create_uds(execution::nonblocking, message_style::datagram);
    EXPECT_TRUE(s.is_open());
    auto type = s.get_option<int>(SOL_SOCKET, SO_TYPE);
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

MAKE_TEST_LIST(OsFile_Lifecycle, OsFile_Move, OsFile_ReleaseFlags,
    OsFile_WriteRead, OsFile_WriteAllReadExact, IpSocket_Lifecycle,
    EventFd_Lifecycle, Epoll_Lifecycle, Epoll_Move, Epoll_Release,
    Epoll_Create, Epoll_ControlWait, Epoll_WaitArray, EventFd_Move,
    EventFd_Release, EventFd_NotifyRead, EventFd_NonblockingEmptyRead,
    EventFd_Create, EventFd_SemaphoreMode, IpSocket_Move, IpSocket_Release,
    IpSocket_Options, IpSocket_Nonblocking, IpSocket_SendRecv,
    IpSocket_BindListenAccept, IpSocket_FactoryMethods);

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
