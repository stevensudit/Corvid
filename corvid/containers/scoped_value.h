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
#include "containers_shared.h"
#include <type_traits>
#include <utility>

namespace corvid { inline namespace container {
inline namespace value_scoping {

// RAII helper for temporarily changing a value and restoring it on scope exit.
// Marked `[[nodiscard]]` because discarding it defeats the scoped restore.
template<typename T>
class [[nodiscard]] scoped_value {
public:
  static_assert(std::is_move_constructible_v<T>,
      "scoped_value requires T to be move constructible");
  static_assert(std::is_nothrow_swappable_v<T>,
      "scoped_value requires T to be nothrow swappable");

  explicit scoped_value(T& target, T newValue) noexcept(
      std::is_nothrow_move_constructible_v<T>)
      : target_(target), oldValue_(std::move(newValue)) {
    using std::swap;
    swap(target_, oldValue_);
  }

  ~scoped_value() noexcept {
    using std::swap;
    swap(target_, oldValue_);
  }

  scoped_value(const scoped_value&) = delete;
  scoped_value(scoped_value&&) = delete;
  scoped_value& operator=(const scoped_value&) = delete;
  scoped_value& operator=(scoped_value&&) = delete;

private:
  T& target_;
  T oldValue_;
};

}}} // namespace corvid::container::value_scoping
