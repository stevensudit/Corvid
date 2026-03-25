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
using namespace std::chrono_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(performance-unnecessary-copy-initialization)

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
    auto v = flag.wait_for(5s, std::identity{});
    EXPECT_TRUE(v);
    EXPECT_TRUE(*v);
    t.join();
  }
  // `wait_for` timeout: predicate never met, returns nullopt.
  if (true) {
    notifiable<bool> flag{false};
    auto v = flag.wait_for(1ms, std::identity{});
    EXPECT_FALSE(v);
  }
}

void Notifiable_WaitUntilChanged() {
  // `wait_until_changed` unblocks when the value changes; returns new value.
  // Capture `old` before spawning the thread so the wait succeeds even if
  // the thread runs before `wait_until_changed` is entered.
  if (true) {
    notifiable<int> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(42); }};
    EXPECT_EQ(n.wait_until_changed(old), 42);
    t.join();
  }
  // `wait_for_changed` unblocks before deadline: returns new value.
  if (true) {
    notifiable<int> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(42); }};
    auto v = n.wait_for_changed(5s, old);
    EXPECT_TRUE(v);
    EXPECT_EQ(*v, 42);
    t.join();
  }
  // `wait_for_changed` timeout: value never changes, returns nullopt.
  if (true) {
    notifiable<int> n{0};
    auto v = n.wait_for_changed(1ms);
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
    auto old = n.get();
    std::thread t{[&] { n.notify(42); }};
    EXPECT_EQ(n.wait_until_changed(old), 42);
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
    auto v = flag.wait_for(5s, [](const std::atomic_bool& b) {
      return b.load();
    });
    EXPECT_TRUE(v);
    EXPECT_TRUE(*v);
    t.join();
  }
  // `wait_for_value` satisfied before deadline.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    EXPECT_TRUE(flag.wait_for_value(5s, true));
    t.join();
  }
  // `wait_for_value` timeout: predicate never met.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    EXPECT_FALSE(flag.wait_for_value(1ms, true));
  }
  // `wait_for_changed` satisfied before deadline.
  if (true) {
    notifiable<std::atomic<int>> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(99); }};
    auto v = n.wait_for_changed(5s, old);
    EXPECT_TRUE(v);
    EXPECT_EQ(*v, 99);
    t.join();
  }
  // `wait_for_changed` timeout: value never changes.
  if (true) {
    notifiable<std::atomic<int>> n{0};
    EXPECT_FALSE(n.wait_for_changed(1ms));
  }
}

void RelaxedAtomic_Basic() {
  // Default-constructed value is zero-initialized.
  relaxed_atomic<int> a;
  EXPECT_EQ(static_cast<int>(a), 0);

  // Value constructor.
  relaxed_atomic<int> b{42};
  EXPECT_EQ(static_cast<int>(b), 42);

  // Implicit conversion reads with relaxed semantics.
  int v = b;
  EXPECT_EQ(v, 42);

  // Assignment stores with relaxed semantics and returns the stored value.
  int r = (b = 99);
  EXPECT_EQ(r, 99);
  EXPECT_EQ(static_cast<int>(b), 99);
}

void RelaxedAtomic_Arrow() {
  // `operator->` exposes the underlying `std::atomic<T>` methods.
  relaxed_atomic<int> a{10};

  // Explicit load with acquire ordering.
  EXPECT_EQ(a->load(std::memory_order::acquire), 10);

  // Explicit store with release ordering.
  a->store(20, std::memory_order::release);
  EXPECT_EQ(static_cast<int>(a), 20);

  // exchange.
  int old = a->exchange(30);
  EXPECT_EQ(old, 20);
  EXPECT_EQ(static_cast<int>(a), 30);

  // compare_exchange_strong: succeeds when expected matches.
  int expected = 30;
  EXPECT_TRUE(a->compare_exchange_strong(expected, 40));
  EXPECT_EQ(static_cast<int>(a), 40);

  // compare_exchange_strong: fails and updates expected when it does not
  // match.
  expected = 0;
  EXPECT_FALSE(a->compare_exchange_strong(expected, 99));
  EXPECT_EQ(expected, 40);
}

