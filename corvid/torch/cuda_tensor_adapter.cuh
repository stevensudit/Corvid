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
#include <type_traits>

#include "../cuda/cuda_matrix.cuh"
#include "tensor_adapter.h"

// Zero-copy adapters between Corvid's device lenses and CUDA tensors, the
// device half of "tensor_adapter.h".
//
// A device lens becomes a CUDA tensor over the same memory with `as_tensor`,
// and a CUDA tensor becomes a device lens with `as_device_lens` (or its
// read-only `as_device_view`), under the host half's rules.
//
//   const auto product =
//       at::matmul(as_tensor(d_a.as_view()), as_tensor(d_b.as_view()));
//   const auto out = as_device_view<float>(product);
//
// Both sides issue their device work on the legacy default stream, so a
// handoff needs no synchronization unless one side moves to a stream of its
// own.

namespace corvid::torch {

#pragma region Lens to tensor

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

#pragma endregion
#pragma region Tensor to lens

// `tensor` as a device lens over its memory, or an empty lens unless it is
// a one- or two-dimensional CUDA tensor of `T` whose rows hold their
// elements adjacently (the rows may lie any stride apart, and a
// one-dimensional tensor is a single row).
//
// The lens reads and writes the tensor's memory, which must outlive it, so
// only a named tensor converts.
template<TensorElement T>
[[nodiscard]] cuda::cuda_matrix_lens<T>
as_device_lens(Tensor& tensor) noexcept {
  return details::lens_of<cuda::cuda_matrix_lens<T>>(tensor, DeviceType::CUDA);
}

// `tensor` as a device view over its memory, or an empty view unless it is
// a one- or two-dimensional CUDA tensor of `T` whose rows hold their
// elements adjacently (the rows may lie any stride apart, and a
// one-dimensional tensor is a single row).
//
// The tensor's memory must outlive the view.
template<TensorElement T>
[[nodiscard]] cuda::cuda_matrix_view<T>
as_device_view(const Tensor& tensor) noexcept {
  return details::lens_of<cuda::cuda_matrix_view<T>>(tensor, DeviceType::CUDA);
}

#pragma endregion

} // namespace corvid::torch
