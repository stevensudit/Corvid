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
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <string>

#include "sync_lock.h"
#include "tombstone.h"

namespace corvid::timers_ns {

using namespace corvid::atomic_tomb;

//
// Thread-safe timers.
//

// Time point and duration using steady clock.
using time_point_t = std::chrono::steady_clock::time_point;
using duration_t = std::chrono::milliseconds;

// Fwd.
class timers;
class timer_event;

// Timers and events can only be constructed as shared.
using timers_ptr = std::shared_ptr<timers>;
using timer_event_ptr = std::shared_ptr<timer_event>;

// Parameter passed to the timer callback. Besides the event itself, and access
// to the `timers` object, it also includes various timestamps and counts
// associated with the invocation. Note that the instance, which is passed by
// reference, is valid only during the invocation.
struct timer_invocation {
  // Access to the timers object that scheduled this event. Useful for
  // scheduling follow-up events as well accessing the current time.
  timers_ptr originating_timers;

  // The event, which was created in `timers::set` and is reused across
  // invocations.
  timer_event_ptr event;

  // Event invocation count, starting at 1 and increasing with each recurring
  // call. Contains a snapshot of the namesake in `timer_event`.
  size_t invocation_count;

  // When the event was scheduled to run, which will be at or after
  // `tick_time`.
  time_point_t scheduled_time;

  // When the clock ticked, leading to this invocation. This will be at or
  // after `scheduled_time`.
  time_point_t tick_time;

  // The actual time when the callback was invoked.
  time_point_t now;
};

// Callback invoked when a timer fires.
//
// See below for notes on synchronization and thread safety.
//
// Return value:
// Typically, you will return zero (which is to say `{}`) and the right thing
// will happen.
//
// For recurring events, this causes the event to be scheduled again after
// `repeat_in`. To be very specific, this duration is added to the current time
// once the callback is done. If you instead return the next invocation time,
// you override this. Among other things, this allows you to space invocations
// based on any of the timestamps in `timer_invocation` or on `get_now()`.
//
// For a one-shot event, you will typically return zero. However, if you
// specify a value other than zero, then it will be scheduled for that time.
// This does not make it a recurring event, though. As soon as you return zero,
// the event will be canceled.
//
// Scheduling in the past means it will be called back on the next tick.
// Scheduling after `stop_at` cancels it, so you can return max to cancel. (You
// can also cancel by explicitly setting the tombstone.)
using timer_callback_t = std::function<time_point_t(const timer_invocation&)>;

// Callback to get the current time. Does not need to be reentrant, but must
// be safe to call from any thread. Value should increase monotonically.
using clock_callback_t = std::function<time_point_t()>;

// Timer event.
//
// Contains full state of the event. All contents are either immutable or
// thread-safe.
//
// Set `canceled` to true to prevent the event from firing again.
class timer_event final {
  enum class allow : std::uint8_t { ctor };

public:
  // Effectively private: can only be constructed through `make` factory
  // method.
  explicit timer_event(allow, time_point_t created_at, time_point_t start_at,
      duration_t repeat_in, time_point_t stop_at, timer_callback_t&& callback,
      bool canceled)
      : created_at{created_at}, start_at{start_at}, repeat_in{repeat_in},
        stop_at{stop_at}, callback{std::move(callback)}, canceled{canceled} {}

  // Factory method to create a new timer event.
  //
  // For `stop_at` and `repeat_in`, zero is interpreted as max. If invalid,
  // starts off canceled.
  [[nodiscard]] static timer_event_ptr make(time_point_t created_at,
      time_point_t start_at, duration_t repeat_in, time_point_t stop_at,
      timer_callback_t&& callback) {
    // If empty, then never.
    if (stop_at == time_point_t{}) stop_at = time_point_t::max();
    if (repeat_in == duration_t{}) repeat_in = duration_t::max();

    // If malformed, then it's DOA.
    bool doa{};
    if (stop_at < start_at) doa = true;
    if (repeat_in < duration_t{}) doa = true;

    return std::make_shared<timer_event>(allow::ctor, created_at, start_at,
        repeat_in, stop_at, std::move(callback), doa);
  }

  // No copy or move: only `std::shared_ptr`.
  timer_event(const timer_event&) = delete;
  timer_event(timer_event&&) = delete;
  timer_event& operator=(const timer_event&) = delete;
  timer_event& operator=(timer_event&&) = delete;

  // When the timer was created.
  const time_point_t created_at;

  // When timer should start firing.
  const time_point_t start_at;

  // When timer should fire again, or max for never.
  const duration_t repeat_in;

  // When timer should stop firing, or max for never.
  const time_point_t stop_at;

