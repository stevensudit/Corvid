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
  // No restart_deadline calls -> deadline matches the registered time,
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
  // A `restart_deadline` between submission and the registered fire
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
  // Calling `restart_deadline` while still Stopped writes
  // `deadline_ = now() + 0 = now()`, a real value. `get_state` then
  // reports `active` even though no sweeper entry exists. The bug
  // (set_mode(active) silently skipping the schedule) is gone now
  // that `was_stopped` checks `deadline_ <= now()` instead of
  // `deadline_ == T0`: the stale deadline equals the `now` at restart
  // time, which is at most the current `now`, so the check correctly
  // schedules a fresh entry.
  sweeper sw;
  auto o = make_owner(sw, 100ms);
  o->now = T(42);
  o->idle.postpone();
  EXPECT_TRUE(sw.empty());
  EXPECT_EQ(static_cast<int>(o->idle.get_mode()),
      static_cast<int>(mode::stopped));

  // Direct recovery: `set_mode(active)` now schedules a fresh entry
  // without first needing `set_mode(stopped)`.
  EXPECT_TRUE(o->idle.set_mode(mode::running));
  EXPECT_EQ(sw.size(), 1U);
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

MAKE_TEST_LIST(IdleTimeout_DefaultIsStopped,
    IdleTimeout_ActiveRequiresNonZeroConfigured, IdleTimeout_StoppedToActive,
    IdleTimeout_ActiveFiresOnIdle, IdleTimeout_RestartPushesDeadline,
    IdleTimeout_RestartFromStoppedIsRecoverable,
    IdleTimeout_StoppedToPausedBootstrap, IdleTimeout_PausedClipsAndStays,
    IdleTimeout_PausedToActive, IdleTimeout_ActiveToStoppedDropsEntry,
    IdleTimeout_ConfigureSyncsActiveOnlyWhenActive,
    IdleTimeout_OwnerDeathDropsCallback, IdleTimeout_NowFnInjection);

// NOLINTEND(readability-function-cognitive-complexity)
