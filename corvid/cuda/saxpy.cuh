// Trivial CUDA helper for the corvid/cuda/ subproject. Computes the
// single-precision
// a*x + y (SAXPY) of one value; callable from host or device. This is a
// placeholder that exercises the CUDA build path until real code lands here.

#pragma once

#include <cuda_runtime.h>

namespace corvid::cuda {

__host__ __device__ inline float saxpy(float a, float x, float y) {
  return (a * x) + y;
}

} // namespace corvid::cuda
