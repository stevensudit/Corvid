// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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

namespace corvid::atomic_tomb {

// A tombstone is a thread-safe, atomic value that can be set to a final value
// which indicates that it is dead.
//
// Once dead, it cannot be changed back to a live value. This is useful for
// resources that should not be reused after being released, such as in a
// resource pool or when implementing a "soft delete" pattern.
//
// By default, the tombstone holds a `bool` that is initially `false` and can
// be set to `true` to indicate that it is dead. As such, a variable name like
// `canceled` makes sense.
//
// Note that there is inherently a race condition in that the value could be
// changed after it's retrieved but before it's used. This is not actually a
// problem, as even a full lock would have the same behavior.
template<typename T = bool, T FV = true, T IV = {}>
class tombstone_of {
public:
  using value_type = T;
  using atomic_type = std::atomic<value_type>;
  static constexpr value_type final_v = FV;
  static constexpr value_type initial_v = IV;

  static_assert(atomic_type::is_always_lock_free,
      "Atomic type must be lock-free for tombstone to work correctly.");
  static_assert(initial_v != final_v,
      "Initial value must not be the same as final value.");

  tombstone_of() = default;

  tombstone_of(const tombstone_of&) = delete;
  tombstone_of(tombstone_of&&) = delete;
  tombstone_of& operator=(const tombstone_of&) = delete;
  tombstone_of& operator=(tombstone_of&&) = delete;

  explicit tombstone_of(value_type v) noexcept : value_{v} {}

  void kill() noexcept { value_.store(final_v, std::memory_order_release); }

  [[nodiscard]] bool dead() const noexcept { return value_.load() == final_v; }
  [[nodiscard]] explicit operator bool() const noexcept { return dead(); }
  [[nodiscard]] bool operator!() const noexcept { return !dead(); }

  value_type get() const noexcept { return value_.load(); }
  value_type operator*() const noexcept { return get(); }

  // Update the value atomically, if not dead.
  void set(value_type v) noexcept {
    value_type expected = value_.load(std::memory_order_acquire);
    while (expected != final_v) {
      if (value_.compare_exchange_weak(expected, v, std::memory_order_acq_rel,
              std::memory_order_acquire))
        break;
    }
  }
  tombstone_of& operator=(const value_type& other) noexcept {
    set(other);
    return *this;
  }

  // Decrement if not dead.
  tombstone_of& operator--() noexcept {
    value_type expected = value_.load(std::memory_order_acquire);
    while (expected != final_v) {
      if (value_.compare_exchange_weak(expected, expected - 1,
              std::memory_order_acq_rel, std::memory_order_acquire))
        break;
    }
    return *this;
  }

  // Increment if not dead.
  tombstone_of& operator++() noexcept {
    value_type expected = value_.load(std::memory_order_acquire);
    while (expected != final_v) {
      if (value_.compare_exchange_weak(expected, expected + 1,
              std::memory_order_acq_rel, std::memory_order_acquire))
        break;
    }
    return *this;
  }

private:
  atomic_type value_{initial_v};
};

using tombstone = tombstone_of<bool, true, false>;

} // namespace corvid::atomic_tomb
