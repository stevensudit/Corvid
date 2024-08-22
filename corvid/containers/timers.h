// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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
#include <functional>
#include <map>
#include <queue>
#include <string_view>

namespace corvid { inline namespace container {
// This namespace is not inline, so we export just the types that the user is
// expected to need, but the others are available if necessary.
namespace timers_ns {

enum class timer_id_t : uint64_t { invalid };

// Time point and duration using steady clock.
using time_point_t = std::chrono::steady_clock::time_point;
using duration_t = std::chrono::milliseconds;

// Forward declarations.
struct timer_event;
struct scheduled_event;

// Callback invoked when a timer fires.
using timer_callback_t = std::function<void(timer_event&)>;

// Callback invoked to delete a timer entry.
using timer_entry_deleter_t = std::function<void(timer_event&)>;

// Callback to get the current time.
using clock_callback_t = std::function<time_point_t()>;

// Map of timers by ID.
using timer_map_t = std::map<timer_id_t, timer_event>;

// Priority queue of scheduled events.
using scheduled_queue_t = std::priority_queue<scheduled_event>;

// Timer event.
//
// Contains full state of the event.
struct timer_event {
  timer_event(timer_id_t timer_id, time_point_t start, duration_t next,
      time_point_t stop, timer_callback_t callback)
      : timer_id{timer_id}, start{start}, next{next}, stop{stop},
        callback{std::move(callback)} {}

  timer_event(timer_event&&) = default;

  timer_event(const timer_event&) = delete;
  timer_event& operator=(const timer_event&) = delete;

  ~timer_event() {
    if (deleter) deleter(*this);
  }

  // Timer ID; unique within `timers` instance.
  const timer_id_t timer_id{};

  // When timer should start firing.
  const time_point_t start;

  // When timer should fire again, or 0 for one-shot.
  const duration_t next;

  // When timer should stop firing, or empty for n/a.
  const time_point_t stop;

  // Callback to invoke when the timer fires.
  const timer_callback_t callback;

  // Name of the timer.
  std::string_view name;

  // User data to store.
  void* user_data{};

  // Deleter to call when the timer is removed.
  timer_entry_deleter_t deleter;

  // Enable this to prevent the timer from being removed automatically.
  bool no_auto_remove{};
};

// Lightweight class to reference an upcoming event.
struct scheduled_event {
  time_point_t next;
  timer_id_t timer_id;

  // Comparison order is reversed so that the next event is at the top.
  auto operator<=>(const scheduled_event& rhs) const {
    return rhs.next <=> next;
  }
};

// Priority queue of timers.
//
// Events may be one-shot or recurring. Callbacks are executed only when `tick`
// is called. During the execution of the callback, the `event` object is
// available.
class timers {
public:
  // Set timer for a new event. Returns the event, so that mutable fields may
  // be set.
  timer_event& set(time_point_t start_at, timer_callback_t callback,
      duration_t next = {}, time_point_t stop_at = {}) {
    // If all slots were filled, we'd loop forever, so just fail.
    if (events_by_id_.size() == std::numeric_limits<uint64_t>::max() - 1)
      throw std::overflow_error("Timer ID overflow");

    // After overflowing the ID, we wrap around, so we need to skip over any
    // that are still in use.
    timer_id_t timer_id{};
    timer_event* new_event{};
    for (;;) {
      timer_id = timer_id_t{++next_timer_id_};
      if (timer_id == timer_id_t::invalid) continue;
      auto [inserted_at, was_inserted] = events_by_id_.emplace(timer_id,
          timer_event{timer_id, start_at, next, stop_at, std::move(callback)});
      if (was_inserted) {
        new_event = &inserted_at->second;
        break;
      }
      // TODO: Consider skipping to next open slot using binary search.
    }
    scheduled_events_.emplace(start_at, timer_id);
    return *new_event;
  }

