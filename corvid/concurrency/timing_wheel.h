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
#include <cassert>
#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "jthread_stoppable_sleep.h"
#include "tombstone.h"

namespace corvid { inline namespace concurrency {

// Single-level timing wheel with configurable precision (100ms by default).
//
// Schedules `std::function<void()>` callbacks and fires them at the
// appropriate time. That is all it does. It has no concept of IDs,
// cancellation tokens, targets, or delivery channels -- those are the
// caller's concern.
//
// Compared to `timers`, which uses a priority queue (O(log n) schedule), the
// timing wheel provides O(1) schedule and O(fired) tick, at the cost of fixed
// precision and a bounded maximum delay (~60 s at the default configuration).
//
// Thread safety: `schedule()` is thread-safe; `tick()` is intended to be
// called from a single driver thread (see `timing_wheel_runner`).
//
// Shutdown: call `stop()` on the runner (or destroy it) before destroying
// any object referenced by a pending callback. Pending callbacks in unfired
// slots are discarded when the runner stops; they do not fire after `stop()`
// returns.
//
// Maximum delay: `(slot_count - 1) * tick_interval`. `schedule()` returns
// false if the delay exceeds this. For longer delays, use `timers` instead.
//
// Example (serving suggestion -- one way to handle write timeouts):
//
//   // Caller generates IDs, captures them and any other context into the
//   // closure, and performs liveness checks inside the callback.
//
//   static std::atomic<uint64_t> next_id{1};
//   uint64_t id = next_id.fetch_add(1);
//   conn.write_id_.store(id);
//
//   runner.wheel().schedule(
//       [id, &loop, conn_weak = weak_ptr<stream_conn>{conn}] {
//           // Deliver result on the loop thread.
//           loop.post([id, conn_weak] {
//               auto c = conn_weak.lock();
//               if (!c || c->write_id_.load() != id) return true; // stale
//               c->handle_write_timeout();
//               return true;
//           });
//       },
//       10s);
//
//   // On write completion -- stales any pending callback:
//   conn.write_id_.store(0);
//
class timing_wheel {
public:
  using time_point_t = std::chrono::steady_clock::time_point;
  using duration_t = std::chrono::milliseconds;

  // Type-erased event callback. Return value is not checked.
  using eventfn = std::function<bool()>;

  // All events scheduled for a given time slot.
  using slot_t = std::vector<eventfn>;

  // All slots in the wheel, indexed by the tick position.
  using slots_t = std::vector<slot_t>;

  // Default slot count covers ~60 s at 100ms per slot.
  static constexpr size_t default_slot_count{600};

  // Default resolution: one 100ms slot per tick.
  static constexpr duration_t default_tick_interval{100};

