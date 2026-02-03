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

#include "../corvid/containers/timers.h"

std::ostream&
operator<<(std::ostream& os, const corvid::timers_ns::time_point_t& when) {
  os << when.time_since_epoch().count();
  return os;
}

#include "minitest.h"

using namespace std::literals;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace corvid;
using namespace corvid::atomic_tomb;
using namespace corvid::timers_ns;

// NOLINTBEGIN(readability-function-cognitive-complexity)

auto make_date(auto date) {
  return steady_clock::time_point{} + sys_days{date}.time_since_epoch();
}

using time_point_t = corvid::timers_ns::time_point_t;

static time_point_t make_time(int ms) {
  return time_point_t{} + milliseconds{ms};
}

void TimersTest_OneShot() {
  auto now = make_time(100);
  auto t = timers::make("test");
  t->set_clock_callback([&]() { return now; });
  std::vector<time_point_t> fired;

  // Schedule event.
  auto ev = t->set(50ms, [&](const timer_invocation& i) -> time_point_t {
    fired.push_back(i.scheduled_time);
    return {};
  });
  EXPECT_FALSE(ev->canceled);

  // Way too soon.
  size_t c{};
  c = t->tick();
  EXPECT_EQ(c, 0U);
  EXPECT_EQ(fired.size(), 0U);

  // Slightly too soon.
  now += 49ms;
  c = t->tick();
  EXPECT_EQ(c, 0U);
  EXPECT_EQ(fired.size(), 0U);

  // Right now.
  now += 1ms;
  c = t->tick();
  EXPECT_EQ(c, 1U);
  EXPECT_EQ(fired.size(), 1U);
  EXPECT_EQ(fired[0], now);

  // No more events.
  now += 50ms;
  c = t->tick();
  EXPECT_EQ(c, 0U);
  EXPECT_EQ(fired.size(), 1U);

  EXPECT_TRUE(ev->canceled);

  // Reschedule self.
  size_t cnt{};
  ev = t->set(50ms, [&](const timer_invocation& i) -> time_point_t {
    if (++cnt < 2) return i.scheduled_time + 50ms;
    return {};
  });

  EXPECT_FALSE(ev->canceled);
  c = t->tick();
  EXPECT_EQ(c, 0U);
  EXPECT_EQ(cnt, 0U);

  now += 50ms;
  c = t->tick();
  EXPECT_EQ(c, 1U);
  EXPECT_EQ(cnt, 1U);
  EXPECT_FALSE(ev->canceled);

  now += 50ms;
  c = t->tick();
  EXPECT_EQ(c, 1U);
  EXPECT_EQ(cnt, 2U);
  EXPECT_TRUE(ev->canceled);
}

void TimersTest_Repeating() {
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
  EXPECT_EQ(calls, 1U);

  now += 15ms;
  t->tick();
  EXPECT_EQ(calls, 2U);

  now += 15ms;
  t->tick();
  EXPECT_EQ(calls, 3U);

  // Cancel by setting tombstone.
  ev->canceled.kill();
  now += 15ms;
  t->tick();
  EXPECT_EQ(calls, 3U); // cancelled
}

void TimersTest_Cancel() {
  auto now = make_time(100);
  auto t = timers::make("test");
  t->set_clock_callback([&]() { return now; });

  bool called = false;
  auto ev = t->set(20ms, [&](const timer_invocation&) -> time_point_t {
    called = true;
    return {};
  });

  // Cancel before it fires.
  EXPECT_FALSE(ev->canceled);
  ev->canceled.kill();
  EXPECT_TRUE(ev->canceled);

  now += 25ms;
  t->tick();
  EXPECT_FALSE(called);
}

void TimersTest_Reschedule() {
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
  EXPECT_EQ(calls, 1U);
  EXPECT_FALSE(ev->canceled);

  now += 10ms;
  t->tick();
  EXPECT_EQ(calls, 1U); // not yet

  now += 10ms;
  t->tick();
  EXPECT_EQ(calls, 2U);
  EXPECT_TRUE(ev->canceled);
}

void TimersTest_General() {
  auto now = make_date(2024y / 1 / 1);
  auto t = timers::make("test");
  t->set_clock_callback([&now]() { return now; });
  std::vector<timer_event_ptr> fired_events;

  // Schedule two one-shot events at different times.
  auto ev1 = t->set(30s, [&](const timer_invocation& i) -> time_point_t {
    fired_events.push_back(i.event);
    return {};
  });
  EXPECT_EQ(ev1->start_at, now + 30s);

  auto ev2 = t->set(60s, [&](const timer_invocation& i) -> time_point_t {
    fired_events.push_back(i.event);
    return {};
  });
  EXPECT_EQ(ev2->start_at, now + 60s);

  size_t count = 0;
  count = t->tick();
  EXPECT_EQ(count, 0U);

  now += 29s;
  count = t->tick();
  EXPECT_EQ(count, 0U);

  now += 1s;
  count = t->tick();
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(fired_events.size(), 1U);
  EXPECT_EQ(fired_events[0], ev1);
  EXPECT_TRUE(ev1->canceled);
  EXPECT_FALSE(ev2->canceled);

  now += 30s;
  count = t->tick();
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(fired_events.size(), 2U);
  EXPECT_EQ(fired_events[1], ev2);
  EXPECT_TRUE(ev2->canceled);
}

void TimersTest_Edge() {
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
  EXPECT_EQ(count, 0U);

  now += 1s;
  count = t->tick();
  EXPECT_EQ(count, 1U); // Fires at 30s.
  EXPECT_EQ(calls, 1U);

  now += 59s;
  count = t->tick();
  EXPECT_EQ(count, 0U);

  now += 1s;
  count = t->tick();
  EXPECT_EQ(count, 1U); // Fires at 90s (30s + 60s).
  EXPECT_EQ(calls, 2U);

  // Cancel to clean up.
  ev->canceled.kill();
}

MAKE_TEST_LIST(TimersTest_OneShot, TimersTest_Repeating, TimersTest_Cancel,
    TimersTest_Reschedule, TimersTest_General, TimersTest_Edge);

// NOLINTEND(readability-function-cognitive-complexity)
