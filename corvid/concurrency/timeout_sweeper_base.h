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
#include <cstdint>

namespace corvid { inline namespace concurrency {

// Common types and constants for `timeout_sweeper` and the timing-wheel
// variant that will eventually share its callback contract.
class timeout_sweeper_base {
public:
  // Timeout mode. Used in `idle_timeout`.
  enum class mode : uint8_t {
    stopped, // Not participating in the sweeper.
    paused,  // Parked at `paused_expiration`; clips periodically.
    running, // Counting down toward a real deadline.
  };

  using time_point_t = std::chrono::steady_clock::time_point;
  using duration_t = std::chrono::steady_clock::duration;
  using now_fn_t = time_point_t (*)() noexcept;

  // Sentinel value used to enter logical pause mode. One year shy of
  // `time_point_t::max()`, leaving ample headroom against overflow when
  // arithmetic is done on it.
  static constexpr time_point_t paused_expiration =
      time_point_t::max() - std::chrono::years{1};

  timeout_sweeper_base() noexcept : now_fn_{&std::chrono::steady_clock::now} {}

  // Single source of truth for the current time. Invokes the injected
  // clock function; defaults to `std::chrono::steady_clock::now`. Tests
  // can substitute a fake clock via `set_now_fn`.
  [[nodiscard]] time_point_t now() const noexcept { return now_fn_(); }

  // Replace the clock function.
  void set_now_fn(now_fn_t fn) noexcept { now_fn_ = fn; }

private:
  now_fn_t now_fn_;
};
}} // namespace corvid::concurrency
