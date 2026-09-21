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
#include <concepts>
#include <cstddef>
#include <type_traits>

#include <cuda_runtime.h>

#include "../containers/utils/matrix_view.h"
#include "./cuda_buffer.cuh"
#include "./cuda_kernel.cuh"
#include "./cuda_status.cuh"

// Matrices in device memory.
//
// `cuda_matrix<T>` owns a packed row-major matrix, and `cuda_matrix_lens<T>`
// is a non-owning window onto one, possibly strided.
//
// Both are host-side handles. Inside a kernel, `kernel_matrix_lens<T>` is what
// a lens becomes at a launch, and `kernel_coord` is the row and column a
// thread owns.
//
//   cuda_matrix<float> qkv(host_qkv);
//   const auto q = qkv[{row_ndx{0}, col_ndx{0}}, q_extent];
//   ... launch over q ...
//   q.store(host_q).or_throw();
namespace corvid::cuda {
using matrix_types::matrix_axis;
using matrix_types::matrix_extent;

#pragma region cuda_matrix_lens

// Fwd.
template<typename T>
class cuda_matrix_lens;

// A read-only `cuda_matrix_lens`.
template<typename T>
using cuda_matrix_view = cuda_matrix_lens<const T>;

// A non-owning lens over a row-major matrix of `T` in device memory, whose
// rows start `stride()` elements apart.
//
// `cuda_matrix_view<T>` is the read-only form, which a lens converts to. A
// packed lens has a stride equal to its column count. Rows are the first
// index, as in `matrix_lens`.
template<typename T>
class cuda_matrix_lens: public matrix_lens_base<T> {
  using base = matrix_lens_base<T>;
  using base::extent_, base::stride_, base::data_;

public:
#pragma region Types

  using typename base::element_t;
  using typename base::extent_t;
  using value_t = std::remove_const_t<element_t>;

#pragma endregion
#pragma region Construction

  using base::base;

#pragma endregion
#pragma region Accessors

  [[nodiscard]] element_t* get() const noexcept { return data_.data(); }

  [[nodiscard]] const cuda_matrix_lens& as_lens() const noexcept {
    return *this;
  }

#pragma endregion
#pragma region Transfer

  // Upload `host`, which must have the same extent.
  [[nodiscard]] cuda_last_status load(matrix_view<value_t> host) const
  requires(!std::is_const_v<element_t>)
  {
    assert(host.extent() == extent_);
    return copy(get(), stride_, host.as_span().data(), host.stride(),
        memcpy_kind::host_to_device);
  }

  // Copy `device`, which must have the same extent.
  [[nodiscard]] cuda_last_status load(cuda_matrix_view<value_t> device) const
  requires(!std::is_const_v<element_t>)
  {
    assert(device.extent() == extent_);
    return copy(get(), stride_, device.get(), device.stride(),
        memcpy_kind::device_to_device);
  }

  // Download into `host`, which must have the same extent.
  [[nodiscard]] cuda_last_status store(matrix_lens<value_t> host) const {
    assert(host.extent() == extent_);
    return copy(host.as_span().data(), host.stride(), get(), stride_,
        memcpy_kind::device_to_host);
  }

#pragma endregion
#pragma region Helpers
private:
  // Copy this lens's extent of elements from `src` to `dest`, each with its
  // own row stride, as one plain transfer when both are packed and as a
  // pitched transfer otherwise.
  [[nodiscard]] cuda_last_status copy(value_t* dest, size_t dest_stride,
      const value_t* src, size_t src_stride, memcpy_kind kind) const {
    if ((dest_stride == extent_.col_count) &&
        (src_stride == extent_.col_count))
      return cuda_buffer<value_t>::copy(dest, src, this->size(), kind);
    const auto width = extent_.col_count * sizeof(value_t);
    return cuda_last_status{cudaMemcpy2D(dest, dest_stride * sizeof(value_t),
        src, src_stride * sizeof(value_t), width, extent_.row_count,
        static_cast<cudaMemcpyKind>(*kind))};
  }

#pragma endregion
};

#pragma endregion
#pragma region kernel_coord

// The row and column a thread owns in a 2-D launch over a matrix.
//
// Rows run along y and columns along x, so the lanes of a warp take
// consecutive columns of one row.
struct kernel_coord {
  size_t row;
  size_t col;

  __device__ kernel_coord()
      : row{cuda_kernel::y_index<size_t>()},
        col{cuda_kernel::x_index<size_t>()} {}

  // Whether this lies inside `extent`.
  __device__ bool is_within(matrix_extent extent) const {
    return (row < extent.row_count) && (col < extent.col_count);
  }
};

#pragma endregion
#pragma region kernel_col_range

// The columns a thread owns in a one-block-per-row launch, walked by a
// range-for.
//
// The thread starts at its index and steps by the block's width, so the
// block's threads together cover every column, and the lanes of a warp touch
// consecutive columns.
struct kernel_col_range {
  size_t cols;
  size_t first = cuda_kernel::x_thread<size_t>();
  size_t step = cuda_kernel::x_block_dim<size_t>();

