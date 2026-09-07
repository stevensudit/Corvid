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
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

#include "../enums/sequence_enum.h"
#include "os_file.h"

namespace corvid { inline namespace filesys {
using namespace std::chrono_literals;

#pragma region wait_result

// Outcome of a timed wait on an `os_event`.
enum class wait_result : uint8_t { failed, timed_out, signaled };
consteval auto corvid_enum_spec(wait_result*) {
  return corvid::enums::sequence::make_sequence_enum_spec<wait_result,
      "failed,timed_out,signaled">();
}

#pragma endregion
#pragma region os_event_base

// CRTP base defining the portable `os_event` interface.
//
// `os_event` is a wake-up event for signaling across threads, using an
// `eventfd` on Linux and a Win32 manual-reset event on Windows. It derives
// from `os_file` and owns its handle the same way.
//
// `wait_for` provides a simple timed wait. Waiting on the event alongside
// other work is a platform matter, driven through the raw handle (as by
// dereferencing), such as registering it with `epoll` on Linux or passing it
// to `WaitForMultipleObjects` on Windows.
//
// The platform implementation derives from this base, supplying the `do_`
// workers and any extras, such as the counter and semaphore modes on Linux.
template<typename Derived>
class os_event_base: public os_file {
public:
#pragma region Types

  using handle_t = os_file::file_handle_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;

#pragma endregion
#pragma region Construction

  // Public so that `Derived` can expose them with `using base::base`; only
  // `Derived` may actually inherit from this class.
  // NOLINTBEGIN(bugprone-crtp-constructor-accessibility)

  // Holds no event by default.
  os_event_base() noexcept = default;

  // Adopt an event held in an `os_file`.
  explicit os_event_base(os_file&& file) noexcept : os_file{std::move(file)} {}

  // NOLINTEND(bugprone-crtp-constructor-accessibility)

  // Create an event in the portable default configuration, initially clear.
  //
  // Check event for validity with `operator bool`, and optionally follow up
  // with `os_error::last()`.
  [[nodiscard]] static Derived create() noexcept {
    return Derived{Derived::do_create()};
  }

#pragma endregion
#pragma region Operations

  // Signal the event so that at least one waiter wakes.
  [[nodiscard]] bool notify() const noexcept {
    return static_cast<const Derived&>(*this).do_notify();
  }

  // Consume any pending notifications so a subsequent wait blocks until the
  // next `notify`.
  [[nodiscard]] bool drain() const noexcept {
    return static_cast<const Derived&>(*this).do_drain();
  }

  // Wait until the event is signaled or `timeout` elapses.
  //
  // A negative `timeout` counts as zero, not infinite. Does not consume the
  // notification; `drain` does. An interrupted wait reports `failed`; re-wait
  // if that matters.
  [[nodiscard]] wait_result wait_for(
      std::chrono::milliseconds timeout) const noexcept {
    return static_cast<const Derived&>(*this).do_wait_for(
        std::max(timeout, 0ms));
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys
