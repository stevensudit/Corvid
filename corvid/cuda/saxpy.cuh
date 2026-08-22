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

// Trivial CUDA helper for the corvid/cuda/ subproject. Computes the
// single-precision `a*x + y` (SAXPY) of one value; callable from host or
// device.
#pragma once

#include <cuda_runtime.h>

namespace corvid::cuda {

__host__ __device__ inline float saxpy(float a, float x, float y) {
  return (a * x) + y;
}

} // namespace corvid::cuda
