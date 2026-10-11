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
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

#include <ATen/ATen.h>

#include "../containers/utils/matrix_view.h"
#include "../cuda/cuda_matrix.cuh"
#include "../math/bfloat16.h"

// Zero-copy adapters between Corvid's matrix lenses and ATen tensors.
//
// A lens borrows into a tensor with `as_tensor`, and a tensor borrows into a
// lens with `as_host_lens` or `as_device_lens` (or their read-only `_view`
// forms). Nothing is copied in either direction. The two share the memory,
// and whichever side owns it must outlive the other. Memory stays where it
// is, so a device lens becomes a CUDA tensor and a CUDA tensor becomes a
// device lens.
//
//   const auto weight = as_tensor(weights.find_matrix<float>("wte.weight"));
//   const auto product =
//       at::matmul(as_tensor(d_a.as_view()), as_tensor(d_b.as_view()));
//   const auto out = as_device_view<float>(product);
//
// Both sides issue their device work on the legacy default stream, so a
// handoff needs no synchronization unless one side moves to a stream of its
// own.

namespace corvid::torch {

using matrix_types::matrix_extent;

#pragma region Element types

// The ATen scalar type whose elements are `T`.
template<typename T>
struct scalar_type_of;

template<>
struct scalar_type_of<float> {
  static constexpr auto value = at::kFloat;
};

template<>
struct scalar_type_of<double> {
  static constexpr auto value = at::kDouble;
};

template<>
struct scalar_type_of<bfloat16_t> {
  static constexpr auto value = at::kBFloat16;
};

template<>
struct scalar_type_of<int64_t> {
  static constexpr auto value = at::kLong;
};

template<>
struct scalar_type_of<int32_t> {
  static constexpr auto value = at::kInt;
};

template<>
struct scalar_type_of<int16_t> {
  static constexpr auto value = at::kShort;
};

template<>
struct scalar_type_of<int8_t> {
  static constexpr auto value = at::kChar;
};

template<>
struct scalar_type_of<uint8_t> {
  static constexpr auto value = at::kByte;
};

template<>
struct scalar_type_of<bool> {
  static constexpr auto value = at::kBool;
};

// An element type that has an ATen scalar type.
template<typename T>
concept TensorElement = requires { scalar_type_of<T>::value; };

template<TensorElement T>
inline constexpr at::ScalarType scalar_type_v = scalar_type_of<T>::value;

// Our storage type and ATen's are both the upper half of an IEEE single, so
// they alias byte for byte.
static_assert(
    sizeof(bfloat16_t) == sizeof(c10::BFloat16) &&
    alignof(bfloat16_t) == alignof(c10::BFloat16) &&
    std::is_trivially_copyable_v<bfloat16_t>);

#pragma endregion
#pragma region Helpers

namespace details {

// The row-major layout of `tensor` in a lens's terms.
struct lens_layout {
  matrix_extent extent;
  size_t stride{};

