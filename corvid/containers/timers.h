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
// Contains full state of the event. Available to the callback handler, which
// can modify certain fields.
struct timer_event {
  timer_event(timer_id_t timer_id, time_point_t start_at, duration_t repeat_in,
      time_point_t stop_at, timer_callback_t callback)
      : timer_id{timer_id}, start_at{start_at}, repeat_in{repeat_in},
        stop_at{stop_at}, callback{std::move(callback)} {}

  timer_event(timer_event&&) = default;

  timer_event(const timer_event&) = delete;
  timer_event& operator=(const timer_event&) = delete;

  ~timer_event() {
    if (deleter) deleter(*this);
  }

  // Timer ID; unique within `timers` instance.
  const timer_id_t timer_id;

  // When timer should start firing.
  const time_point_t start_at;

  // When timer should fire again, or 0 for one-shot.
  const duration_t repeat_in;

  // When timer should stop firing, or empty for n/a.
  const time_point_t stop_at;

  // Callback to invoke when the timer fires.
  const timer_callback_t callback;

  // Name of the timer.
  std::string_view name;

  // Number of times the callback was invoked by the timer.
  size_t callbacks{};

  // User data to store.
  void* user_data{};

  // Deleter to call when the timer is removed.
  timer_entry_deleter_t deleter;

  // When the timer event is next scheduled to fire. See `tick` for details.
  time_point_t next_at;
};

// Lightweight class to reference an upcoming event.
struct scheduled_event {
  time_point_t next_at;
  timer_id_t timer_id;

  // Comparison order is reversed so that the next event is at the top.
  auto operator<=>(const scheduled_event& rhs) const {
    return rhs.next_at <=> next_at;
  }
};

// Priority queue of timers.
//
// Events may be one-shot or recurring. Callbacks are executed only when `tick`
// is called. During the execution of the callback, the `event` object is
// available and certain fields may be modified.
class timers {
public:
  // Get current time according to the registered clock callback.
  auto get_now() const { return clock_callback_(); }

