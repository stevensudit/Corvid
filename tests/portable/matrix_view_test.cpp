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
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include "corvid/containers/utils/matrix_view.h"
#include "corvid/enums/enum_conversion.h"
#include "catch2_main.h"

using namespace corvid;

using view_t = matrix_view<float>;
using row_ndx = view_t::row_ndx;
using col_ndx = view_t::col_ndx;
using coord = view_t::coord;
using extent = view_t::extent;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// A `rows` by `cols` matrix whose value is 10 * row + col.
std::vector<float> make_storage(size_t rows, size_t cols) {
  std::vector<float> storage(rows * cols);
  const view_t m(storage, {.row_count = rows, .col_count = cols});
  for (const auto r : m.row_interval())
    for (const auto c : m.col_interval())
      m[r, c] = static_cast<float>((10 * *r) + *c);
  return storage;
}

} // namespace

TEST_CASE("Index types", "[MatrixViewTest]") {
  constexpr auto max = std::numeric_limits<size_t>::max();
  static_assert(*row_ndx::npos == max);
  static_assert(*col_ndx::npos == max);
  static_assert(
      (coord::npos.row == row_ndx::npos) &&
      (coord::npos.col == col_ndx::npos));
  static_assert(
      (extent::npos.row_count == max) && (extent::npos.col_count == max));
  static_assert(extent{}.row_count == extent::npos.row_count);

  // Sequence arithmetic and comparison; `npos` is one past the last index.
  row_ndx r{};
  ++r;
  r += 2;
  CHECK(*r == 3);
  CHECK(r < row_ndx{5});
  CHECK(r != row_ndx::npos);
  auto top = row_ndx{max - 1};
  ++top;
  CHECK(top == row_ndx::npos);

  // Only `npos` is named; every other value prints numerically.
  using namespace corvid::strings;
  CHECK(enum_as_string(row_ndx::npos) == "npos");
  CHECK(enum_as_string(row_ndx{3}) == "3");
  CHECK(enum_as_string(col_ndx::npos) == "npos");
  CHECK(enum_as_string(col_ndx{}) == "0");
}

TEST_CASE("Packed view indexes row-major", "[MatrixViewTest]") {
  std::vector<float> storage(2UZ * 3);
  view_t m(storage, {.row_count = 2, .col_count = 3});
  REQUIRE(m.row_extent() == 2);
  REQUIRE(m.col_extent() == 3);
  REQUIRE(m.get_extent().row_count == 2);
  REQUIRE(m.get_extent().col_count == 3);
  REQUIRE(m.size() == 6);
  REQUIRE(m.stride() == 3);
  REQUIRE_FALSE(m.empty());
  REQUIRE(m.as_span().data() == storage.data());
  REQUIRE(m.as_span().size() == 6);

  // `end_*` is one past; the intervals are closed.
  CHECK(m.end_row() == row_ndx{2});
  CHECK(m.end_col() == col_ndx{3});
  CHECK(m.row_interval() == interval<row_ndx>{row_ndx{0}, row_ndx{1}});
  CHECK(m.col_interval().size() == 3);
  CHECK(m.col_interval().back() == col_ndx{2});

  m[row_ndx{1}, col_ndx{2}] = 5.0F;
  m[coord{row_ndx{0}, col_ndx{1}}] = 7.0F;
  CHECK(storage[5] == 5.0F);
  CHECK(storage[1] == 7.0F);

  // A row, whole and trimmed at either end.
  const auto second = m.row_span(row_ndx{1});
  CHECK(second.size() == 3);
  CHECK(second.data() == storage.data() + 3);
  CHECK(second[2] == 5.0F);
  const auto tail = m.row_span(row_ndx{1}, col_ndx{1});
  CHECK(tail.size() == 2);
  CHECK(tail.data() == storage.data() + 4);
  const auto middle = m.row_span(row_ndx{1}, col_ndx{1}, col_ndx{2});
  CHECK(middle.size() == 1);
  CHECK(middle[0] == storage[4]);
  CHECK(m.row_span(row_ndx{0}, col_ndx{3}).empty());

  const view_t unset;
  CHECK(unset.empty());
  CHECK(unset.size() == 0);
  CHECK(unset.row_interval().empty());
  CHECK(unset.col_interval().empty());
  CHECK(unset.as_span().empty());
}

