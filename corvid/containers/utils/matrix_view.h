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
#include <algorithm>
#include <bit>
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
// Slicing out a block, and the window at the origin of another matrix's
// extent:
//   const auto block = m[{r, c}, {.row_count = 2, .col_count = 3}];
//   const auto window = m[other.extent()];
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

  // The element count, which is what storage for the extent holds.
  [[nodiscard]] constexpr size_t size() const noexcept {
    return row_count * col_count;
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

// The row-major layout of a matrix in memory, as its extent and the stride
// its rows start apart.
//
// A default-constructed layout is empty. Every lens is a layout, since
// `matrix_lens_base` derives from it.
class matrix_layout {
public:
  constexpr matrix_layout() noexcept = default;

  // Layout of `extent` whose rows start `stride` elements apart, which must
  // be at least `extent.col_count`.
  constexpr matrix_layout(matrix_extent extent, size_t stride) noexcept
      : extent_{extent}, stride_{stride} {}

  [[nodiscard]] constexpr matrix_extent extent() const noexcept {
    return extent_;
  }
  [[nodiscard]] constexpr size_t stride() const noexcept { return stride_; }
  [[nodiscard]] constexpr size_t row_extent() const noexcept {
    return extent_.row_count;
  }
  [[nodiscard]] constexpr size_t col_extent() const noexcept {
    return extent_.col_count;
  }

  // The element count, which is what packed storage for the extent holds.
  [[nodiscard]] constexpr size_t size() const noexcept {
    return extent_.size();
  }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return !extent_.row_count || !extent_.col_count;
  }
  [[nodiscard]] constexpr bool is_packed() const noexcept {
    return (stride_ == extent_.col_count);
  }

  // The element count from the first element of the first row through the
  // last of the last row, which is what a lens of the layout reaches.
  [[nodiscard]] constexpr size_t footprint() const noexcept {
    return extent_.row_count
               ? ((extent_.row_count - 1) * stride_) + extent_.col_count
               : 0;
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

  // The element offset of row `r`, column `c`, both of which must be in range.
  [[nodiscard]] constexpr size_t
  offset_of(row_ndx r, col_ndx c) const noexcept {
    assert((*r < extent_.row_count) && (*c < extent_.col_count));
    return (*r * stride_) + *c;
  }

  // The extent of the rectangle from `from` up to, but not including, `to`,
  // where a member of `to` at its `npos` means the end of that dimension.
  //
  // The rectangle must lie within the layout.
  [[nodiscard]] constexpr matrix_extent
  window_extent(coord from, coord to) const noexcept {
    const auto row_end =
        (to.row == row_ndx::npos) ? extent_.row_count : *to.row;
    const auto col_end =
        (to.col == col_ndx::npos) ? extent_.col_count : *to.col;
    assert((*from.row <= row_end) && (row_end <= extent_.row_count));
    assert((*from.col <= col_end) && (col_end <= extent_.col_count));
    return {row_end - *from.row, col_end - *from.col};
  }

  // The extent of the rectangle of `size` at `from`, where a count of `size`
  // at its `npos` means the rest of that dimension.
  //
  // The rectangle must lie within the layout.
  [[nodiscard]] constexpr matrix_extent
  window_extent(coord from, matrix_extent size) const noexcept {
    assert(
        (*from.row <= extent_.row_count) && (*from.col <= extent_.col_count));
    const auto rows_left = extent_.row_count - *from.row;
    const auto cols_left = extent_.col_count - *from.col;
    const auto row_count =
        (size.row_count == matrix_extent::npos.row_count)
            ? rows_left
            : size.row_count;
    const auto col_count =
        (size.col_count == matrix_extent::npos.col_count)
            ? cols_left
            : size.col_count;
    assert((row_count <= rows_left) && (col_count <= cols_left));
    return {row_count, col_count};
  }

  [[nodiscard]] constexpr bool operator==(
      const matrix_layout&) const noexcept = default;

private:
  matrix_extent extent_{.row_count = 0, .col_count = 0};
  size_t stride_{};
};

} // namespace matrix_types

#pragma endregion
#pragma region Row and column spans

// A span of one row is indexed by column, and a span of one column by row.
template<typename T>
using row_span = enum_span<T, matrix_types::col_ndx>;
template<typename T>
using col_span = enum_span<T, matrix_types::row_ndx>;

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
class matrix_lens_base: public matrix_types::matrix_layout {
public:
#pragma region Types

