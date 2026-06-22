// Smoke test for the corvid/cuda/ subproject and the CUDA build path: launches
// a trivial kernel that calls corvid::cuda::saxpy on the device and checks
// that the result round-trips back to the host. Doubles as the canary that the
// CUDA toolchain (nvcc + g++-15) and the .cu Catch2 wiring work end to end.

#include <cassert>
#include <cmath>
#include <cstdio>
#include <print>
#include <vector>

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "corvid/cuda/cuda_event.cuh"
#include "corvid/math.h"

using namespace corvid::cuda;

// A kernel gets additional implicit arguments:
//
// threadIdx: the thread's index within its block (0..blockDim.x-1)
//            uint3, per-thread.
//
// blockIdx: the block's index within the grid (0..gridDim.x-1)
//        uint3, per-block.
//
// blockDim: the number of threads per block (same for all blocks)
//        dim3.
//
// gridDim: the number of blocks in the grid (same for all blocks)
//        dim3.
//
// warpSize: the number of threads in a warp (32 on all current NVIDIA GPUs)
//        int, per-device.
__global__ void sapxy(int n, float a, const float* x, float* y) {
  // Where am I in the global array? This is THE canonical CUDA idiom.
  // Flatten the thread inside the block and the block inside the grid into a
  // linear index.
  // Note: This cast is only honest so long as the index fits in 32 bits.
  const auto i = static_cast<int>((blockIdx.x * blockDim.x) + threadIdx.x);
  if (i < n) { // N rarely divides evenly by block size; guard it.
    y[i] = (a * x[i]) + y[i];
  }
}

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
  const int N = 1 << 26; // ~67M elements (~268 MB per array)
  const size_t bytes = (size_t)N * sizeof(float);
  const float a = 2.0F;

  std::vector<float> host_x(N, 1.0F);
  std::vector<float> host_y(N, 2.0F);

  // --- Device memory ---
  cuda_ptr<float> device_x(N);
  cuda_ptr<float> device_y(N);
  *device_x;
  *device_y;

  // --- Host -> Device copy (over PCIe; deliberately NOT timed below) ---
  *device_x.load(host_x);
  *device_y.load(host_y);

  // --- Launch configuration ---
  const int threadsPerBlock = 256; // multiple of 32 (warp size); good default
  const int blocks = corvid::ceil_div(N, threadsPerBlock);

  // Warm-up launch: the FIRST launch pays one-time context/JIT setup.
  // Timing that instead of the kernel is a classic beginner mistake.
  sapxy<<<blocks, threadsPerBlock>>>(N, a, device_x, device_y);

  *cuda_timer::synchronize(); // wait for the warm-up to finish

  // Reset `device_y`; the warm-up already mutated it, and the timed run must
  // start from the same inputs the check below expects.
  *device_y.load(host_y);

  float ms;
  if (auto timer = cuda_timer{ms}; true) {
    sapxy<<<blocks, threadsPerBlock>>>(N, a, device_x, device_y);
    *cuda_last_status{}; // check for launch errors
  }

  // --- Verify correctness against the known CPU answer (2*1 + 2 == 4) ---
  *device_y.store(host_y);

  float maxErr = 0.0F;
  for (int i = 0; i < N; ++i) maxErr = fmaxf(maxErr, fabsf(host_y[i] - 4.0F));
  // 2*1 + 2 == 4 is exact in float, so a correct run must match to the bit.
  if (maxErr != 0.0F) {
    std::println(stderr, "verification failed: max error {}", maxErr);
    return 1;
  }

  // --- The whole point: effective bandwidth ---
  // SAXPY touches 3 arrays' worth of memory per run: read x, read y, write y.
  double gb = 3.0 * (double)bytes / 1.0e9;
  double bandwidth = gb / (ms / 1000.0);

  std::println("max error          : {}", maxErr);
  std::println("kernel time        : {:.3f} ms", ms);
  std::println("effective bandwidth: {:.1f} GB/s", bandwidth);

  return 0;
}
