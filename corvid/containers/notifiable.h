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
#include "scope_exit.h"

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace corvid { inline namespace container {
inline namespace notifiable_ns {

namespace details {
// Helper: the "plain" value type for `notifiable<T>`. For a non-atomic `T`
// this is `T` itself; for `std::atomic<U>` it is `U`. Implemented as a
// partial specialization rather than `std::conditional_t` to avoid eagerly
// instantiating `typename T::value_type` for non-atomic types.
template<typename T>
struct notifiable_result {
  using type = T;
};
template<typename U>
struct notifiable_result<std::atomic<U>> {
  using type = U;
};
} // namespace details

// A value of type `T` guarded by a mutex and condition variable.
//
// Bundles the three ingredients that this pattern always needs together: the
// mutex, the CV, and the guarded state. Enforces safe patterns of use and
// avoids tricky edge conditions.
//
// The mutating methods (`notify`, `notify_one`, `modify_and_notify`,
// `modify_and_notify_one`) hold the lock while updating the value, release it,
// then call `std::condition_variable::notify_all()` or
// `std::condition_variable::notify_one()`. This is the correct ordering that
// prevents missed wakeups.
//
// All waiting methods always supply a predicate to
// `std::condition_variable::wait*`, eliminating spurious-wakeup bugs.
//
// Not copyable or movable (owns a `std::mutex`).
//
// Typical use, signaling a bool flag from another thread:
//
//   // The `bool` type is deduced by CTAD, but could be explicitly specified.
//   notifiable running{false};
//
//   // Setter thread:
//   running.notify(true);
//
//   // Waiting thread:
//   running.wait_until_value(true);
//
// ---
//
// About `std::atomic`:
//
// `T` may be a specialization of `std::atomic`. In that case, `get()` skips
// the mutex and delegates directly to `T::load()`, so readers pay no locking
// cost while writers still go through the mutex to ensure correct
// notification ordering.
//
// When considering `notifiable<std::atomic<U>>` vs. `std::atomic<U>`'s own
// `wait`/`notify_one`/`notify_all` interface (added in C++20), prefer the
// native interface when raw throughput matters most: it avoids a mutex
// entirely and is typically implemented with a futex or equivalent. Prefer
// `notifiable` when you need features the native interface lacks:
//   - Timeouts: `wait_for` / `wait_for_changed` with a duration.
//   - Predicate waiting: block until an arbitrary condition holds, not just
//     until the value changes.
template<typename T>
class notifiable {
public:
  // Return type of `wait_until` / `wait_until_changed` / `wait_for` /
  // `wait_for_changed`, and parameter type of `notify` / `notify_one`.
  // For non-atomic `T` this is `T` itself; for `std::atomic<U>` it is `U`,
  // since atomics are neither copyable nor movable.
  using value_t = details::notifiable_result<T>::type;

  static_assert(
      std::copyable<T> ||
          (is_specialization_of_v<T, std::atomic> && std::copyable<value_t>),
      "notifiable<T> requires T to be copyable, or a std::atomic<U> where U "
      "is copyable");

  explicit notifiable(value_t initial = {}) : value_(std::move(initial)) {}

  notifiable(const notifiable&) = delete;
  notifiable& operator=(const notifiable&) = delete;

  // Set value to `val` under lock, then wake all waiters.
  //
  // `notify_all()` is called even if the move assignment throws, so waiters
  // are never left stuck.
  void notify(value_t val) {
    auto on_exit = make_scope_exit([&]() noexcept { cv_.notify_all(); });
    std::scoped_lock lock{mutex_};
    value_ = std::move(val);
  }

  // Set value to `val` under lock, then wake one waiter.
  //
  // Prefer `notify` (which wakes all) unless there is exactly one waiter or
  // the change is single-consumer (though note that there is no guarantee that
  // the waiter will be woken before the value changes again).
  void notify_one(value_t val) {
    auto on_exit = make_scope_exit([&]() noexcept { cv_.notify_one(); });
    std::scoped_lock lock{mutex_};
    value_ = std::move(val);
  }

  // Call `modify_value(value_)` under lock, then wake all waiters.
  // `modify_value` receives `value_` by `T&` and modifies it in place
  // (e.g., `[](int& v){ ++v; }`). Use this when the new value depends on the
  // old one.
  //
  // `notify_all()` is called even if `modify_value` throws, so waiters are
  // never left stuck.
  void modify_and_notify(std::invocable<T&> auto modify_value) {
    auto on_exit = make_scope_exit([&]() noexcept { cv_.notify_all(); });
    std::scoped_lock lock{mutex_};
    modify_value(value_);
  }

  // Call `modify_value(value_)` under lock, then wake one waiter.
  //
  // Prefer `modify_and_notify` (which wakes all) unless there is exactly one
  // waiter or the change is single-consumer (though note that there is no
  // guarantee that the waiter will be woken before the value changes again).
  void modify_and_notify_one(std::invocable<T&> auto modify_value) {
    auto on_exit = make_scope_exit([&]() noexcept { cv_.notify_one(); });
    std::scoped_lock lock{mutex_};
    modify_value(value_);
  }

  // Return a snapshot of the current value under lock.
  [[nodiscard]] T get() const
  requires(!is_specialization_of_v<T, std::atomic>)
  {
    std::scoped_lock lock{mutex_};
    return value_;
  }

  // Return a snapshot of the current value, without locking the mutex. Only
  // available when `T` is a specialization of `std::atomic`. Returns
  // `T::value_type`.
  [[nodiscard]] value_t
  get(std::memory_order order = std::memory_order::relaxed) const
  requires(is_specialization_of_v<T, std::atomic>)
  {
    return value_.load(order);
  }

  // Block until `pred(value)` returns true; return the value at that point.
  template<std::predicate<const T&> Pred>
  [[nodiscard]] value_t wait_until(Pred pred) const {
    std::unique_lock lock{mutex_};
    cv_.wait(lock, [&] { return pred(value_); });
    return do_load();
  }

  // Block until `value == target`. `target` may be any type that `T` supports
  // `operator==` with (e.g., `std::string_view` when `T` is `std::string`).
  void wait_until_value(
      const std::equality_comparable_with<value_t> auto& target) const {
    (void)wait_until([&](const T& v) { return load_value(v) == target; });
  }

  // Block until `value` changes from its current state; return the new value.
  //
  // Spurious-wakeup safe because the predicate `value != old_value` is
  // re-evaluated on every wakeup. Subject to ABA: if the value changes and
  // then reverts before this thread is scheduled, the wakeup is suppressed and
  // the wait continues.
  [[nodiscard]] value_t wait_until_changed() const {
    std::unique_lock lock{mutex_};
    const auto old_value = do_load();
    cv_.wait(lock, [&] { return do_load() != old_value; });
    return do_load();
  }

  // Block until `pred(value)` returns true or `timeout` elapses.
  //
  // Returns the matching value if the predicate was satisfied before the
  // deadline, or `nullopt` if the timeout expired first.
  template<typename Rep, typename Period, std::predicate<const T&> Pred>
  [[nodiscard]] std::optional<value_t>
  wait_for(std::chrono::duration<Rep, Period> timeout, Pred pred) const {
    std::unique_lock lock{mutex_};
    if (cv_.wait_for(lock, timeout, [&] { return pred(value_); }))
      return do_load();
    return std::nullopt;
  }

  // Block until `value == target` or `timeout` elapses. `target` may be any
  // type that `T` supports `operator==` with. Returns true if the target was
  // reached before the deadline, false if the timeout expired first.
  template<typename Rep, typename Period>
  [[nodiscard]] bool wait_for_value(std::chrono::duration<Rep, Period> timeout,
      const std::equality_comparable_with<value_t> auto& target) const {
    return wait_for(timeout, [&](const T& v) {
      return load_value(v) == target;
    }).has_value();
  }

  // Block until `value` changes from its current state or `timeout` elapses.
  // Returns the new value if a change was observed, or `nullopt` on timeout.
  //
  // Subject to the same ABA caveat as `wait_until_changed`.
  template<typename Rep, typename Period>
  [[nodiscard]] std::optional<value_t>
  wait_for_changed(std::chrono::duration<Rep, Period> timeout) const {
    std::unique_lock lock{mutex_};
    const auto old_value = do_load();
    if (cv_.wait_for(lock, timeout, [&] { return do_load() != old_value; }))
      return do_load();
    return std::nullopt;
  }

  // Read a `T` as a `value_t`. For `std::atomic<U>`, uses a `relaxed` load
  // rather than the implicit conversion operator (which would be `seq_cst`).
  // This is correct when called inside the mutex, which already provides the
  // necessary ordering.
  [[nodiscard]] static value_t load_value(const T& v) {
    if constexpr (is_specialization_of_v<T, std::atomic>)
      return v.load(std::memory_order::relaxed);
    else
      return v;
  }

private:
  [[nodiscard]] value_t do_load() const { return load_value(value_); }

  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  T value_;
};

}}} // namespace corvid::container::notifiable_ns
