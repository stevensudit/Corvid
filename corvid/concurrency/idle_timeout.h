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
#include <cassert>
#include <memory>
#include <utility>

#include "../meta/fixed_function.h"
#include "../infra/relaxed_atomic.h"
#include "../infra/scope_exit.h"
#include "timeout_sweeper.h"
#include "timeouts.h"

namespace corvid { inline namespace concurrency {

#pragma region idle_timeout

// Mechanism for implementing an idle timeout for a given operation, providing
// a full state machine over the duration and callbacks. The use case is to set
// an idle timeout and keep postponing it as more work is accomplished.
//
// Works with a sweeper implementation (such as `timeout_sweeper`). Manages the
// configured/active duration, current deadline, sweeper integration, and the
// three-state machine that owners drive via `set_mode` and its synonyms.
//
// Owner must inherit from `std::enable_shared_from_this`. The aliased
// keepalive is built lazily on the first `set_mode` transition that requires
// scheduling (`mode::stopped` -> `mode::running` or `mode::stopped` ->
// `mode::paused`). By then the owner's `shared_ptr` is established and
// `shared_from_this` works. `idle_timeout` can therefore be a value member of
// the owner and constructed in the owner's member-init list.
//
// The `postpone` method is fully thread-safe. Past that, individual loads and
// stores are atomic, but there is no serialization of concurrent mutators. To
// put it another way, it is best to call these other methods from a single
// thread. Mode changes must also be serialized against the sweeper's driver
// (in practice, both already run on the loop thread); only `postpone` may
// come from any thread.
template<typename Owner, typename Sweeper = timeout_sweeper<>>
class idle_timeout: public timeouts {
#pragma region Types
public:
  using time_point_t = steady_now_clock::time_point_t;
  using duration_t = steady_now_clock::duration_t;

  using owner_t = Owner;
  using sweeper_t = Sweeper;
  using callback_t = Sweeper::callback_t;

  // Cancellation action invoked when the idle timer expires.
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
    assert((configured >= duration_t{}) && (configured <= max_timeout));
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

  // Current deadline.
  //
  // When `mode::running`, this time was set to `infra::steady_now_clock::now()
  // + active_timeout()` and used to schedule the next sweep.
  //
  // When `mode::paused`, this is at or past the sentinel value
  // (`paused_expiration`) that signals the clipping behavior, so that the next
  // sweep is scheduled for `infra::steady_now_clock::now() +
  // configured_timeout()` but will not trigger an expiration.
  //
  // When `mode::stopped`, this is zero.
  [[nodiscard]] time_point_t deadline() const noexcept { return deadline_; }

  // Current mode.
  [[nodiscard]] mode get_mode() const noexcept {
    const auto deadline_snapshot = *deadline_;
    if ((scheduled_count_ == 0) || (deadline_snapshot == time_point_t{}))
      return mode::stopped;
    if (deadline_snapshot >= paused_expiration) return mode::paused;
    return mode::running;
  }

#pragma endregion
#pragma region Modifiers

  // Update the configured timeout.
  //
  // Syncs `active_timeout` if in `mode::running` (so the next deadline
  // reset uses the new value), but will not clear it.
  void configure(duration_t d) noexcept {
    assert((d >= duration_t{}) && (d <= max_timeout));
    configured_ = d;
    if (d == duration_t{}) return;
    auto expected = *active_;
    if (expected == duration_t{}) return;
    (void)active_.compare_exchange(expected, d);
  }

  // Postpone the expiration after progress
  //
  // Safe to call in any mode, from any thread: a postpone that races `stop` or
  // `pause` may be dropped, but can never resurrect a deadline they parked.
  void postpone() noexcept {
    auto expected = *deadline_;
    // Not running: nothing to postpone.
    if ((expected == time_point_t{}) || (expected >= paused_expiration))
      return;
    // Determine how long to postpone; if it got zeroed out, we lost the race.
    const auto active_snapshot = *active_;
    if (active_snapshot == duration_t{}) return;
    // Postpone deadline. If someone else changed it, we lost the race.
    (void)deadline_.compare_exchange(expected,
        steady_now_clock::now() + active_snapshot);
  }

