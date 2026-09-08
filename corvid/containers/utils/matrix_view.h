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
#include <type_traits>

#include "../../enums/sequence_enum.h"
#include "enum_span.h"
#include "interval.h"

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
// are nested under `matrix_view`.
//
// Viewing packed storage and indexing it:
//   using view_t = matrix_view<float>;
//   std::vector<float> storage(rows * cols);
//   view_t m(storage, {.row_count = rows, .col_count = cols});
//   for (const auto r : m.row_interval())
//     for (const auto c : m.col_interval()) m[r, c] = 1.0F;
//   for (const auto value : m.row_as_span(r)) ...
//
// Slicing out a block:
//   const auto block = m.subview({r, c},
//       {.row_count = 2, .col_count = 3});
namespace corvid { inline namespace container { inline namespace matrices {

#pragma region matrix_types

// Add `using matrix_types;` to bring these into scope.
namespace matrix_types {

// Row index. `npos` is the end of the rows, used through `coord::npos`.
enum class row_ndx : size_t { npos = std::numeric_limits<size_t>::max() };
consteval auto corvid_enum_spec(row_ndx*) {
  return corvid::enums::sequence::make_sequence_enum_spec<row_ndx,
      "0,|18446744073709551615,npos">();
}

// Column index. `npos` is the end of the columns, used through `coord::npos`
// and as the `last` default of `row_as_span`.
enum class col_ndx : size_t { npos = std::numeric_limits<size_t>::max() };
consteval auto corvid_enum_spec(col_ndx*) {
  return corvid::enums::sequence::make_sequence_enum_spec<col_ndx,
      "0,|18446744073709551615,npos">();
}

// The position of an element: a row and a column.
struct coord {
  row_ndx row{};
  col_ndx col{};

  static const coord npos;
};
inline constexpr coord coord::npos{row_ndx::npos, col_ndx::npos};

// The size of a rectangle of elements, as row and column counts.
struct matrix_extent {
  size_t row_count = dynamic_extent;
  size_t col_count = dynamic_extent;

  static constexpr size_t dynamic_extent = std::numeric_limits<size_t>::max();

  static const matrix_extent npos;
  static const matrix_extent dynamic;
};
inline constexpr matrix_extent matrix_extent::npos{};
inline constexpr matrix_extent matrix_extent::dynamic{
    matrix_extent::dynamic_extent, matrix_extent::dynamic_extent};

} // namespace matrix_types

#pragma endregion
#pragma region matrix_view

template<typename T>
class matrix_view {
public:
#pragma region Types

  using element_t = T;
  using row_ndx = matrix_types::row_ndx;
  using col_ndx = matrix_types::col_ndx;
  using coord = matrix_types::coord;
  using extent_t = matrix_types::matrix_extent;
  using row_span = enum_span<T, col_ndx>;

#pragma endregion
#pragma region Construction

  constexpr matrix_view() = default;

  // Packed view over `data`, which must hold exactly the elements of `size`
  // (asserted).
  constexpr matrix_view(std::span<T> data, extent_t size) noexcept
      : matrix_view{data, size, size.col_count} {
    assert(data.size() == size.row_count * size.col_count);
  }

  // Strided view over `data`, whose rows start `stride` elements apart and
  // show only their first `size.col_count` elements.
  //
  // `stride` must be at least `size.col_count`, and `data` must reach the
  // last element of the last row (both asserted).
  constexpr matrix_view(std::span<T> data, extent_t size,
      size_t stride) noexcept
      : data_{data}, extent_{size}, stride_{stride} {
    assert(stride >= extent_.col_count);
    assert(data.size() >= footprint(size, stride));
    data_ = data_.first(footprint(size, stride));
  }

  // Implicit conversion to a read-only view of a mutable view of the same
  // element type.
  template<typename U>
  requires(std::is_same_v<const U, T> && !std::is_same_v<U, T>)
  constexpr matrix_view(matrix_view<U> other) noexcept
      : data_{other.as_span()}, extent_{other.extent()},
        stride_{other.stride()} {}

#pragma endregion
#pragma region Accessors

  // The elements the view reaches, from the first of the first row through
  // the last of the last row, so a strided view includes the gaps between
  // its rows.
  [[nodiscard]] constexpr std::span<T> as_span() const noexcept {
    return data_;
  }

  [[nodiscard]] constexpr size_t stride() const noexcept { return stride_; }

  [[nodiscard]] constexpr size_t row_extent() const noexcept {
    return extent_.row_count;
  }
  [[nodiscard]] constexpr size_t col_extent() const noexcept {
    return extent_.col_count;
  }
  [[nodiscard]] constexpr row_ndx row_extent_as_enum() const noexcept {
    return row_ndx{extent_.row_count};
  }
  [[nodiscard]] constexpr col_ndx col_extent_as_enum() const noexcept {
    return col_ndx{extent_.col_count};
  }
  [[nodiscard]] constexpr extent_t extent() const noexcept { return extent_; }
  [[nodiscard]] constexpr size_t size() const noexcept {
    return extent_.row_count * extent_.col_count;
  }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return !extent_.row_count || !extent_.col_count;
  }

