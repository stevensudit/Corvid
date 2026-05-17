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
#include <cstdint>
#include <memory>
#include <utility>

#include "../meta/fixed_function.h"
#include "relaxed_atomic.h"
#include "timeout_sweeper.h"
#include "timeout_sweeper_base.h"

namespace corvid { inline namespace concurrency {

#pragma region idle_timeout

// Idle-timeout helper bound to the class that contains it and to a
// `timeout_sweeper`. Packages the idle timeout for a particular type of
// operation, such as reads or writes. Manages the configured/active duration,
// current deadline, sweeper integration, and the three-state machine that
// owners drive via `set_mode`.
//
// Owner must inherit from `std::enable_shared_from_this`. The aliased
// keepalive is built lazily on the first `set_mode` transition that requires
// scheduling (`mode::stopped` -> `mode::running` or `mode::stopped` ->
// `mode::paused`) -- by then the owner's `shared_ptr` is established and
// `shared_from_this()` works. `idle_timeout` can therefore be a value member
// of the owner and constructed in the owner's member-init list.
//
// The `postpone` method is fully thread-safe. Past that, individual loads and
// stores are atomic, but there is no serialization of concurrent mutators. To
// put it another way, it is best to call these other methods from a single
// thread.
template<typename Owner, typename Sweeper = timeout_sweeper<>>
class idle_timeout: public timeout_sweeper_base {
#pragma region Types
public:
  using owner_t = Owner;
  using sweeper_t = Sweeper;
  using callback_t = Sweeper::callback_t;

  // Cancelation action invoked when the idle timer expires.
  using cancel_action_t = meta::fixed_function<32, void()>;

#pragma endregion
#pragma region Construction

  // Construct as a value member of `owner`. Only the raw pointer to the owner
  // is stored, and `shared_from_this` is invoked lazily when a fresh sweeper
  // entry needs to be scheduled.
  explicit idle_timeout(sweeper_t& sweeper, owner_t& owner,
      cancel_action_t&& on_idle, duration_t configured = {}) noexcept
      : sweeper_{sweeper}, owner_{owner}, on_idle_{std::move(on_idle)},
        configured_{configured} {
    on_idle_once_ = &on_idle_;
    assert(configured >= duration_t{});
    assert(on_idle_);
  }

  idle_timeout(const idle_timeout&) = delete;
  idle_timeout(idle_timeout&&) = delete;
  idle_timeout& operator=(const idle_timeout&) = delete;
  idle_timeout& operator=(idle_timeout&&) = delete;

#pragma endregion
#pragma region Accessors

  // Read-only accessors (thread-safe snapshots).

  // Timeout configuration. Never changes unless `configure` is called.
  [[nodiscard]] duration_t configured_timeout() const noexcept {
    return configured_;
  }

  // Active timeout. Non-zero iff currently `mode::running`.
  [[nodiscard]] duration_t active_timeout() const noexcept { return active_; }

  // Current deadline. When `mode::running`, this time was set to `now() +
  // active_timeout()` and used to schedule the next sweep. When
  // `mode::paused`, this is at or past the sentinel value
  // (`paused_expiration`) that signals the clipping behavior, so that the next
  // sweep is scheduled for `now() + configured_timeout()` but will not
  // trigger an expiration. When `mode::stopped`, this will be a value in the
  // past.
  [[nodiscard]] time_point_t deadline() const noexcept { return deadline_; }

  // Current mode.
  [[nodiscard]] mode get_mode() const noexcept {
    if (*deadline_ >= paused_expiration) return mode::paused;
    if (*deadline_ <= now()) return mode::stopped;
    return mode::running;
  }

#pragma endregion
#pragma region Modifiers

  // Update the configured timeout.
  //
  // Syncs `active_timeout` if in `mode::running` (so the next deadline
  // reset uses the new value), but will not clear it.
  void configure(duration_t d) noexcept {
    assert(d >= duration_t{});
    configured_ = d;
    if (d == duration_t{}) return;
    auto expected = *active_;
    if (expected == duration_t{}) return;
    active_.compare_exchange(expected, d);
  }

  // Postpone the expiration after progress. Safe to call in any mode.
  void postpone() noexcept {
    const auto active_snapshot = *active_;
    if (active_snapshot == duration_t{}) return;
    deadline_ = now() + active_snapshot;
  }

  // Stop the timeout and cancel any pending entry. Idempotent. Safe to call in
  // any mode. Leaves `configured_timeout` and `on_idle` intact so the timeout
  // can be restarted later.
  [[nodiscard]] bool stop() noexcept {
    active_ = duration_t{};
    deadline_ = time_point_t{};
    return true;
  }