  // Callback invoked when the timer fires.
  //
  // To associate specific identification or other data with the callback, use
  // a lambda and bind that information into it through a capture. Note,
  // however, that the callback is a `std::function`, so it must be copyable.
  // As a result, if you have an immovable resource, you may need to instead
  // bind to a `std::shared_ptr` to it.
  //
  // Unless the callback processes the event synchronously, it is possible for
  // multiple callbacks to be invoked concurrently. In this case, if there is
  // data bound to the lambda that is mutable and not thread-safe, you will
  // need to protect it by locking on `sync`.
  //
  // Note also that the `timer_invocation` parameter, passed in by reference,
  // is only available during the callback execution. So if you want to access
  // this later, you will need to copy it in whole or in part.
  //
  // See `timer_callback_t` definition for details about the optional
  // return value.
  const timer_callback_t callback;

  // Number of times the callback was invoked by the timer. Incremented
  // automatically and used to set `invocation_count` in `timer_invocation`.
  //
  // The user can choose to cancel after a certain number of callback
  // invocations. This is more reliable than trying to set the expiration time
  // to an exact multiple of the repeat interval.
  std::atomic_size_t invocation_count{0};

  // Tombstone. Setting this prevents the event from firing again.
  tombstone canceled;

  // Synchronize access to data bound into the callback, if needed.
  synchronizer sync;
};

// Priority queue of timers.
//
// Events may be one-shot or recurring. Callbacks are executed only when `tick`
// is called. All methods are thread-safe and may be safely invoked even from
// inside a callback without deadlock.
class timers final: public std::enable_shared_from_this<timers> {
  // Lightweight internal class to reference an upcoming event. If there's no
  // `scheduled_event` for an `event` in the priority queue, it's not
  // scheduled. Note that the `event` is kept alive by its presence in a
  // `scheduled_event` instance.
  struct scheduled_event final {
    time_point_t next_at;
    timer_event_ptr event;

    // Comparison order is reversed so that the next event is at the top.
    auto operator<=>(const scheduled_event& rhs) const {
      return rhs.next_at <=> next_at;
    }
  };
  using scheduled_queue_t = std::priority_queue<scheduled_event>;
  enum class allow : std::uint8_t { ctor };

public:
  // Effectively private: can only be constructed through `make` factory
  // method.
  explicit timers(allow, std::string name) : name{std::move(name)} {}

  // No copy or move: only `std::shared_ptr`.
  timers(const timers&) = delete;
  timers(timers&&) = delete;
  timers& operator=(const timers&) = delete;
  timers& operator=(timers&&) = delete;

  // Factory method to create a new `timers` object.
  [[nodiscard]] static timers_ptr make(std::string name) {
    return std::make_shared<timers>(allow::ctor, std::move(name));
  }

  // Get current time according to the registered clock callback.
  [[nodiscard]] auto get_now(const lock& attestation = {}) const {
    attestation(sync);
    return clock_callback_();
  }

  // Set timer for a new event at an absolute time. On success, schedules the
  // event for execution at the specified time.
  //
  // Returns the created event, which is never null but may already be canceled
  // if the times don't make sense.
  //
  // If `start_at` is at or before the current time, the event will be
  // scheduled immediately (and executed at the next tick).
  //
  // If `repeat_in` is zero, the event will be one-shot. If it's negative, then
  // it's malformed and therefore canceled. Note that you cannot repeat over
  // and over again by passing a zero `repeat_in`. You can either pass the min
  // or take control over rescheduling through the return value of the
  // callback.
  //
  // If `stop_at` isn't specified and the event is recurring, the event will
  // repeat indefinitely (or until canceled). If it is specified and is before
  // `start_at`, the event is never triggered.
  auto set(time_point_t start_at, timer_callback_t callback,
      duration_t repeat_in = {}, time_point_t stop_at = {},
      const lock& attestation = {}) {
    attestation(sync);
    auto event = timer_event::make(get_now(attestation), start_at, repeat_in,
        stop_at, std::move(callback));
    if (!event->canceled) scheduled_events_.emplace(start_at, event);
    return event;
  }

