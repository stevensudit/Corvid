#include <cassert>
#include <cmath>
#include <print>
#include <vector>

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_device.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "corvid/cuda/cuda_event.cuh"
#include "corvid/cuda/cuda_cublas.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/math.h"

using namespace corvid;
using namespace corvid::cuda;

// NOLINTBEGIN(modernize-use-std-print)

__host__ int cubed(int x) { return x * x * x; }

__host__ __device__ int squared(int x) { return x * x; }

__device__ int tot(int x) {
  static int total = 0;
  return atomicAdd(&total, x);
}

__global__ void test(int n) {
  auto t = tot(1);
  if (cuda_kernel::index() == 500)
    printf("Hello from block %d, thread %d, index %d, sq %d, t %d\n",
        blockIdx.x, threadIdx.x, cuda_kernel::x_index(), squared(n), t);
}

__global__ void iota(int n, int* out) {
  const auto i = cuda_kernel::x_index();
  if (i < n) out[i] = i;
}

__global__ void fill_multiplication_grid(int width, int height, int* out) {
  const auto x = cuda_kernel::x_index();
  const auto y = cuda_kernel::y_index();
  if (x >= width || y >= height) return;
  out[(y * width) + x] = x * y;
}

// Each CUDA block takes a chunk of the input array and reduces it to one
// partial sum, which it writes to `block_sums`. This is not an ideal solution:
// it should group work within each warp, writing the partial sum for that warp
// into shared memory, then have a single warp reduce those partial sums to a
// single block sum.
__global__ void reduce_block(const float* in, float* block_sums, int n) {
  // Shared by threads in block.
  __shared__ float s[256];

  // Which of the 256 in this block am I? (0..255)
  const int thread_id = cuda_kernel::x_thread();

  // Which element of the input array am I responsible for? This is the
  // canonical CUDA idiom for parallelizing over an array of arbitrary size,
  // even when it doesn't divide evenly by the block size.
  const int index = cuda_kernel::x_index();

  // Each thread loads one element from global memory into shared memory,
  // padding with 0 when out of bounds.
  s[thread_id] = (index < n) ? in[index] : 0.0F;
  __syncthreads();

  // With each loop, half the remaining threads drop out.
  for (int stride = cuda_kernel::x_block_dim() / 2; stride > 0; stride /= 2) {
    if (thread_id < stride) s[thread_id] += s[thread_id + stride];
    __syncthreads();
  }

  if (thread_id == 0) block_sums[blockIdx.x] = s[0];
}

// NOLINTEND(modernize-use-std-print)

// NOLINTNEXTLINE(bugprone-exception-escape)
int main() {
  int n = 42;
  test<<<4, 256>>>(n); // warm-up
  *cuda_timer::synchronize();

  // Fill an array using one thread per element.
  n = 1024;

  std::vector<int> h_I(n);
  cuda_ptr<int> d_I(n);
  *d_I;

  iota<<<1, n>>>(n, d_I);
  *cuda_timer::synchronize();

  *d_I.store(h_I);

  for (int i = 0; i < n; ++i)
    if (h_I[i] != i) assert(false);

  // The 2D version: a 32 x 32 multiplication table, one thread per cell. One
  // 32 x 32 block is 1024 threads, the same element count as the 1D case
  // above, just named with (x, y) instead of a flat index.
  const int w = 32;
  const int h = 32;

  const auto cells = static_cast<size_t>(w) * h;
  std::vector<int> h_M(cells);
  cuda_ptr<int> d_M(cells);
  *d_M;

  fill_multiplication_grid<<<1, dim3(w, h)>>>(w, h, d_M);
  *cuda_timer::synchronize();

  *d_M.store(h_M);

  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (h_M[(y * w) + x] != x * y) assert(false);

  return 0;
}
