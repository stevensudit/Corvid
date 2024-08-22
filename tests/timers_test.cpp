// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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

std::ostream&
operator<<(std::ostream& os, const corvid::timers_ns::timer_id_t& id) {
  os << (uint64_t)id;
  return os;
}

#include "AccutestShim.h"

using namespace std::literals;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace corvid;

auto make_date(auto date) {
  return steady_clock::time_point{} + sys_days{date}.time_since_epoch();
}

void TimersTest_General() {
  timers t;
  auto now = make_date(2024y / 1 / 1);
  std::vector<timer_id_t> ids;
  auto now_cb = [&now]() { return now; };
  t.set_clock_callback(now_cb);
  auto cb = [&ids](timer_event& event) { ids.push_back(event.timer_id); };

  EXPECT_EQ(t.events().size(), 0u);

  auto& t1 = t.set(30s, cb);
  EXPECT_EQ(t.events().size(), 1u);
  auto id1 = t1.timer_id;
  EXPECT_EQ(id1, timer_id_t{1});
  auto& t1b = t.event(id1);
  EXPECT_EQ(t1b.timer_id, id1);
  t1b.name = "t1";
  auto t1_start = t1b.start;
  EXPECT_EQ(t1_start, now + 30s);

  auto& t2 = t.set(60s, cb);
  EXPECT_EQ(t.events().size(), 2u);
  auto id2 = t2.timer_id;
  EXPECT_EQ(id2, timer_id_t{2});
  auto& t2b = t.event(id2);
  EXPECT_EQ(t2b.timer_id, id2);
  t2b.name = "t2";
  auto t2_start = t2b.start;
  EXPECT_EQ(t2_start, now + 60s);

  size_t count = 0;
  count = t.tick();
  EXPECT_EQ(count, 0u);
  now += 29s;
  count = t.tick();
  EXPECT_EQ(count, 0u);
  now += 1s;
  EXPECT_EQ(t.events().size(), 2u);
  count = t.tick();
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(ids.size(), 1u);
  EXPECT_EQ(ids[0], id1);
  EXPECT_EQ(t.events().size(), 1u);
  now += 30s;
  count = t.tick();
  EXPECT_EQ(count, 1u);
  EXPECT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[1], id2);
  EXPECT_EQ(t.events().size(), 0u);
}

void TimersTest_Edge() {
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
  EXPECT_EQ(count, 0u);
  now += 1s;
  count = t.tick();
  EXPECT_EQ(count, 1u); // Fires at 30s.
  EXPECT_EQ(ids.size(), 1u);
  now += 59s;
  count = t.tick();
  EXPECT_EQ(count, 0u);
  now += 1s;
  count = t.tick();
  EXPECT_EQ(count, 1u); // Fires at 90s.
  EXPECT_EQ(ids.size(), 2u);
}

MAKE_TEST_LIST(TimersTest_General, TimersTest_Edge);

// TODO: Expose next_timer_id_ for testing, and prove that the scheme works.
