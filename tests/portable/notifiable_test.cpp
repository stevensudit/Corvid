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

#include <thread>

#include "corvid/concurrency/notifiable.h"
#include "corvid/infra/relaxed_atomic.h"
#include "catch2_main.h"

using namespace corvid;
using namespace std::chrono_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(performance-unnecessary-copy-initialization)

#pragma region NotifyAndWait

TEST_CASE("NotifyAndWait", "[Notifiable]") {
  // `notify` + `wait_until`: waiter unblocks when flag becomes true.
  if (true) {
    notifiable<bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_until([](bool b) { return b; });
    CHECK(v);
    t.join();
  }
  // `std::identity` shorthand for bool flag.
  if (true) {
    notifiable<bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_until(std::identity{});
    CHECK(v);
    t.join();
  }
}

#pragma endregion
#pragma region ModifyAndNotify

TEST_CASE("ModifyAndNotify", "[Notifiable]") {
  // `modify_and_notify`: waiter unblocks once value exceeds threshold.
  notifiable<int> counter{0};
  std::thread t{[&] {
    for (int i = 0; i < 5; ++i) counter.modify_and_notify([](int& v) { ++v; });
  }};
  auto v = counter.wait_until([](int n) { return n >= 5; });
  CHECK(v == 5);
  t.join();
}

#pragma endregion
#pragma region WaitFor

TEST_CASE("WaitFor", "[Notifiable]") {
  // `wait_for` satisfied before deadline: returns the matching value.
  if (true) {
    notifiable<bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_for(5s, std::identity{});
    CHECK(v);
    CHECK(v.value());
    t.join();
  }
  // `wait_for` timeout: predicate never met, returns nullopt.
  if (true) {
    notifiable<bool> flag{false};
    auto v = flag.wait_for(1ms, std::identity{});
    CHECK_FALSE(v);
  }
}

#pragma endregion
#pragma region WaitUntilChanged

TEST_CASE("WaitUntilChanged", "[Notifiable]") {
  // `wait_until_changed` unblocks when the value changes; returns new value.
  // Capture `old` before spawning the thread so the wait succeeds even if
  // the thread runs before `wait_until_changed` is entered.
  if (true) {
    notifiable<int> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(42); }};
    CHECK(n.wait_until_changed(old) == 42);
    t.join();
  }
  // `wait_for_changed` unblocks before deadline: returns new value.
  if (true) {
    notifiable<int> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(42); }};
    auto v = n.wait_for_changed(5s, old);
    CHECK(v);
    CHECK(v.value() == 42);
    t.join();
  }
  // `wait_for_changed` timeout: value never changes, returns nullopt.
  if (true) {
    notifiable<int> n{0};
    auto v = n.wait_for_changed(1ms);
    CHECK_FALSE(v);
  }
}

#pragma endregion
#pragma region Get

TEST_CASE("Get", "[Notifiable]") {
  // `get` returns snapshot without blocking.
  notifiable<int> n{42};
  CHECK(n.get() == 42);
  n.notify(99);
  CHECK(n.get() == 99);
}

#pragma endregion
#pragma region Atomic

TEST_CASE("Atomic", "[Notifiable]") {
  // `get` on `std::atomic<bool>`: lock-free relaxed load.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    CHECK_FALSE(flag.get());
    flag.notify(true);
    CHECK(flag.get());
    CHECK(flag.get(std::memory_order::acquire));
  }
  // `load_value` static helper: relaxed read without implicit `seq_cst` cast.
  if (true) {
    std::atomic_bool a{true};
    CHECK(notifiable<std::atomic_bool>::load_value(a));
  }
  // `notify` + `wait_until`: waiter unblocks when atomic flag becomes true.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_until([](const std::atomic_bool& b) {
      return b.load();
    });
    CHECK(v);
    t.join();
  }
  // `wait_until_value`: uses `load_value` internally (no implicit `seq_cst`).
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    flag.wait_until_value(true);
    CHECK(flag.get());
    t.join();
  }
  // `wait_until_changed` on atomic int: returns new value.
  if (true) {
    notifiable<std::atomic<int>> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(42); }};
    CHECK(n.wait_until_changed(old) == 42);
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
    CHECK(v == 5);
    t.join();
  }
  // `wait_for` satisfied before deadline.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_for(5s, [](const std::atomic_bool& b) {
      return b.load();
    });
    CHECK(v);
    CHECK(v.value());
    t.join();
  }
  // `wait_for_value` satisfied before deadline.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    CHECK(flag.wait_for_value(5s, true));
    t.join();
  }
  // `wait_for_value` timeout: predicate never met.
  if (true) {
    notifiable<std::atomic_bool> flag{false};
    CHECK_FALSE(flag.wait_for_value(1ms, true));
  }
  // `wait_for_changed` satisfied before deadline.
  if (true) {
    notifiable<std::atomic<int>> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(99); }};
    auto v = n.wait_for_changed(5s, old);
    CHECK(v);
    CHECK(v.value() == 99);
    t.join();
  }
  // `wait_for_changed` timeout: value never changes.
  if (true) {
    notifiable<std::atomic<int>> n{0};
    CHECK_FALSE(n.wait_for_changed(1ms));
  }
}

#pragma endregion
#pragma region RelaxedAtomic_Basic

TEST_CASE("Basic", "[RelaxedAtomic]") {
  // Default-constructed value is zero-initialized.
  relaxed_atomic<int> a;
  CHECK(static_cast<int>(a) == 0);

  // Value constructor.
  relaxed_atomic<int> b{42};
  CHECK(static_cast<int>(b) == 42);

  // Implicit conversion reads with relaxed semantics.
  int v = b;
  CHECK(v == 42);

  // Assignment stores with relaxed semantics and returns the stored value.
  int r = (b = 99);
  CHECK(r == 99);
  CHECK(static_cast<int>(b) == 99);
}

#pragma endregion
#pragma region RelaxedAtomic_Arrow

TEST_CASE("Arrow", "[RelaxedAtomic]") {
  // `operator->` exposes the underlying `std::atomic<T>` methods.
  relaxed_atomic<int> a{10};

  // Explicit load with acquire ordering.
  CHECK(a->load(std::memory_order::acquire) == 10);

  // Explicit store with release ordering.
  a->store(20, std::memory_order::release);
  CHECK(static_cast<int>(a) == 20);

  // exchange.
  int old = a.exchange(30);
  CHECK(old == 20);
  CHECK(static_cast<int>(a) == 30);

  // compare_exchange_strong: succeeds when expected matches.
  int expected = 30;
  CHECK(a->compare_exchange_strong(expected, 40));
  CHECK(static_cast<int>(a) == 40);

  // compare_exchange_strong: fails and updates expected when it does not
  // match.
  expected = 0;
  CHECK_FALSE(a->compare_exchange_strong(expected, 99));
  CHECK(expected == 40);
}

#pragma endregion
#pragma region RelaxedAtomic_Bool

TEST_CASE("Bool", "[RelaxedAtomic]") {
  // Works for `bool` values.
  relaxed_atomic<bool> flag{false};
  CHECK_FALSE(static_cast<bool>(flag));
  flag = true;
  CHECK(static_cast<bool>(flag));
}

#pragma endregion
#pragma region RelaxedAtomic

TEST_CASE("RelaxedAtomic", "[Notifiable]") {
  // `get` on `relaxed_atomic<bool>`: lock-free relaxed load.
  if (true) {
    notifiable<relaxed_atomic_bool> flag{false};
    CHECK_FALSE(flag.get());
    flag.notify(true);
    CHECK(flag.get());
    CHECK(flag.get(std::memory_order::acquire));
  }
  // `notify` + `wait_until`: waiter unblocks when flag becomes true.
  if (true) {
    notifiable<relaxed_atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_until([](const relaxed_atomic_bool& b) {
      return b->load();
    });
    CHECK(v);
    t.join();
  }
  // `wait_until_value` on `relaxed_atomic<bool>`.
  if (true) {
    notifiable<relaxed_atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    flag.wait_until_value(true);
    CHECK(flag.get());
    t.join();
  }
  // `wait_until_changed` on `relaxed_atomic<int>`: returns new value.
  if (true) {
    notifiable<relaxed_atomic<int>> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(42); }};
    CHECK(n.wait_until_changed(old) == 42);
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
    CHECK(v == 5);
    t.join();
  }
  // `wait_for` satisfied before deadline.
  if (true) {
    notifiable<relaxed_atomic_bool> flag{false};
    std::thread t{[&] { flag.notify(true); }};
    auto v = flag.wait_for(5s, [](const relaxed_atomic_bool& b) {
      return b->load();
    });
    CHECK(v);
    CHECK(v.value());
    t.join();
  }
  // `wait_for_changed` satisfied before deadline.
  if (true) {
    notifiable<relaxed_atomic<int>> n{0};
    auto old = n.get();
    std::thread t{[&] { n.notify(99); }};
    auto v = n.wait_for_changed(5s, old);
    CHECK(v);
    CHECK(v.value() == 99);
    t.join();
  }
  // `wait_for_changed` timeout: value never changes.
  if (true) {
    notifiable<relaxed_atomic<int>> n{0};
    CHECK_FALSE(n.wait_for_changed(1ms));
  }
}
#pragma endregion

// NOLINTEND(performance-unnecessary-copy-initialization)
// NOLINTEND(readability-function-cognitive-complexity)
