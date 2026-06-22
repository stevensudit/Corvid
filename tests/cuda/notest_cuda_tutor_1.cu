#include <cmath>
#include <print>
#include <vector>

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_device.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "corvid/cuda/cuda_event.cuh"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/math.h"

using namespace corvid;
using namespace corvid::cuda;

// Kernel function to add the elements of two arrays
__global__ void add(int n, float* sum, const float* x, const float* y) {
  const auto index = (blockIdx.x * blockDim.x) + threadIdx.x;
  const auto stride = gridDim.x * blockDim.x;

  for (int i = index; i < n; i += stride) sum[i] = x[i] + y[i];
}

static double gflops(double N, double ms) {
  // vector add does 1*N flops: one add per element
  return (1.0 * N) / (ms / 1000.0) / 1e9;
}

static double gbps(double N, double ms) {
  // 2 loads + 1 store of float per element
  return (3.0 * N * sizeof(float)) / (ms / 1000.0) / 1e9;
}

int main() {
  const int n = 1 << 24;

  std::vector<float> h_A(n);
  std::vector<float> h_B(n);
  std::vector<float> h_C(n);
  for (int i = 0; i < n; ++i) {
    h_A[i] = 1.F;
    h_B[i] = 2.F;
  }

  // --- Device memory ---
  cuda_ptr<float> d_A(n);
  cuda_ptr<float> d_B(n);
  cuda_ptr<float> d_C(n);
  *d_A;
  *d_B;
  *d_C;

  // --- Host -> Device copy (over PCIe; deliberately NOT timed below) ---
  *d_A.load(h_A);
  *d_B.load(h_B);

  float ms;

  // ---------- Naive kernel, ceil_div ----------
  size_t block_size = 1024;
  dim3 blockDim(block_size);
  dim3 gridDim(ceil_div(n, block_size)); // enough blocks to cover n

  add<<<gridDim, blockDim>>>(n, d_C, d_A, d_B); // warm-up
  *cuda_timer::synchronize();

  if (auto timer = cuda_timer{ms}; true) {
    add<<<gridDim, blockDim>>>(n, d_C, d_A, d_B);
    *cuda_last_status{}; // check for launch errors
  }
  *d_C.store(h_C);

  std::println("naive  : {:8.2f} ms   {:9.1f} GFLOP/s   {:8.1f} GB/s", ms,
      gflops(n, ms), gbps(n, ms));

  // ---------- Naive kernel, scaled  ----------
  const auto mp_count =
      cuda_device{}.get_attribute(cuda_device_attr::multi_processor_count);
  dim3 blockDimS(block_size);
  dim3 gridDimS(32 * mp_count);

  add<<<gridDimS, blockDimS>>>(n, d_C, d_A, d_B); // warm-up
  *cuda_timer::synchronize();

  if (auto timer = cuda_timer{ms}; true) {
    add<<<gridDimS, blockDimS>>>(n, d_C, d_A, d_B);
    *cuda_last_status{}; // check for launch errors
  }
  *d_C.store(h_C);

  std::println("scaled : {:8.2f} ms   {:9.1f} GFLOP/s   {:8.1f} GB/s", ms,
      gflops(n, ms), gbps(n, ms));

  // Check for errors (all values should be 3.0f)
  float maxError = 0.0F;
  for (int i = 0; i < n; i++) maxError = fmax(maxError, fabs(h_C[i] - 3.0F));
  std::println("Max error: {}", maxError);

  return 0;
}