  // One past the last row or column, as index types.
  [[nodiscard]] constexpr row_ndx end_row() const noexcept {
    return row_ndx{extent_.row_count};
  }
  [[nodiscard]] constexpr col_ndx end_col() const noexcept {
    return col_ndx{extent_.col_count};
  }

  // The row or column indexes as a closed interval, for ranged-for:
  //   for (const auto r : m.row_interval())
  [[nodiscard]] constexpr auto row_interval() const noexcept {
    return interval<row_ndx>::iota(extent_.row_count);
  }
  [[nodiscard]] constexpr auto col_interval() const noexcept {
    return interval<col_ndx>::iota(extent_.col_count);
  }

  // Element at row `r`, column `c`, both of which must be in range
  // (asserted).
  [[nodiscard]] constexpr T& operator[](row_ndx r, col_ndx c) const noexcept {
    assert((*r < extent_.row_count) && (*c < extent_.col_count));
    return data_[(*r * stride_) + *c];
  }

  // Element at `at`, which must be in range (asserted).
  [[nodiscard]] constexpr T& operator[](coord at) const noexcept {
    return (*this)[at.row, at.col];
  }

  // The elements of row `r` from column `first` up to, but not including,
  // `last`, where `col_ndx::npos` means the end of the row.
  //
  // `r` must be in range, and `first <= last <= col_extent()` (asserted).
  [[nodiscard]] constexpr row_span row_as_span(row_ndx r, col_ndx first = {},
      col_ndx last = col_ndx::npos) const noexcept {
    assert(*r < extent_.row_count);
    const auto end = (last == col_ndx::npos) ? extent_.col_count : *last;
    assert((*first <= end) && (end <= extent_.col_count));
    return {data_.subspan((*r * stride_) + *first, end - *first)};
  }

#pragma endregion
#pragma region Slicing

  // View of the rectangle from `from` up to, but not including, `to`.
  //
  // A member of `to` at its `npos` means to the end of that dimension. The
  // rectangle must lie within the view (asserted).
  [[nodiscard]] constexpr matrix_view
  subview(coord from, coord to) const noexcept {
    const auto row_end =
        (to.row == row_ndx::npos) ? extent_.row_count : *to.row;
    const auto col_end =
        (to.col == col_ndx::npos) ? extent_.col_count : *to.col;
    assert((*from.row <= row_end) && (row_end <= extent_.row_count));
    assert((*from.col <= col_end) && (col_end <= extent_.col_count));
    return do_subview(from, {row_end - *from.row, col_end - *from.col});
  }

  // View of the rectangle of `size` starting at `from`.
  //
  // A count of `size` at its `npos` means the rest of that dimension, so the
  // default takes everything from `from` on.
  //
  // The rectangle must lie within the view (asserted).
  [[nodiscard]] constexpr matrix_view
  subview(coord from, extent_t size = extent_t::npos) const noexcept {
    assert(
        (*from.row <= extent_.row_count) && (*from.col <= extent_.col_count));
    const auto rows_left = extent_.row_count - *from.row;
    const auto cols_left = extent_.col_count - *from.col;
    const auto row_count =
        (size.row_count == extent_t::npos.row_count)
            ? rows_left
            : size.row_count;
    const auto col_count =
        (size.col_count == extent_t::npos.col_count)
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
  footprint(extent_t size, size_t stride) noexcept {
    return size.row_count ? ((size.row_count - 1) * stride) + size.col_count
                          : 0;
  }

  // View of the rectangle of `size` at `from`, both already checked.
  [[nodiscard]] constexpr matrix_view
  do_subview(coord from, extent_t size) const noexcept {
    return {data_.subspan((*from.row * stride_) + *from.col,
                footprint(size, stride_)),
        size, stride_};
  }

#pragma endregion
#pragma region Data members

  std::span<T> data_;
  extent_t extent_{0, 0};
  size_t stride_{};

#pragma endregion
};

template<typename T>
matrix_view(std::span<T>, matrix_types::matrix_extent) -> matrix_view<T>;

template<typename T>
matrix_view(std::span<T>, matrix_types::matrix_extent, size_t)
    -> matrix_view<T>;

#pragma endregion
#pragma region Aliases

using float_matrix_view = matrix_view<float>;
using const_float_matrix_view = matrix_view<const float>;
using double_matrix_view = matrix_view<double>;
using const_double_matrix_view = matrix_view<const double>;

using float_span = std::span<float>;
using const_float_span = std::span<const float>;
using double_span = std::span<double>;
using const_double_span = std::span<const double>;

// A span of one row is indexed by column, and a span of one column by row.
using float_col_span = enum_span<float, matrix_types::row_ndx>;
using const_float_col_span = enum_span<const float, matrix_types::row_ndx>;
using float_row_span = enum_span<float, matrix_types::col_ndx>;
using const_float_row_span = enum_span<const float, matrix_types::col_ndx>;

using double_col_span = enum_span<double, matrix_types::row_ndx>;
using const_double_col_span = enum_span<const double, matrix_types::row_ndx>;
using double_row_span = enum_span<double, matrix_types::col_ndx>;
using const_double_row_span = enum_span<const double, matrix_types::col_ndx>;

#pragma endregion
}}} // namespace corvid::container::matrices
