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
#ifndef CORVID_IMP_LINUX_OS_EVENT_H
#ifndef CORVID_OS_EVENT_ENTRY
#if defined(CORVID_CLANGD) || !defined(NDEBUG)
// Reroute a standalone dev-time inclusion through the entry point.
#include "../os_event.h"
#else
#error "Include \"os_event.h\" instead of this implementation header."
#endif
#else
#define CORVID_IMP_LINUX_OS_EVENT_H

#include <optional>
#include <utility>

#include <sys/eventfd.h>

#include "../../enums/bool_enums.h"
#include "../os_file.h"

// Linux implementation of "os_event.h", wrapping an `eventfd`.

namespace corvid { inline namespace filesys {
using namespace bool_enums;

#pragma region os_event

// Wake-up event backed by a Linux `eventfd`. See "os_event.h" for the
// portable contract.
//
// `os_event` owns a single eventfd handle and inherits the general fd helpers
// from `os_file`. The raw handle can be registered with a poll set such as
// `epoll`. Beyond the portable surface, it adds typed counter-based read
// operations and the counter/semaphore and blocking mode choices.
class [[nodiscard]] os_event: public os_file {
public:
#pragma region Types

  using handle_t = os_file::file_handle_t;
  using counter_t = eventfd_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;
  static constexpr int default_flags = EFD_CLOEXEC | EFD_NONBLOCK;

#pragma endregion
#pragma region Construction

  os_event() noexcept = default;
  explicit os_event(os_file&& file) noexcept : os_file{std::move(file)} {}

  os_event(os_event&&) noexcept = default;
  os_event(const os_event&) = delete;

  os_event& operator=(os_event&&) noexcept = default;
  os_event& operator=(const os_event&) = delete;

  // Create an `os_event` with `initial_value`. Defaults to non-blocking
  // counter mode (`EFD_CLOEXEC | EFD_NONBLOCK`); pass `event_mode::semaphore`
  // to add `EFD_SEMAPHORE`, or `execution::blocking` to omit `EFD_NONBLOCK`.
  [[nodiscard]] static os_event create(counter_t initial_value = 0,
      event_mode mode = event_mode::counter,
      execution exec = execution::nonblocking) noexcept {
    int flags = EFD_CLOEXEC;
    if (mode == event_mode::semaphore) flags |= EFD_SEMAPHORE;
    if (exec == execution::nonblocking) flags |= EFD_NONBLOCK;
    return os_event{os_file{::eventfd(initial_value, flags)}};
  }

#pragma endregion
#pragma region Operations

  // Add `value` to the counter. Returns true on success.
  [[nodiscard]] bool notify(counter_t value = 1) const noexcept {
    return ::eventfd_write(handle(), value) == 0;
  }

  // Consume any pending notifications so a subsequent wait blocks until the
  // next `notify`. Returns true when the event was drained or already clear,
  // false on hard error. Assumes the default non-blocking mode; in blocking
  // mode, an already-clear counter would block.
  [[nodiscard]] bool drain() const noexcept {
    counter_t value{};
    if (::eventfd_read(handle(), &value) == 0) return true;
    return !os_error::last().is_hard_error();
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
};

#pragma endregion
}} // namespace corvid::filesys

#endif // CORVID_OS_EVENT_ENTRY
#endif // CORVID_IMP_LINUX_OS_EVENT_H