  // Stop the timeout and cancel any pending entry.
  //
  // Idempotent. Safe to call in any mode. Leaves `configured_timeout` and
  // `on_idle` intact so the timeout can be restarted later.
  [[nodiscard]] bool stop() noexcept {
    active_ = duration_t{};
    deadline_ = time_point_t{};
    return true;
  }

  // Start the timeout that was stopped or paused.
  //
  // If a sweeper entry is already in flight (running, paused, or stopped but
  // not yet swept), all this does is move the deadline; the entry adapts on
  // its next fire (`mode::paused` -> `mode::running` may see up to one
  // `configured_timeout` of slop). Fails if `configured_timeout` is zero or
  // it can't schedule.
  [[nodiscard]] bool start() {
    const auto configured_snapshot = *configured_;
    if (configured_snapshot == duration_t{}) return false;
    active_ = configured_snapshot;
    const auto deadline_snapshot =
        steady_now_clock::now() + configured_snapshot;
    deadline_ = deadline_snapshot;
    return ensure_scheduled(deadline_snapshot);
  }

  // Pause the timeout. If already paused, does nothing.
  //
  // In pause mode, the deadline is parked at the sentinel value, and the
  // sweeper callback clips it back to `infra::steady_now_clock::now() +
  // configured_timeout()` on each fire without ever invoking the `on_idle`
  // expiration callback. Also, `postpone` becomes a no-op.
  //
  // Fails if `configured_timeout` is zero or it can't schedule.
  [[nodiscard]] bool pause() {
    const auto configured_snapshot = *configured_;
    if (configured_snapshot == duration_t{}) return false;
    active_ = duration_t{};
    // Park the sentinel BEFORE scheduling the bootstrap entry, so it can
    // never match a real deadline and fire; it lands on the clip path.
    deadline_ = paused_expiration;
    return ensure_scheduled(steady_now_clock::now() + configured_snapshot);
  }

  // Change the mode to start, stop, or pause the timeout.
  //
  // Returns `false` if `configured_timeout` is zero and `target` is not
  // `mode::stopped`, or if scheduling a fresh sweeper entry fails.
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
    cancel_action_t* expected{};
    (void)on_idle_once_.compare_exchange(expected, &on_idle_);
  }

#pragma endregion
#pragma region Helpers
private:
  // Ensure the single sweeper entry is in flight, scheduling one at `expire`
  // if not.
  //
  // An entry that is already in flight adapts to the current `deadline_` on
  // its next fire, so there is nothing to do. Fails when the sweeper refuses
  // (it is closing), and throws when building the callback or scheduling it
  // does. Either way the claim is released and the state unwinds to stopped,
  // so the mode reads truthfully and a later attempt can retry.
  [[nodiscard]] bool ensure_scheduled(time_point_t expire) {
    auto expected = *scheduled_count_;
    if (expected != 0) return true;
    // Claim the 0 -> 1 transition; losing means someone else just scheduled.
    if (!scheduled_count_.compare_exchange(expected, 1)) return true;

    // Every exit but success unwinds the claim, so a refusal and a throw
    // leave the same state.
    scope_exit unwind{[this]() noexcept {
      --scheduled_count_;
      (void)stop();
    }};
    if (!sweeper_.schedule(expire, build_sweeper_cb())) return false;
    unwind.release();
    return true;
  }

  // Build the sweeper closure.
  [[nodiscard]] callback_t build_sweeper_cb() {
    std::shared_ptr<idle_timeout> self_sp{owner_.shared_from_this(), this};
    return [weak = std::weak_ptr<idle_timeout>{std::move(self_sp)}](
               time_point_t fired) -> time_point_t {
      auto self = weak.lock();
      if (!self) return {};
      // A duplicate in-flight entry (which should not happen) drains itself
      // rather than shadowing the real one.
      if (self->scheduled_count_ > 1) {
        --self->scheduled_count_;
        return {};
      }
      const auto result = self->on_sweep(fired);
      if (result.fire_idle) self->expire();
      // Dropping the entry releases its claim on `scheduled_count_`.
      if (result.next_deadline == time_point_t{}) --self->scheduled_count_;
      return result.next_deadline;
    };
  }

