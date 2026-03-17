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

// A value of type `T` guarded by a mutex and condition variable.
//
// Bundles the three ingredients that `std::condition_variable` always needs
// together -- the mutex, the CV, and the guarded state -- so that callers
// cannot accidentally omit any of them.
//
// The mutating methods (`notify`, `modify_and_notify`) hold the lock while
// updating the value, release it, then call `notify_all()`. This is the
// correct ordering that prevents missed wakeups.
//
// The waiting methods (`wait_until`, `wait_for`) always supply a predicate to
// `std::condition_variable::wait*`, eliminating spurious-wakeup bugs.
//
// Not copyable or movable (owns a `std::mutex`).
//
// Typical use -- signal a bool flag from another thread:
//
//   // Type deduced by CTAD, but could be explicitly specified.
//   notifiable running{false};
//
//   // Setter thread:
//   running.notify(true);
//
//   // Waiting thread:
//   running.wait_until_value(false);
template<typename T>
class notifiable {
public:
  explicit notifiable(T initial = {}) : value_(std::move(initial)) {}

  notifiable(const notifiable&) = delete;
  notifiable& operator=(const notifiable&) = delete;

  // Set value to `val` under lock, then wake all waiters.
  //
  // `notify_all()` is called even if the move assignment throws, so waiters
  // are never left stuck on a value that was partially modified.
  void notify(T val) {
    auto notify = make_scope_exit([&]() noexcept { cv_.notify_all(); });
    std::scoped_lock lock{mutex_};
    value_ = std::move(val);
  }

  // Invoke `fn(value)` under lock, then wake all waiters. Use this when the
  // new value depends on the old one (e.g., incrementing a counter).
  //
  // `notify_all()` is called even if `fn` throws, so waiters are never left
  // stuck on a value that was partially modified before the exception.
  template<std::invocable<T&> Fn>
  void modify_and_notify(Fn fn) {
    auto notify = make_scope_exit([&]() noexcept { cv_.notify_all(); });
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
  [[nodiscard]] T wait_until(Pred pred) const {
    std::unique_lock lock{mutex_};
    cv_.wait(lock, [&] { return pred(value_); });
    return value_;
  }

  // Block until `value == target`.
  void wait_until_value(const T& target) const {
    wait_until([&](const T& v) { return v == target; });
  }

  // Block until `pred(value)` returns true or `timeout` elapses.
  //
  // Returns the matching value if the predicate was satisfied before the
  // deadline, or `nullopt` if the timeout expired first.
  template<typename Rep, typename Period, std::predicate<const T&> Pred>
  [[nodiscard]] std::optional<T>
  wait_for(std::chrono::duration<Rep, Period> timeout, Pred pred) const {
    std::unique_lock lock{mutex_};
    if (cv_.wait_for(lock, timeout, [&] { return pred(value_); }))
      return value_;
    return std::nullopt;
  }

  // Block until `value == target` or `timeout` elapses.
  //
  // Returns true if the target was reached before the deadline, false if the
  // timeout expired first.
  template<typename Rep, typename Period>
  [[nodiscard]] bool wait_for_value(std::chrono::duration<Rep, Period> timeout,
      const T& target) const {
    return wait_for(timeout, [&](const T& v) {
      return v == target;
    }).has_value();
  }

private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  T value_;
};

}}} // namespace corvid::container::notifiable_ns
