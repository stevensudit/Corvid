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
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <type_traits>

#include "../../enums/sequence_enum.h"
#include "../../meta/containers.h"
#include "enum_span.h"
#include "interval.h"

// `matrix_lens` and `matrix_view` are two-dimensional references into
// contiguous memory, providing row-major access.
//
// A lens reads and writes its elements, and a view only reads them. A
// `matrix_view<T>` is a `matrix_lens<const T>`, so a lens converts to a view
// of the same element type, and everything below holds for both.
//
// Like `std::mdspan`, which this is an extreme simplification of, a lens does
// not own the memory. It pairs a pointer with a row count, a column count, and
// a stride, which is the element distance between the starts of consecutive
// rows.
//
// For a packed matrix, which is typical, the stride is the column count. A
// larger stride allows a lens to refer to a rectangular slice of a matrix.
//
// Rows and columns are indexed by distinct types, `row_ndx` and `col_ndx`, so
// the two cannot be swapped by accident. `coord` pairs one of each to name an
// element, and `extent` holds a rectangle's row and column counts. All four
// are nested under `matrix_lens`.
//
// Wrapping packed storage and indexing it:
//   using lens_t = matrix_lens<float>;
//   std::vector<float> storage(rows * cols);
//   lens_t m(storage, {.row_count = rows, .col_count = cols});
//   for (const auto r : m.row_indexes())
//     for (const auto c : m.col_indexes()) m[r, c] = 1.0F;
//   for (const auto value : m[r]) ...
//
// Walking the rows of two views together:
//   for (auto [out_row, in_row] : std::views::zip(out.rows(), in.rows()))
//     std::ranges::copy(in_row, out_row.begin());
//
// Slicing out a block:
//   const auto block = m[{r, c}, {.row_count = 2, .col_count = 3}];
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

// The position of an element, as row and column.
struct coord {
  row_ndx row{};
  col_ndx col{};

  static const coord npos;
};
inline constexpr coord coord::npos{row_ndx::npos, col_ndx::npos};

// An axis of a matrix.
enum class matrix_axis : uint8_t { rows, cols };

// Tag that selects the lens constructor requiring the span to hold exactly
// the elements of the extent.
struct exact_size_t {
  explicit exact_size_t() = default;
};
inline constexpr exact_size_t exact_size{};

// The size of a rectangle of elements, as row and column counts.
struct matrix_extent {
  size_t row_count = dynamic_extent;
  size_t col_count = dynamic_extent;

  static constexpr size_t dynamic_extent = std::numeric_limits<size_t>::max();

  // The extent of the transposed matrix, with the counts swapped.
  [[nodiscard]] constexpr matrix_extent transposed() const noexcept {
    return {.row_count = col_count, .col_count = row_count};
  }

  // The count along `axis`. Prefer the subscript.
  template<typename Self>
  [[nodiscard]] constexpr auto&
  count(this Self& self, matrix_axis axis) noexcept {
    return (axis == matrix_axis::rows) ? self.row_count : self.col_count;
  }
  template<typename Self>
  [[nodiscard]] constexpr auto&
  operator[](this Self& self, matrix_axis axis) noexcept {
    return self.count(axis);
  }

  [[nodiscard]] constexpr bool operator==(
      const matrix_extent&) const noexcept = default;

  static const matrix_extent npos;
  static const matrix_extent dynamic;
};
inline constexpr matrix_extent matrix_extent::npos{};
inline constexpr matrix_extent matrix_extent::dynamic{
    matrix_extent::dynamic_extent, matrix_extent::dynamic_extent};

} // namespace matrix_types

#pragma endregion
#pragma region matrix_lens_base

// A row-major lens over a span of `T`, with the shape, the index math, and
// the slicing that every storage shares.
//
// `matrix_lens` and `cuda_matrix_lens` derive from it and add what reaches an
// element, host access or device transfers. The base never dereferences the
// span, so it serves device memory as well as host memory.
//
// A derived class inherits the constructors, which is how `slice` builds one.
template<typename T>
class matrix_lens_base {
public:
#pragma region Types

