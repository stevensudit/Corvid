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

#include <cuda_runtime.h>

#include "./cuda_warp.cuh"

// CUDA block-centered utilities.
//
// https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html

namespace corvid::cuda {

// Device-side reductions across the threads of one block, for kernels that
// give a block one row and let its threads share the row's columns.
class cuda_block {
public:
  // The most warps a block can hold, since a block is at most 1024 threads.
  static constexpr auto max_warps = 32U;

  // The sum of `value` over every thread of the block, returned to every one
  // of them.
  //
  // Each warp sums itself with shuffles, lane 0 of each warp parks that
  // partial in shared memory, and warp 0 sums the partials. With 256 threads
  // that is 8 partials, so the second round is one warp sum with lanes 8
  // through 31 contributing zero.
  //
  // Every thread of the block must call it, since it synchronizes the block
  // twice. The block must be one-dimensional, a multiple of `warpSize`, and
  // at most 1024 threads. Two calls in a row are safe, because the second
  // call's partials land in slots the first call has finished reading, and
  // its total is written only after a synchronization every reader of the
  // first total has passed.
  template<typename T>
  __device__ static T sum(T value) {
    __shared__ T partials[max_warps];
    __shared__ T total;
    const auto lane = cuda_warp::lane_id();
    const auto warp = cuda_warp::warp_id();
    const auto warps = blockDim.x / warpSize;

    value = cuda_warp::sum(value);
    if (lane == 0) partials[warp] = value;
    __syncthreads();

    if (warp == 0) {
      value = cuda_warp::sum((lane < warps) ? partials[lane] : T{});
      if (lane == 0) total = value;
    }
    __syncthreads();
    return total;
  }
};

} // namespace corvid::cuda
