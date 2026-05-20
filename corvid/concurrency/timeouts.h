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
#include <chrono>
#include <cassert>
#include <cstdint>

#include "relaxed_atomic.h"

namespace corvid { inline namespace concurrency {

#pragma region timeouts

// Utility class for timeouts.
//
// Provides definitions of relevant types, a single point for replacing the
// real `std::chrono::steady_clock` with a fake clock for testing, some helper
// functions.
//
// Offers only static members and can be be inherited from.
struct timeouts {
public:
#pragma region Types
  using time_point_t = std::chrono::steady_clock::time_point;
  using duration_t = std::chrono::steady_clock::duration;

  using now_fnt = time_point_t (*)() noexcept;

#pragma endregion
#pragma region Clock

  // Single source of truth for the current time. When a steady clock timestamp
  // is needed, this method should be called instead of directly invoking
  // `std::chrono::steady_clock::now()`. By default, it points to
  // `std::chrono::steady_clock::now`, but tests can replace it with a fake
  // clock by calling `set_now_fn`.
  [[nodiscard]] static time_point_t now() noexcept { return now_fn(); }

  // Replace the clock function with `fn`. If `fn` is null, installs
  // `fake_now_cb`, which returns `fake_now`.
  static void set_now_fn(now_fnt fn = nullptr) noexcept {
    if (!fn) fn = fake_now_cb;
    now_fn = fn;
  }

  // When `fake_now_cb` is installed, this value is returned.
  static relaxed_atomic<time_point_t> fake_now;

  // Returns `fake_now`.
  static time_point_t fake_now_cb() noexcept { return fake_now; }

  // Function invoked by `now`.
  static relaxed_atomic<now_fnt> now_fn;

#pragma endregion
#pragma region Constants

  // Timeout mode.
  enum class mode : uint8_t {
    stopped, // Timeout fully stopped.
    paused,  // Timeout will not trigger, but is not stopped.
    running, // Timeout will trigger.
  };

  // Sentinel value used to enter logical pause mode. One decade shy of
  // `time_point_t::max()`, leaving ample headroom against overflow when
  // arithmetic is done on it.
  static constexpr time_point_t paused_expiration =
      time_point_t::max() - std::chrono::years{10};

#pragma endregion
#pragma region Conversions

  [[nodiscard]] static uint64_t as_nanoseconds(time_point_t tp) noexcept {
    assert(tp >= time_point_t{});
    if (tp == time_point_t::max()) return UINT64_MAX;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        tp.time_since_epoch())
        .count();
  }

  [[nodiscard]] static time_point_t from_nanoseconds(uint64_t ns) noexcept {
    return from_nanoseconds(static_cast<int64_t>(ns));
  }

  [[nodiscard]] static time_point_t from_nanoseconds(int64_t ns) noexcept {
    // Map UINT64_MAX sentinel to our  max.
    if (ns < 0) return time_point_t::max();
    return time_point_t(std::chrono::nanoseconds(ns));
  }

#pragma endregion
};

inline relaxed_atomic<timeouts::now_fnt> timeouts::now_fn{
    &std::chrono::steady_clock::now};

inline relaxed_atomic<timeouts::time_point_t> timeouts::fake_now{};

#pragma endregion

}} // namespace corvid::concurrency