  using element_t = T;
  using span_t = std::span<T>;
  using row_ndx = matrix_types::row_ndx;
  using col_ndx = matrix_types::col_ndx;
  using coord = matrix_types::coord;
  using extent_t = matrix_types::matrix_extent;

#pragma endregion
#pragma region Construction

  constexpr matrix_lens_base() = default;

  // Packed lens over the start of `data`, which must hold at least the
  // elements of `extent`.
  constexpr matrix_lens_base(span_t data, extent_t extent) noexcept
      : matrix_lens_base{data, extent, extent.col_count} {}

  // Packed lens over `data`, which must hold exactly the elements of `extent`.
  constexpr matrix_lens_base(span_t data, extent_t extent,
      matrix_types::exact_size_t) noexcept
      : matrix_lens_base{data, extent, extent.col_count} {
    assert(data.size() == extent.row_count * extent.col_count);
  }

  // Strided lens over `data`, whose rows start `stride` elements apart and
  // show only their first `extent.col_count` elements.
  //
  // `stride` must be at least `extent.col_count`, and `data` must reach the
  // last element of the last row.
  constexpr matrix_lens_base(span_t data, extent_t extent,
      size_t stride) noexcept
      : extent_{extent}, stride_{stride}, data_{data} {
    assert(stride >= extent.col_count);
    assert(data.size() >= footprint());
    data_ = data_.first(footprint());
  }

  // Implicit conversion of a lens to a view of the same element type.
  template<typename U>
  requires(std::is_same_v<const U, element_t> && !std::is_same_v<U, element_t>)
  constexpr matrix_lens_base(const matrix_lens_base<U>& other) noexcept
      : extent_{other.extent()}, stride_{other.stride()},
        data_{other.as_span()} {}

#pragma endregion
#pragma region Accessors

  // The elements the view reaches, from the first of the first row through
  // the last of the last row, so a strided view includes the gaps between
  // its rows.
  [[nodiscard]] constexpr span_t as_span() const noexcept { return data_; }

  [[nodiscard]] constexpr size_t stride() const noexcept { return stride_; }

  [[nodiscard]] constexpr size_t row_extent() const noexcept {
    return extent_.row_count;
  }
  [[nodiscard]] constexpr size_t col_extent() const noexcept {
    return extent_.col_count;
  }
  [[nodiscard]] constexpr extent_t extent() const noexcept { return extent_; }
  [[nodiscard]] constexpr size_t size() const noexcept {
    return extent_.row_count * extent_.col_count;
  }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return !extent_.row_count || !extent_.col_count;
  }
  [[nodiscard]] constexpr bool is_packed() const noexcept {
    return (stride_ == extent_.col_count);
  }

  // One past the last row or column, as index types.
  [[nodiscard]] constexpr row_ndx end_row() const noexcept {
    return row_ndx{extent_.row_count};
  }
  [[nodiscard]] constexpr col_ndx end_col() const noexcept {
    return col_ndx{extent_.col_count};
  }

  // The row or column indexes as a closed interval, for ranged-for:
  //   for (const auto r : m.row_indexes())
  [[nodiscard]] constexpr auto row_indexes() const noexcept {
    return interval<row_ndx>::iota(extent_.row_count);
  }
  [[nodiscard]] constexpr auto col_indexes() const noexcept {
    return interval<col_ndx>::iota(extent_.col_count);
  }

