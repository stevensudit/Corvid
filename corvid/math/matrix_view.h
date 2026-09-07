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
#pragma once
#include <cassert>
#include <cstddef>
#include <limits>
#include <span>
#include <type_traits>

#include "../enums/sequence_enum.h"

// `matrix_view` is a two-dimensional view over contiguous memory, providing
// row-major access.
//
// Like `std::mdspan`, which this is an extreme simplification of, it does not
// own the memory. It pairs a pointer with a row count, a column count, and a
// stride, which is the element distance between the starts of consecutive
// rows.
//
// For a packed matrix, which is typical, the stride is the column count. A
// larger stride allows a view to refer to a rectangular slice of a matrix.
//
// Rows and columns are indexed by distinct types, `row_ndx` and `col_ndx`, so
// the two cannot be swapped by accident. `coord` pairs one of each to name an
// element, and `extent` holds a rectangle's row and column counts. All four
// are aliased into `matrix_view`.
//
// Viewing packed storage and indexing it:
//   using view_t = matrix_view<float>;
//   std::vector<float> storage(rows * cols);
//   view_t m(storage, {.row_count = rows, .col_count = cols});
//   m[view_t::row_ndx{r}, view_t::col_ndx{c}] = 1.0F;
//   for (const auto value : m.row_span(view_t::row_ndx{r})) ...
//
// Slicing out a block:
//   const auto block = m.subview({view_t::row_ndx{1}, view_t::col_ndx{2}},
//       {.row_count = 2, .col_count = 3});
namespace corvid { inline namespace math { inline namespace matrices {

#pragma region details

namespace details {

// Row index. `npos` is the end of the rows, used through `coord::npos`.
enum class row_ndx : size_t { npos = std::numeric_limits<size_t>::max() };
consteval auto corvid_enum_spec(row_ndx*) {
  return corvid::enums::sequence::make_sequence_enum_spec<row_ndx,
      "0,|18446744073709551615,npos">();
}

// Column index. `npos` is the end of the columns, used through `coord::npos`
// and as the `last` default of `row_span`.
enum class col_ndx : size_t { npos = std::numeric_limits<size_t>::max() };
consteval auto corvid_enum_spec(col_ndx*) {
  return corvid::enums::sequence::make_sequence_enum_spec<col_ndx,
      "0,|18446744073709551615,npos">();
}

// The position of an element: a row and a column.
struct coord {
  row_ndx row = row_ndx::npos;
  col_ndx col = col_ndx::npos;

  static const coord npos;
};
inline constexpr coord coord::npos{};

// The size of a rectangle of elements, as row and column counts.
struct extent {
  size_t row_count = std::numeric_limits<size_t>::max();
  size_t col_count = std::numeric_limits<size_t>::max();

  static const extent npos;
};
inline constexpr extent extent::npos{};

} // namespace details

using corvid::enums::sequence::ops::operator*;

#pragma endregion
#pragma region matrix_view

template<typename T>
class matrix_view {
public:
#pragma region Types

  using element_t = T;
  using row_ndx = details::row_ndx;
  using col_ndx = details::col_ndx;
  using coord = details::coord;
  using extent = details::extent;

#pragma endregion
#pragma region Construction

  constexpr matrix_view() = default;

  // Packed view over `data`, which must hold exactly the elements of `size`
  // (asserted).
  constexpr matrix_view(std::span<T> data, extent size) noexcept
      : matrix_view{data, size, size.col_count} {
    assert(data.size() == rows_ * cols_);
  }

  // Strided view over `data`, whose rows start `stride` elements apart and
  // show only their first `size.col_count` elements.
  //
  // `stride` must be at least `size.col_count`, and `data` must reach the
  // last element of the last row (both asserted).
  constexpr matrix_view(std::span<T> data, extent size, size_t stride) noexcept
      : data_{data.data()}, rows_{size.row_count}, cols_{size.col_count},
        stride_{stride} {
    assert(stride >= cols_);
    assert(data.size() >= footprint(size, stride));
  }

  // Implicit conversion to a read-only view of a mutable view of the same
  // element type.
  template<typename U>
  requires(std::is_same_v<const U, T> && !std::is_same_v<U, T>)
  constexpr matrix_view(matrix_view<U> other) noexcept
      : data_{other.data()}, rows_{*other.rows()}, cols_{*other.cols()},
        stride_{other.stride()} {}

#pragma endregion
#pragma region Accessors