void RelaxedAtomic_Bool() {
  // Works for `bool` values.
  relaxed_atomic<bool> flag{false};
  EXPECT_FALSE(static_cast<bool>(flag));
  flag = true;
  EXPECT_TRUE(static_cast<bool>(flag));
}

void Notifiable_RelaxedAtomic() {
  // `get` on `relaxed_atomic<bool>`: lock-free relaxed load.
  if (true) {
    notifiable<relaxed_atomic_bool> flag{false};
    EXPECT_FALSE(flag.get());
    flag.notify(true);
    EXPECT_TRUE(flag.get());
    EXPECT_TRUE(flag.get(std::memory_order::acquire));
  }
  // `notify` + `wait_until`: waiter unblocks when flag becomes true.
  if (true) {
    notifiable<relaxed_atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_until([](const relaxed_atomic_bool& b) {
      return b->load();
    });
    EXPECT_TRUE(v);
    t.join();
  }
  // `wait_until_value` on `relaxed_atomic<bool>`.
  if (true) {
    notifiable<relaxed_atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    flag.wait_until_value(true);
    EXPECT_TRUE(flag.get());
    t.join();
  }
  // `wait_until_changed` on `relaxed_atomic<int>`: returns new value.
  if (true) {
    notifiable<relaxed_atomic<int>> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(42); }};
    EXPECT_EQ(n.wait_until_changed(old), 42);
    t.join();
  }
  // `modify_and_notify` on `relaxed_atomic<int>`.
  if (true) {
    notifiable<relaxed_atomic<int>> counter{0};
    std::thread t{[&] {
      for (int i = 0; i < 5; ++i)
        counter.modify_and_notify([](relaxed_atomic<int>& v) {
          ++v.underlying();
        });
    }};
    auto v = counter.wait_until([](const relaxed_atomic<int>& n) {
      return n->load() >= 5;
    });
    EXPECT_EQ(v, 5);
    t.join();
  }
  // `wait_for` satisfied before deadline.
  if (true) {
    notifiable<relaxed_atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_for(5s, [](const relaxed_atomic_bool& b) {
      return b->load();
    });
    EXPECT_TRUE(v);
    EXPECT_TRUE(*v);
    t.join();
  }
  // `wait_for_changed` satisfied before deadline.
  if (true) {
    notifiable<relaxed_atomic<int>> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(99); }};
    auto v = n.wait_for_changed(5s, old);
    EXPECT_TRUE(v);
    EXPECT_EQ(*v, 99);
    t.join();
  }
  // `wait_for_changed` timeout: value never changes.
  if (true) {
    notifiable<relaxed_atomic<int>> n{0};
    EXPECT_FALSE(n.wait_for_changed(1ms));
  }
}

// Minimal resource type for timer_fuse tests. Holds the sequencer that
// `timer_fuse` uses for liveness checks.
struct FakeResource {
  std::atomic_uint64_t seq{0};
  int value{42};
};

void TimerFuse_Default() {
  // Default-constructed fuse is permanently unarmed.
  timer_fuse<FakeResource> fuse;
  EXPECT_EQ(fuse.get_if_armed(), nullptr);

  // Copyability: a copy is also unarmed.
  auto copy = fuse;
  EXPECT_EQ(copy.get_if_armed(), nullptr);
}

void TimerFuse_ArmedFires() {
  // A fuse whose payload fires while the sequencer is unchanged returns the
  // live resource from `get_if_armed`.
  auto resource = std::make_shared<FakeResource>();
  auto t0 = std::chrono::steady_clock::now();
  timing_wheel wheel{4, 1ms, t0};

  bool fired{false};
  bool saw_resource{false};
  auto ok = timer_fuse<FakeResource>::set_timeout(wheel, resource->seq,
      resource, 1ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        fired = true;
        saw_resource = (f.get_if_armed() != nullptr);
        return true;
      });
  EXPECT_TRUE(ok);
  wheel.tick(t0 + 4ms);
  EXPECT_TRUE(fired);
  EXPECT_TRUE(saw_resource);
}

