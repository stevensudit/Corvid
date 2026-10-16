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
// A lens becomes a tensor over the same memory with `as_tensor`, and a tensor
// becomes a lens over its memory with `as_host_lens` or `as_device_lens` (or
// their read-only `_view` forms). Nothing is copied and nothing changes
// hands. The two share the memory, and whichever side owns it must outlive
// the other. Memory stays where it is, so a device lens becomes a CUDA tensor
// and a CUDA tensor becomes a device lens.
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

using at::Tensor;
using at::TensorOptions;
using c10::DeviceType;
using c10::ScalarType;
using matrix_types::matrix_extent;
using matrix_types::matrix_layout;

#pragma region Element types

// The ATen scalar type whose elements are `T`.
template<typename T>
struct scalar_type_of;

template<>
struct scalar_type_of<float> {
  static constexpr auto value = ScalarType::Float;
};
template<>
struct scalar_type_of<double> {
  static constexpr auto value = ScalarType::Double;
};
template<>
struct scalar_type_of<bfloat16_t> {
  static constexpr auto value = ScalarType::BFloat16;
};
template<>
struct scalar_type_of<int64_t> {
  static constexpr auto value = ScalarType::Long;
};
template<>
struct scalar_type_of<int32_t> {
  static constexpr auto value = ScalarType::Int;
};
template<>
struct scalar_type_of<int16_t> {
  static constexpr auto value = ScalarType::Short;
};
template<>
struct scalar_type_of<int8_t> {
  static constexpr auto value = ScalarType::Char;
};
template<>
struct scalar_type_of<uint8_t> {
  static constexpr auto value = ScalarType::Byte;
};
template<>
struct scalar_type_of<bool> {
  static constexpr auto value = ScalarType::Bool;
};

// An element type that has an ATen scalar type.
template<typename T>
concept TensorElement = requires { scalar_type_of<T>::value; };

template<TensorElement T>
inline constexpr ScalarType scalar_type_v = scalar_type_of<T>::value;

// Our storage type and ATen's are both the upper half of an IEEE single, so
// they alias byte for byte.
static_assert(
    sizeof(bfloat16_t) == sizeof(c10::BFloat16) &&
    alignof(bfloat16_t) == alignof(c10::BFloat16) &&
    std::is_trivially_copyable_v<bfloat16_t>);

#pragma endregion
#pragma region Helpers

