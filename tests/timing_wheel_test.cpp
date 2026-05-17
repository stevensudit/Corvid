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

#include "../corvid/concurrency/timing_wheel.h"

#include "catch2_main.h"

using namespace std::chrono_literals;
using namespace corvid::concurrency;

using tp = timing_wheel::time_point_t;
using dur = timing_wheel::duration_t;

// Construct a time_point at `ms` milliseconds past the epoch.
static tp T(int ms) { return tp{} + std::chrono::milliseconds{ms}; }

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region BasicFire

TEST_CASE("TimingWheel_BasicFire", "[TimingWheel]") {
  // Schedule at 200ms; should not fire at 100ms but should fire at 200ms.
  timing_wheel wheel(600, dur{100}, T(0));
  int fired = 0;

  CHECK((wheel.schedule(
            [&] {
              ++fired;
              return true;
            },
            200ms)) == (true));

  wheel.tick(T(100));
  CHECK((fired) == (0));

  wheel.tick(T(200));
  CHECK((fired) == (1));

  // Should not fire again.
  wheel.tick(T(300));
  CHECK((fired) == (1));
}

#pragma endregion
#pragma region NotFiredEarly

TEST_CASE("TimingWheel_NotFiredEarly", "[TimingWheel]") {
  // Verify no premature fire.
  timing_wheel wheel(600, dur{100}, T(0));
  int fired = 0;

  CHECK((wheel.schedule(
            [&] {
              ++fired;
              return true;
            },
            300ms)) == (true));

  wheel.tick(T(100));
  CHECK((fired) == (0));
  wheel.tick(T(200));
  CHECK((fired) == (0));
  wheel.tick(T(300));
  CHECK((fired) == (1));
}

#pragma endregion
#pragma region MultiSlot

TEST_CASE("TimingWheel_MultiSlot", "[TimingWheel]") {
  // Three entries at 100/200/300ms; exactly one fires per 100ms tick.
  timing_wheel wheel(600, dur{100}, T(0));
  int count = 0;

  CHECK((wheel.schedule(
            [&] {
              ++count;
              return true;
            },
            100ms)) == (true));
  CHECK((wheel.schedule(
            [&] {
              ++count;
              return true;
            },
            200ms)) == (true));
  CHECK((wheel.schedule(
            [&] {
              ++count;
              return true;
            },
            300ms)) == (true));

  wheel.tick(T(100));
  CHECK((count) == (1));
  wheel.tick(T(200));
  CHECK((count) == (2));
  wheel.tick(T(300));
  CHECK((count) == (3));
}

#pragma endregion
#pragma region TickSkip

TEST_CASE("TimingWheel_TickSkip", "[TimingWheel]") {
  // A single tick jump drains all three slots at once.
  timing_wheel wheel(600, dur{100}, T(0));
  int count = 0;

  CHECK((wheel.schedule(
            [&] {
              ++count;
              return true;
            },
            100ms)) == (true));
  CHECK((wheel.schedule(
            [&] {
              ++count;
              return true;
            },
            200ms)) == (true));
  CHECK((wheel.schedule(
            [&] {
              ++count;
              return true;
            },
            300ms)) == (true));

  wheel.tick(T(300));
  CHECK((count) == (3));
}

#pragma endregion
#pragma region TickTooEarly

TEST_CASE("TimingWheel_TickTooEarly", "[TimingWheel]") {
  // Ticks separated by less than 100ms are no-ops.
  timing_wheel wheel(600, dur{100}, T(0));
  int fired = 0;

  CHECK((wheel.schedule(
            [&] {
              ++fired;
              return true;
            },
            100ms)) == (true));

  wheel.tick(T(50));
  CHECK((fired) == (0));
  wheel.tick(T(80));
  CHECK((fired) == (0));
  wheel.tick(T(100));
  CHECK((fired) == (1));
}

#pragma endregion
#pragma region ZeroDelay

TEST_CASE("TimingWheel_ZeroDelay", "[TimingWheel]") {
  // Zero delay is clamped to one tick; fires on the next tick, not current.
  timing_wheel wheel(600, dur{100}, T(0));
  int fired = 0;

  CHECK((wheel.schedule(
            [&] {
              ++fired;
              return true;
            },
            dur{0})) == (true));

  // Should not fire synchronously.
  CHECK((fired) == (0));

  wheel.tick(T(100));
  CHECK((fired) == (1));
}

#pragma endregion
#pragma region OverflowFails

