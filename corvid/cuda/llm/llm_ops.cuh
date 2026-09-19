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

#include <cuda_runtime.h>

#include "../../llm/llm_ops.h"
#include "../cuda_kernel.cuh"
#include "../cuda_matrix.cuh"
#include "../cuda_status.cuh"
#include "../linalg/linear_algebra.cuh"

// The transformer ops on the device, in fp32, one free function per op.
//
// Every op writes into a caller-owned `cuda_matrix`, launches on the default
// stream, and returns whether its launches were accepted, so a fault inside a
// kernel surfaces at the next synchronizing call, such as a `store`. The
// scalar formulas are the CPU ops' own, shared through `CUDA_HOST_DEVICE`.
namespace corvid::cuda::llm {

using namespace corvid::cuda::linalg;

#pragma region gelu_new

namespace details {

// Apply the scalar `gelu_new` to `size` elements, one thread per element.
template<Floating T>
__global__ void apply_gelu_new(T* out, const T* in, size_t size) {
  const auto i = cuda_kernel::x_index<size_t>();
  if (i < size) out[i] = corvid::llm::gelu_new(in[i]);
}

} // namespace details

// Apply `gelu_new` to every element of `in`, into `out`.
//
// `out` and `in` must have the same extent. `out` can be `in`, applying it in
// place. Returns false when the launch is refused, leaving `out` unspecified.
template<Floating T>
[[nodiscard]] bool gelu_new(cuda_matrix<T>& out, const cuda_matrix<T>& in) {
  assert((out.row_extent() == in.row_extent()) &&
         (out.col_extent() == in.col_extent()));

  details::apply_gelu_new<T><<<blocks_for(out.size()), threads_per_block>>>(
      out.get(), in.get(), out.size());
  return cuda_last_status{}.ok();
}

#pragma endregion

} // namespace corvid::cuda::llm