  // Set timer for a new event. Returns the event, so that mutable fields may
  // be set.
  timer_event& set(time_point_t start_at, timer_callback_t callback,
      duration_t repeat_in = {}, time_point_t stop_at = {}) {
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
          timer_event{timer_id, start_at, repeat_in, stop_at,
              std::move(callback)});
      if (was_inserted) {
        new_event = &inserted_at->second;
        break;
      }
      // TODO: Consider skipping to next open slot using binary search.
    }
    new_event->next_at = start_at;
    scheduled_events_.emplace(new_event->next_at, new_event->timer_id);
    return *new_event;
  }

  auto& set(duration_t start_in, timer_callback_t callback,
      duration_t repeat_in = {}, duration_t stop_in = {}) {
    const auto now = clock_callback_();
    const auto start_at = clock_callback_() + start_in;
    const auto stop_at =
        (stop_in == duration_t{}) ? time_point_t{} : now + stop_in;
    return set(start_at, std::move(callback), repeat_in, stop_at);
  }

  // TODO: Consider offering overloads that take when/expire_in and
  // in/expiration.

  // TODO: Consider adding overloads to populate the user data atomically,
  // avoiding any possible race between setting and ticking.

  // TODO: Consider making this entire class thread-safe.

  // Cancel a timer,
  bool cancel(timer_id_t timer_id) {
    auto it = events_by_id_.find(timer_id);
    if (it == events_by_id_.end()) return false;
    events_by_id_.erase(it);
    return true;
  }

  // Service timers, firing any that are ready. Returns the number of callbacks
  // invoked. If `max_callbacks` is specified, it will stop after that many.
  //
  // About event lifespan: The event remains valid during the callback, but
  // it might be removed immediately afterwards, at which point its deleter
  // callback will be invoked. So if the callback handler needs to retain
  // information about the event or pass it to another thread, then it should
  // copy what it needs before returning. It may have to take ownership of
  // whatever `user_data` refers to, zeroing it out to disable the deleter. It
  // could also reschedule the event for `time_point_t::max()` and then cancel
  // it once it's no longer needed.
  //
  // About `next_at`: Before the callback is invoked, its `next_at` is
  // updated to the current time, both so that the callback knows what
  // time it is and so that it can increment this value to control when
  // it's rescheduled for. Note that this works even if the event was
  // otherwise a one-shot. In either case, it doesn't allow scheduling
  // past the `stop`. On the other hand, if it was recurring, then
  // clearing `next_at` will prevent it from firing again.
  size_t tick(size_t max_callbacks = -1) {
    const auto tick_now = clock_callback_();
    size_t callbacks{};

    // Loop until we've caught up to the current time.
    while (!scheduled_events_.empty()) {
      // Stop when we hit the now or triggered enough callbacks.
      const auto [next_at, timer_id] = scheduled_events_.top();
      if (next_at > tick_now || callbacks >= max_callbacks) break;

      // Look for the matching `timer_event`. If not found, it was canceled, so
      // this is a false alarm. Note how we check the `next_at` to avoid the
      // unlikely possibility of the event ID being reused while pending.
      scheduled_events_.pop();
      auto it = events_by_id_.find(timer_id);
      if (it == events_by_id_.end() || it->second.next_at != next_at) continue;

      // If it expired, cancel it. This means that an event whose `stop_at` is
      // specified might never get triggered.
      auto& event = it->second;
      if (event.stop_at != time_point_t{} && event.stop_at <= tick_now) {
        events_by_id_.erase(it);
        continue;
      }

      // Call back with the event. Note how we set the `next_at` to
      // `callback_now`, the actual start, not the nominal start, which is
      // `tick_now`, in a recognition that some time may have passed since we
      // entered this loop.
      const auto callback_now = get_now();
      event.next_at = callback_now;
      ++event.callbacks;
      ++callbacks;
      event.callback(event);

      // If the callback didn't reschedule, try to schedule the next time.
      // Note how we add `repeat_in` to `get_now()`, not `callback_now`, so
      // that it's the interval between calls, not returns. If this isn't the
      // desired behavior, then the callback handler should reschedule itself.
      if (event.next_at == callback_now) {
        if (event.repeat_in != duration_t{})
          event.next_at = get_now() + event.repeat_in;
        else
          event.next_at = time_point_t{};
      }

      // Don't bother scheduling if it'll expire before it can trigger. We
      // don't have to do this, but we might as well because it's cheap and
      // avoids possibly waking up prematurely.
      if (event.stop_at != time_point_t{} && event.stop_at <= event.next_at)
        event.next_at = time_point_t{};

      // If it's not scheduled, then there's no reason to keep it.
      if (event.next_at <= callback_now) {
        events_by_id_.erase(it);
        continue;
      }

      scheduled_events_.emplace(event.next_at, event.timer_id);
    }

    return callbacks;
  }

  // Returns the time until the next event, or 0 if one is ready now. If no
  // events, returns `default_duration`, for polling.
  //
  // The purpose is to allow the caller to sleep until the next event. Note,
  // however, that an event that's inserted may be ready sooner.
  duration_t next_in(duration_t default_duration = duration_t{}) const {
    auto next_delay = default_duration;
    if (!scheduled_events_.empty()) {
      next_delay = duration_cast<duration_t>(
          scheduled_events_.top().next_at - clock_callback_());
    }
    // If we're overdue, instead claim we're ready now.
    if (next_delay < duration_t{}) next_delay = duration_t{};
    return next_delay;
  }

  // Returns the time of the next event (which could be in the past if we're
  // overdue). If no events, returns `default_time`.
  time_point_t next_at(time_point_t default_time = time_point_t{}) const {
    auto next_time = default_time;
    if (!scheduled_events_.empty())
      next_time = scheduled_events_.top().next_at;
    return next_time;
  }

  const auto& events() const { return events_by_id_; }
  timer_event& event(timer_id_t timer_id) {
    return events_by_id_.at(timer_id);
  }

  // For testing only.

  void set_clock_callback(clock_callback_t callback) {
    clock_callback_ = callback;
  }

  void set_next_timer_id(uint64_t next_timer_id) {
    next_timer_id_ = next_timer_id;
  }

private:
  // Callback to get the current time.
  clock_callback_t clock_callback_ = [] {
    return std::chrono::steady_clock::now();
  };

  // Next timer ID to assign. May wrap around.
  uint64_t next_timer_id_{};

  // We store each `timer_event` in `events_by_id_`, removing it as soon as
  // we've determined that it won't be scheduled again.
  timer_map_t events_by_id_;

  // For each event, there should be exactly one entry in `scheduled_events_`
  // at any time. It's possible for an entry to reference an event that has
  // been removed, in which case the ID lookup will harmlessly fail.
  scheduled_queue_t scheduled_events_;
};
} // namespace timers_ns

// Exported types.
using timer_id_t = timers_ns::timer_id_t;
using timers = timers_ns::timers;
using timer_event = timers_ns::timer_event;

}} // namespace corvid::container

// TODO: Add unit tests to confirm correctness.
