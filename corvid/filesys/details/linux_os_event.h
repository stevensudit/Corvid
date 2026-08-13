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
#pragma once

// Standalone inclusion is permitted in dev builds and under clangd, so this
// file can be viewed and parsed on its own; release builds must come through
// the entry point.
#if !defined(CORVID_OS_EVENT_ENTRY) && defined(NDEBUG) &&                     \
    !defined(CORVID_CLANGD)
#error "Include \"os_event.h\" instead of this implementation header."
#endif

// The body drops out on the wrong platform, keeping cross-platform viewing
// quiet.
#ifndef _WIN32

#include <algorithm>
#include <chrono>
#include <limits>
#include <optional>

#include <poll.h>
#include <sys/eventfd.h>

#include "../../enums/bool_enums.h"
#include "../os_event_base.h"

// Linux implementation of "os_event.h", wrapping an `eventfd`.

namespace corvid { inline namespace filesys {
using namespace bool_enums;

#pragma region os_event

// Wake-up event backed by a Linux `eventfd`.
//
// Beyond the portable surface, it adds typed counter-based notify/read
// operations and the counter/semaphore and blocking mode choices.
class [[nodiscard]] os_event: public os_event_base<os_event> {
  using base = os_event_base<os_event>;
  friend base;

public:
#pragma region Types

  using counter_t = eventfd_t;
  static constexpr efd_flags default_flags =
      efd_flags::cloexec | efd_flags::nonblock;

#pragma endregion
#pragma region Construction

  using base::base;
  using base::create;

  // Create an `os_event` with `initial_value`.
  //
  // Pass `event_mode::semaphore` to add `EFD_SEMAPHORE`, or
  // `execution::blocking` to omit `EFD_NONBLOCK`.
  //
  // The first parameter takes no default so that the portable no-argument
  // `create` stays unambiguous; it is the equivalent of passing 0 here.
  [[nodiscard]] static os_event create(counter_t initial_value,
      event_mode mode = event_mode::counter,
      execution exec = execution::nonblocking) noexcept {
    auto flags = efd_flags::cloexec;
    if (mode == event_mode::semaphore) flags |= efd_flags::semaphore;
    if (exec == execution::nonblocking) flags |= efd_flags::nonblock;

    // If it fits in 32 bits, we can do it in one step.
    if (initial_value <= std::numeric_limits<uint32_t>::max())
      return os_event{
          os_file{::eventfd(static_cast<uint32_t>(initial_value), *flags)}};

    os_event ev{os_file{::eventfd(0, *flags)}};
    if (ev.is_open() && !ev.notify(initial_value)) return os_event{};
    return ev;
  }

#pragma endregion
#pragma region Operations

  using base::notify;

  // Add `value` to the counter. Returns true on success.
  [[nodiscard]] bool notify(counter_t value) const noexcept {
    return ::eventfd_write(handle(), value) == 0;
  }

  // Read the counter, returning the consumed value on success.
  //
  // In counter mode (the default), drains the full accumulated count and
  // resets to 0. In semaphore mode, always returns 1 and decrements the
  // counter by 1.
  [[nodiscard]] std::optional<counter_t> read() const noexcept {
    counter_t value{};
    if (::eventfd_read(handle(), &value)) return std::nullopt;
    return value;
  }

  // Read the counter into `value`.
  //
  // Returns true on success. In counter mode (the default), drains the full
  // accumulated count and resets to 0. In semaphore mode, always sets `value`
  // to 1 and decrements the counter by 1.
  [[nodiscard]] bool read(counter_t& value) const noexcept {
    return ::eventfd_read(handle(), &value) == 0;
  }

#pragma endregion
#pragma region Workers
private:
  // Create a non-blocking counter-mode eventfd, initially 0.
  [[nodiscard]] static os_file do_create() noexcept {
    return os_file{::eventfd(0, *default_flags)};
  }

  // Add 1 to the counter.
  [[nodiscard]] bool do_notify() const noexcept {
    return ::eventfd_write(handle(), 1) == 0;
  }

  // Consume the accumulated count.
  //
  // Assumes the default non-blocking mode; in blocking mode, an already-clear
  // counter would block.
  [[nodiscard]] bool do_drain() const noexcept {
    counter_t value{};
    if (::eventfd_read(handle(), &value) == 0) return true;
    return !os_error::last().is_hard_error();
  }

  // Wait for readability via `poll`.
  //
  // A negative timeout would mean infinite, so clamp to [0, INT_MAX]
  // milliseconds. `poll` silently ignores a closed (-1) fd, so check for one
  // explicitly to fail fast, as Windows does.
  [[nodiscard]] wait_result do_wait_for(
      std::chrono::milliseconds timeout) const noexcept {
    if (!is_open()) return wait_result::failed;
    pollfd pfd{.fd = handle(), .events = POLLIN, .revents = 0};
    const auto ms = static_cast<int>(
        std::clamp<std::chrono::milliseconds::rep>(timeout.count(), 0,
            std::numeric_limits<int>::max()));
    const auto n = ::poll(&pfd, 1, ms);
    if (n > 0)
      return (pfd.revents & POLLIN)
                 ? wait_result::signaled
                 : wait_result::failed;
    return (n == 0) ? wait_result::timed_out : wait_result::failed;
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys

#endif // !_WIN32