  struct sweep_result {
    time_point_t next_deadline;
    bool fire_idle{};
  };

  [[nodiscard]] sweep_result on_sweep(time_point_t fired) noexcept {
    // `mode::stopped`: we transitioned out from under the sweeper. Drop the
    // entry; no rearm, no cancellation.
    if (*deadline_ == time_point_t{}) return {{}, false};
    // `mode::paused`: parked at the sentinel. The deadline is in the far
    // future, but we always set the next callback to the configured timeout
    // after now.
    if (*deadline_ >= paused_expiration) {
      // Defensive: configured was zeroed while paused, so the clip path
      // would compute `now + 0`, hot-looping. Collapse to Stopped.
      const auto configured_snapshot = *configured_;
      if (configured_snapshot == duration_t{}) {
        deadline_ = time_point_t{};
        return {{}, false};
      }
      // Clip back to a near-future fire so we keep checking periodically.
      return {steady_now_clock::now() + configured_snapshot, false};
    }
    // `mode::running` and deadline reached: nobody restarted between the
    // schedule and now. Fire the cancellation action and drop the entry.
    if (*deadline_ == fired) {
      deadline_ = time_point_t{};
      return {{}, true};
    }
    // `mode::running`, but deadline moved (`postpone` pushed it forward):
    // rearm to the new deadline; do not fire the cancellation action.
    return {*deadline_, false};
  }

#pragma endregion
#pragma region Data members

  // Summary:
  //
  // `sweeper_` is the non-owning reference to the sweeper, and is used to call
  // its `schedule` method to register the next callback.
  //
  // `owner_` is the non-owning reference to the object that owns this
  // `idle_timeout` instance. This is used in `build_sweeper_cb` to create a
  // `std::weak_ptr` to the owner, so that the callback harmlessly fails if the
  // owner has been destroyed.
  //
  // `on_idle_` is the cancellation action that is invoked when the idle
  // timeout expires.
  //
  // `on_idle_once_` is an atomic pointer to `on_idle_`, used to ensure that it
  // is invoked only once. It can be reset with `reset_expiration()`.
  //
  // `configured_` is the configured timeout duration, which can be updated
  // with `configure()`. This is used to set `active_` when starting.
  //
  // `active_` is the active timeout duration, which is non-zero only when in
  // `mode::running`. It is used to compute the next deadline when postponing.
  //
  // `deadline_` is the current deadline for the idle timeout. When zero, the
  // timeout is stopped. When running, it is set to `now() + active_`; with
  // each `postpone`, it is set again, only using the current `now()` value.
  // In paused mode, it is set to the sentinel value `paused_expiration`, which
  // is a time point far in the future.
  //
  // `scheduled_count_` is the number of sweeper entries currently in flight
  // for this instance: 0 or 1 in normal operation. Mutators claim the 0 -> 1
  // transition before scheduling, and the sweeper callback decrements when it
  // drops an entry, so restarting before a stopped entry is swept can never
  // double-schedule. It is a counter rather than a flag to leave room for
  // deliberately scheduling an earlier second entry (e.g. so a shortened
  // timeout takes effect immediately), with the extra entry draining itself.

  sweeper_t& sweeper_;
  owner_t& owner_;
  cancel_action_t on_idle_;
  relaxed_atomic<cancel_action_t*> on_idle_once_;
  relaxed_atomic<duration_t> configured_;
  relaxed_atomic<duration_t> active_;
  relaxed_atomic<time_point_t> deadline_;
  relaxed_atomic_int scheduled_count_;

#pragma endregion
};

#pragma endregion

}} // namespace corvid::concurrency
