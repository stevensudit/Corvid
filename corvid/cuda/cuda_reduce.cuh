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

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>

#include <cuda_runtime.h>

#include "./cuda_block.cuh"
#include "./cuda_warp.cuh"

// CUDA reductions across the lanes of a warp and the threads of a block.

namespace corvid::cuda {

// Device-side reductions, for kernels that give a block one row and let its
// threads share the row's columns.
//
// Each thread brings one value, and every thread gets the result back. What a
// value stands for is the caller's business. A thread with nothing to
// contribute brings the identity, zero for a sum and the lowest `T` for a
// max, and still takes part, since every reduction here synchronizes the
// threads it runs over.
class cuda_reduce {
public:
  // The sum of `value` over the lanes of `mask`, returned to every one of
  // them.
  //
  // Every lane in `mask` must call it, so the default is the whole warp
  // rather than the active lanes.
  template<typename T>
  __device__ static T warp_sum(T value, uint32_t mask = cuda_warp::all_mask) {
    return warp_reduce(value, std::plus<>{}, mask);
  }

  // The largest `value` over the lanes of `mask`, returned to every one of
  // them.
  //
  // Every lane in `mask` must call it, so the default is the whole warp
  // rather than the active lanes.
  template<typename T>
  __device__ static T warp_max(T value, uint32_t mask = cuda_warp::all_mask) {
    return warp_reduce(value, std::ranges::max, mask);
  }

  // The sum of `value` over every thread of the block, returned to every one
  // of them.
  //
  // Every thread of the block must call it. The block must be one-dimensional
  // and a multiple of `warpSize`.
  template<typename T>
  __device__ static T block_sum(T value) {
    return block_reduce(value, std::plus<>{}, T{});
  }

  // The largest `value` over every thread of the block, returned to every one
  // of them.
  //
  // Every thread of the block must call it. The block must be one-dimensional
  // and a multiple of `warpSize`.
  template<typename T>
  __device__ static T block_max(T value) {
    return block_reduce(value, std::ranges::max,
        std::numeric_limits<T>::lowest());
  }

private:
  // Fold `value` through `op` over the lanes of `mask`, returned to every one
  // of them.
  //
  // It is a butterfly of `shuffle_xor`, so with 32 lanes it takes 5 rounds
  // (16, 8, 4, 2, 1), each round pairing every lane with a partner that
  // already holds the fold of a disjoint half, and every lane ends with the
  // total.
  template<typename T, typename Op>
  __device__ static T warp_reduce(T value, Op op, uint32_t mask) {
    const auto lanes = static_cast<unsigned>(warpSize);
    for (auto offset = lanes / 2; offset > 0; offset /= 2)
      value = op(value, cuda_warp::shuffle_xor(value, offset, mask));

    return value;
  }

  // Fold `value` through `op` over every thread of the block, returned to
  // every one of them, with `identity` standing in where a warp has no
  // partial to offer.
  //
  // Each warp folds itself with shuffles, lane 0 of each warp parks that
  // partial in shared memory, and warp 0 folds the partials. With 256
  // threads, that is 8 partials, so the second round is one warp fold with
  // lanes 8 through 31 contributing `identity`.
  //
  // It synchronizes the block twice. Two calls in a row are safe because the
  // second call's partials land in slots the first call has finished reading,
  // and its total is written only after a synchronization every reader of
  // the first total has passed.
  template<typename T, typename Op>
  __device__ static T block_reduce(T value, Op op, T identity) {
    __shared__ T partials[cuda_warp::max_warps_per_block];
    __shared__ T total;
    const auto lane = cuda_warp::lane_id();
    const auto warp = cuda_warp::warp_id();
    const auto warps = cuda_warp::warps_in_block();

    value = warp_reduce(value, op, cuda_warp::all_mask);
    if (lane == 0) partials[warp] = value;
    cuda_block::sync();

    if (warp == 0) {
      value = warp_reduce((lane < warps) ? partials[lane] : identity, op,
          cuda_warp::all_mask);
      if (lane == 0) total = value;
    }
    cuda_block::sync();
    return total;
  }
};

} // namespace corvid::cuda
