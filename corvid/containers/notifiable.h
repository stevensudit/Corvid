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
// Bundles the three ingredients that `std::condition_variable` always needs
// together: the mutex, the CV, and the guarded state. Enforces safe patterns
// of use and avoids tricky edge conditions.
//
// The mutating methods (`notify`, `modify_and_notify`) hold the lock while
// updating the value, release it, then call `notify_all()`. This is the
// correct ordering that prevents missed wakeups.
//
// All waiting methods always supply a predicate to
// `std::condition_variable::wait*`, eliminating spurious-wakeup bugs.
//
// Not copyable or movable (owns a `std::mutex`).
//
// Typical use -- signal a bool flag from another thread:
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
//
//   - Timeouts: `wait_for` / `wait_for_changed` with a duration.
//   - Predicate waiting: block until an arbitrary condition holds, not just
//     until the value changes.
template<typename T>
class notifiable {
public:
  // Return type of `wait_until` / `wait_until_changed` / `wait_for` /
  // `wait_for_changed`, and parameter type of `notify` / `notify_one`. For
  // non-atomic `T` this is `T` itself; for `std::atomic<U>` it is `U`, since
  // atomics are neither copyable nor movable.
  using value_t = typename details::notifiable_result<T>::type;

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

  // Invoke `fn(value)` under lock, then wake all waiters. Use this when the
  // new value depends on the old one (e.g., incrementing a counter).
  //
  // `notify_all()` is called even if `fn` throws, so waiters are never left
  // stuck.
  template<std::invocable<T&> Fn>
  void modify_and_notify(Fn fn) {
    auto on_exit = make_scope_exit([&]() noexcept { cv_.notify_all(); });
    std::scoped_lock lock{mutex_};
    fn(value_);
  }

  // Invoke `fn(value)` under lock, then wake one waiter.
  //
  // Prefer `modify_and_notify` (which wakes all) unless there is exactly one
  // waiter or the change is single-consumer (though note that there is no
  // guarantee that the waiter will be woken before the value changes again).
  template<std::invocable<T&> Fn>
  void modify_and_notify_one(Fn fn) {
    auto on_exit = make_scope_exit([&]() noexcept { cv_.notify_one(); });
    std::scoped_lock lock{mutex_};
    fn(value_);
  }

  // Return a snapshot of the current value under lock.
  [[nodiscard]] T get() const
  requires(!is_specialization_of_v<T, std::atomic>)
  {
    std::scoped_lock lock{mutex_};
    return value_;
  }

  // Load the atomic value without locking the mutex.
  //
  // Only available when `T` is a specialization of `std::atomic`. Returns
  // `T::value_type`. Defaults to `memory_order::relaxed`; pass a stricter
  // order when the load must synchronize with a specific store.
  [[nodiscard]] auto
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
    return static_cast<value_t>(value_);
  }

  // Block until `value == target`. `target` may be any type that `T` supports
  // `operator==` with (e.g., `std::string_view` when `T` is `std::string`).
  void wait_until_value(const auto& target) const {
    (void)wait_until([&](const T& v) { return v == target; });
  }

  // Block until `value` changes from its current state; return the new value.
  //
  // Spurious-wakeup safe: the predicate `value != old_value` is re-evaluated
  // on every wakeup. Subject to ABA: if the value changes and then reverts
  // before this thread is scheduled, the wakeup is suppressed and the wait
  // continues.
  [[nodiscard]] value_t wait_until_changed() const {
    std::unique_lock lock{mutex_};
    const auto old_value = static_cast<value_t>(value_);
    cv_.wait(lock, [&] { return static_cast<value_t>(value_) != old_value; });
    return static_cast<value_t>(value_);
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
      return static_cast<value_t>(value_);
    return std::nullopt;
  }

  // Block until `value == target` or `timeout` elapses. `target` may be any
  // type that `T` supports `operator==` with. Returns true if the target was
  // reached before the deadline, false if the timeout expired first.
  template<typename Rep, typename Period>
  [[nodiscard]] bool wait_for_value(std::chrono::duration<Rep, Period> timeout,
      const auto& target) const {
    return wait_for(timeout, [&](const T& v) {
      return v == target;
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
    const auto old_value = static_cast<value_t>(value_);
    if (cv_.wait_for(lock, timeout, [&] {
          return static_cast<value_t>(value_) != old_value;
        }))
      return static_cast<value_t>(value_);
    return std::nullopt;
  }

private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  T value_;
};

}}} // namespace corvid::container::notifiable_ns