  // Construct a timing wheel with `slot_count` slots and `tick_interval`
  // precision, starting from `start_time`. `slot_count` must be at least 2.
  // `tick_interval` must be at least 500000ns (the minimum reliable resolution
  // of `nanosleep` on Linux). `start_time` defaults to the current wall time;
  // pass an explicit value for testing with a fake clock. The maximum
  // schedulable delay is `(slot_count - 1) * tick_interval`.
  // Throws `std::invalid_argument` if either constraint is violated.
  explicit timing_wheel(size_t slot_count = default_slot_count,
      duration_t tick_interval = default_tick_interval,
      time_point_t start_time = std::chrono::steady_clock::now())
      : slots_(slot_count), tick_interval_(tick_interval),
        last_tick_(start_time) {
    if (slot_count < 2)
      throw std::invalid_argument{"timing_wheel: slot_count must be >= 2"};
    if (std::chrono::nanoseconds{tick_interval_}.count() < 500'000)
      throw std::invalid_argument{
          "timing_wheel: tick_interval must be >= 500000ns"};
  }

  timing_wheel(const timing_wheel&) = delete;
  timing_wheel(timing_wheel&&) = delete;
  timing_wheel& operator=(const timing_wheel&) = delete;
  timing_wheel& operator=(timing_wheel&&) = delete;

  // Schedule `callback` to fire after `delay`. Thread-safe.
  //
  // Returns false if `delay` exceeds `(slot_count - 1) * tick_interval`;
  // use `timers` for longer delays, or chain callbacks.
  // Delays below `tick_interval` are clamped up to one tick (fires on the
  // next `tick()` call, not the current one, to avoid re-entrancy).
  // The `delay` represents the minimum time before the callback fires; actual
  // firing time is rounded to the next slot boundary after that, and only
  // occurs after all previous callbacks have fired.
  [[nodiscard]] bool schedule(eventfn callback, duration_t delay) {
    const auto max_delay =
        tick_interval_ * static_cast<int>(slots_.size() - 1);
    if (delay > max_delay) return false;
    delay = std::max(delay, tick_interval_);
    const auto ticks_ahead = static_cast<size_t>(delay / tick_interval_);

    std::scoped_lock lock{mutex_};
    const auto target = (current_slot_ + ticks_ahead) % slots_.size();
    slots_[target].push_back(std::move(callback));
    return true;
  }

  // Signal that no further callbacks should fire. Called by
  // `timing_wheel_runner` via `std::stop_callback` when a stop is requested,
  // causing any in-progress `tick()` to bail at the next callback boundary.
  // Idempotent.
  [[nodiscard]] bool stop() { return stopped_.kill(); }

  // Advance the wheel to `now`, firing all expired callbacks outside the
  // lock. Multiple elapsed slots are drained in order.
  //
  // If the elapsed time exceeds the full ring (more than
  // `(slot_count - 1) * tick_interval`), each slot is drained at most once.
  //
  // `last_tick_` advances by the consumed tick intervals rather than by the
  // full elapsed time, so sub-interval remainders carry over correctly.
  //
  // Called by `timing_wheel_runner`; also callable directly in tests with a
  // manually advanced fake clock.
  void tick(time_point_t now) {
    slots_t ready_events;

    {
      std::scoped_lock lock{mutex_};

      // Guard against clock going backward or redundant calls.
      if (now <= last_tick_) return;

      const auto elapsed =
          std::chrono::duration_cast<duration_t>(now - last_tick_);
      auto advance = static_cast<size_t>(elapsed / tick_interval_);
      if (advance == 0) return;

      // Cap to avoid visiting any slot twice in one tick call.
      advance = std::min(advance, slots_.size() - 1);

      // Swap each expired slot into a local buffer while holding the lock.
      // The slot is left empty for future use. Releasing the lock before
      // firing allows `schedule()` to run concurrently.
      ready_events.resize(advance);
      for (auto& slot : ready_events) {
        current_slot_ = (current_slot_ + 1) % slots_.size();
        slot.swap(slots_[current_slot_]);
      }

      // Advance by the consumed ticks only; carry the sub-interval remainder.
      last_tick_ += tick_interval_ * static_cast<int>(advance);
    }

    // Fire all collected callbacks outside the lock, bailing immediately if
    // a stop has been requested. Callbacks that call `schedule()` land in
    // future slots (guaranteed by the 1-tick minimum delay), so no slot is
    // visited twice.
    for (auto& slot : ready_events) {
      for (auto& cb : slot) {
        if (stopped_.dead()) return;
        (void)cb();
      }
    }
  }

  // Return the time at which the next slot will open. Used by
  // `timing_wheel_runner` to compute the sleep deadline.
  [[nodiscard]] time_point_t next_tick_time() const {
    std::scoped_lock lock{mutex_};
    return last_tick_ + tick_interval_;
  }

private:
  mutable std::mutex mutex_;

  // The ring: one slot per tick interval, where each slot contains all
  // callbacks scheduled for that tick.
  slots_t slots_;

  const duration_t tick_interval_;
  size_t current_slot_{0};
  time_point_t last_tick_;
  tombstone stopped_;
};

// Runs a `timing_wheel` in its own background thread.
//
// The thread sleeps until the next slot boundary then calls `tick()`.
//
// Shutdown ordering: destroy the runner (or call `stop()` then let it go out
// of scope) before destroying any object that a pending callback might
// reference. Pending callbacks in unfired slots are discarded; none fire after
// the runner is destroyed.
class [[nodiscard]] timing_wheel_runner {
public:
  explicit timing_wheel_runner(
      size_t slot_count = timing_wheel::default_slot_count,
      timing_wheel::duration_t tick_interval =
          timing_wheel::default_tick_interval)
      : wheel_{std::make_shared<timing_wheel>(slot_count, tick_interval)},
        thread_{[this](const std::stop_token& st) { run(st); }} {}

  timing_wheel_runner(const timing_wheel_runner&) = delete;
  timing_wheel_runner(timing_wheel_runner&&) = delete;
  timing_wheel_runner& operator=(const timing_wheel_runner&) = delete;
  timing_wheel_runner& operator=(timing_wheel_runner&&) = delete;

  // Signal the wheel thread to exit. Pending callbacks are discarded.
  // Idempotent and thread-safe. Also called implicitly by the destructor.
  [[nodiscard]] bool stop() { return thread_.request_stop(); }

  // Return the shared `timing_wheel`. Multiple owners may share it; the wheel
  // remains live until all `shared_ptr` copies are released.
  [[nodiscard]] const std::shared_ptr<timing_wheel>& wheel() noexcept {
    return wheel_;
  }
  [[nodiscard]] operator timing_wheel&() noexcept { return *wheel_; }

private:
  void run(const std::stop_token& st) {
    // Kill the wheel's tombstone immediately when a stop is requested, so
    // any in-progress `tick()` bails at the next callback boundary.
    std::stop_callback on_stop{st, [this] { (void)wheel_->stop(); }};
    jthread_stoppable_sleep sleep;
    while (!sleep.until(st, wheel_->next_tick_time()))
      wheel_->tick(std::chrono::steady_clock::now());
  }

  std::shared_ptr<timing_wheel> wheel_;
  std::jthread thread_;
};

}} // namespace corvid::concurrency