  // Set timer for a new event at a relative time.  On success, schedules the
  // event for execution at the specified time.
  //
  // Returns the created event, which is never null, but might start off
  // canceled.
  //
  // If `start_in` is zero (or negative), the event will be scheduled
  // immediately (and executed at the next tick).
  //
  // If `repeat_in` is zero, the event will be one-shot. If it's negative, then
  // it's malformed and therefore canceled. Note that you cannot repeat over
  // and over again by passing a zero `repeat_in`. You can either pass the min
  // or take control over rescheduling through the return value of the
  // callback.
  //
  // If `stop_in` is specified and is before `start_in`, it's canceled.
  auto set(duration_t start_in, timer_callback_t callback,
      duration_t repeat_in = {}, duration_t stop_in = {},
      const lock& attestation = {}) {
    attestation(sync);
    const auto now = get_now(attestation);
    const auto start_at = now + start_in;
    const auto stop_at =
        (stop_in == duration_t{}) ? time_point_t{} : now + stop_in;

    auto event = timer_event::make(now, start_at, repeat_in, stop_at,
        std::move(callback));
    if (!event->canceled) scheduled_events_.emplace(start_at, event);
    return event;
  }

  // Service timers, firing any that are ready. Returns the number of callbacks
  // invoked.
  //
  // If `max_callbacks` is specified, it will stop after that many.
  size_t tick(size_t max_callbacks = -1, const lock& attestation = {}) {
    attestation(sync);
    const auto snapshot_now = get_now(attestation);
    timer_invocation invocation{.originating_timers = shared_from_this(),
        .event = {},
        .invocation_count = 0,
        .scheduled_time = snapshot_now,
        .tick_time = snapshot_now,
        .now = snapshot_now};

    // Loop until we've caught up to the current time.
    size_t callbacks_invoked{};
    while (!scheduled_events_.empty()) {
      // Stop when we hit the now or triggered enough callbacks.
      const auto& [event_scheduled, event_ref] = scheduled_events_.top();
      if (event_scheduled > invocation.tick_time ||
          callbacks_invoked >= max_callbacks)
        break;

      // Take ownership of the event and remove the entry.
      invocation.event = event_ref;
      invocation.scheduled_time = event_scheduled;
      auto& event = *invocation.event;
      scheduled_events_.pop();

      // If it was canceled after being scheduled, just skip it.
      if (event.canceled) continue;

      // If it expired, cancel it. This means that an event whose `stop_at` is
      // specified might never get triggered. Note that we check the time again
      // because we care about the current time, not when the tick started. We
      // could have already called other callbacks that took a while.
      invocation.now = get_now(attestation);
      if (event.stop_at <= invocation.now) continue;

      // Set up invocation fully and invoke outside of the lock.
      ++callbacks_invoked;
      invocation.invocation_count = ++event.invocation_count;
      time_point_t next_at{};
      if (auto reverse_lock = attestation.lock_reverse()) {
        next_at = event.callback(invocation);
      }

      // If next time is never, we're canceled.
      if (next_at == time_point_t::max()) event.canceled.kill();

      // If the event was canceled, don't reschedule it.
      if (event.canceled) continue;

      invocation.now = get_now(attestation);

      // If no time specified, calculate it. Note how we add `repeat_in` to
      // `get_now()`, not `callback_now`, so that it's the interval between
      // returns, not calls.
      if (next_at == time_point_t{} && event.repeat_in != duration_t::max())
        next_at = invocation.now + event.repeat_in;

      // Don't bother scheduling if it'll expire before it can trigger. We
      // don't have to do this, but we might as well because it's cheap and
      // avoids possibly waking up prematurely.
      if (event.stop_at <= next_at) next_at = {};

      // If it's not scheduled, then there's no reason to keep it.
      if (next_at <= invocation.now) {
        event.canceled.kill();
        continue;
      }

      scheduled_events_.emplace(next_at, invocation.event);
    }

    return callbacks_invoked;
  }

  // TODO: Add method to block until next event is scheduled, which may be
  // pre-empted by the rescheduling of a repeating event or insertion of a new
  // event.

  // For testing only, replace clock with artificial one.
  void set_clock_callback(const clock_callback_t& callback,
      const lock& attestation = {}) {
    attestation(sync);
    clock_callback_ = callback;
  }

  // Name, for logging.
  const std::string name;

  // Synchronizer for thread safety.
  synchronizer sync;

private:
  // Callback to get the current time.
  clock_callback_t clock_callback_ = [] {
    return std::chrono::steady_clock::now();
  };

  // For each event that is scheduled, there should be exactly one entry in
  // `scheduled_events_` at any time.
  scheduled_queue_t scheduled_events_;
};

// TODO: Add poll method that waits until the next event is scheduled or a new
// event is scheduled. This requires an interruptible wait.
// TODO: Add clear or reset to drop all scheduled events. Perhaps add another
// method to remove all recurring events, optionally sparing them if they've
// never executed. Do we need a monotonic class that allows incrementing but
// not anything else?
// TODO: Add shutdown tombstone to prevent events from being rescheduled. Wrap
// it with a method, perhaps a second one that clears recurring.

} // namespace corvid::timers_ns
