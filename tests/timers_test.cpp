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

#include "../corvid/concurrency/timers.h"

std::ostream&
operator<<(std::ostream& os, const corvid::concurrency::time_point_t& when) {
  os << when.time_since_epoch().count();
  return os;
}

#include "catch2_main.h"

using namespace std::literals;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

auto make_date(auto date) {
  return steady_clock::time_point{} + sys_days{date}.time_since_epoch();
}

using time_point_t = corvid::concurrency::time_point_t;

static time_point_t make_time(int ms) {
  return time_point_t{} + milliseconds{ms};
}

#pragma region OneShot

TEST_CASE("OneShot", "[TimersTest]") {
  auto now = make_time(100);
  auto t = timers::make("test");
  t->set_clock_callback([&]() { return now; });
  std::vector<time_point_t> fired;

  // Schedule event.
  auto ev = t->set(50ms, [&](const timer_invocation& i) -> time_point_t {
    fired.push_back(i.scheduled_time);
    return {};
  });
  CHECK_FALSE((ev->canceled));

  // Way too soon.
  size_t c{};
  c = t->tick();
  CHECK((c) == (0U));
  CHECK((fired.size()) == (0U));

  // Slightly too soon.
  now += 49ms;
  c = t->tick();
  CHECK((c) == (0U));
  CHECK((fired.size()) == (0U));

  // Right now.
  now += 1ms;
  c = t->tick();
  CHECK((c) == (1U));
  CHECK((fired.size()) == (1U));
  CHECK((fired[0]) == (now));

  // No more events.
  now += 50ms;
  c = t->tick();
  CHECK((c) == (0U));
  CHECK((fired.size()) == (1U));

  CHECK((ev->canceled));

  // Reschedule self.
  size_t cnt{};
  ev = t->set(50ms, [&](const timer_invocation& i) -> time_point_t {
    if (++cnt < 2) return i.scheduled_time + 50ms;
    return {};
  });

  CHECK_FALSE((ev->canceled));
  c = t->tick();
  CHECK((c) == (0U));
  CHECK((cnt) == (0U));

  now += 50ms;
  c = t->tick();
  CHECK((c) == (1U));
  CHECK((cnt) == (1U));
  CHECK_FALSE((ev->canceled));

  now += 50ms;
  c = t->tick();
  CHECK((c) == (1U));
  CHECK((cnt) == (2U));
  CHECK((ev->canceled));
}

#pragma endregion
#pragma region Repeating

TEST_CASE("Repeating", "[TimersTest]") {
  auto now = make_time(100);
  auto t = timers::make("test");
  t->set_clock_callback([&]() { return now; });
  size_t calls{};

  // Schedule a repeating event: fires at 10ms, repeats every 15ms.
  auto ev = t->set(
      10ms,
      [&](const timer_invocation&) -> time_point_t {
        ++calls;
        return {};
      },
      15ms);

  now += 10ms;
  t->tick();
  CHECK((calls) == (1U));

  now += 15ms;
  t->tick();
  CHECK((calls) == (2U));

  now += 15ms;
  t->tick();
  CHECK((calls) == (3U));

  // Cancel by setting tombstone.
  (void)ev->canceled.kill();
  now += 15ms;
  t->tick();
  CHECK((calls) == (3U)); // cancelled
}

#pragma endregion
#pragma region Cancel

TEST_CASE("Cancel", "[TimersTest]") {
  auto now = make_time(100);
  auto t = timers::make("test");
  t->set_clock_callback([&]() { return now; });

  bool called = false;
  auto ev = t->set(20ms, [&](const timer_invocation&) -> time_point_t {
    called = true;
    return {};
  });

  // Cancel before it fires.
  CHECK_FALSE((ev->canceled));
  (void)ev->canceled.kill();
  CHECK((ev->canceled));

  now += 25ms;
  t->tick();
  CHECK_FALSE((called));
}

#pragma endregion
#pragma region Reschedule

TEST_CASE("Reschedule", "[TimersTest]") {
  auto now = make_time(100);
  auto t = timers::make("test");
  t->set_clock_callback([&]() { return now; });

  size_t calls{};
  auto ev = t->set(10ms, [&](const timer_invocation& i) -> time_point_t {
    ++calls;
    if (calls == 1) {
      // Reschedule for 20ms from now.
      return i.now + 20ms;
    }
    // Don't reschedule again.
    return {};
  });

  now += 10ms;
  t->tick();
  CHECK((calls) == (1U));
  CHECK_FALSE((ev->canceled));

  now += 10ms;
  t->tick();
  CHECK((calls) == (1U)); // not yet

  now += 10ms;
  t->tick();
  CHECK((calls) == (2U));
  CHECK((ev->canceled));
}

#pragma endregion
#pragma region General

TEST_CASE("General", "[TimersTest]") {
  auto now = make_date(2024y / 1 / 1);
  auto t = timers::make("test");
  t->set_clock_callback([&now]() { return now; });
  std::vector<timer_event_ptr> fired_events;

  // Schedule two one-shot events at different times.
  auto ev1 = t->set(30s, [&](const timer_invocation& i) -> time_point_t {
    fired_events.push_back(i.event);
    return {};
  });
  CHECK((ev1->start_at) == (now + 30s));

  auto ev2 = t->set(60s, [&](const timer_invocation& i) -> time_point_t {
    fired_events.push_back(i.event);
    return {};
  });
  CHECK((ev2->start_at) == (now + 60s));

  size_t count = 0;
  count = t->tick();
  CHECK((count) == (0U));

  now += 29s;
  count = t->tick();
  CHECK((count) == (0U));

  now += 1s;
  count = t->tick();
  CHECK((count) == (1U));
  CHECK((fired_events.size()) == (1U));
  CHECK((fired_events[0]) == (ev1));
  CHECK((ev1->canceled));
  CHECK_FALSE((ev2->canceled));

  now += 30s;
  count = t->tick();
  CHECK((count) == (1U));
  CHECK((fired_events.size()) == (2U));
  CHECK((fired_events[1]) == (ev2));
  CHECK((ev2->canceled));
}

#pragma endregion
#pragma region Edge

TEST_CASE("Edge", "[TimersTest]") {
  auto now = make_date(2024y / 1 / 1);
  auto t = timers::make("test");
  t->set_clock_callback([&now]() { return now; });
  size_t calls{};

  size_t count = 0;

  // Repeating: fires at 30s, then every 60s after.
  auto ev = t->set(
      30s,
      [&](const timer_invocation&) -> time_point_t {
        ++calls;
        return {};
      },
      60s);

  now += 29s;
  count = t->tick();
  CHECK((count) == (0U));

  now += 1s;
  count = t->tick();
  CHECK((count) == (1U)); // Fires at 30s.
  CHECK((calls) == (1U));

  now += 59s;
  count = t->tick();
  CHECK((count) == (0U));

  now += 1s;
  count = t->tick();
  CHECK((count) == (1U)); // Fires at 90s (30s + 60s).
  CHECK((calls) == (2U));

  // Cancel to clean up.
  (void)ev->canceled.kill();
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
