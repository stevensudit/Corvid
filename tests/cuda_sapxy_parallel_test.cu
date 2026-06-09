// Smoke test for the corvid/cuda/ subproject and the CUDA build path: launches
// a trivial kernel that calls corvid::cuda::saxpy on the device and checks
// that
// the result round-trips back to the host. Doubles as the canary that the CUDA
// toolchain (nvcc + g++-15) and the .cu Catch2 wiring work end to end.

#include <cuda_runtime.h>

#include "../corvid/cuda/cuda_ptr.cuh"
#include "../corvid/cuda/cuda_status.cuh"

using namespace corvid::cuda;

// Wrap every CUDA call in this. CUDA fails silently otherwise — a kernel can
// "run" and quietly do nothing. Cheap insurance you should never omit.
#define CUDA_CHECK(call)                                                      \
  do {                                                                        \
    cuda_last_status status = (call);                                         \
    status.throw_if_error();                                                  \
  } while (0)

// THE KERNEL. This is the body of ONE thread. The runtime will run N of these.
// __global__ = "callable from host, runs on device."
__global__ void saxpy(int n, float a, const float* x, float* y) {
  // Where am I in the global array? This is THE canonical CUDA idiom.
  int i = blockIdx.x * blockDim.x + threadIdx.x;
  if (i < n) { // N rarely divides evenly by block size; guard it.
    y[i] = a * x[i] + y[i];
  }
}

int main() {
  const int N = 1 << 26; // ~67M elements (~268 MB per array)
  const size_t bytes = (size_t)N * sizeof(float);
  const float a = 2.0F;

  std::vector<float> host_x(N, 1.0F);
  std::vector<float> host_y(N, 2.0F);

  // --- Host memory ---
  auto h_x = host_x.data();
  auto h_y = host_y.data();

  // --- Device memory ---
  float* d_x;
  float* d_y;
  CUDA_CHECK(cudaMalloc(&d_x, bytes));
  CUDA_CHECK(cudaMalloc(&d_y, bytes));

  // --- Host -> Device copy (over PCIe; deliberately NOT timed below) ---
  CUDA_CHECK(cudaMemcpy(d_x, h_x, bytes, cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_y, h_y, bytes, cudaMemcpyHostToDevice));

  // --- Launch configuration ---
  const int threadsPerBlock = 256; // multiple of 32 (warp size); good default
  const int blocks = (N + threadsPerBlock - 1) / threadsPerBlock;

  // Warm-up launch: the FIRST launch pays one-time context/JIT setup.
  // Timing that instead of the kernel is a classic beginner mistake.
  saxpy<<<blocks, threadsPerBlock>>>(N, a, d_x, d_y);
  CUDA_CHECK(cudaDeviceSynchronize());

  // --- Time the kernel with CUDA events (kernel launches are async; you
  //     cannot trust wall-clock timers around them without synchronizing) ---
  cudaEvent_t start;
  cudaEvent_t stop;
  CUDA_CHECK(cudaEventCreate(&start));
  CUDA_CHECK(cudaEventCreate(&stop));

  CUDA_CHECK(cudaEventRecord(start));
  saxpy<<<blocks, threadsPerBlock>>>(N, a, d_x, d_y);
  CUDA_CHECK(cudaEventRecord(stop));
  CUDA_CHECK(cudaEventSynchronize(stop));
  CUDA_CHECK(cudaGetLastError()); // catch launch errors (bad config, etc.)

  float ms = 0.0F;
  CUDA_CHECK(cudaEventElapsedTime(&ms, start, stop));

  // --- Verify correctness against the known CPU answer (2*1 + 2 == 4) ---
  CUDA_CHECK(cudaMemcpy(h_y, d_y, bytes, cudaMemcpyDeviceToHost));
  float maxErr = 0.0F;
  for (int i = 0; i < N; ++i) maxErr = fmaxf(maxErr, fabsf(h_y[i] - 4.0F));

  // --- The whole point: effective bandwidth ---
  // SAXPY touches 3 arrays' worth of memory per run: read x, read y, write y.
  double gb = 3.0 * (double)bytes / 1.0e9;
  double bandwidth = gb / (ms / 1000.0);

  printf("max error          : %f\n", maxErr);
  printf("kernel time        : %.3f ms\n", ms);
  printf("effective bandwidth: %.1f GB/s\n", bandwidth);

  cudaFree(d_x);
  cudaFree(d_y);

  return 0;
}
