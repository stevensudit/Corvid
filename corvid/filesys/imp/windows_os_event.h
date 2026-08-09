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
#ifndef CORVID_IMP_WINDOWS_OS_EVENT_H
#ifndef CORVID_OS_EVENT_ENTRY
#if defined(CORVID_CLANGD) || !defined(NDEBUG)
// Reroute a standalone dev-time inclusion through the entry point.
#include "../os_event.h"
#else
#error "Include \"os_event.h\" instead of this implementation header."
#endif
#else
#define CORVID_IMP_WINDOWS_OS_EVENT_H

#include <utility>

#include "../os_file.h"

// Windows implementation of "os_event.h", wrapping a Win32 event.

namespace corvid { inline namespace filesys {

#pragma region os_event

// Wake-up event backed by a Win32 manual-reset event. See "os_event.h" for
// the portable contract.
//
// `os_event` owns a single event handle and inherits the general handle
// helpers from `os_file`. The raw handle can be waited on directly, as with
// `WaitForSingleObject`. The event is manual-reset so that any number of
// `notify` calls coalesce into one signaled state that stays set until
// `drain` resets it, mirroring the level-triggered readability of the Linux
// eventfd counter.
class [[nodiscard]] os_event: public os_file {
public:
#pragma region Types

  using handle_t = os_file::file_handle_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;

#pragma endregion
#pragma region Construction

  os_event() noexcept = default;
  explicit os_event(os_file&& file) noexcept : os_file{std::move(file)} {}

  os_event(os_event&&) noexcept = default;
  os_event(const os_event&) = delete;

  os_event& operator=(os_event&&) noexcept = default;
  os_event& operator=(const os_event&) = delete;

  // Create an `os_event`, initially clear.
  [[nodiscard]] static os_event create() noexcept {
    return os_event{os_file{::CreateEventW(nullptr, TRUE, FALSE, nullptr)}};
  }

#pragma endregion
#pragma region Operations

  // Signal the event. Returns true on success.
  [[nodiscard]] bool notify() const noexcept {
    return ::SetEvent(handle()) != 0;
  }

  // Consume any pending notifications so a subsequent wait blocks until the
  // next `notify`. Returns true when the event was drained or already clear,
  // false on hard error.
  [[nodiscard]] bool drain() const noexcept {
    return ::ResetEvent(handle()) != 0;
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys

#endif // CORVID_OS_EVENT_ENTRY
#endif // CORVID_IMP_WINDOWS_OS_EVENT_H
