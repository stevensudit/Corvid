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

#include "../corvid/concurrency.h"
#include "catch2_main.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region TombStone_Basic

TEST_CASE("TombStone_Basic", "[TombStone]") {
  tombstone t;
  CHECK_FALSE((t.dead()));
  CHECK_FALSE((t.get()));
  CHECK_FALSE((*t));
  if (t) {
    CHECK_FALSE((true));
  } else {
    CHECK_FALSE((false));
  }
  if (!t) {
    CHECK((true));
  } else {
    CHECK_FALSE((false));
  }
  t.set(false);
  CHECK_FALSE((t.dead()));
  t.set(true);
  CHECK((t.dead()));
  CHECK((t.get()));
  CHECK((*t));
  t.set(false);
  CHECK((t.dead()));
  CHECK((t.get()));
  CHECK((*t));
}

#pragma endregion
#pragma region TombStone_TrySet

TEST_CASE("TombStone_TrySet", "[TombStone]") {
  tombstone t;
  // Returns false when value is already the target.
  CHECK_FALSE((t.try_set(false)));
  CHECK_FALSE((t.dead()));
  // Returns true when value changes.
  CHECK((t.try_set(true)));
  CHECK((t.dead()));
  // Returns false when dead (even for a different value).
  CHECK_FALSE((t.try_set(false)));
  CHECK((t.dead()));
}

#pragma endregion
#pragma region TombStone_Kill

TEST_CASE("TombStone_Kill", "[TombStone]") {
  tombstone t;
  // First kill succeeds.
  CHECK((t.kill()));
  CHECK((t.dead()));
  // Second kill reports already dead.
  CHECK_FALSE((t.kill()));
  CHECK((t.dead()));
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