  struct sentinel {
    size_t cols;
  };

  struct iterator {
    size_t col;
    size_t step;

    __device__ size_t operator*() const { return col; }
    __device__ iterator& operator++() {
      col += step;
      return *this;
    }
    __device__ bool operator==(sentinel end) const {
      return (col >= end.cols);
    }
  };

  __device__ iterator begin() const { return {first, step}; }
  __device__ sentinel end() const { return {cols}; }
};

#pragma endregion
#pragma region kernel_matrix_lens

// Fwd.
template<typename T>
class kernel_matrix_lens;

// A read-only `kernel_matrix_lens`.
template<typename T>
using kernel_matrix_view = kernel_matrix_lens<const T>;

// A kernel's lens over a row-major matrix of `T` in device memory, whose rows
// start `stride` elements apart.
//
// It is what a `cuda_matrix_lens` becomes at a launch.
template<typename T>
class kernel_matrix_lens {
public:
  using element_t = T;

  kernel_matrix_lens(cuda_matrix_lens<element_t> lens) noexcept
      : data_{lens.get()}, stride_{lens.stride()} {}

  __device__ element_t& operator[](size_t row, size_t col) const {
    return data_[(row * stride_) + col];
  }
  __device__ element_t& operator[](kernel_coord at) const {
    return (*this)[at.row, at.col];
  }

private:
  element_t* data_;
  size_t stride_;
};

#pragma endregion
#pragma region cuda_matrix

// A row-major matrix of `T` in device memory, packed with no gap between rows.
//
// It owns its allocation and carries its extent. Transfers go through its
// `as_lens()` and `as_view()`, and it converts to either where one is
// expected.
template<typename T>
class cuda_matrix {
public:
  using element_t = T;
  using extent_t = matrix_extent;
  using lens_t = cuda_matrix_lens<element_t>;
  using view_t = cuda_matrix_view<element_t>;
  using coord = matrix_types::coord;

#pragma region Construction

  // Allocate `extent` elements, uninitialized, or throw.
  explicit cuda_matrix(extent_t extent)
      : buffer_(extent.row_count * extent.col_count), extent_{extent} {}

  // Allocate and upload `host`, or throw.
  explicit cuda_matrix(matrix_view<element_t> host)
      : cuda_matrix{host.extent()} {
    as_lens().load(host).or_throw();
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] auto* get(this auto&& self) noexcept {
    return self.buffer_.get();
  }

  [[nodiscard]] auto& buffer(this auto&& self) noexcept {
    return self.buffer_;
  }

  [[nodiscard]] extent_t extent() const noexcept { return extent_; }
  [[nodiscard]] size_t row_extent() const noexcept {
    return extent_.row_count;
  }
  [[nodiscard]] size_t col_extent() const noexcept {
    return extent_.col_count;
  }
  [[nodiscard]] size_t size() const noexcept {
    return extent_.row_count * extent_.col_count;
  }

  // The whole matrix as a packed lens, or as a packed view.
  [[nodiscard]] lens_t as_lens() noexcept {
    return {buffer_.as_span(), extent_};
  }
  [[nodiscard]] view_t as_view() const noexcept {
    return {buffer_.as_span(), extent_};
  }
  operator lens_t() noexcept { return as_lens(); }
  operator view_t() const noexcept { return as_view(); }

  // The lens's `slice`, over the whole matrix. Prefer the subscript.
  [[nodiscard]] lens_t
  slice(coord from, extent_t size = extent_t::npos) noexcept {
    return as_lens().slice(from, size);
  }
  [[nodiscard]] view_t
  slice(coord from, extent_t size = extent_t::npos) const noexcept {
    return as_view().slice(from, size);
  }
  [[nodiscard]] lens_t operator[](coord from, extent_t size) noexcept {
    return slice(from, size);
  }
  [[nodiscard]] view_t operator[](coord from, extent_t size) const noexcept {
    return slice(from, size);
  }

#pragma endregion
#pragma region Data members
private:
  cuda_buffer<element_t> buffer_;
  extent_t extent_;

#pragma endregion
};

#pragma endregion
#pragma region Concepts

// The element type of `M`, a device matrix, lens, or view, however `M` itself
// is qualified.
template<typename M>
using device_element_t = std::remove_cvref_t<M>::element_t;

// A device matrix, or a lens over one, whose elements can be written.
//
// That is a `cuda_matrix<T>`, or a `cuda_matrix_lens<T>` for a non-const `T`,
// which leaves out a `cuda_matrix_view`.
template<typename M>
concept DeviceMatrixLike = requires(M& m) {
  requires !std::is_const_v<device_element_t<M>>;
  {
    m.as_lens()
  } -> std::convertible_to<cuda_matrix_lens<device_element_t<M>>>;
};

#pragma endregion

} // namespace corvid::cuda
