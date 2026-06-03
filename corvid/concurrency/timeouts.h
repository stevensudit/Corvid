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

#include "../infra/clocks.h"

namespace corvid { inline namespace concurrency {

#pragma region timeouts

// Shared utilities for timeout implementations.
//
// Offers only static members and can be inherited from.
struct timeouts {
public:
#pragma region Constants

  // Timeout mode.
  enum class mode : uint8_t {
    stopped, // Timeout fully stopped.
    paused,  // Timeout will not trigger, but is not stopped.
    running, // Timeout will trigger.
  };

  // Sentinel value used to enter logical pause mode. One decade shy of
  // `time_point_t::max`, leaving ample headroom against overflow when
  // arithmetic is done on it.
  static constexpr steady_now_clock::time_point_t paused_expiration =
      steady_now_clock::time_point_t::max() - std::chrono::years{10};

#pragma endregion
};

#pragma endregion

}} // namespace corvid::concurrency