  // The elements from the first of the first row through the last of the
  // last row.
  [[nodiscard]] size_t footprint() const noexcept {
    if (extent.row_count == 0) return 0;
    return (stride * (extent.row_count - 1)) + extent.col_count;
  }
};

// Whether `tensor` is a two-dimensional tensor of `T` on `device` whose rows
// are contiguous.
template<TensorElement T>
[[nodiscard]] bool
is_matrix(const at::Tensor& tensor, at::DeviceType device) noexcept {
  if (!tensor.defined() || (tensor.dim() != 2) ||
      (tensor.scalar_type() != scalar_type_v<T>) ||
      (tensor.device().type() != device))
    return false;
  // A single-column tensor may carry any inner stride, since no second
  // element is ever reached through it.
  return (tensor.size(1) <= 1) || (tensor.stride(1) == 1);
}

// The layout of `tensor`, which `is_matrix` must have accepted.
[[nodiscard]] inline lens_layout layout_of(const at::Tensor& tensor) noexcept {
  const matrix_extent extent{.row_count = static_cast<size_t>(tensor.size(0)),
      .col_count = static_cast<size_t>(tensor.size(1))};
  // A single-row tensor's outer stride is likewise arbitrary, so a lens over
  // it is packed.
  const auto stride =
      (extent.row_count <= 1)
          ? extent.col_count
          : static_cast<size_t>(tensor.stride(0));
  return {.extent = extent, .stride = stride};
}

// `tensor` as a lens of `Lens`'s kind, or an empty lens unless it is a
// two-dimensional tensor of the lens's element type on `device` whose rows
// are contiguous.
template<typename Lens>
[[nodiscard]] Lens
borrow_tensor(const at::Tensor& tensor, at::DeviceType device) noexcept {
  using element_t = Lens::element_t;
  using value_t = std::remove_const_t<element_t>;
  if (!is_matrix<value_t>(tensor, device)) return {};
  const auto layout = layout_of(tensor);
  // ATen types its data pointer only for its own element types, so the typed
  // pointer comes from the untyped one.
  element_t* data;
  if constexpr (std::is_const_v<element_t>)
    data = static_cast<element_t*>(tensor.const_data_ptr());
  else
    data = static_cast<element_t*>(tensor.data_ptr());
  return {std::span<element_t>(data, layout.footprint()), layout.extent,
      layout.stride};
}

// `lens` borrowed as a tensor on `device`, without a copy.
template<typename Lens>
[[nodiscard]] at::Tensor borrow_lens(const Lens& lens, at::DeviceType device) {
  using value_t = std::remove_const_t<typename Lens::element_t>;
  const std::array<int64_t, 2> sizes{static_cast<int64_t>(lens.row_extent()),
      static_cast<int64_t>(lens.col_extent())};
  const auto options =
      at::TensorOptions{}.dtype(scalar_type_v<value_t>).device(device);
  if (lens.empty()) return at::empty(sizes, options);
  const std::array<int64_t, 2> strides{static_cast<int64_t>(lens.stride()), 1};
  // `from_blob` takes a mutable pointer whatever the caller does with it, so
  // a view's read-only promise ends here.
  return at::from_blob(const_cast<value_t*>(lens.as_span().data()), sizes,
      strides, options);
}

} // namespace details

#pragma endregion
#pragma region Lens to tensor

// `host` borrowed as a tensor of the same shape and strides, without a copy.
//
// The tensor reads and writes the lens's memory, which must outlive it. A
// view borrows the same way, and the tensor is writable regardless.
template<typename T>
requires TensorElement<std::remove_const_t<T>>
[[nodiscard]] at::Tensor as_tensor(matrix_lens<T> host) {
  return details::borrow_lens(host, at::kCPU);
}

// `device` borrowed as a CUDA tensor of the same shape and strides, without
// a copy.
//
// The tensor reads and writes the lens's memory, which must outlive it. A
// view borrows the same way, and the tensor is writable regardless.
template<typename T>
requires TensorElement<std::remove_const_t<T>>
[[nodiscard]] at::Tensor as_tensor(cuda::cuda_matrix_lens<T> device) {
  return details::borrow_lens(device, at::kCUDA);
}

#pragma endregion
#pragma region Tensor to lens

// `tensor` as a lens over its memory, or an empty lens unless it is a
// two-dimensional tensor of `T` on the host whose rows are contiguous.
//
// The lens reads and writes the tensor's memory, which must outlive it.
template<TensorElement T>
[[nodiscard]] matrix_lens<T> as_host_lens(const at::Tensor& tensor) noexcept {
  return details::borrow_tensor<matrix_lens<T>>(tensor, at::kCPU);
}

// `tensor` as a view over its memory, or an empty view unless it is a
// two-dimensional tensor of `T` on the host whose rows are contiguous.
//
// The tensor's memory must outlive the view.
template<TensorElement T>
[[nodiscard]] matrix_view<T> as_host_view(const at::Tensor& tensor) noexcept {
  return details::borrow_tensor<matrix_view<T>>(tensor, at::kCPU);
}

// `tensor` as a device lens over its memory, or an empty lens unless it is
// a two-dimensional CUDA tensor of `T` whose rows are contiguous.
//
// The lens reads and writes the tensor's memory, which must outlive it.
template<TensorElement T>
[[nodiscard]] cuda::cuda_matrix_lens<T>
as_device_lens(const at::Tensor& tensor) noexcept {
  return details::borrow_tensor<cuda::cuda_matrix_lens<T>>(tensor, at::kCUDA);
}

// `tensor` as a device view over its memory, or an empty view unless it is
// a two-dimensional CUDA tensor of `T` whose rows are contiguous.
//
// The tensor's memory must outlive the view.
template<TensorElement T>
[[nodiscard]] cuda::cuda_matrix_view<T>
as_device_view(const at::Tensor& tensor) noexcept {
  return details::borrow_tensor<cuda::cuda_matrix_view<T>>(tensor, at::kCUDA);
}

#pragma endregion

} // namespace corvid::torch