namespace details {

// The layout of `tensor` as a lens, or an empty layout unless it is a
// two-dimensional tensor of `T` on `device` whose rows hold their elements
// adjacently.
//
// The rows themselves may lie any stride apart, which the layout carries.
// Only the inner stride must be one, since a lens has no stride within a
// row, and a single-column tensor may carry any, since no second element is
// ever reached through it. A single-row tensor's outer stride is likewise
// arbitrary, so its layout is packed.
template<TensorElement T>
[[nodiscard]] matrix_layout
layout_of(const Tensor& tensor, DeviceType device) noexcept {
  if (!tensor.defined() || (tensor.dim() != 2) ||
      (tensor.scalar_type() != scalar_type_v<T>) ||
      (tensor.device().type() != device) ||
      ((tensor.size(1) > 1) && (tensor.stride(1) != 1)))
    return {};
  const matrix_extent extent{.row_count = static_cast<size_t>(tensor.size(0)),
      .col_count = static_cast<size_t>(tensor.size(1))};
  const auto stride =
      (extent.row_count <= 1)
          ? extent.col_count
          : static_cast<size_t>(tensor.stride(0));
  return {.extent = extent, .stride = stride};
}

// `tensor` as a lens of `Lens`'s kind over its memory, or an empty lens
// unless `layout_of` accepts it for the lens's element type on `device`.
template<typename Lens>
[[nodiscard]] Lens lens_of(const Tensor& tensor, DeviceType device) noexcept {
  using element_t = Lens::element_t;
  using value_t = std::remove_const_t<element_t>;
  const auto layout = layout_of<value_t>(tensor, device);
  if (layout.empty()) return {};
  // A read-only lens asks for the const pointer, which leaves copy-on-write
  // storage shared, while a writable one asks for the mutable pointer, which
  // makes the storage its own first. ATen types neither for our element
  // types, so the typed pointer comes from the untyped one.
  element_t* data;
  if constexpr (std::is_const_v<element_t>)
    data = static_cast<element_t*>(tensor.const_data_ptr());
  else
    data = static_cast<element_t*>(tensor.mutable_data_ptr());
  return {std::span<element_t>(data, layout.footprint()), layout};
}

// `lens` as a tensor on `device` over its memory.
template<typename Lens>
[[nodiscard]] Tensor tensor_of(const Lens& lens, DeviceType device) {
  using value_t = std::remove_const_t<typename Lens::element_t>;
  const std::array<int64_t, 2> sizes{static_cast<int64_t>(lens.row_extent()),
      static_cast<int64_t>(lens.col_extent())};
  const auto options =
      TensorOptions{}.dtype(scalar_type_v<value_t>).device(device);
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

// `host_matrix` as a tensor of the same shape and strides over its memory.
//
// The tensor reads and writes the lens's memory, which must outlive it. A
// view converts the same way, and the tensor is writable regardless.
template<typename T>
requires TensorElement<std::remove_const_t<T>>
[[nodiscard]] Tensor as_tensor(matrix_lens<T> host_matrix) {
  return details::tensor_of(host_matrix, DeviceType::CPU);
}

// `device_matrix` as a CUDA tensor of the same shape and strides over its
// memory.
//
// The tensor reads and writes the lens's memory, which must outlive it. A
// view converts the same way, and the tensor is writable regardless.
template<typename T>
requires TensorElement<std::remove_const_t<T>>
[[nodiscard]] Tensor as_tensor(cuda::cuda_matrix_lens<T> device_matrix) {
  return details::tensor_of(device_matrix, DeviceType::CUDA);
}

// `host_vector` as a one-dimensional tensor over its memory.
//
// The tensor reads and writes the span's memory, which must outlive it. A
// span of const elements converts the same way, and the tensor is writable
// regardless.
template<typename T>
requires TensorElement<std::remove_const_t<T>>
[[nodiscard]] Tensor as_tensor(std::span<T> host_vector) {
  using value_t = std::remove_const_t<T>;
  const std::array<int64_t, 1> sizes{static_cast<int64_t>(host_vector.size())};
  const auto options =
      TensorOptions{}.dtype(scalar_type_v<value_t>).device(DeviceType::CPU);
  if (host_vector.empty()) return at::empty(sizes, options);
  return at::from_blob(const_cast<value_t*>(host_vector.data()), sizes,
      options);
}

#pragma endregion
#pragma region Tensor to lens

// `tensor` as a lens over its memory, or an empty lens unless it is a
// two-dimensional tensor of `T` on the host whose rows hold their elements
// adjacently (the rows may lie any stride apart).
//
// The lens reads and writes the tensor's memory, which must outlive it, so
// only a named tensor converts.
template<TensorElement T>
[[nodiscard]] matrix_lens<T> as_host_lens(Tensor& tensor) noexcept {
  return details::lens_of<matrix_lens<T>>(tensor, DeviceType::CPU);
}

// `tensor` as a view over its memory, or an empty view unless it is a
// two-dimensional tensor of `T` on the host whose rows hold their elements
// adjacently (the rows may lie any stride apart).
//
// The tensor's memory must outlive the view.
template<TensorElement T>
[[nodiscard]] matrix_view<T> as_host_view(const Tensor& tensor) noexcept {
  return details::lens_of<matrix_view<T>>(tensor, DeviceType::CPU);
}

// `tensor` as a device lens over its memory, or an empty lens unless it is
// a two-dimensional CUDA tensor of `T` whose rows hold their elements
// adjacently (the rows may lie any stride apart).
//
// The lens reads and writes the tensor's memory, which must outlive it, so
// only a named tensor converts.
template<TensorElement T>
[[nodiscard]] cuda::cuda_matrix_lens<T>
as_device_lens(Tensor& tensor) noexcept {
  return details::lens_of<cuda::cuda_matrix_lens<T>>(tensor, DeviceType::CUDA);
}

// `tensor` as a device view over its memory, or an empty view unless it is
// a two-dimensional CUDA tensor of `T` whose rows hold their elements
// adjacently (the rows may lie any stride apart).
//
// The tensor's memory must outlive the view.
template<TensorElement T>
[[nodiscard]] cuda::cuda_matrix_view<T>
as_device_view(const Tensor& tensor) noexcept {
  return details::lens_of<cuda::cuda_matrix_view<T>>(tensor, DeviceType::CUDA);
}

#pragma endregion

} // namespace corvid::torch