TEST_CASE("TimingWheel_OverflowFails", "[TimingWheel]") {
  // A delay exceeding the wheel range is rejected; returns false.
  // Use slot_count=10 so the max is 900ms.
  timing_wheel wheel(10, dur{100}, T(0));
  int fired = 0;

  // 10 seconds far exceeds the 900ms maximum: schedule fails.
  CHECK((wheel.schedule(
            [&] {
              ++fired;
              return true;
            },
            10000ms)) == (false));

  // Exact maximum is accepted.
  CHECK((wheel.schedule(
            [&] {
              ++fired;
              return true;
            },
            900ms)) == (true));

  // Advances to max slot; only the valid callback fires.
  wheel.tick(T(900));
  CHECK((fired) == (1));
}

#pragma endregion
#pragma region SameSlotMultiple

TEST_CASE("TimingWheel_SameSlotMultiple", "[TimingWheel]") {
  // Five callbacks in the same slot all fire together.
  timing_wheel wheel(600, dur{100}, T(0));
  int count = 0;

  for (int i = 0; i < 5; ++i)
    CHECK((wheel.schedule(
              [&] {
                ++count;
                return true;
              },
              300ms)) == (true));

  wheel.tick(T(300));
  CHECK((count) == (5));
}

#pragma endregion
#pragma region RingWrap

TEST_CASE("TimingWheel_RingWrap", "[TimingWheel]") {
  // Advance near the end of the ring; a new entry wraps around to slot 0.
  // slot_count=10: slots 0..9, max delay = 900ms.
  timing_wheel wheel(10, dur{100}, T(0));
  int fired = 0;

  // Advance 8 slots: current_slot becomes 8, last_tick = T(800).
  wheel.tick(T(800));

  // Schedule 200ms from now: ticks_ahead=2, target=(8+2)%10=0 (wraps).
  CHECK((wheel.schedule(
            [&] {
              ++fired;
              return true;
            },
            200ms)) == (true));

  CHECK((fired) == (0));

  // Advance 2 more slots (9 and 0); callback fires when slot 0 is drained.
  wheel.tick(T(1000));
  CHECK((fired) == (1));
}

#pragma endregion
#pragma region ScheduleDuringTick

TEST_CASE("TimingWheel_ScheduleDuringTick", "[TimingWheel]") {
  // A callback that calls schedule() places the new entry in a future slot.
  timing_wheel wheel(600, dur{100}, T(0));
  int first = 0;
  int second = 0;

  CHECK((wheel.schedule(
            [&] {
              ++first;
              // Schedule a follow-up 100ms later.
              CHECK((wheel.schedule(
                        [&] {
                          ++second;
                          return true;
                        },
                        100ms)) == (true));
              return true;
            },
            100ms)) == (true));

  wheel.tick(T(100));
  CHECK((first) == (1));
  CHECK((second) == (0)); // Not yet fired.

  wheel.tick(T(200));
  CHECK((second) == (1));
}

#pragma endregion
#pragma region CallbackOwnsMeta

TEST_CASE("TimingWheel_CallbackOwnsMeta", "[TimingWheel]") {
  // The callback closure owns all its own metadata (simulated ID + target).
  timing_wheel wheel(600, dur{100}, T(0));

  // Simulate a caller-owned ID scheme.
  uint64_t active_id = 42;
  uint64_t received_id = 0;

  const uint64_t scheduled_id = active_id;
  CHECK((wheel.schedule(
            [scheduled_id, &active_id, &received_id] {
              // Relevance check: stale if IDs don't match.
              if (active_id != scheduled_id) return false;
              received_id = scheduled_id;
              return true;
            },
            100ms)) == (true));

  wheel.tick(T(100));
  CHECK((received_id) == (42U));

  // Simulate a second schedule where the operation completed before the timer.
  uint64_t received_id2 = 0;
  const uint64_t scheduled_id2 = 43;
  CHECK((wheel.schedule(
            [&active_id, &received_id2] {
              if (active_id != scheduled_id2) return false; // stale
              received_id2 = scheduled_id2;
              return true;
            },
            100ms)) == (true));

  // Operation completed: "cancel" by advancing active_id.
  active_id = 0;

  wheel.tick(T(200));
  CHECK((received_id2) == (0U)); // Callback fired but was stale.
}

#pragma endregion
#pragma region StopAbortsTick

TEST_CASE("TimingWheel_StopAbortsTick", "[TimingWheel]") {
  // Calling stop() during tick() prevents remaining callbacks from firing.
  timing_wheel wheel(600, dur{100}, T(0));
  int count = 0;

  // Schedule five callbacks in the same slot; the first one calls stop().
  for (int i = 0; i < 5; ++i)
    CHECK((wheel.schedule(
              [&] {
                ++count;
                return wheel.stop(); // kills the tombstone
              },
              100ms)) == (true));

  wheel.tick(T(100));

  // Only the first callback fires before the tombstone is seen.
  CHECK((count) == (1));
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