void TimerFuse_Disarm() {
  // Calling `disarm` before the wheel ticks causes the payload to see a null
  // resource from `get_if_armed`.
  auto resource = std::make_shared<FakeResource>();
  auto t0 = std::chrono::steady_clock::now();
  timing_wheel wheel{4, 1ms, t0};

  bool fired{false};
  bool saw_null{false};
  (void)timer_fuse<FakeResource>::set_timeout(wheel, resource->seq, resource,
      1ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        fired = true;
        saw_null = (f.get_if_armed() == nullptr);
        return true;
      });
  timer_fuse<FakeResource>::disarm(resource->seq);
  wheel.tick(t0 + 4ms);
  EXPECT_TRUE(fired);
  EXPECT_TRUE(saw_null);
}

void TimerFuse_Rearm() {
  // Re-arming increments the sequencer so the earlier fuse fizzles; only the
  // most-recently-armed fuse sees a live resource.
  auto resource = std::make_shared<FakeResource>();
  auto t0 = std::chrono::steady_clock::now();
  timing_wheel wheel{6, 1ms, t0};

  bool first_fired{false};
  bool first_saw_resource{false};
  bool second_fired{false};
  bool second_saw_resource{false};

  (void)timer_fuse<FakeResource>::set_timeout(wheel, resource->seq, resource,
      1ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        first_fired = true;
        first_saw_resource = (f.get_if_armed() != nullptr);
        return true;
      });

  // Re-arm: the earlier fuse's target no longer matches the sequencer.
  (void)timer_fuse<FakeResource>::set_timeout(wheel, resource->seq, resource,
      2ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        second_fired = true;
        second_saw_resource = (f.get_if_armed() != nullptr);
        return true;
      });

  wheel.tick(t0 + 6ms);
  EXPECT_TRUE(first_fired);
  EXPECT_FALSE(first_saw_resource); // fizzled
  EXPECT_TRUE(second_fired);
  EXPECT_TRUE(second_saw_resource); // still armed
}

void TimerFuse_ResourceExpired() {
  // If the `shared_ptr` owning the resource is released before the payload
  // fires, `get_if_armed` returns nullptr (the `weak_ptr` is expired).
  auto resource = std::make_shared<FakeResource>();
  auto t0 = std::chrono::steady_clock::now();
  timing_wheel wheel{4, 1ms, t0};

  bool fired{false};
  bool saw_null{false};
  (void)timer_fuse<FakeResource>::set_timeout(wheel, resource->seq, resource,
      1ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        fired = true;
        saw_null = (f.get_if_armed() == nullptr);
        return true;
      });
  resource.reset();
  wheel.tick(t0 + 4ms);
  EXPECT_TRUE(fired);
  EXPECT_TRUE(saw_null);
}

void TimerFuse_ExceedMaxDelay() {
  // `set_timeout` returns false when the delay exceeds the wheel's range.
  // With slot_count=2 and tick_interval=1ms, max delay = 1ms.
  auto resource = std::make_shared<FakeResource>();
  timing_wheel wheel{2, 1ms};

  auto ok = timer_fuse<FakeResource>::set_timeout(wheel, resource->seq,
      resource, 2ms,
      [](const timer_fuse<FakeResource>&) -> bool { return true; });
  EXPECT_FALSE(ok);
}

MAKE_TEST_LIST(TombStone_Basic, TombStone_TrySet, TombStone_Kill,
    Notifiable_NotifyAndWait, Notifiable_ModifyAndNotify, Notifiable_WaitFor,
    Notifiable_WaitUntilChanged, Notifiable_Get, Notifiable_Atomic,
    RelaxedAtomic_Basic, RelaxedAtomic_Arrow, RelaxedAtomic_Bool,
    Notifiable_RelaxedAtomic, TimerFuse_Default, TimerFuse_ArmedFires,
    TimerFuse_Disarm, TimerFuse_Rearm, TimerFuse_ResourceExpired,
    TimerFuse_ExceedMaxDelay);

// NOLINTEND(performance-unnecessary-copy-initialization)
// NOLINTEND(readability-function-cognitive-complexity)
