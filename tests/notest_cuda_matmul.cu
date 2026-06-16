// Benchmarks a naive SGEMM kernel against cuBLAS on a square matrix, then
// cross-checks the two results. Uses the corvid/cuda/ wrappers throughout:
// cuda_ptr, cuda_timer, cuda_last_status, and cublas_handle.

#include <cmath>
#include <print>
#include <vector>

#include <cuda_runtime.h>

#include "../corvid/cuda/cuda_ptr.cuh"
#include "../corvid/cuda/cuda_status.cuh"
#include "../corvid/cuda/cuda_event.cuh"
#include "../corvid/cuda/cuda_cublas.cuh"
#include "../corvid/math.h"

using namespace corvid;
using namespace corvid::cuda;

// Naive SGEMM, column-major to match cuBLAS's native layout. Each thread
// computes ONE element of C (M x N), walking the entire K dimension and
// reading a full column of A and a full row of B straight from global memory.
// No staging, no reuse. This is the floor.
__global__ void
matmul_naive(int M, int N, int K, const float* A, const float* B, float* C) {
  const auto col =
      static_cast<int>((blockIdx.x * blockDim.x) + threadIdx.x); // 2D index
  const auto row = static_cast<int>((blockIdx.y * blockDim.y) + threadIdx.y);
  if (row < M && col < N) {
    float acc = 0.0F;
    // Column-major addressing: element (i, j) of a P-row matrix is at j*P + i.
    for (int k = 0; k < K; ++k) acc += A[(k * M) + row] * B[(col * K) + k];
    C[(col * M) + row] = acc;
  }
}

static double gflops(double M, double N, double K, double ms) {
  // matmul does 2*M*N*K flops: one multiply + one add per inner-loop step
  return (2.0 * M * N * K) / (ms / 1000.0) / 1e9;
}

int main() {
  const int n = 4096; // square: M = N = K = n
  const size_t count = (size_t)n * n;

  std::vector<float> h_A(count);
  std::vector<float> h_B(count);
  std::vector<float> h_C_naive(count);
  std::vector<float> h_C_cublas(count);
  for (int i = 0; i < n * n; ++i) {
    h_A[i] = static_cast<float>(i % 7) * 0.1F;
    h_B[i] = static_cast<float>(i % 5) * 0.2F;
  }

  // --- Device memory ---
  cuda_ptr<float> d_A(count);
  cuda_ptr<float> d_B(count);
  cuda_ptr<float> d_C(count);
  *d_A;
  *d_B;
  *d_C;

  // --- Host -> Device copy (over PCIe; deliberately NOT timed below) ---
  *d_A.load(h_A);
  *d_B.load(h_B);

  float ms;

  // ---------- Naive kernel ----------
  dim3 block(16, 16); // 256 threads, 2D
  dim3 grid(ceil_div(n, block.x), ceil_div(n, block.y));

  matmul_naive<<<grid, block>>>(n, n, n, d_A, d_B, d_C); // warm-up
  *cuda_timer::synchronize();

  if (auto timer = cuda_timer{ms}; true) {
    matmul_naive<<<grid, block>>>(n, n, n, d_A, d_B, d_C);
    *cuda_last_status{}; // check for launch errors
  }
  *d_C.store(h_C_naive);

  std::println("naive  : {:8.2f} ms   {:9.1f} GFLOP/s", ms,
      gflops(n, n, n, ms));

  // ---------- cuBLAS reference ----------
  // Both sides are column-major now, so this is the textbook call: C = A*B
  // with A, B, and C all column-major and every leading dimension n. No
  // operand swap, no transpose flags.
  cublas_handle handle;
  *handle;
  const float alpha = 1.0F;
  const float beta = 0.0F;

  *handle.multiply(n, alpha, d_A, d_B, beta, d_C); // warm-up
  *cuda_timer::synchronize();

  if (auto timer = cuda_timer{ms}; true) {
    *handle.multiply(n, alpha, d_A, d_B, beta, d_C);
  }
  *d_C.store(h_C_cublas);
  std::println("cuBLAS : {:8.2f} ms   {:9.1f} GFLOP/s", ms,
      gflops(n, n, n, ms));

  // ---------- Cross-check: the two answers must agree ----------
  // Validates both your kernel AND that its column-major addressing matches
  // what cuBLAS computed.
  double maxRel = 0.0;
  for (int i = 0; i < n * n; ++i) {
    double a = h_C_naive[i];
    double b = h_C_cublas[i];
    double denom = fabs(b) > 1e-6 ? fabs(b) : 1e-6;
    maxRel = fmax(maxRel, fabs(a - b) / denom);
  }
  std::println("max relative diff (naive vs cuBLAS): {:.2e}", maxRel);

  return 0;
}
