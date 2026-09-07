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
#include <span>
#include <type_traits>

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
// Viewing packed storage and indexing it:
//   std::vector<float> storage(rows * cols);
//   matrix_view<float> m(storage, rows, cols);
//   m[r, c] = 1.0F;
//   for (const auto value : m.row(r)) ...
//
// Slicing out a block of columns:
//   const auto block = m.columns(first, count);
namespace corvid { inline namespace math {
template<typename T>
class matrix_view {
public:
  using element_t = T;

  constexpr matrix_view() = default;

  // Packed view over `data`, which must hold exactly `rows * cols` elements
  // (asserted).
  constexpr matrix_view(std::span<T> data, size_t rows, size_t cols) noexcept
      : data_{data.data()}, rows_{rows}, cols_{cols}, stride_{cols} {
    assert(data.size() == rows * cols);
  }

  // Strided view over `data`, whose rows start `stride` elements apart and
  // show only their first `cols` elements.
  //
  // `stride` must be at least `cols` (asserted).
  constexpr matrix_view(T* data, size_t rows, size_t cols,
      size_t stride) noexcept
      : data_{data}, rows_{rows}, cols_{cols}, stride_{stride} {
    assert(stride >= cols);
  }

  // Implicit conversion to a read-only view of a mutable view of the same
  // element type.
  template<typename U>
  requires(std::is_same_v<const U, T> && !std::is_same_v<U, T>)
  constexpr matrix_view(matrix_view<U> other) noexcept
      : data_{other.data()}, rows_{other.rows()}, cols_{other.cols()},
        stride_{other.stride()} {}

  [[nodiscard]] constexpr T* data() const noexcept { return data_; }
  [[nodiscard]] constexpr size_t rows() const noexcept { return rows_; }
  [[nodiscard]] constexpr size_t cols() const noexcept { return cols_; }
  [[nodiscard]] constexpr size_t stride() const noexcept { return stride_; }
  [[nodiscard]] constexpr bool empty() const noexcept {
    return !rows_ || !cols_;
  }

  // Element at row `r`, column `c`, both of which must be in range
  // (asserted).
  [[nodiscard]] constexpr T& operator[](size_t r, size_t c) const noexcept {
    assert((r < rows_) && (c < cols_));
    return data_[(r * stride_) + c];
  }

  // The elements of row `r`, which must be in range (asserted).
  [[nodiscard]] constexpr std::span<T> row(size_t r) const noexcept {
    assert(r < rows_);
    // Parens, not braces: C++26 gives span an initializer_list constructor.
    // NOLINTNEXTLINE(modernize-return-braced-init-list)
    return std::span<T>(data_ + (r * stride_), cols_);
  }

  // View of `count` columns starting at column `first`, over the same rows.
  //
  // The range must lie within the view (asserted).
  [[nodiscard]] constexpr matrix_view
  columns(size_t first, size_t count) const noexcept {
    assert((first <= cols_) && (count <= cols_ - first));
    return {data_ + first, rows_, count, stride_};
  }

private:
  T* data_{};
  size_t rows_{};
  size_t cols_{};
  size_t stride_{};
};

template<typename T>
matrix_view(std::span<T>, size_t, size_t) -> matrix_view<T>;

}} // namespace corvid::math