  // Start the timeout that was stopped or paused. If already running, all it
  // does is postpone once. Fails if `configured_timeout` is zero or it can't
  // schedule.
  [[nodiscard]] bool start() {
    const auto now_time = now();
    const auto was_stopped = (*deadline_ <= now_time);
    const auto configured_snapshot = *configured_;
    if (configured_snapshot == duration_t{}) return false;
    active_ = configured_snapshot;
    const auto deadline_snapshot = now_time + configured_snapshot;
    deadline_ = deadline_snapshot;
    // Existing entry adapts on its next fire (`mode::paused` ->
    // `mode::running` may see up to one `configured_timeout` of slop).
    if (!was_stopped) return true;
    return sweeper_.schedule(deadline_snapshot, build_sweeper_cb());
  }

  // Pause the timeout. If already paused, does nothing.
  //
  // In pause mode, the deadline is parked at the sentinel value, and the
  // sweeper callback clips it back to `now() + configured_timeout()` on each
  // fire without ever invoking the `on_idle` expiration callback. Also,
  // `postpone` becomes a no-op.
  //
  // Fails if `configured_timeout` is zero or it can't schedule.
  [[nodiscard]] bool pause() {
    const auto now_time = now();
    const auto was_stopped = (*deadline_ <= now_time);
    const auto configured_snapshot = *configured_;
    if (configured_snapshot == duration_t{}) return false;
    auto succeeded = true;
    if (was_stopped) {
      // From `mode::stopped`: bootstrap. Schedule a near-future fire so the
      // clipping cycle starts, then revert to the sentinel.
      const auto deadline_snapshot = now_time + configured_snapshot;
      deadline_ = deadline_snapshot;
      succeeded = sweeper_.schedule(deadline_snapshot, build_sweeper_cb());
    }
    active_ = duration_t{};
    deadline_ = paused_expiration;
    return succeeded;
  }

  // Change the mode to start, stop, or pause the timeout. Returns `false` if
  // `configured_timeout` is zero and `target` is not `mode::stopped`, or if
  // scheduling a fresh sweeper entry fails.
  [[nodiscard]] bool set_mode(mode target) {
    switch (target) {
    case mode::stopped: return stop();
    case mode::paused: return pause();
    case mode::running: return start();
    default: return false;
    }
  }

  // Expire now. Idempotent.
  void expire() {
    active_ = duration_t{};
    deadline_ = time_point_t{};
    auto on_idle = on_idle_once_.exchange(nullptr);
    if (on_idle) (*on_idle)();
  }

  // If expiration was not fatal, you must reset it. Idempotent.
  void reset_expiration() noexcept {
    cancel_action_t* expected = nullptr;
    on_idle_once_.compare_exchange(expected, &on_idle_);
  }

#pragma endregion
#pragma region Helpers
private:
  // Build the sweeper closure. Mints a fresh aliased `weak_ptr` each
  // call; the lambda captures it by value (16 bytes), fitting
  // `callback_t`. Reaches `expire` through the locked `self`.
  [[nodiscard]] callback_t build_sweeper_cb() {
    auto self_sp =
        std::shared_ptr<idle_timeout>{owner_.shared_from_this(), this};
    return [weak = std::weak_ptr<idle_timeout>{std::move(self_sp)}](
               time_point_t fired) -> time_point_t {
      auto self = weak.lock();
      if (!self) return {};
      const auto result = self->on_sweep(fired);
      if (result.fire_idle && self->on_idle_) self->expire();
      return result.next_deadline;
    };
  }

  struct sweep_result {
    time_point_t next_deadline;
    bool fire_idle;
  };

  [[nodiscard]] sweep_result on_sweep(time_point_t fired) noexcept {
    // `mode::stopped`: we transitioned out from under the sweeper. Drop the
    // entry; no rearm, no cancelation.
    if (*deadline_ == time_point_t{}) return {{}, false};
    // `mode::paused`: parked at the sentinel. The deadline is in the far
    // future, but we always set the next callback to the configured timeout
    // after now.
    if (*deadline_ >= paused_expiration) {
      // Defensive: configured was zeroed while paused, so the clip path
      // would compute `now + 0`, hot-looping. Collapse to Stopped.
      if (*configured_ == duration_t{}) {
        deadline_ = time_point_t{};
        return {{}, false};
      }
      // Clip back to a near-future fire so we keep checking periodically.
      return {now() + *configured_, false};
    }
    // `mode::running` and deadline reached: nobody restarted between the
    // schedule and now. Fire the cancelation action and drop the entry.
    if (*deadline_ == fired) {
      deadline_ = time_point_t{};
      return {{}, true};
    }
    // `mode::running`, but deadline moved (`postpone` pushed it forward):
    // rearm to the new deadline; do not fire the cancelation action.
    return {*deadline_, false};
  }

#pragma endregion
#pragma region Data members

  sweeper_t& sweeper_;
  owner_t& owner_;
  cancel_action_t on_idle_;
  relaxed_atomic<cancel_action_t*> on_idle_once_;
  relaxed_atomic<duration_t> configured_;
  relaxed_atomic<duration_t> active_;
  relaxed_atomic<time_point_t> deadline_;

#pragma endregion
};

#pragma endregion

}} // namespace corvid::concurrency
