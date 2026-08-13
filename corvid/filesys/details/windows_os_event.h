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

#include "../os_event_base.h"

// Windows implementation of "os_event.h", wrapping a Win32 event.

namespace corvid { inline namespace filesys {

#pragma region os_event

// Wake-up event backed by a Win32 manual-reset event. The portable interface
// is defined by `os_event_base` in "os_event_base.h".
//
// The event is manual-reset so that any number of `notify` calls coalesce
// into one signaled state that stays set until `drain` resets it, mirroring
// the level-triggered readability of the Linux eventfd counter.
class [[nodiscard]] os_event: public os_event_base<os_event> {
  using base = os_event_base<os_event>;
  friend base;

public:
  using base::base;

#pragma region Workers
private:
  // Create a manual-reset event, initially clear.
  [[nodiscard]] static os_file do_create() noexcept {
    return os_file{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  }

  // Signal the event.
  [[nodiscard]] bool do_notify() const noexcept {
    return ::SetEvent(handle()) != 0;
  }

  // Reset the event to clear.
  [[nodiscard]] bool do_drain() const noexcept {
    return ::ResetEvent(handle()) != 0;
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys
