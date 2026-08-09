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

#include "corvid/infra/relaxed_atomic.h"

#include "catch2_main.h"

#include <atomic>
#include <type_traits>

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region relaxed_atomic

TEST_CASE("relaxed_atomic converts and assigns", "[infra][relaxed_atomic]") {
  relaxed_atomic_int a{5};
  int copied = a;
  CHECK(copied == 5);
  CHECK(*a == 5);

  a = 7;
  CHECK(*a == 7);

  // Assignment returns the assigned value, matching `std::atomic`.
  CHECK((a = 9) == 9);

  // Default construction value-initializes.
  relaxed_atomic_int zeroed;
  CHECK(*zeroed == 0);

  relaxed_atomic_bool flag{false};
  CHECK_FALSE(flag);
  flag = true;
  CHECK(flag);
}

TEST_CASE("relaxed_atomic increments and decrements",
    "[infra][relaxed_atomic]") {
  relaxed_atomic_int cnt{10};

  // Pre forms return the updated value, post forms the previous one.
  CHECK(++cnt == 11);
  CHECK(cnt++ == 11);
  CHECK(*cnt == 12);
  CHECK(--cnt == 11);
  CHECK(cnt-- == 11);
  CHECK(*cnt == 10);

  CHECK((cnt += 5) == 15);
  CHECK((cnt -= 3) == 12);
}

TEST_CASE("relaxed_atomic exchanges", "[infra][relaxed_atomic]") {
  relaxed_atomic_int a{1};
  CHECK(a.exchange(2) == 1);
  CHECK(*a == 2);
}

TEST_CASE("relaxed_atomic compare-exchange", "[infra][relaxed_atomic]") {
  relaxed_atomic_int a{2};

  // Success sets the new value.
  int expected = 2;
  CHECK(a.compare_exchange(expected, 3));
  CHECK(*a == 3);

  // Failure leaves the value alone and updates `expected` to the current
  // value.
  expected = 99;
  CHECK_FALSE(a.compare_exchange(expected, 4));
  CHECK(expected == 3);
  CHECK(*a == 3);

  // The weak form may fail spuriously, so retry in a loop.
  expected = 3;
  while (!a.try_compare_exchange(expected, 5)) {}
  CHECK(*a == 5);
}

TEST_CASE("relaxed_atomic exposes the underlying atomic",
    "[infra][relaxed_atomic]") {
  relaxed_atomic_int a{1};

  // `underlying` hands out the `std::atomic` itself, for interfaces that
  // want one.
  std::atomic<int>& raw = a.underlying();
  raw.store(2);
  CHECK(*a == 2);

  // `operator->` reaches the full `std::atomic` API, including explicit
  // stronger orderings.
  CHECK(a->load(std::memory_order::seq_cst) == 2);
  a->fetch_add(3, std::memory_order::acq_rel);
  CHECK(*a == 5);
}

TEST_CASE("relaxed_atomic is not copyable", "[infra][relaxed_atomic]") {
  STATIC_CHECK_FALSE(std::is_copy_constructible_v<relaxed_atomic_int>);
  STATIC_CHECK_FALSE(std::is_copy_assignable_v<relaxed_atomic_int>);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