#pragma endregion
#pragma region Slicing

  // Slice out the rectangle from `from` up to, but not including, `to`,
  // sharing this one's stride. Prefer the subscript, `m[from, to]`.
  //
  // The result is of the type it is sliced from, so a lens gives a lens and a
  // view gives a view. A member of `to` at its `npos` means the end of that
  // dimension. The rectangle must lie within this one.
  template<typename Self>
  [[nodiscard]] constexpr Self
  slice(this const Self& self, coord from, coord to) noexcept {
    return self.do_slice(from, self.window_size(from, to));
  }
  template<typename Self>
  [[nodiscard]] constexpr Self
  operator[](this const Self& self, coord from, coord to) noexcept {
    return self.slice(from, to);
  }

  // Slice out the rectangle of `size` starting at `from`, sharing this one's
  // stride. Prefer the subscript, `m[from, size]`.
  //
  // A count of `size` at its `npos` means the rest of that dimension, so the
  // default takes everything from `from` on. A subscript has no default, and
  // `m[from]` is an element, so the subscript for that names the extent, as
  // `m[from, matrix_extent::npos]`.
  //
  // The rectangle must lie within this one.
  template<typename Self>
  [[nodiscard]] constexpr Self slice(this const Self& self, coord from,
      extent_t size = extent_t::npos) noexcept {
    return self.do_slice(from, self.window_size(from, size));
  }
  template<typename Self>
  [[nodiscard]] constexpr Self
  operator[](this const Self& self, coord from, extent_t size) noexcept {
    return self.slice(from, size);
  }

#pragma endregion
#pragma region Workers
private:
  // The rectangle of `size` at `from`, both already checked.
  template<typename Self>
  [[nodiscard]] constexpr Self
  do_slice(this const Self& self, coord from, extent_t size) noexcept {
    const auto stride = self.stride();
    return Self{self.as_span().subspan((*from.row * stride) + *from.col,
                    footprint(size, stride)),
        size, stride};
  }

protected:
  // The element offset of row `r`, column `c`, both of which must be in range.
  [[nodiscard]] constexpr size_t
  offset_of(row_ndx r, col_ndx c) const noexcept {
    assert((*r < extent_.row_count) && (*c < extent_.col_count));
    return (*r * stride_) + *c;
  }

  // The element count from the first element of the first row through the
  // last element of the last row, which is what a view of `size` reaches.
  [[nodiscard]] static constexpr size_t
  footprint(extent_t size, size_t stride) noexcept {
    return size.row_count ? ((size.row_count - 1) * stride) + size.col_count
                          : 0;
  }
  [[nodiscard]] constexpr size_t footprint() const noexcept {
    return footprint(extent_, stride_);
  }

  // The size of the rectangle from `from` up to, but not including, `to`,
  // where a member of `to` at its `npos` means the end of that dimension.
  //
  // The rectangle must lie within the view.
  [[nodiscard]] constexpr extent_t
  window_size(coord from, coord to) const noexcept {
    const auto row_end =
        (to.row == row_ndx::npos) ? extent_.row_count : *to.row;
    const auto col_end =
        (to.col == col_ndx::npos) ? extent_.col_count : *to.col;
    assert((*from.row <= row_end) && (row_end <= extent_.row_count));
    assert((*from.col <= col_end) && (col_end <= extent_.col_count));
    return {row_end - *from.row, col_end - *from.col};
  }

  // The size of the rectangle of `size` at `from`, where a count of `size`
  // at its `npos` means the rest of that dimension.
  //
  // The rectangle must lie within the view.
  [[nodiscard]] constexpr extent_t
  window_size(coord from, extent_t size) const noexcept {
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
    return {row_count, col_count};
  }

#pragma endregion
#pragma region Data members

  extent_t extent_{0, 0};
  size_t stride_{};
  span_t data_;

#pragma endregion
};

#pragma endregion
#pragma region matrix_lens

// Fwd.
template<typename T>
class matrix_lens;

// A read-only `matrix_lens`.
template<typename T>
using matrix_view = matrix_lens<const T>;

// Shared across CPU and GPU.
template<typename T>
class matrix_lens: public matrix_lens_base<T> {
  using base = matrix_lens_base<T>;
  using base::extent_, base::stride_, base::data_;

public:
#pragma region Types

  using typename base::element_t;
  using typename base::span_t;
  using typename base::row_ndx;
  using typename base::col_ndx;
  using typename base::coord;
  using typename base::extent_t;
  using row_span = enum_span<element_t, col_ndx>;

#pragma endregion
#pragma region Construction

  using base::base;

#pragma endregion
#pragma region Accessors

  // The base's slicing subscripts, which the ones below would hide.
  using base::operator[];

  // Element at row `r`, column `c`, both of which must be in range.
  [[nodiscard]] constexpr element_t&
  operator[](row_ndx r, col_ndx c) const noexcept {
    return data_[this->offset_of(r, c)];
  }

