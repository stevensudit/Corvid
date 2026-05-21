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
#include <algorithm>
#include <chrono>
#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

#include "relaxed_atomic.h"
#include "timeouts.h"

namespace corvid { inline namespace concurrency {
#pragma region timeout_sweeper

// A min-heap of (`expiration`, `callback`) pairs, swept by an external driver
// that calls `tick` periodically.
//
// Each callback receives the expiration time at which it was registered and
// returns either the zero `time_point_t` (drop the entry) or a new expiration
// (rearm the same callback at that time). The sweeper has no cancel API:
// "cancel" is achieved by having the callback return zero on a future
// invocation. Idle pausing is achieved by having the callback rearm at a
// caller-defined far-future sentinel-driven cadence (see the example below).
//
// Compared to `timing_wheel`, the sweeper trades O(1) schedule for unbounded
// delay range and per-entry rearm logic chosen by the callback, and no
// synthetic counter. Compared to `timers`, it offers a much smaller API
// surface: no cancellation tokens, no recurrence parameters, no
// `timer_invocation` struct -- just register, tick, and a return-value
// contract.
//
// Thread safety: `schedule` is thread-safe and may be called from any thread,
// including re-entrantly from inside a callback. `tick` is intended to be
// called from a single driver thread. Callbacks are invoked outside the
// internal lock, so other threads may `schedule` while a callback is running.
// If a callback throws, the exception propagates out of `tick`; the popped
// entry has already been removed from the heap.
//
// Example: idle-read timeout tracking for a connection. The connection holds
// `relaxed_atomic<time_point_t> read_expiration_` and `duration_t
// read_timeout_`. The conn registers once on construction; thereafter every
// state change (extend, pause, resume) is a single write to `read_expiration_`
// with no further call into the sweeper.
//
//   using sweeper_t = timeout_sweeper<>;
//   // Initial registration. Set a real deadline, register, then mark as
//   // paused so the first real read activity will trigger a rearm.
//   conn->read_expiration_ = sweeper_t::now() + conn->read_timeout_;
//   sweeper.schedule(conn->read_expiration_,
//       [weak_conn = std::weak_ptr{conn}](sweeper_t::time_point_t expire)
//           -> sweeper_t::time_point_t {
//         auto conn = weak_conn.lock();
//         if (!conn) return {};
//         const auto current = conn->read_expiration_;
//         if (current == expire) { conn->close(); return {}; }
//         // Only needed in callback when timer can be logically paused.
//         if (current == sweeper_t::paused_expiration)
//           current = sweeper_t::now() + conn->read_timeout_;
//         return current;
//       });
//   // Logically pause. It will rearm every read_timeout_, without ever
//   // triggering. Setting read_expiration_ to an actual value will
//   // unpause. You can then re-pause at any time.
//   read_expiration_ = sweeper_t::paused_expiration;
//
// The default callback storage is `std::function`. Callers that want to
// avoid the heap allocation can specialize on a `fixed_function` sized to
// fit their captures (typically just a `weak_ptr`, so a capacity in the
// low tens of bytes is enough). Example:
//
//   using my_sweeper = timeout_sweeper<fixed_function<32,
//       std::chrono::steady_clock::time_point(
//           std::chrono::steady_clock::time_point)>>;
template<typename CB =
             std::function<timeouts::time_point_t(timeouts::time_point_t)>>
class timeout_sweeper: public timeouts {
#pragma region Types
public:
  using callback_t = CB;

  static_assert(
      std::is_invocable_r_v<time_point_t, callback_t&, time_point_t> &&
          std::is_default_constructible_v<callback_t> &&
          std::is_move_constructible_v<callback_t>,
      "callback_t must be invocable as time_point_t(time_point_t), "
      "default-constructible, and move-constructible");

#pragma endregion
#pragma region Construction

  timeout_sweeper() { heap_.reserve(64); };

  timeout_sweeper(const timeout_sweeper&) = delete;
  timeout_sweeper(timeout_sweeper&&) = delete;
  timeout_sweeper& operator=(const timeout_sweeper&) = delete;
  timeout_sweeper& operator=(timeout_sweeper&&) = delete;

  // Mark the sweeper as closing (further `schedule` calls are rejected) and
  // drain anything still in the heap by invoking every remaining callback
  // once.
  ~timeout_sweeper() noexcept(false) {
    closing_ = true;
    tick(time_point_t::max());
  }

#pragma endregion
#pragma region Modifiers

  // Discard all registered entries without invoking their callbacks.
  // Thread-safe.
  void clear() noexcept {
    std::scoped_lock lock{mutex_};
    heap_.clear();
  }

  // Register `callback` to be invoked once after `expire` has been reached.
  // Thread-safe.  Returns `false` if the sweeper is closing; in which case
  // `callback` is dropped on the floor.
  bool schedule(time_point_t expire, callback_t callback) {
    std::scoped_lock lock{mutex_};
    if (closing_) return false;
    heap_.push_back({expire, std::move(callback)});
    std::push_heap(heap_.begin(), heap_.end());
    return true;
  }

#pragma endregion
#pragma region Driver

  // Pop and invoke every callback whose expiration is at or before `now`.
  // Callbacks are invoked outside the internal lock; a non-zero return value
  // causes the callback to be re-inserted at the returned time.
  //
  // Intended to be called from a single driver thread.
  void tick(time_point_t now) {
    for (;;) {
      callback_t callback;
      time_point_t expire;

      if (std::scoped_lock lock{mutex_}; true) {
        // Stop when the heap is empty or the next entry hasn't expired yet.
        if (heap_.empty()) return;
        if (heap_.front().expire > now) return;

        // Move next entry to back and extract it.
        std::pop_heap(heap_.begin(), heap_.end());
        callback = std::move(heap_.back().callback);
        expire = heap_.back().expire;
        heap_.pop_back();
      }

      // Note how we pass `expire`, not `now`.
      const auto next = callback(expire);

      // Short-circuit the rearm path during shutdown so the drain terminates.
      if (next == time_point_t{} || closing_) continue;

      std::scoped_lock lock{mutex_};
      heap_.push_back({next, std::move(callback)});
      std::push_heap(heap_.begin(), heap_.end());
    }
  }

#pragma endregion
#pragma region Accessors

  // Number of registered entries.
  [[nodiscard]] size_t size() const noexcept {
    std::scoped_lock lock{mutex_};
    return heap_.size();
  }

  // True when no entries are registered.
  [[nodiscard]] bool empty() const noexcept {
    std::scoped_lock lock{mutex_};
    return heap_.empty();
  }

#pragma endregion
#pragma region Internals
private:
  struct entry {
    time_point_t expire;
    callback_t callback;

    friend bool operator<(const entry& lhs, const entry& rhs) noexcept {
      return lhs.expire > rhs.expire;
    }
  };

  mutable std::mutex mutex_;
  std::vector<entry> heap_;
  relaxed_atomic_bool closing_;

#pragma endregion
};

#pragma endregion

}} // namespace corvid::concurrency
