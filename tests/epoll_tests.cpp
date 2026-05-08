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

#pragma region Lifecycle

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

#pragma endregion

#pragma region Create

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

#pragma endregion

#pragma region Move

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

#pragma endregion

#pragma region Release

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

#pragma endregion

#pragma region ControlWait

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

#pragma endregion

#pragma region WaitArray

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

#pragma endregion

#pragma region EventFd_Lifecycle

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

#pragma endregion

#pragma region EventFd_Move

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

#pragma endregion

#pragma region EventFd_Release

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

#pragma endregion

#pragma region EventFd_NotifyRead

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

#pragma endregion

#pragma region EventFd_Create

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

#pragma endregion

#pragma region EventFd_SemaphoreMode

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

#pragma endregion

#pragma region EventFd_NonblockingEmptyRead

void EventFd_NonblockingEmptyRead() {
  // Default-created eventfds are non-blocking, so an empty read returns
  // nullopt.
  event_fd e{0};
  auto value = e.read();
  EXPECT_FALSE(value.has_value());
  EXPECT_EQ(errno, EAGAIN);
}

#pragma endregion

MAKE_TEST_LIST(Epoll_Lifecycle, Epoll_Create, Epoll_Move, Epoll_Release,
    Epoll_ControlWait, Epoll_WaitArray, EventFd_Lifecycle, EventFd_Move,
    EventFd_Release, EventFd_NotifyRead, EventFd_Create, EventFd_SemaphoreMode,
    EventFd_NonblockingEmptyRead);
// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