  // Element at `at`, which must be in range.
  [[nodiscard]] constexpr element_t& operator[](coord at) const noexcept {
    return (*this)[at.row, at.col];
  }

  // Row at `r`, as a span of its columns.
  [[nodiscard]] constexpr row_span operator[](row_ndx r) const noexcept {
    return row_as_span(r);
  }

  // The elements of row `r` from column `first` up to, but not including,
  // `last`, where `col_ndx::npos` means the end of the row. Prefer
  // `operator[row_ndx]` instead.
  //
  // `r` must be in range, and `first <= last <= col_extent()`.
  [[nodiscard]] constexpr row_span row_as_span(row_ndx r, col_ndx first = {},
      col_ndx last = col_ndx::npos) const noexcept {
    assert(*r < extent_.row_count);
    const auto end = (last == col_ndx::npos) ? extent_.col_count : *last;
    assert((*first <= end) && (end <= extent_.col_count));
    return {data_.subspan((*r * stride_) + *first, end - *first)};
  }

  // The rows in order, as a random-access range of `row_span`, so that two
  // views' rows can be walked together with `std::views::zip`.
  //
  // The range holds a copy of the view, so it remains valid when taken from
  // a temporary.
  [[nodiscard]] constexpr auto rows() const noexcept {
    return std::views::iota(size_t{0}, extent_.row_count) |
           std::views::transform([m = *this](size_t r) {
             return m[row_ndx{r}];
           });
  }

#pragma endregion
#pragma region Arithmetic

  // Add `other` to every element, in place. Prefer `operator+=`.
  //
  // The extents must match, and `other` must be this view or disjoint from it.
  [[nodiscard]] constexpr const matrix_lens&
  add(matrix_view<element_t> other) const noexcept
  requires(!std::is_const_v<element_t>)
  {
    assert((other.row_extent() == extent_.row_count) &&
           (other.col_extent() == extent_.col_count));
    assert(is_same_or_disjoint(data_, other.as_span()));
    // The zip element is a prvalue tuple of two spans, so nothing is copied;
    // the check fires only because MSVC's tuple is not trivially copyable.
    // NOLINTNEXTLINE(performance-for-range-copy)
    for (auto [row, other_row] : std::views::zip(rows(), other.rows()))
      for (auto [value, other_value] : std::views::zip(row, other_row))
        value += other_value;
    return *this;
  }

  constexpr const matrix_lens&
  operator+=(matrix_view<element_t> other) const noexcept
  requires(!std::is_const_v<element_t>)
  {
    return add(other);
  }

  // Subtract `other` from every element, in place. Prefer `operator-=`.
  //
  // The extents must match, and `other` must be this view or disjoint from it.
  [[nodiscard]] constexpr const matrix_lens&
  subtract(matrix_view<element_t> other) const noexcept
  requires(!std::is_const_v<element_t>)
  {
    assert((other.row_extent() == extent_.row_count) &&
           (other.col_extent() == extent_.col_count));
    assert(is_same_or_disjoint(data_, other.as_span()));
    // The same prvalue tuple as in `add`.
    // NOLINTNEXTLINE(performance-for-range-copy)
    for (auto [row, other_row] : std::views::zip(rows(), other.rows()))
      for (auto [value, other_value] : std::views::zip(row, other_row))
        value -= other_value;
    return *this;
  }

  constexpr const matrix_lens&
  operator-=(matrix_view<element_t> other) const noexcept
  requires(!std::is_const_v<element_t>)
  {
    return subtract(other);
  }

#pragma endregion
};

template<typename T>
matrix_lens(std::span<T>, matrix_types::matrix_extent) -> matrix_lens<T>;

template<typename T>
matrix_lens(std::span<T>, matrix_types::matrix_extent, size_t)
    -> matrix_lens<T>;

#pragma endregion
#pragma region Aliases

using float_matrix_lens = matrix_lens<float>;
using float_matrix_view = matrix_view<float>;
using double_matrix_lens = matrix_lens<double>;
using double_matrix_view = matrix_view<double>;

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
