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

#include <chrono>
#include <functional>
#include <thread>

#include "../corvid/concurrency.h"
#include "minitest.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

void TombStone_Basic() {
  tombstone t;
  EXPECT_FALSE(t.dead());
  EXPECT_FALSE(t.get());
  EXPECT_FALSE(*t);
  if (t) {
    EXPECT_FALSE(true);
  } else {
    EXPECT_FALSE(false);
  }
  if (!t) {
    EXPECT_TRUE(true);
  } else {
    EXPECT_FALSE(false);
  }
  t.set(false);
  EXPECT_FALSE(t.dead());
  t.set(true);
  EXPECT_TRUE(t.dead());
  EXPECT_TRUE(t.get());
  EXPECT_TRUE(*t);
  t.set(false);
  EXPECT_TRUE(t.dead());
  EXPECT_TRUE(t.get());
  EXPECT_TRUE(*t);
}

void TombStone_TrySet() {
  tombstone t;
  // Returns false when value is already the target.
  EXPECT_FALSE(t.try_set(false));
  EXPECT_FALSE(t.dead());
  // Returns true when value changes.
  EXPECT_TRUE(t.try_set(true));
  EXPECT_TRUE(t.dead());
  // Returns false when dead (even for a different value).
  EXPECT_FALSE(t.try_set(false));
  EXPECT_TRUE(t.dead());
}

void TombStone_Kill() {
  tombstone t;
  // First kill succeeds.
  EXPECT_TRUE(t.kill());
  EXPECT_TRUE(t.dead());
  // Second kill reports already dead.
  EXPECT_FALSE(t.kill());
  EXPECT_TRUE(t.dead());
}

void Notifiable_NotifyAndWait() {
  // `notify` + `wait_until`: waiter unblocks when flag becomes true.
  if (true) {
    notifiable<bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_until([](bool b) { return b; });
    EXPECT_TRUE(v);
    t.join();
  }
  // `std::identity` shorthand for bool flag.
  if (true) {
    notifiable<bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_until(std::identity{});
    EXPECT_TRUE(v);
    t.join();
  }
}

void Notifiable_ModifyAndNotify() {
  // `modify_and_notify`: waiter unblocks once value exceeds threshold.
  notifiable<int> counter{0};
  std::thread t{[&] {
    for (int i = 0; i < 5; ++i) counter.modify_and_notify([](int& v) { ++v; });
  }};
  auto v = counter.wait_until([](int n) { return n >= 5; });
  EXPECT_EQ(v, 5);
  t.join();
}

void Notifiable_WaitFor() {
  // `wait_for` satisfied before deadline: returns the matching value.
  if (true) {
    notifiable<bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_for(std::chrono::seconds{5}, std::identity{});
    EXPECT_TRUE(v);
    EXPECT_TRUE(*v);
    t.join();
  }
  // `wait_for` timeout: predicate never met, returns nullopt.
  if (true) {
    notifiable<bool> flag{false};
    auto v = flag.wait_for(std::chrono::milliseconds{1}, std::identity{});
    EXPECT_FALSE(v);
  }
}

void Notifiable_WaitUntilChanged() {
  // `wait_until_changed` unblocks when the value changes; returns new value.
  if (true) {
    notifiable<int> n{0};
    std::thread t{[&] { n.notify(42); }};
    EXPECT_EQ(n.wait_until_changed(), 42);
    t.join();
  }
  // `wait_for_changed` unblocks before deadline: returns new value.
  if (true) {
    notifiable<int> n{0};
    std::thread t{[&] { n.notify(42); }};
    auto v = n.wait_for_changed(std::chrono::seconds{5});
    EXPECT_TRUE(v);
    EXPECT_EQ(*v, 42);
    t.join();
  }
  // `wait_for_changed` timeout: value never changes, returns nullopt.
  if (true) {
    notifiable<int> n{0};
    auto v = n.wait_for_changed(std::chrono::milliseconds{1});
    EXPECT_FALSE(v);
  }
}

void Notifiable_Get() {
  // `get` returns snapshot without blocking.
  notifiable<int> n{42};
  EXPECT_EQ(n.get(), 42);
  n.notify(99);
  EXPECT_EQ(n.get(), 99);
}

void Notifiable_Atomic() {
  // `get` on `std::atomic<bool>`: lock-free relaxed load.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    EXPECT_FALSE(flag.get());
    flag.notify(true);
    EXPECT_TRUE(flag.get());
    EXPECT_TRUE(flag.get(std::memory_order::acquire));
  }
  // `load_value` static helper: relaxed read without implicit `seq_cst` cast.
  if (true) {
    std::atomic_bool a{true};
    EXPECT_TRUE(notifiable<std::atomic_bool>::load_value(a));
  }
  // `notify` + `wait_until`: waiter unblocks when atomic flag becomes true.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_until([](const std::atomic_bool& b) {
      return b.load();
    });
    EXPECT_TRUE(v);
    t.join();
  }
  // `wait_until_value`: uses `load_value` internally (no implicit `seq_cst`).
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    flag.wait_until_value(true);
    EXPECT_TRUE(flag.get());
    t.join();
  }
  // `wait_until_changed` on atomic int: returns new value.
  if (true) {
    notifiable<std::atomic<int>> n{0};
    std::thread t{[&] { n.notify(42); }};
    EXPECT_EQ(n.wait_until_changed(), 42);
    t.join();
  }
  // `modify_and_notify` on atomic int: waiter unblocks once threshold reached.
  if (true) {
    notifiable<std::atomic<int>> counter{0};
    std::thread t{[&] {
      for (int i = 0; i < 5; ++i)
        counter.modify_and_notify([](std::atomic<int>& v) { ++v; });
    }};
    auto v = counter.wait_until([](const std::atomic<int>& n) {
      return n.load() >= 5;
    });
    EXPECT_EQ(v, 5);
    t.join();
  }
  // `wait_for` satisfied before deadline.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_for(std::chrono::seconds{5},
        [](const std::atomic_bool& b) { return b.load(); });
    EXPECT_TRUE(v);
    EXPECT_TRUE(*v);
    t.join();
  }
  // `wait_for_value` satisfied before deadline.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    EXPECT_TRUE(flag.wait_for_value(std::chrono::seconds{5}, true));
    t.join();
  }
  // `wait_for_value` timeout: predicate never met.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    EXPECT_FALSE(flag.wait_for_value(std::chrono::milliseconds{1}, true));
  }
  // `wait_for_changed` satisfied before deadline.
  if (true) {
    notifiable<std::atomic<int>> n{0};
    std::thread t{[&] { n.notify(99); }};
    auto v = n.wait_for_changed(std::chrono::seconds{5});
    EXPECT_TRUE(v);
    EXPECT_EQ(*v, 99);
    t.join();
  }
  // `wait_for_changed` timeout: value never changes.
  if (true) {
    notifiable<std::atomic<int>> n{0};
    EXPECT_FALSE(n.wait_for_changed(std::chrono::milliseconds{1}));
  }
}

MAKE_TEST_LIST(TombStone_Basic, TombStone_TrySet, TombStone_Kill,
    Notifiable_NotifyAndWait, Notifiable_ModifyAndNotify, Notifiable_WaitFor,
    Notifiable_WaitUntilChanged, Notifiable_Get, Notifiable_Atomic);

// NOLINTEND(readability-function-cognitive-complexity)
