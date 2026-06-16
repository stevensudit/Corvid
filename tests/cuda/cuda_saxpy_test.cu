// Smoke test for the corvid/cuda/ subproject and the CUDA build path: launches
// a trivial kernel that calls corvid::cuda::saxpy on the device and checks
// that
// the result round-trips back to the host. Doubles as the canary that the CUDA
// toolchain (nvcc + g++-15) and the .cu Catch2 wiring work end to end.

#include <cuda_runtime.h>

#include "corvid/cuda/saxpy.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "catch2_main.h"

namespace {

// This is a global function, which CUDA refers to as a kernel, for reasons.
// It's invoked with `gridDim`, `blockDim`, `sharedMem`, and `stream`.
//
// A thread is one execution of the kernel body.

// A (thread) block is a group of threads that can synchronize with each other
// (via `__syncthreads`) and share memory.
//
// A grid is a group of blocks.
//
// The `blockDim` and `gridDim` are `dim3`, meaning they're up to 3D, so a bare
// `1` maps to `(1,1,1)`.
//
// `sharedMem` is the amount of shared memory to allocate for the block.
//
// `stream` is the stream to launch the kernel on; `0` means the default
// stream, which is sequential and synchronous with the host.
__global__ void saxpy_kernel(float a, float x, float y, float* out) {
  *out = corvid::cuda::saxpy(a, x, y);
}

} // namespace

using namespace corvid::cuda;

TEST_CASE("cuda saxpy kernel runs on the device", "[cuda]") {
  float h_out = 0.0F;
  if (cuda_ptr<float> d_out; true) {
    REQUIRE(d_out.ok());
    // Runs a single grid of a single block of a single thread.
    saxpy_kernel<<<1, 1>>>(2.0F, 3.0F, 4.0F, d_out.device_ptr());
    // Because we're on the default stream, the copy from device to host
    // blocks.
    REQUIRE(d_out.store(h_out));
  }
  // We have to check here, not when we invoke the kernel, because CUDA kernel
  // launches are asynchronous and return before the kernel has actually
  // executed. The error is recorded and
  // can be checked explicitly.
  REQUIRE(cuda_last_status{}.ok());

  // 2*3 + 4 == 10, exactly representable in float.
  CHECK(h_out == 10.0F);
}
