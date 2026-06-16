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

// CUDA kernel-centered utilities.
//
// https://docs.nvidia.com/cuda/cuda-runtime-api/index.html

namespace corvid::cuda {

class cuda_kernel {
public:
  // TODO: Use an instance for methods that call a lambda.
  __device__ cuda_kernel(unsigned n) : n_(n) {}

  // Thread in each dimension.
  template<typename T = int>
  __device__ static T x_thread() {
    return static_cast<T>(threadIdx.x);
  }
  template<typename T = int>
  __device__ static T y_thread() {
    return static_cast<T>(threadIdx.y);
  }
  template<typename T = int>
  __device__ static T z_thread() {
    return static_cast<T>(threadIdx.z);
  }

  // Block dim in each dimension.
  template<typename T = int>
  __device__ static T x_block_dim() {
    return static_cast<T>(blockDim.x);
  }
  template<typename T = int>
  __device__ static T y_block_dim() {
    return static_cast<T>(blockDim.y);
  }
  template<typename T = int>
  __device__ static T z_block_dim() {
    return static_cast<T>(blockDim.z);
  }

  // Index in each dimension.

  template<typename T = int>
  __device__ static T x_index() {
    return static_cast<T>((blockIdx.x * blockDim.x) + threadIdx.x);
  }
  template<typename T = int>
  __device__ static T y_index() {
    return static_cast<T>((blockIdx.y * blockDim.y) + threadIdx.y);
  }
  template<typename T = int>
  __device__ static T z_index() {
    return static_cast<T>((blockIdx.z * blockDim.z) + threadIdx.z);
  }
  template<typename T = int>
  __device__ static T index() {
    return (z_index<T>() * y_stride() * x_stride()) +
           (y_index<T>() * x_stride()) + x_index<T>();
  }

  // Stride in each dimension.

  template<typename T = int>
  __device__ static T x_stride() {
    return static_cast<T>(gridDim.x * blockDim.x);
  }
  template<typename T = int>
  __device__ static T y_stride() {
    return static_cast<T>(gridDim.y * blockDim.y);
  }
  template<typename T = int>
  __device__ static T z_stride() {
    return static_cast<T>(gridDim.z * blockDim.z);
  }
  template<typename T = int>
  __device__ static T stride() {
    return x_stride<T>() * y_stride<T>() * z_stride<T>();
  }

  // Launch size. For `n` elements and `threads_per_block` threads per block,
  // how many blocks are needed (rounded up)?
  __host__ __device__ static unsigned
  blocks_for_threads(unsigned n, unsigned threads_per_block) {
    return ceil_div(n, threads_per_block);
  }

  // Divide `a` by `b`, rounding up.
  __host__ __device__ static unsigned ceil_div(unsigned a, unsigned b) {
    return (a + b - 1) / b;
  }

private:
  unsigned n_;
};

} // namespace corvid::cuda
