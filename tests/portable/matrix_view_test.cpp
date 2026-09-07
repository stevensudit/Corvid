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
#include <cstddef>
#include <span>
#include <vector>

#include "corvid/math/matrix_view.h"
#include "catch2_main.h"

using namespace corvid::math;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Packed view indexes row-major", "[MatrixViewTest]") {
  std::vector<float> storage(2UZ * 3);
  matrix_view<float> m(storage, 2, 3);
  REQUIRE(m.rows() == 2);
  REQUIRE(m.cols() == 3);
  REQUIRE(m.stride() == 3);
  REQUIRE_FALSE(m.empty());
  REQUIRE(m.data() == storage.data());

  m[1, 2] = 5.0F;
  m[0, 1] = 7.0F;
  CHECK(storage[5] == 5.0F);
  CHECK(storage[1] == 7.0F);

  const auto second = m.row(1);
  CHECK(second.size() == 3);
  CHECK(second.data() == storage.data() + 3);
  CHECK(second[2] == 5.0F);

  const matrix_view<float> unset;
  CHECK(unset.empty());
  CHECK(unset.rows() == 0);
}

TEST_CASE("Mutable view converts to read-only", "[MatrixViewTest]") {
  std::vector<float> storage{1.0F, 2.0F, 3.0F, 4.0F};
  matrix_view<float> m(storage, 2, 2);
  const matrix_view<const float> ro = m;
  CHECK(ro[1, 0] == 3.0F);
  CHECK(ro.data() == m.data());

  const std::span<const float> bytes = storage;
  const matrix_view deduced(bytes, 2, 2);
  static_assert(
      std::is_same_v<decltype(deduced), const matrix_view<const float>>);
  CHECK(deduced[0, 1] == 2.0F);
}

TEST_CASE("Column block keeps the stride", "[MatrixViewTest]") {
  // A 2 x 6 matrix whose value is 10 * row + col.
  std::vector<float> storage(2UZ * 6);
  matrix_view<float> m(storage, 2, 6);
  for (auto r = 0UZ; r < m.rows(); ++r)
    for (auto c = 0UZ; c < m.cols(); ++c)
      m[r, c] = static_cast<float>((10 * r) + c);

  const auto block = m.columns(2, 3);
  REQUIRE(block.rows() == 2);
  REQUIRE(block.cols() == 3);
  REQUIRE(block.stride() == 6);
  CHECK(block[0, 0] == 2.0F);
  CHECK(block[1, 2] == 14.0F);
  CHECK(block.row(1).size() == 3);
  CHECK(block.row(1)[0] == 12.0F);

  // Writes through the block land in the original storage.
  block[1, 1] = -1.0F;
  CHECK(m[1, 3] == -1.0F);

  // Slicing a slice composes.
  const auto inner = block.columns(1, 1);
  CHECK(inner.cols() == 1);
  CHECK(inner[0, 0] == 3.0F);
  CHECK(inner.stride() == 6);

  // Empty slices at either edge are fine.
  CHECK(m.columns(6, 0).empty());
  CHECK(m.columns(0, 0).empty());
}

// NOLINTEND(readability-function-cognitive-complexity)