TEST_CASE("Mutable view converts to read-only", "[MatrixViewTest]") {
  std::vector<float> storage{1.0F, 2.0F, 3.0F, 4.0F};
  view_t m(storage, {.row_count = 2, .col_count = 2});
  const matrix_view<const float> ro = m;
  CHECK(ro[row_ndx{1}, col_ndx{0}] == 3.0F);
  CHECK(ro.as_span().data() == m.as_span().data());

  const std::span<const float> bytes = storage;
  const matrix_view deduced(bytes, {.row_count = 2, .col_count = 2});
  static_assert(
      std::is_same_v<decltype(deduced), const matrix_view<const float>>);
  CHECK(deduced[row_ndx{0}, col_ndx{1}] == 2.0F);
}

TEST_CASE("Subview keeps the stride", "[MatrixViewTest]") {
  auto storage = make_storage(2, 6);
  view_t m(storage, {.row_count = 2, .col_count = 6});

  // A block of columns by extent.
  const auto block =
      m.subview({row_ndx{0}, col_ndx{2}}, {.row_count = 2, .col_count = 3});
  REQUIRE(block.row_extent() == 2);
  REQUIRE(block.col_extent() == 3);
  REQUIRE(block.stride() == 6);
  CHECK(block.as_span().size() == 9);
  CHECK(block[row_ndx{0}, col_ndx{0}] == 2.0F);
  CHECK(block[row_ndx{1}, col_ndx{2}] == 14.0F);
  CHECK(block.row_span(row_ndx{1}).size() == 3);
  CHECK(block.row_span(row_ndx{1})[0] == 12.0F);

  // Writes through the block land in the original storage.
  block[row_ndx{1}, col_ndx{1}] = -1.0F;
  CHECK(m[row_ndx{1}, col_ndx{3}] == -1.0F);

  // Slicing a slice composes.
  const auto inner = block.subview({row_ndx{0}, col_ndx{1}},
      {.row_count = 2, .col_count = 1});
  CHECK(inner.col_extent() == 1);
  CHECK(inner[row_ndx{0}, col_ndx{0}] == 3.0F);
  CHECK(inner.stride() == 6);

  // Half-open corners trim both dimensions.
  const auto corner =
      m.subview({row_ndx{1}, col_ndx{1}}, coord{row_ndx{2}, col_ndx{4}});
  CHECK(corner.row_extent() == 1);
  CHECK(corner.col_extent() == 3);
  CHECK(corner[row_ndx{0}, col_ndx{0}] == 11.0F);
  CHECK(corner[row_ndx{0}, col_ndx{2}] == -1.0F);

  // `coord::npos`, the default extent, and a half-specified extent all run to
  // the end.
  const auto to_end = m.subview({row_ndx{1}, col_ndx{4}}, coord::npos);
  CHECK(to_end.row_extent() == 1);
  CHECK(to_end.col_extent() == 2);
  CHECK(to_end[row_ndx{0}, col_ndx{1}] == 15.0F);
  const auto rest = m.subview({row_ndx{1}, col_ndx{4}});
  CHECK(rest.as_span().data() == to_end.as_span().data());
  CHECK(rest.col_extent() == 2);
  const auto strip = m.subview({row_ndx{0}, col_ndx{3}}, {.row_count = 1});
  CHECK(strip.row_extent() == 1);
  CHECK(strip.col_extent() == 3);
  CHECK(strip[row_ndx{0}, col_ndx{2}] == 5.0F);

  // Empty slices at either edge are fine.
  CHECK(m.subview({row_ndx{2}, col_ndx{0}}).empty());
  CHECK(m.subview({row_ndx{0}, col_ndx{6}}).empty());
  CHECK(m.subview({row_ndx{0}, col_ndx{0}}, coord{row_ndx{0}, col_ndx{0}})
          .empty());
}

TEST_CASE("Strided view over a larger buffer", "[MatrixViewTest]") {
  // Three rows of four, viewed as three rows of two.
  auto storage = make_storage(3, 4);
  const view_t m(storage, {.row_count = 3, .col_count = 2}, 4);
  CHECK(m.stride() == 4);
  CHECK(m.size() == 6);
  CHECK(m.as_span().size() == 10);
  CHECK(m[row_ndx{2}, col_ndx{1}] == 21.0F);
  CHECK(m.row_span(row_ndx{1}).size() == 2);

  // The buffer only has to reach the last element of the last row.
  const view_t tight(std::span{storage}.first(10),
      {.row_count = 3, .col_count = 2}, 4);
  CHECK(tight[row_ndx{2}, col_ndx{1}] == 21.0F);
}

// NOLINTEND(readability-function-cognitive-complexity)