  using element_t = T;
  using span_t = std::span<T>;
  using row_ndx = matrix_types::row_ndx;
  using col_ndx = matrix_types::col_ndx;
  using coord = matrix_types::coord;
  using extent_t = matrix_types::matrix_extent;
  using layout_t = matrix_types::matrix_layout;
  using row_span_t = row_span<element_t>;
  using col_span_t = col_span<element_t>;

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
    assert(data.size() == extent.size());
  }

  // Strided lens over `data`, whose rows start `stride` elements apart and
  // show only their first `extent.col_count` elements.
  //
  // `stride` must be at least `extent.col_count`, and `data` must reach the
  // last element of the last row.
  constexpr matrix_lens_base(span_t data, extent_t extent,
      size_t stride) noexcept
      : layout_t{extent, stride}, data_{data} {
    assert(stride >= extent.col_count);
    assert(data.size() >= footprint());
    data_ = data_.first(footprint());
  }

  // Strided lens over `data` in `layout`, under the same requirements.
  constexpr matrix_lens_base(span_t data, layout_t layout) noexcept
      : matrix_lens_base{data, layout.extent(), layout.stride()} {}

  // Single-row lens over `row`, packed.
  constexpr explicit matrix_lens_base(row_span_t row) noexcept
      : matrix_lens_base{span_t(row.data(), row.size()),
            extent_t{.row_count = 1, .col_count = row.size()}} {}

  // Single-column lens over `column`, packed.
  constexpr explicit matrix_lens_base(col_span_t column) noexcept
      : matrix_lens_base{span_t(column.data(), column.size()),
            extent_t{.row_count = column.size(), .col_count = 1}} {}

  // Implicit conversion of a lens to a view of the same element type.
  template<typename U>
  requires(std::is_same_v<const U, element_t> && !std::is_same_v<U, element_t>)
  constexpr matrix_lens_base(const matrix_lens_base<U>& other) noexcept
      : layout_t{other.layout()}, data_{other.as_span()} {}

#pragma endregion
#pragma region Accessors

  // The elements the view reaches, from the first of the first row through
  // the last of the last row, so a strided view includes the gaps between
  // its rows.
  [[nodiscard]] constexpr span_t as_span() const noexcept { return data_; }

  // The layout alone, as the base.
  [[nodiscard]] constexpr const layout_t& layout() const noexcept {
    return *this;
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
    return self.do_slice(from, self.window_extent(from, to));
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
    return self.do_slice(from, self.window_extent(from, size));
  }
  template<typename Self>
  [[nodiscard]] constexpr Self
  operator[](this const Self& self, coord from, extent_t size) noexcept {
    return self.slice(from, size);
  }

  // Slice out the rectangle of `size` at the origin, sharing this one's
  // stride. Prefer the subscript, `m[size]`.
  //
  // This is how a matrix takes the window of another's extent, as
  // `m[other.extent()]`. The rectangle must lie within this one.
  template<typename Self>
  [[nodiscard]] constexpr Self
  slice(this const Self& self, extent_t size) noexcept {
    return self.slice(coord{}, size);
  }
  template<typename Self>
  [[nodiscard]] constexpr Self
  operator[](this const Self& self, extent_t size) noexcept {
    return self.slice(size);
  }

#pragma endregion
#pragma region Overlap

  // `is_disjoint` determines whether this lens and `other` reach no element in
  // common, so that one can be written while the other is read.
  //
  // Two lenses whose strides and element sizes agree are read as windows of
  // one row-major parent, where the gap between a row's end and the next
  // row's start belongs to neither, so column windows lying side by side are
  // disjoint although their footprints interleave. Lenses that disagree in
  // either cannot share a parent, and for them the footprints decide.
  template<typename U>
  [[nodiscard]] bool
  is_disjoint(const matrix_lens_base<U>& other) const noexcept {
    if ((sizeof(U) != sizeof(element_t)) || (other.stride() != stride()))
      return corvid::is_disjoint(data_, other.as_span());
    if (empty() || other.empty()) return true;
    const auto first = std::bit_cast<uintptr_t>(data_.data());
    const auto other_first = std::bit_cast<uintptr_t>(other.as_span().data());
    if (other_first < first)
      return !windows_overlap(other_first, other.extent(), first, extent(),
          stride());
    return !windows_overlap(first, extent(), other_first, other.extent(),
        stride());
  }

  // Whether this lens and `other` reach exactly the same elements.
  template<typename U>
  [[nodiscard]] bool is_same(const matrix_lens_base<U>& other) const noexcept {
    return (sizeof(U) == sizeof(element_t)) &&
           (static_cast<const void*>(data_.data()) ==
               static_cast<const void*>(other.as_span().data())) &&
           (layout() == other.layout());
  }

  // Whether this lens and `other` reach exactly the same elements or none in
  // common, which are the only two ways an in-place operation may alias.
  template<typename U>
  [[nodiscard]] bool
  is_same_or_disjoint(const matrix_lens_base<U>& other) const noexcept {
    return is_same(other) || is_disjoint(other);
  }

