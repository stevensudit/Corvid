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

#include "corvid/concurrency/tombstone.h"
#include "catch2_main.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region TombStone_Basic

TEST_CASE("Basic", "[TombStone]") {
  tombstone t;
  CHECK_FALSE(t.dead());
  CHECK_FALSE(t.get());
  CHECK_FALSE(*t);
  if (t) {
    CHECK_FALSE(true);
  } else {
    CHECK_FALSE(false);
  }
  if (!t) {
    CHECK(true);
  } else {
    CHECK_FALSE(false);
  }
  t.set(false);
  CHECK_FALSE(t.dead());
  t.set(true);
  CHECK(t.dead());
  CHECK(t.get());
  CHECK(*t);
  t.set(false);
  CHECK(t.dead());
  CHECK(t.get());
  CHECK(*t);
}

#pragma endregion
#pragma region TombStone_TrySet

TEST_CASE("TrySet", "[TombStone]") {
  tombstone t;
  // Returns false when value is already the target.
  CHECK_FALSE(t.try_set(false));
  CHECK_FALSE(t.dead());
  // Returns true when value changes.
  CHECK(t.try_set(true));
  CHECK(t.dead());
  // Returns false when dead (even for a different value).
  CHECK_FALSE(t.try_set(false));
  CHECK(t.dead());
}

#pragma endregion
#pragma region TombStone_Kill

TEST_CASE("Kill", "[TombStone]") {
  tombstone t;
  // First kill succeeds.
  CHECK(t.kill());
  CHECK(t.dead());
  // Second kill reports already dead.
  CHECK_FALSE(t.kill());
  CHECK(t.dead());
}

#pragma endregion
#pragma region TombStone_Countdown

// Probe for the stepping operators. The template parameter is load-bearing:
// a requires-expression with no dependent names hard-errors instead of
// yielding false.
template<typename TS>
constexpr bool can_step_v = requires(TS& t) {
  ++t;
  --t;
};

TEST_CASE("Countdown", "[TombStone]") {
  // Counting down to `final_v` kills the tombstone by design; once dead, it
  // stays dead and further steps are no-ops.
  tombstone_of<int, 0, 3> t;
  CHECK_FALSE(t.dead());
  --t;
  --t;
  CHECK_FALSE(t.dead());
  CHECK(t.get() == 1);

  // The step that lands on the final value kills.
  --t;
  CHECK(t.dead());

  // Dead stays dead: neither direction moves it.
  --t;
  ++t;
  CHECK(t.dead());
  CHECK(t.get() == 0);

  // An increment can also land on death.
  tombstone_of<int, 2, 0> u;
  ++u;
  CHECK_FALSE(u.dead());
  ++u;
  CHECK(u.dead());

  // For the `bool` tombstone, stepping is pointless (both directions would
  // land on death), so the operators are constrained away.
  static_assert(can_step_v<tombstone_of<int, 0, 3>>);
  static_assert(!can_step_v<tombstone>);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
