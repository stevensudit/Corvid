// Smoke test for the cuda/ subproject and the CUDA build path. Launches a
// trivial kernel that calls corvid::cuda::saxpy on the device and checks that
// the result round-trips back to the host. Plain main(), not Catch2: having
// nvcc parse the full Catch2 header is needless risk for a smoke test. Returns
// nonzero on any CUDA error or mismatch so CTest flags a regression.

#include <cstdio>

#include "../cuda/saxpy.cuh"

namespace {

__global__ void saxpy_kernel(float a, float x, float y, float* out) {
  *out = corvid::cuda::saxpy(a, x, y);
}

} // namespace

int main() {
  float* d_out = nullptr;
  cudaMalloc(&d_out, sizeof(float));
  saxpy_kernel<<<1, 1>>>(2.0F, 3.0F, 4.0F, d_out);
  float h_out = 0.0F;
  cudaMemcpy(&h_out, d_out, sizeof(float), cudaMemcpyDeviceToHost);
  cudaFree(d_out);

  cudaError_t err = cudaGetLastError();
  if (err != cudaSuccess) {
    std::fprintf(stderr, "CUDA error: %s\n", cudaGetErrorString(err));
    return 1;
  }

  // 2*3 + 4 == 10, exactly representable in float.
  if (h_out != 10.0F) {
    std::fprintf(stderr, "saxpy mismatch: got %f, want 10\n", h_out);
    return 1;
  }

  std::fprintf(stderr, "Great success!\n");

  return 0;
}
