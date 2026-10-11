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

// CUDA block-centered utilities.
//
// https://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html

namespace corvid::cuda {

// Device-side block helpers.
class cuda_block {
public:
  // Wait until every thread of the block has arrived, and make their earlier
  // writes to shared and global memory visible to each other.
  //
  // Every thread of the block must reach it, so it cannot sit in a branch
  // that only some threads take.
  __device__ static void sync() { __syncthreads(); }
};

} // namespace corvid::cuda