  [[nodiscard]] constexpr T* data() const noexcept { return data_; }
  [[nodiscard]] constexpr row_ndx rows() const noexcept {
    return row_ndx{rows_};
  }
  [[nodiscard]] constexpr col_ndx cols() const noexcept {
    return col_ndx{cols_};
  }
  [[nodiscard]] constexpr size_t stride() const noexcept { return stride_; }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return !rows_ || !cols_;
  }

  // Element at row `r`, column `c`, both of which must be in range
  // (asserted).
  [[nodiscard]] constexpr T& operator[](row_ndx r, col_ndx c) const noexcept {
    assert((*r < rows_) && (*c < cols_));
    return data_[(*r * stride_) + *c];
  }

  // Element at `at`, which must be in range (asserted).
  [[nodiscard]] constexpr T& operator[](coord at) const noexcept {
    return (*this)[at.row, at.col];
  }

  // The elements of row `r` from column `first` up to, not including, `last`,
  // where `col_ndx::npos` means the end of the row.
  //
  // `r` must be in range, and `first <= last <= cols()` (asserted).
  [[nodiscard]] constexpr std::span<T> row_span(row_ndx r, col_ndx first = {},
      col_ndx last = col_ndx::npos) const noexcept {
    assert(*r < rows_);
    const auto end = (last == col_ndx::npos) ? cols_ : *last;
    assert((*first <= end) && (end <= cols_));
    // Parens, not braces: C++26 gives span an initializer_list constructor.
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::span<T>(data_ + (*r * stride_) + *first, end - *first);
  }

#pragma endregion
#pragma region Slicing

  // View of the rectangle from `from` up to, not including, `to`, where a
  // member of `to` at its `npos` means the end of that dimension.
  //
  // The rectangle must lie within the view (asserted).
  [[nodiscard]] constexpr matrix_view
  subview(coord from, coord to) const noexcept {
    const auto row_end = (to.row == row_ndx::npos) ? rows_ : *to.row;
    const auto col_end = (to.col == col_ndx::npos) ? cols_ : *to.col;
    assert((*from.row <= row_end) && (row_end <= rows_));
    assert((*from.col <= col_end) && (col_end <= cols_));
    return do_subview(from, {row_end - *from.row, col_end - *from.col});
  }

  // View of the rectangle of `size` starting at `from`, where a count of
  // `size` at its maximum means the rest of that dimension; the default takes
  // everything from `from` on.
  //
  // The rectangle must lie within the view (asserted).
  [[nodiscard]] constexpr matrix_view
  subview(coord from, extent size = extent::npos) const noexcept {
    assert((*from.row <= rows_) && (*from.col <= cols_));
    const auto rows_left = rows_ - *from.row;
    const auto cols_left = cols_ - *from.col;
    const auto row_count =
        (size.row_count == extent::npos.row_count)
            ? rows_left
            : size.row_count;
    const auto col_count =
        (size.col_count == extent::npos.col_count)
            ? cols_left
            : size.col_count;
    assert((row_count <= rows_left) && (col_count <= cols_left));
    return do_subview(from, {row_count, col_count});
  }

#pragma endregion
#pragma region Workers
private:
  // The element count from the first element of the first row through the
  // last element of the last row, which is what a view of `size` reaches.
  [[nodiscard]] static constexpr size_t
  footprint(extent size, size_t stride) noexcept {
    return size.row_count ? ((size.row_count - 1) * stride) + size.col_count
                          : 0;
  }

  // View of the rectangle of `size` at `from`, both already checked.
  [[nodiscard]] constexpr matrix_view
  do_subview(coord from, extent size) const noexcept {
    // Parens, not braces: C++26 gives span an initializer_list constructor.
    return {std::span<T>(data_ + (*from.row * stride_) + *from.col,
                footprint(size, stride_)),
        size, stride_};
  }

#pragma endregion
#pragma region Data members

  T* data_{};
  size_t rows_{};
  size_t cols_{};
  size_t stride_{};

#pragma endregion
};

template<typename T>
matrix_view(std::span<T>, details::extent) -> matrix_view<T>;

template<typename T>
matrix_view(std::span<T>, details::extent, size_t) -> matrix_view<T>;

#pragma endregion
}}} // namespace corvid::math::matrices
