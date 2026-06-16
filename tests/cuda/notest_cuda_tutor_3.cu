#include <cassert>
#include <cmath>
#include <print>
#include <vector>

#include <cuda_runtime.h>

#include "../corvid/cuda/cuda_device.cuh"
#include "../corvid/cuda/cuda_ptr.cuh"
#include "../corvid/cuda/cuda_status.cuh"
#include "../corvid/cuda/cuda_event.cuh"
#include "../corvid/cuda/cuda_cublas.cuh"
#include "../corvid/cuda/cuda_kernel.cuh"
#include "../corvid/math.h"

using namespace corvid;
using namespace corvid::cuda;

// NOLINTBEGIN(modernize-use-std-print)

__global__ void iota(int n, int* out) {
  const auto i = cuda_kernel::x_index();
  if (i < n) out[i] = i;
}

// NOLINTEND(modernize-use-std-print)

int main() {
  int n = 42;

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

  return 0;
}
