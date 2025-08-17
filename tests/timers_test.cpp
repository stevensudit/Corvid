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
#if 0
  timers t;
  time_point_t now{};
  t.set_clock_callback([&]() { return now; });
  size_t calls{};
  auto& ev = t.set(10ms, [&](timer_event&) { ++calls; }, 15ms);
  now += 10ms;
  t.tick();
  EXPECT_EQ(calls, 1U);

  now += 15ms;
  t.tick();
  EXPECT_EQ(calls, 2U);

  now += 15ms;
  t.tick();
  EXPECT_EQ(calls, 3U);

  t.cancel(ev.timer_id);
  now += 15ms;
  t.tick();
  EXPECT_EQ(calls, 3U); // cancelled
#endif
}

void TimersTest_Cancel() {
#if 0
  timers t;
  time_point_t now{};
  t.set_clock_callback([&]() { return now; });

  bool called = false;
  auto id = t.set(20ms, [&](timer_event&) { called = true; }).timer_id;
  EXPECT_TRUE(t.cancel(id));

  now += 25ms;
  t.tick();
  EXPECT_FALSE(called);
#endif
}

void TimersTest_Reschedule() {
#if 0
  timers t;
  time_point_t now{};
  t.set_clock_callback([&]() { return now; });

  size_t calls{};
  t.set(10ms, [&](timer_event& e) {
    ++calls;
    if (calls == 1) {
      e.next_at = t.get_now() + 20ms; // reschedule
    }
  });

  now += 10ms;
  t.tick();
  EXPECT_EQ(calls, 1U);
  EXPECT_EQ(t.events().size(), 1U);

  now += 10ms;
  t.tick();
  EXPECT_EQ(calls, 1U); // not yet

  now += 10ms;
  t.tick();
  EXPECT_EQ(calls, 2U);
  EXPECT_TRUE(t.events().empty());
#endif
}

void TimersTest_General() {
#if 0
  timers t;
  auto now = make_date(2024y / 1 / 1);
  std::vector<timer_id_t> ids;
  auto now_cb = [&now]() { return now; };
  t.set_clock_callback(now_cb);
  auto cb = [&ids](timer_event& event) { ids.push_back(event.timer_id); };

  EXPECT_EQ(t.events().size(), 0U);

  auto& t1 = t.set(30s, cb);
  EXPECT_EQ(t.events().size(), 1U);
  auto id1 = t1.timer_id;
  EXPECT_EQ(id1, timer_id_t{1});
  auto& t1b = t.event(id1);
  EXPECT_EQ(t1b.timer_id, id1);
  t1b.name = "t1";
  auto t1_start = t1b.start_at;
  EXPECT_EQ(t1_start, now + 30s);

  auto& t2 = t.set(60s, cb);
  EXPECT_EQ(t.events().size(), 2U);
  auto id2 = t2.timer_id;
  EXPECT_EQ(id2, timer_id_t{2});
  auto& t2b = t.event(id2);
  EXPECT_EQ(t2b.timer_id, id2);
  t2b.name = "t2";
  auto t2_start = t2b.start_at;
  EXPECT_EQ(t2_start, now + 60s);

  size_t count = 0;
  count = t.tick();
  EXPECT_EQ(count, 0U);
  now += 29s;
  count = t.tick();
  EXPECT_EQ(count, 0U);
  now += 1s;
  EXPECT_EQ(t.events().size(), 2U);
  count = t.tick();
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(ids.size(), 1U);
  EXPECT_EQ(ids[0], id1);
  EXPECT_EQ(t.events().size(), 1U);
  now += 30s;
  count = t.tick();
  EXPECT_EQ(count, 1U);
  EXPECT_EQ(ids.size(), 2U);
  EXPECT_EQ(ids[1], id2);
  EXPECT_EQ(t.events().size(), 0U);
#endif
}

void TimersTest_Edge() {
#if 0
  timers t;
  auto now = make_date(2024y / 1 / 1);
  std::vector<timer_id_t> ids;
  auto now_cb = [&now]() { return now; };
  t.set_clock_callback(now_cb);
  auto cb = [&ids](timer_event& event) { ids.push_back(event.timer_id); };

  size_t count = 0;

  // Repeating.
  [[maybe_unused]] auto& t1 = t.set(30s, cb, 60s);
  now += 29s;
  count = t.tick();
  EXPECT_EQ(count, 0U);
  now += 1s;
  count = t.tick();
  EXPECT_EQ(count, 1U); // Fires at 30s.
  EXPECT_EQ(ids.size(), 1U);
  now += 59s;
  count = t.tick();
  EXPECT_EQ(count, 0U);
  now += 1s;
  count = t.tick();
  EXPECT_EQ(count, 1U); // Fires at 90s.
  EXPECT_EQ(ids.size(), 2U);
#endif
}

MAKE_TEST_LIST(TimersTest_OneShot, TimersTest_Repeating, TimersTest_Cancel,
    TimersTest_Reschedule, TimersTest_General, TimersTest_Edge);

// NOLINTEND(readability-function-cognitive-complexity)
