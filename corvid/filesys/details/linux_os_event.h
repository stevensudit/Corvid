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

#include <optional>

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
  static constexpr int default_flags = EFD_CLOEXEC | EFD_NONBLOCK;

#pragma endregion
#pragma region Construction

  using base::base;
  using base::create;

  // Create an `os_event` with `initial_value`. Pass `event_mode::semaphore`
  // to add `EFD_SEMAPHORE`, or `execution::blocking` to omit `EFD_NONBLOCK`.
  // The first parameter takes no default so that the portable no-argument
  // `create` stays unambiguous; it is the equivalent of passing 0 here.
  [[nodiscard]] static os_event create(counter_t initial_value,
      event_mode mode = event_mode::counter,
      execution exec = execution::nonblocking) noexcept {
    int flags = EFD_CLOEXEC;
    if (mode == event_mode::semaphore) flags |= EFD_SEMAPHORE;
    if (exec == execution::nonblocking) flags |= EFD_NONBLOCK;
    return os_event{os_file{::eventfd(initial_value, flags)}};
  }

#pragma endregion
#pragma region Operations

  using base::notify;

  // Add `value` to the counter. Returns true on success.
  [[nodiscard]] bool notify(counter_t value) const noexcept {
    return ::eventfd_write(handle(), value) == 0;
  }

  // Read the counter, returning the consumed value on success. In counter
  // mode (the default), drains the full accumulated count and resets to 0.
  // In semaphore mode, always returns 1 and decrements the counter by 1.
  [[nodiscard]] std::optional<counter_t> read() const noexcept {
    counter_t value{};
    if (::eventfd_read(handle(), &value) != 0) return std::nullopt;
    return value;
  }

  // Read the counter into `value`. Returns true on success. In counter mode
  // (the default), drains the full accumulated count and resets to 0. In
  // semaphore mode, always sets `value` to 1 and decrements the counter by 1.
  [[nodiscard]] bool read(counter_t& value) const noexcept {
    return ::eventfd_read(handle(), &value) == 0;
  }

#pragma endregion
#pragma region Workers
private:
  // Create a non-blocking counter-mode eventfd, initially 0.
  [[nodiscard]] static os_file do_create() noexcept {
    return os_file{::eventfd(0, default_flags)};
  }

  // Add 1 to the counter.
  [[nodiscard]] bool do_notify() const noexcept {
    return ::eventfd_write(handle(), 1) == 0;
  }

  // Consume the accumulated count. Assumes the default non-blocking mode; in
  // blocking mode, an already-clear counter would block.
  [[nodiscard]] bool do_drain() const noexcept {
    counter_t value{};
    if (::eventfd_read(handle(), &value) == 0) return true;
    return !os_error::last().is_hard_error();
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys
