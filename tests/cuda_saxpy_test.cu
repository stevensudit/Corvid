// Smoke test for the corvid/cuda/ subproject and the CUDA build path: launches
// a trivial kernel that calls corvid::cuda::saxpy on the device and checks
// that
// the result round-trips back to the host. Doubles as the canary that the CUDA
// toolchain (nvcc + g++-15) and the .cu Catch2 wiring work end to end.

#include <cuda_runtime.h>

#include "../corvid/cuda/saxpy.cuh"
#include "catch2_main.h"

namespace {

__global__ void saxpy_kernel(float a, float x, float y, float* out) {
  *out = corvid::cuda::saxpy(a, x, y);
}

} // namespace

TEST_CASE("cuda saxpy kernel runs on the device", "[cuda]") {
  float* d_out = nullptr;
  REQUIRE(cudaMalloc(&d_out, sizeof(float)) == cudaSuccess);

  saxpy_kernel<<<1, 1>>>(2.0F, 3.0F, 4.0F, d_out);

  float h_out = 0.0F;
  REQUIRE(cudaMemcpy(&h_out, d_out, sizeof(float), cudaMemcpyDeviceToHost) ==
          cudaSuccess);
  REQUIRE(cudaFree(d_out) == cudaSuccess);
  REQUIRE(cudaGetLastError() == cudaSuccess);

  // 2*3 + 4 == 10, exactly representable in float.
  CHECK(h_out == 10.0F);
}
