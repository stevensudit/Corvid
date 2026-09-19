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
#include <type_traits>

#include <cuda_runtime.h>

#include "../containers/utils/matrix_view.h"
#include "./cuda_buffer.cuh"
#include "./cuda_status.cuh"

// Matrices in device memory.
//
// `cuda_matrix<T>` owns a packed row-major matrix, and `cuda_matrix_view<T>`
// is a non-owning window onto one, possibly strided.
//
// A view derived from a column block of a wider matrix carries the wider
// matrix's stride.
//
//   cuda_matrix<float> qkv(host_qkv);
//   const auto q = qkv.view().subview({row_ndx{0}, col_ndx{0}}, q_extent);
//   ... launch over q ...
//   q.store(host_q).or_throw();
namespace corvid::cuda {
using matrix_types::matrix_extent;

#pragma region cuda_matrix_view

// A non-owning view of a row-major matrix of `T` in device memory, whose
// rows start `stride()` elements apart.
//
// `cuda_matrix_view<const T>` is the read-only form, which a mutable view
// converts to. A packed view has a stride equal to its column count. Rows
// are the first index, as in `matrix_view`.
template<typename T>
class cuda_matrix_view: public matrix_view_base<T> {
  using base = matrix_view_base<T>;
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

#pragma endregion
#pragma region Transfer

  // Upload `host`, which must have the same extent.
  [[nodiscard]] cuda_last_status load(matrix_view<const value_t> host) const
  requires(!std::is_const_v<element_t>)
  {
    assert(is_same_extent(host.extent()));
    return copy(get(), stride_, host.as_span().data(), host.stride(),
        memcpy_kind::host_to_device);
  }

  // Copy `device`, another view, which must have the same extent.
  [[nodiscard]] cuda_last_status
  load(cuda_matrix_view<const value_t> device) const
  requires(!std::is_const_v<element_t>)
  {
    assert(is_same_extent(device.extent()));
    return copy(get(), stride_, device.get(), device.stride(),
        memcpy_kind::device_to_device);
  }

  // Download into `host`, which must have the same extent.
  [[nodiscard]] cuda_last_status store(matrix_view<value_t> host) const {
    assert(is_same_extent(host.extent()));
    return copy(host.as_span().data(), host.stride(), get(), stride_,
        memcpy_kind::device_to_host);
  }

#pragma endregion
#pragma region Helpers
private:
  [[nodiscard]] bool is_same_extent(extent_t other) const noexcept {
    return (other.row_count == extent_.row_count) &&
           (other.col_count == extent_.col_count);
  }

  // Copy this view's extent of elements from `src` to `dest`, each with its
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
#pragma region cuda_matrix

// A row-major matrix of `T` in device memory, packed with no gap between rows.
//
// It owns its allocation and carries its extent. Transfers and ops go
// through its `view()`, and it converts to a view where one is expected.
template<typename T>
class cuda_matrix {
public:
  using element_t = T;
  using extent_t = matrix_extent;
  using view_t = cuda_matrix_view<element_t>;
  using const_view_t = cuda_matrix_view<const element_t>;
  using coord = matrix_types::coord;

#pragma region Construction

  // Allocate `extent` elements, uninitialized, or throw.
  explicit cuda_matrix(extent_t extent)
      : buffer_(extent.row_count * extent.col_count), extent_{extent} {}

  // Allocate and upload `host`, or throw.
  explicit cuda_matrix(matrix_view<const element_t> host)
      : cuda_matrix{host.extent()} {
    view().load(host).or_throw();
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

  // The whole matrix as a packed view.
  [[nodiscard]] view_t view() noexcept { return {buffer_.as_span(), extent_}; }
  [[nodiscard]] const_view_t view() const noexcept {
    return {buffer_.as_span(), extent_};
  }
  operator view_t() noexcept { return view(); }
  operator const_view_t() const noexcept { return view(); }

  // The view's `subview`, over the whole matrix.
  [[nodiscard]] view_t
  subview(coord from, extent_t size = extent_t::npos) noexcept {
    return view().subview(from, size);
  }
  [[nodiscard]] const_view_t
  subview(coord from, extent_t size = extent_t::npos) const noexcept {
    return view().subview(from, size);
  }

#pragma endregion
#pragma region Data members
private:
  cuda_buffer<element_t> buffer_;
  extent_t extent_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
