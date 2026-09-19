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

#include <cstdint>

#include <cuda_runtime.h>

// CUDA warp-centered utilities.
//
// https://docs.nvidia.com/cuda/cuda-runtime-api/index.html

namespace corvid::cuda {

// Device-side warp helpers.
//
// Contains the calling thread's lane and warp, the active mask, and typed
// wrappers over the `_sync` shuffle and sync intrinsics that default the mask
// to the active lanes.
class cuda_warp {
public:
  // Lane ID within the warp (0..31).
  __device__ static unsigned lane_id() { return threadIdx.x % warpSize; }

  // Warp ID within the block.
  __device__ static unsigned warp_id() { return threadIdx.x / warpSize; }

  // Mask of active threads in the warp.
  __device__ static uint32_t active_mask() { return __activemask(); }

  // A mask with the bit for every thread in the warp set.
  static constexpr uint32_t all_mask = 0xffffffff;

  // Sync threads in the warp.
  __device__ static void sync(uint32_t mask = active_mask()) {
    __syncwarp(mask);
  }

  // Return value of `value` from the thread with lane ID `src_lane`.
  template<typename T>
  __device__ static T
  shuffle(T value, int src_lane, uint32_t mask = active_mask()) {
    return __shfl_sync(mask, value, src_lane);
  }

  // Return value of `value` from the thread with current lane ID - `delta`.
  template<typename T>
  __device__ static T shuffle_up(T value, unsigned delta,
      uint32_t mask = active_mask(), unsigned width = warpSize) {
    return __shfl_up_sync(mask, value, delta, width);
  }

  // Return value of `value` from the thread with current lane ID + `delta`.
  template<typename T>
  __device__ static T shuffle_down(T value, unsigned delta,
      uint32_t mask = active_mask(), unsigned width = warpSize) {
    return __shfl_down_sync(mask, value, delta, width);
  }

  // Return value of `value` from the thread with current lane ID XOR
  // `lane_mask`.
  template<typename T>
  __device__ static T shuffle_xor(T value, unsigned lane_mask,
      uint32_t mask = active_mask(), unsigned width = warpSize) {
    return __shfl_xor_sync(mask, value, lane_mask, width);
  }

  // The sum of `value` over the lanes of `mask`, returned to every one of
  // them.
  //
  // Every lane in `mask` must call it, so the default is the whole warp
  // rather than the active lanes. It is a butterfly of `shuffle_xor`, so with
  // 32 lanes it takes 5 rounds (16, 8, 4, 2, 1) and each round pairs every
  // lane with a partner that already holds the sum of a disjoint half.
  template<typename T>
  __device__ static T sum(T value, uint32_t mask = all_mask) {
    const auto warps = static_cast<unsigned>(warpSize);
    for (auto offset = warps / 2; offset > 0; offset /= 2)
      value += shuffle_xor(value, offset, mask);

    return value;
  }
};

} // namespace corvid::cuda