  auto& set(duration_t start_in, timer_callback_t callback,
      duration_t next = {}, duration_t stop_in = {}) {
    const auto now = clock_callback_();
    const auto start_at = clock_callback_() + start_in;
    const auto stop_at =
        (stop_in == duration_t{}) ? time_point_t{} : now + stop_in;
    return set(start_at, std::move(callback), next, stop_at);
  }

  // TODO: Consider offering overloads that take when/expire_in and
  // in/expiration.

  // Cancel a timer,
  bool cancel(timer_id_t timer_id) {
    auto it = events_by_id_.find(timer_id);
    if (it == events_by_id_.end()) return false;
    events_by_id_.erase(it);
    // TODO: Consider belt-and-suspenders approach of removing from queue. This
    // would prevent the event from firing if it was scheduled for the far
    // future, canceled, and then the ID was reassigned. However, to do this,
    // we'd need a priority queue of our own, on top of a heap. The alternative
    // might be to have a "next_fire" timestamp in the event, so that if it
    // doesn't match the scheduled one, we know it's an error. We'd have to
    // hide it in a subclass.
    return true;
  }

  size_t tick() {
    const auto now = clock_callback_();
    size_t callbacks{};

    // Loop until we've caught up to the current time.
    while (!scheduled_events_.empty() && scheduled_events_.top().next <= now) {
      const auto [deadline, timer_id] = scheduled_events_.top();
      scheduled_events_.pop();

      // If it's been cancelled, skip it.
      auto it = events_by_id_.find(timer_id);
      if (it == events_by_id_.end()) continue;

      // If it expired, cancel it.
      auto& event = it->second;
      if (event.stop != time_point_t{} && event.stop <= now) {
        if (!event.no_auto_remove) events_by_id_.erase(it);
        continue;
      }

      // Call back with the event. Note that, while the event remains valid
      // during the callback, it might be removed immediately afterwards, and
      // its deleter callback will be invoked. If you retain the `event_id`,
      // you can attempt to find it in the `events()` later.
      event.callback(event);
      ++callbacks;

      // Reschedule if needed. Note that, if we were very late, it's possible
      // that we'll fire twice in this call. But if the next time would be
      // after expiration, we don't reschedule.
      if (event.next != duration_t{}) {
        auto next_at = deadline + event.next;
        if (event.stop == time_point_t{} || next_at < event.stop) {
          scheduled_events_.emplace(next_at, timer_id);
          continue;
        }
      }

      // If it wasn't rescheduled, remove it.
      if (!event.no_auto_remove) events_by_id_.erase(it);
    }

    return callbacks;
  }

  auto get_now() const { return clock_callback_(); }

  // Returns the time until the next event, or 0 if one is ready now. If no
  // events, returns `default_duration`.
  duration_t next_in(duration_t default_duration = duration_t{}) const {
    auto next_delay = default_duration;
    if (!scheduled_events_.empty()) {
      next_delay = duration_cast<duration_t>(
          scheduled_events_.top().next - clock_callback_());
    }
    // If we're overdue, instead claim we're ready now.
    if (next_delay < duration_t{}) next_delay = duration_t{};
    return next_delay;
  }

  // Returns the time of the next event (which could be in the past if we're
  // overdue). If no events, returns `default_time`.
  time_point_t next_at(time_point_t default_time = time_point_t{}) const {
    auto next_time = default_time;
    if (!scheduled_events_.empty()) next_time = scheduled_events_.top().next;
    return next_time;
  }

  const auto& events() const { return events_by_id_; }
  auto& event(timer_id_t timer_id) { return events_by_id_.at(timer_id); }

  void set_clock_callback(clock_callback_t callback) {
    clock_callback_ = callback;
  }

private:
  clock_callback_t clock_callback_ = [] {
    return std::chrono::steady_clock::now();
  };

  uint64_t next_timer_id_{};
  timer_map_t events_by_id_;
  scheduled_queue_t scheduled_events_;
};
} // namespace timers_ns

// Exported types.
using timer_id_t = timers_ns::timer_id_t;
using timers = timers_ns::timers;
using timer_event = timers_ns::timer_event;

}} // namespace corvid::container
