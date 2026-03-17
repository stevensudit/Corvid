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
#include <optional>
#include <utility>

#include <sys/eventfd.h>

#include "../enums/bool_enums.h"
#include "os_file.h"

namespace corvid { inline namespace filesys {
using namespace bool_enums;

// RAII wrapper around Linux `eventfd`.
//
// `event_fd` owns a single eventfd handle, inherits the general fd helpers
// from `os_file`, and adds typed counter-based read/write operations.
class [[nodiscard]] event_fd: public os_file {
public:
  using handle_t = os_file::file_handle_t;
  using counter_t = eventfd_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;
  static constexpr int default_flags = EFD_CLOEXEC | EFD_NONBLOCK;

  event_fd() noexcept = default;
  explicit event_fd(counter_t initial_value,
      int flags = default_flags) noexcept
      : os_file(::eventfd(initial_value, flags)) {}
  explicit event_fd(os_file&& file) noexcept : os_file(std::move(file)) {}

  event_fd(event_fd&&) noexcept = default;
  event_fd(const event_fd&) = delete;

  event_fd& operator=(event_fd&&) noexcept = default;
  event_fd& operator=(const event_fd&) = delete;

  ~event_fd() = default;

  // Create an `event_fd` with `initial_value`. Defaults to non-blocking
  // counter mode (`EFD_CLOEXEC | EFD_NONBLOCK`); pass `event_mode::semaphore`
  // to add `EFD_SEMAPHORE`, or `execution::blocking` to omit `EFD_NONBLOCK`.
  [[nodiscard]] static event_fd create(counter_t initial_value = 0,
      event_mode mode = event_mode::counter,
      execution exec = execution::nonblocking) noexcept {
    int flags = EFD_CLOEXEC;
    if (mode == event_mode::semaphore) flags |= EFD_SEMAPHORE;
    if (exec == execution::nonblocking) flags |= EFD_NONBLOCK;
    return event_fd{initial_value, flags};
  }

  // Add `value` to the counter. Returns true on success.
  [[nodiscard]] bool notify(counter_t value = 1) const noexcept {
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
};

}} // namespace corvid::filesys
