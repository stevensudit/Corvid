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
#include "minitest.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region TombStone_Basic

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

#pragma endregion
#pragma region TombStone_TrySet

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

#pragma endregion
#pragma region TombStone_Kill

void TombStone_Kill() {
  tombstone t;
  // First kill succeeds.
  EXPECT_TRUE(t.kill());
  EXPECT_TRUE(t.dead());
  // Second kill reports already dead.
  EXPECT_FALSE(t.kill());
  EXPECT_TRUE(t.dead());
}

#pragma endregion

MAKE_TEST_LIST(TombStone_Basic, TombStone_TrySet, TombStone_Kill);

// NOLINTEND(readability-function-cognitive-complexity)