#pragma endregion
#pragma region Workers
private:
  // `windows_overlap` determines whether two windows of one row-major parent,
  // whose rows start `stride` elements apart, share an element. Each window is
  // given as the address of its first element and its extent; `b_first` must
  // not precede `a_first`.
  //
  // `b`'s first element lies `distance` elements past `a`'s, which is
  // `rows_down` whole rows and `cols_right` more elements. In the parent that
  // puts `b` either `cols_right` columns to the right of `a` in that row or,
  // if the parent's row ends first, one more row down and `stride -
  // cols_right` columns to the left. The addresses alone do not say which,
  // but a placement that overlaps `a` rules the other one out: `b` to the
  // right overlapping `a` means `a` extends past `cols_right`, so `b` to the
  // left would have to start before the row does; `b` to the left overlapping
  // `a` means `b` is wider than the row has room for to the right. So the two
  // placements' overlap tests are simply combined.
  [[nodiscard]] static constexpr bool windows_overlap(uintptr_t a_first,
      extent_t a, uintptr_t b_first, extent_t b, size_t stride) noexcept {
    assert(a_first <= b_first);
    const auto distance = (b_first - a_first) / sizeof(element_t);
    const auto rows_down = distance / stride;
    const auto cols_right = distance % stride;
    const auto right_of_a =
        (cols_right < a.col_count) && (rows_down < a.row_count);
    const auto left_of_a =
        (cols_right + b.col_count > stride) && (rows_down + 1 < a.row_count);
    return right_of_a || left_of_a;
  }

  // Helper to slice out the rectangle of `size` at `from`, both already
  // checked.
  //
  // An empty rectangle at the row past the last would start beyond a strided
  // lens's footprint, so the offset is clamped; it has no element to reach.
  template<typename Self>
  [[nodiscard]] constexpr Self
  do_slice(this const Self& self, coord from, extent_t size) noexcept {
    const auto stride = self.stride();
    const auto data = self.as_span();
    const auto offset =
        std::min((*from.row * stride) + *from.col, data.size());
    return Self{data.subspan(offset, layout_t{size, stride}.footprint()), size,
        stride};
  }

#pragma endregion
#pragma region Data members
protected:
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
  using base::data_;

public:
#pragma region Types

  using typename base::element_t;
  using typename base::span_t;
  using typename base::row_ndx;
  using typename base::col_ndx;
  using typename base::coord;
  using typename base::extent_t;
  using typename base::row_span_t;
  using typename base::col_span_t;

#pragma endregion
#pragma region Construction

  using base::base;

#pragma endregion
#pragma region Accessors

  // The base's slicing subscripts, which the ones below would hide, and the
  // layout accessors, which a dependent base would otherwise hide.
  using base::operator[], base::extent, base::stride;

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
  [[nodiscard]] constexpr row_span_t operator[](row_ndx r) const noexcept {
    return row_as_span(r);
  }

  // The elements of row `r` from column `first` up to, but not including,
  // `last`, where `col_ndx::npos` means the end of the row. Prefer
  // `operator[row_ndx]` instead.
  //
  // `r` must be in range, and `first <= last <= col_extent()`.
  [[nodiscard]] constexpr row_span_t row_as_span(row_ndx r, col_ndx first = {},
      col_ndx last = col_ndx::npos) const noexcept {
    assert(*r < extent().row_count);
    const auto end = (last == col_ndx::npos) ? extent().col_count : *last;
    assert((*first <= end) && (end <= extent().col_count));
    return {data_.subspan((*r * stride()) + *first, end - *first)};
  }

  // The rows in order, as a random-access range of `row_span`, so that two
  // views' rows can be walked together with `std::views::zip`.
  //
  // The range holds a copy of the view, so it remains valid when taken from
  // a temporary.
  [[nodiscard]] constexpr auto rows() const noexcept {
    return std::views::iota(size_t{0}, extent().row_count) |
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
    assert((other.row_extent() == extent().row_count) &&
           (other.col_extent() == extent().col_count));
    assert(this->is_same_or_disjoint(other));
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
    assert((other.row_extent() == extent().row_count) &&
           (other.col_extent() == extent().col_count));
    assert(this->is_same_or_disjoint(other));
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

using float_col_span = col_span<float>;
using const_float_col_span = col_span<const float>;
using float_row_span = row_span<float>;
using const_float_row_span = row_span<const float>;

using double_col_span = col_span<double>;
using const_double_col_span = col_span<const double>;
using double_row_span = row_span<double>;
using const_double_row_span = row_span<const double>;

#pragma endregion
}}} // namespace corvid::container::matrices
