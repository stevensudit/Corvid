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

#include "os_file.h"

namespace corvid { inline namespace filesys {

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

  // Add `value` to the counter. Returns true on success.
  [[nodiscard]] bool notify(counter_t value = 1) const noexcept {
    return ::eventfd_write(handle(), value) == 0;
  }

  // Read and reset the counter, returning the consumed value on success.
  [[nodiscard]] std::optional<counter_t> read() const noexcept {
    counter_t value{};
    if (::eventfd_read(handle(), &value) != 0) return std::nullopt;
    return value;
  }

  // Read and reset the counter into `value`. Returns true on success.
  [[nodiscard]] bool read(counter_t& value) const noexcept {
    return ::eventfd_read(handle(), &value) == 0;
  }
};

}} // namespace corvid::filesys
