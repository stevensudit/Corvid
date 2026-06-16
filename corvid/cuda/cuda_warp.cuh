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
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>

#include <cuda_runtime.h>

#include "./cuda_status.cuh"

// CUDA warp-centered utilities.
//
// https://docs.nvidia.com/cuda/cuda-runtime-api/index.html

namespace corvid::cuda {

class cuda_warp {
public:
  // Lane ID within the warp (0..31).
  __device__ static unsigned lane_id() { return threadIdx.x % warpSize; }

  // Warp ID within the block.
  __device__ static unsigned warp_id() { return threadIdx.x / warpSize; }

  // Mask of active threads in the warp.
  __device__ static uint32_t active_mask() { return __activemask(); }

  // A mask with the bit for every thread in the warp set.
  __host__ __device__ static constexpr uint32_t all_mask = 0xffffffff;

  // Sync threads in the warp.
  __device__ static void sync(uint32_t mask = active_mask()) {
    __syncwarp(static_cast<int>(mask));
  }

  // Return value of `value` from the thread with lane ID `src_lane`.
  template<typename T>
  __device__ static T
  shuffle(T value, int src_lane, uint32_t mask = active_mask()) {
    return __shfl_sync(static_cast<int>(mask), value, src_lane);
  }

  // Return value of `value` from the thread with lane ID `src_lane` + `delta`.
  // If the source lane ID would be outside the warp, the return value is
  // undefined so the caller should either avoid calling

  template<typename T>
  __device__ static T shuffle_up(T value, unsigned delta,
      uint32_t mask = active_mask(), unsigned width = warpSize) {
    return __shfl_up_sync(static_cast<int>(mask), value, delta, width);
  }

  // Return value of `value` from the thread with lane ID `src_lane` - `delta`.
  template<typename T>
  __device__ static T shuffle_down(T value, unsigned delta,
      uint32_t mask = active_mask(), unsigned width = warpSize) {
    return __shfl_down_sync(static_cast<int>(mask), value, delta, width);
  }

  // Return value of `value` from the thread with lane ID `src_lane` XOR
  // `laneMask`.
  template<typename T>
  __device__ static T shuffle_xor(T value, unsigned laneMask,
      uint32_t mask = active_mask(), unsigned width = warpSize) {
    return __shfl_xor_sync(static_cast<int>(mask), value, laneMask, width);
  }
};

} // namespace corvid::cuda
