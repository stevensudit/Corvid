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

#include <optional>
#include <type_traits>
#include <utility>

namespace corvid { inline namespace container {
inline namespace value_scoping {

// RAII helper for temporarily changing a value and restoring it on scope exit.
template<typename T>
class [[nodiscard]] scoped_value {
public:
  static_assert(std::is_move_constructible_v<T>,
      "scoped_value requires T to be move constructible");
  static_assert(std::is_copy_constructible_v<T>,
      "scoped_value requires T to be copy constructible");
  static_assert(std::is_nothrow_swappable_v<T>,
      "scoped_value requires T to be nothrow swappable");

  // Construct a `scoped_value` that sets `target` to `new_value` for the
  // duration.
  explicit scoped_value(T& target, T new_value) noexcept(
      std::is_nothrow_move_constructible_v<T>)
      : target_(&target), old_value_(std::move(new_value)) {
    do_swap();
  }

  ~scoped_value() noexcept { restore(); }

  scoped_value(const scoped_value&) = delete;
  scoped_value(scoped_value&& other) noexcept(
      std::is_nothrow_move_constructible_v<T> &&
      std::is_nothrow_copy_constructible_v<T>)
      : target_(other.target_), old_value_(std::move(other.old_value_)) {
    other.release();
  }
  scoped_value& operator=(const scoped_value&) = delete;
  scoped_value& operator=(scoped_value&& other) noexcept(
      std::is_nothrow_move_assignable_v<T> &&
      std::is_nothrow_copy_constructible_v<T>) {
    if (this == &other) return *this;
    restore();

    target_ = other.target_;
    old_value_ = std::move(other.old_value_);

    other.release();
    return *this;
  }

  // Disarm the `scoped_value`, leaving the current value in place and
  // preventing any future restore.
  void release() noexcept(std::is_nothrow_copy_constructible_v<T>) {
    target_ = nullptr;
  }

private:
  T* target_{};
  T old_value_;

  void do_swap() noexcept(std::is_nothrow_move_constructible_v<T>) {
    using std::swap;
    swap(*target_, old_value_);
  }

  void restore() noexcept(std::is_nothrow_move_constructible_v<T>) {
    if (target_) do_swap();
  }
};

}}} // namespace corvid::container::value_scoping
