// Smoke test for the corvid/cuda/ subproject and the CUDA build path: launches
// a trivial kernel that calls corvid::cuda::saxpy on the device and checks
// that
// the result round-trips back to the host. Doubles as the canary that the CUDA
// toolchain (nvcc + g++-15) and the .cu Catch2 wiring work end to end.

#include <cuda_runtime.h>

#include "../corvid/cuda/saxpy.cuh"
#include "../corvid/cuda/cuda_ptr.cuh"
#include "../corvid/cuda/cuda_status.cuh"
#include "catch2_main.h"

namespace {

// The kernel function is the body of one thread, which the runtime will
// execute on the device in parallel. `__global__` identifies a kernel function
// that can be launched from the host.
__global__ void saxpy_kernel(float a, float x, float y, float* out) {
  *out = corvid::cuda::saxpy(a, x, y);
}

} // namespace

using namespace corvid::cuda;

TEST_CASE("cuda saxpy kernel runs on the device", "[cuda]") {
  float h_out = 0.0F;
  if (cuda_ptr<float> d_out; true) {
    REQUIRE(d_out.ok());
    saxpy_kernel<<<1, 1>>>(2.0F, 3.0F, 4.0F, d_out.device_ptr());
    REQUIRE(d_out.store(h_out));
  }
  REQUIRE(cuda_last_status{}.ok());

  // 2*3 + 4 == 10, exactly representable in float.
  CHECK(h_out == 10.0F);
}
