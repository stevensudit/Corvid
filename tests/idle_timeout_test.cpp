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

#include "../corvid/concurrency/idle_timeout.h"
#include "../corvid/concurrency/timeout_sweeper.h"

#include "minitest.h"

#include <memory>

using namespace std::chrono_literals;
using namespace corvid::concurrency;

using sweeper = timeout_sweeper<>;
using tp = timeout_sweeper_base::time_point_t;
using dur = timeout_sweeper_base::duration_t;
using mode = timeout_sweeper_base::mode;

// Construct a deterministic `time_point` at `ms` milliseconds past the
// steady-clock epoch. Tests drive the clock by writing to `test_owner::now`
// and by passing the desired `now` to `sweeper::tick` directly.
static tp T(int ms) { return tp{} + std::chrono::milliseconds{ms}; }

#pragma region Fixture

namespace {

// Minimal owner: holds one `idle_timeout`, exposes a fake clock that tests
// advance manually, and records every invocation of the cancel action.
//
// `now` is a `static inline` so the test fake-clock function can be a
// non-capturing lambda (convertible to the raw function pointer that
// `set_now_fn` takes). The tests stay single-owner; if we ever wanted
// concurrent fixtures we'd need a different strategy.
struct test_owner: std::enable_shared_from_this<test_owner> {
  static inline tp now{};
  int idle_count{0};
  idle_timeout<test_owner> idle;

  test_owner(sweeper& sw, dur configured)
      : idle{sw, *this, idle_timeout<test_owner>::cancel_action_t{[this] {
               ++idle_count;
             }},
            configured} {
    // Reset between tests so each fixture starts with `now == T(0)`.
    now = tp{};
    // Replace the default clock function with one that reads our fake.
    // Non-capturing + noexcept -> convertible to the raw function pointer.
    idle.set_now_fn([]() noexcept { return now; });
  }
};

// Wrap construction so the test always has a valid `shared_ptr` before
// any `set_mode` call (the lazy aliased keepalive requires it).
[[nodiscard]] std::shared_ptr<test_owner>
make_owner(sweeper& sw, dur configured) {
  return std::make_shared<test_owner>(sw, configured);
}

} // namespace

#pragma endregion

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region DefaultIsStopped

void IdleTimeout_DefaultIsStopped() {
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::stopped));
  EXPECT_EQ(o->idle.configured_timeout(), dur{100ms});
  EXPECT_EQ(o->idle.active_timeout(), dur{});
}

#pragma endregion
#pragma region ActiveRequiresNonZeroConfigured

void IdleTimeout_ActiveRequiresNonZeroConfigured() {
  // With configured == 0, only Stopped is reachable.
  sweeper sw;
  auto o = make_owner(sw, dur{});
  EXPECT_FALSE(o->idle.set_mode(mode::running));
  EXPECT_FALSE(o->idle.set_mode(mode::paused));
  EXPECT_TRUE(o->idle.set_mode(mode::stopped));
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::stopped));
  EXPECT_EQ(sw.size(), 0U);
}

#pragma endregion
#pragma region StoppedToActive

void IdleTimeout_StoppedToActive() {
  // Transitioning Stopped -> Active sets the deadline to now+configured
  // and schedules a fresh sweeper entry.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(50);
  EXPECT_TRUE(o->idle.set_mode(mode::running));
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::running));
  EXPECT_EQ(o->idle.active_timeout(), dur{100ms});
  EXPECT_EQ(o->idle.deadline().time_since_epoch().count(),
      T(150).time_since_epoch().count());
  EXPECT_EQ(sw.size(), 1U);
}

#pragma endregion
#pragma region ActiveFiresOnIdle

void IdleTimeout_ActiveFiresOnIdle() {
  // No `postpone` calls -> deadline matches the registered time,
  // callback invokes the cancel action exactly once, sweeper entry drops.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::running));

  // Not yet expired.
  o->now = T(99);
  sw.tick(T(99));
  EXPECT_EQ(o->idle_count, 0);
  EXPECT_EQ(sw.size(), 1U);

  // Expired.
  o->now = T(100);
  sw.tick(T(100));
  EXPECT_EQ(o->idle_count, 1);
  EXPECT_TRUE(sw.empty());
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::stopped));
}

