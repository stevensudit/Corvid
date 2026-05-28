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
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>

namespace corvid { inline namespace infra {

#pragma region global_clock

// Indirection over `Clock`. Provides a single point where production reads the
// wall-clock and tests can install a fake.
//
// Static-only; the class exists to namespace the clock state and the
// replaceable `now()` entry point. Atomic ops use relaxed ordering on the
// read path (single-threaded fairness isn't needed because the value being
// loaded is just a function pointer or time_point that updates rarely).
//
// The `size_t` template parameter is just to give each instantiation a unique
// address for its statics, so different clock types don't step on each other's
// `now_fn_` and `fake_now_`. This can happen with, for example,
// `high_resolution_clock`, which is typically defined as a typedef of another
// clock.
template<typename Clock, size_t = 0>
class global_clock {
public:
  using clock_t = Clock;

#pragma region Types

  using time_point_t = typename clock_t::time_point;
  using duration_t = typename clock_t::duration;
  using now_fnt = time_point_t (*)() noexcept(noexcept(Clock::now()));

#pragma endregion

  global_clock() = delete;

#pragma region Clock

  // Single source of truth for the current time. When a steady-clock timestamp
  // is needed, call this instead of `clock_t::now` directly, which is where it
  // points to by default. Tests can replace it via `set_now_fn`.
  [[nodiscard]] static time_point_t now() noexcept(noexcept(Clock::now())) {
    return now_fn_.load(std::memory_order::relaxed)();
  }

  // Install a custom clock function. Passing null (the default) installs the
  // fake-clock callback, which returns whatever `set_fake_now` last stored.
  static void set_now_fn(now_fnt fn = nullptr) noexcept {
    if (!fn) fn = fake_now_cb;
    now_fn_.store(fn, std::memory_order::relaxed);
  }

  // Set the value returned by the fake clock. No-op unless `set_now_fn()` has
  // installed `fake_now_cb`.
  static void set_fake_now(time_point_t tp) noexcept {
    fake_now_.store(tp, std::memory_order::relaxed);
  }

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
    // Map UINT64_MAX sentinel to our max.
    if (ns < 0) return time_point_t::max();
    return time_point_t(std::chrono::nanoseconds(ns));
  }

#pragma endregion
#pragma region Helpers
private:
  static time_point_t fake_now_cb() noexcept {
    return fake_now_.load(std::memory_order::relaxed);
  }

#pragma endregion
#pragma region Data members

  inline static std::atomic<time_point_t> fake_now_{};
  inline static std::atomic<now_fnt> now_fn_{&clock_t::now};

#pragma endregion
};

#pragma endregion
#pragma region Clocks

using steady_clock = global_clock<std::chrono::steady_clock, 1>;
using system_clock = global_clock<std::chrono::system_clock, 2>;
using file_clock = global_clock<std::chrono::file_clock, 3>;
using high_resolution_clock =
    global_clock<std::chrono::high_resolution_clock, 4>;
using utc_clock = global_clock<std::chrono::utc_clock, 5>;
using gps_clock = global_clock<std::chrono::gps_clock, 6>;
using tai_clock = global_clock<std::chrono::tai_clock, 7>;

#pragma endregion

}} // namespace corvid::infra