#pragma endregion
#pragma region RestartPushesDeadline

void IdleTimeout_RestartPushesDeadline() {
  // A `postpone` call between submission and the registered fire
  // pushes the deadline forward; the callback rearms instead of firing
  // the idle action.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::running));
  // Initially registered at T(100).

  // Activity at T(50): push deadline to T(150).
  o->now = T(50);
  o->idle.postpone();
  EXPECT_EQ(o->idle.deadline().time_since_epoch().count(),
      T(150).time_since_epoch().count());

  // Tick at T(100): the registered entry fires, but current != registered,
  // so it rearms to T(150) without firing the idle action.
  o->now = T(100);
  sw.tick(T(100));
  EXPECT_EQ(o->idle_count, 0);
  EXPECT_EQ(sw.size(), 1U);

  // Tick at T(150): registered entry now matches; idle action fires once.
  o->now = T(150);
  sw.tick(T(150));
  EXPECT_EQ(o->idle_count, 1);
  EXPECT_TRUE(sw.empty());
}

#pragma endregion
#pragma region RestartFromStoppedIsRecoverable

void IdleTimeout_RestartFromStoppedIsRecoverable() {
  // `postpone` is a no-op while Stopped (active_ is zero), so `deadline_`
  // stays at T0 and `get_mode` keeps reporting Stopped. A direct
  // `set_mode(running)` then schedules a fresh entry: `was_stopped` is
  // satisfied because `deadline_ == T0 <= now`.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(42);
  o->idle.postpone();
  EXPECT_EQ(o->idle.deadline().time_since_epoch().count(),
      tp{}.time_since_epoch().count());
  EXPECT_TRUE(sw.empty());
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::stopped));

  EXPECT_TRUE(o->idle.set_mode(mode::running));
  EXPECT_EQ(sw.size(), 1U);
}

#pragma endregion
#pragma region PostponeIsNoOpOutsideRunning

void IdleTimeout_PostponeIsNoOpOutsideRunning() {
  // `postpone` reads `active_` and bails when zero, so neither Paused nor
  // Stopped should observe any change to `deadline_`.
  sweeper sw;
  auto o = make_owner(sw, 100ms);

  // Paused: postpone must not disturb the sentinel, must not break the
  // clip cycle, and must leave the mode as Paused.
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::paused));
  o->now = T(50);
  o->idle.postpone();
  EXPECT_EQ(o->idle.deadline().time_since_epoch().count(),
      timeout_sweeper_base::paused_expiration.time_since_epoch().count());
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::paused));

  // Bootstrap entry at T(100) still clips back to now + configured.
  o->now = T(100);
  sw.tick(T(100));
  EXPECT_EQ(o->idle_count, 0);
  EXPECT_EQ(sw.size(), 1U);
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::paused));

  // Stopped: postpone must leave `deadline_` at T0.
  EXPECT_TRUE(o->idle.set_mode(mode::stopped));
  o->idle.postpone();
  EXPECT_EQ(o->idle.deadline().time_since_epoch().count(),
      tp{}.time_since_epoch().count());
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::stopped));
}

#pragma endregion
#pragma region StoppedToPausedBootstrap

void IdleTimeout_StoppedToPausedBootstrap() {
  // Stopped -> Paused schedules a near-future entry then parks the
  // deadline at the sentinel.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::paused));
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::paused));
  EXPECT_EQ(o->idle.deadline().time_since_epoch().count(),
      timeout_sweeper_base::paused_expiration.time_since_epoch().count());
  EXPECT_EQ(sw.size(), 1U);
}

#pragma endregion
#pragma region PausedClipsAndStays

void IdleTimeout_PausedClipsAndStays() {
  // When the bootstrap entry fires, the callback sees the sentinel and
  // clips back to now + configured, staying in Paused.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::paused));

  // First sweep at T(100) hits the bootstrap entry. Callback sees the
  // sentinel and reschedules at now+100 = T(200). No idle fire.
  o->now = T(100);
  sw.tick(T(100));
  EXPECT_EQ(o->idle_count, 0);
  EXPECT_EQ(sw.size(), 1U);
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::paused));

  // Second sweep at T(200). Same thing.
  o->now = T(200);
  sw.tick(T(200));
  EXPECT_EQ(o->idle_count, 0);
  EXPECT_EQ(sw.size(), 1U);
}

#pragma endregion
#pragma region PausedToActive

void IdleTimeout_PausedToActive() {
  // Resume from Paused: deadline becomes now+configured immediately; no
  // new schedule is needed (the existing entry adapts on its next fire).
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::paused));
  EXPECT_EQ(sw.size(), 1U);

  o->now = T(50);
  EXPECT_TRUE(o->idle.set_mode(mode::running));
  EXPECT_EQ(o->idle.deadline().time_since_epoch().count(),
      T(150).time_since_epoch().count());
  // No fresh schedule; we reused the existing entry.
  EXPECT_EQ(sw.size(), 1U);

  // The original bootstrap entry was at T(100). It fires there, sees
  // current=T(150) != registered, rearms to T(150).
  o->now = T(100);
  sw.tick(T(100));
  EXPECT_EQ(o->idle_count, 0);
  EXPECT_EQ(sw.size(), 1U);

  // Tick at T(150): idle action fires.
  o->now = T(150);
  sw.tick(T(150));
  EXPECT_EQ(o->idle_count, 1);
}

#pragma endregion
#pragma region ActiveToStoppedDropsEntry

void IdleTimeout_ActiveToStoppedDropsEntry() {
  // Going Stopped from Active sets the deadline to T0. The existing
  // entry fires at the originally-registered time, sees T0, drops.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::running));
  EXPECT_EQ(sw.size(), 1U);

  o->now = T(50);
  EXPECT_TRUE(o->idle.set_mode(mode::stopped));
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::stopped));
  EXPECT_EQ(o->idle.active_timeout(), dur{});
  // Sweeper still has the original entry; it'll drop on next fire.
  EXPECT_EQ(sw.size(), 1U);

  o->now = T(100);
  sw.tick(T(100));
  EXPECT_EQ(o->idle_count, 0);
  EXPECT_TRUE(sw.empty());
}

#pragma endregion
#pragma region ConfigureSyncsActiveOnlyWhenActive

void IdleTimeout_ConfigureSyncsActiveOnlyWhenActive() {
  sweeper sw;
  auto o = make_owner(sw, 100ms);

  // Stopped: configure should NOT sync active (active was 0; stays 0).
  o->idle.configure(dur{200ms});
  EXPECT_EQ(o->idle.configured_timeout(), dur{200ms});
  EXPECT_EQ(o->idle.active_timeout(), dur{});

  // Activate: active picks up the configured value (200ms).
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::running));
  EXPECT_EQ(o->idle.active_timeout(), dur{200ms});

  // While active, configure with a non-zero value DOES sync active.
  o->idle.configure(dur{500ms});
  EXPECT_EQ(o->idle.configured_timeout(), dur{500ms});
  EXPECT_EQ(o->idle.active_timeout(), dur{500ms});

  // While active, configure(0) updates configured_ but leaves active_
  // alone -- only `set_mode(stopped)` can clear active_. Prevents the
  // silent-close footgun where the next sweep would fire the cancel
  // action because active was zeroed mid-run.
  o->idle.configure(dur{});
  EXPECT_EQ(o->idle.configured_timeout(), dur{});
  EXPECT_EQ(o->idle.active_timeout(), dur{500ms});
  // get_state still says active (deadline_ is a real value).
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::running));
  // set_mode(active|paused) now rejected because configured_ is 0.
  EXPECT_FALSE(o->idle.set_mode(mode::running));
  EXPECT_FALSE(o->idle.set_mode(mode::paused));
  // Only set_mode(stopped) actually stops the idle clock.
  EXPECT_TRUE(o->idle.set_mode(mode::stopped));
  EXPECT_EQ(o->idle.active_timeout(), dur{});
}

#pragma endregion
#pragma region OwnerDeathDropsCallback

void IdleTimeout_OwnerDeathDropsCallback() {
  // When the owner is destroyed before the entry fires, the weak_ptr
  // lock fails and the callback returns {} so the sweeper drops the
  // entry. The idle action never fires.
  sweeper sw;
  int fired_before_dtor = 0;
  {
    auto o = make_owner(sw, 100ms);
    o->now = T(0);
    EXPECT_TRUE(o->idle.set_mode(mode::running));
    fired_before_dtor = o->idle_count;
    EXPECT_EQ(sw.size(), 1U);
    // owner falls out of scope here.
  }
  EXPECT_EQ(fired_before_dtor, 0);
  // Sweeper still has the entry; tick should drop it cleanly.
  sw.tick(T(100));
  EXPECT_TRUE(sw.empty());
}

#pragma endregion
#pragma region NowFnInjection

void IdleTimeout_NowFnInjection() {
  // Sanity check that the injected clock is what the class actually
  // calls -- different `set_now_fn` should produce a different deadline.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(12345);
  EXPECT_TRUE(o->idle.set_mode(mode::running));
  EXPECT_EQ(o->idle.deadline().time_since_epoch().count(),
      T(12445).time_since_epoch().count());
}

#pragma endregion
#pragma region ExpireIsIdempotent

void IdleTimeout_ExpireIsIdempotent() {
  // `expire` fires the cancel action exactly once. `reset_expiration` arms
  // it to fire again. Both calls are also no-ops if no rearm is pending.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::running));

  o->idle.expire();
  EXPECT_EQ(o->idle_count, 1);
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::stopped));

  // Repeat calls do nothing until `reset_expiration` arms again.
  o->idle.expire();
  o->idle.expire();
  EXPECT_EQ(o->idle_count, 1);

  o->idle.reset_expiration();
  o->idle.expire();
  EXPECT_EQ(o->idle_count, 2);

  // `reset_expiration` is itself idempotent: calling it twice doesn't
  // unlock two more fires.
  o->idle.reset_expiration();
  o->idle.reset_expiration();
  o->idle.expire();
  EXPECT_EQ(o->idle_count, 3);
  o->idle.expire();
  EXPECT_EQ(o->idle_count, 3);
}

#pragma endregion
#pragma region SweepAndExpireFireOnce

void IdleTimeout_SweepAndExpireFireOnce() {
  // The sweeper-driven fire and a manual `expire` share the same one-shot
  // slot, so a manual call after the sweep is a no-op.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(0);
  EXPECT_TRUE(o->idle.set_mode(mode::running));

  o->now = T(100);
  sw.tick(T(100));
  EXPECT_EQ(o->idle_count, 1);

  o->idle.expire();
  EXPECT_EQ(o->idle_count, 1);

  // And in the other order: a manual `expire` first means the subsequent
  // sweep doesn't double-fire.
  EXPECT_TRUE(o->idle.set_mode(mode::stopped));
  o->idle.reset_expiration();
  EXPECT_TRUE(o->idle.set_mode(mode::running));
  o->now = T(200);
  o->idle.expire();
  EXPECT_EQ(o->idle_count, 2);

  // The lingering sweeper entry from set_mode(mode::running) drops cleanly
  // on its next fire without invoking the cancel action again.
  o->now = T(300);
  sw.tick(T(300));
  EXPECT_EQ(o->idle_count, 2);
  EXPECT_TRUE(sw.empty());
}

#pragma endregion

MAKE_TEST_LIST(IdleTimeout_DefaultIsStopped,
    IdleTimeout_ActiveRequiresNonZeroConfigured, IdleTimeout_StoppedToActive,
    IdleTimeout_ActiveFiresOnIdle, IdleTimeout_RestartPushesDeadline,
    IdleTimeout_RestartFromStoppedIsRecoverable,
    IdleTimeout_PostponeIsNoOpOutsideRunning,
    IdleTimeout_StoppedToPausedBootstrap, IdleTimeout_PausedClipsAndStays,
    IdleTimeout_PausedToActive, IdleTimeout_ActiveToStoppedDropsEntry,
    IdleTimeout_ConfigureSyncsActiveOnlyWhenActive,
    IdleTimeout_OwnerDeathDropsCallback, IdleTimeout_NowFnInjection,
    IdleTimeout_ExpireIsIdempotent, IdleTimeout_SweepAndExpireFireOnce);

// NOLINTEND(readability-function-cognitive-complexity)
