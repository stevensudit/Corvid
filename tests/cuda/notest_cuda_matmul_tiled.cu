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

__global__ void
matmul_stupid(int M, int N, int K, const float* A, const float* B, float* C) {
  // 2D index now
  int col = static_cast<int>((blockIdx.x * blockDim.x) + threadIdx.x);
  int row = static_cast<int>((blockIdx.y * blockDim.y) + threadIdx.y);
  if (row < M && col < N) {
    float acc = 0.0F;
    for (int k = 0; k < K; ++k)
      acc += A[(row * K) + k] * B[(k * N) + col]; // row-major addressing
    C[(row * N) + col] = acc;
  }
}

// Naive, but with the COALESCED index mapping for column-major: threadIdx.x ->
// row, because consecutive rows are contiguous in memory here.
__global__ void
matmul_naive(int M, int N, int K, const float* A, const float* B, float* C) {
  const auto row = static_cast<int>((blockIdx.x * blockDim.x) + threadIdx.x);
  const auto col = static_cast<int>((blockIdx.y * blockDim.y) + threadIdx.y);
  if (row < M && col < N) {
    float acc = 0.0F;
    for (int k = 0; k < K; ++k)
      acc += A[((size_t)k * M) + row] * B[((size_t)col * K) + k];
    C[((size_t)col * M) + row] = acc;
  }
}

const int k_tile = 32;

// Tiled SGEMM. The whole game: each block cooperatively stages a TILE x TILE
// patch of A and of B from global memory into shared memory ONCE, then every
// thread reuses those patches TILE times from the fast on-chip tier.
//
// Shared tiles are stored "k-major" (first index = position along K). That
// keeps the inner-loop reads conflict-free: As[k][tx] walks consecutive banks
// across a warp, and Bs[k][ty] is identical across a warp (a broadcast).
__global__ void
matmul_tiled(int M, int N, int K, const float* A, const float* B, float* C) {
  __shared__ float As[k_tile][k_tile + 1]; // As[k][i] = A[row0 + i, kTile + k]
  __shared__ float Bs[k_tile][k_tile + 1]; // Bs[k][j] = B[kTile + k, col0 + j]

  auto tx = static_cast<int>(threadIdx.x);
  auto ty = static_cast<int>(threadIdx.y);
  // tx -> row: coalesced in column-major
  auto row = static_cast<int>((blockIdx.x * k_tile) + tx);
  auto col = static_cast<int>((blockIdx.y * k_tile) + ty);

  float acc = 0.0F;
  int numTiles = (K + k_tile - 1) / k_tile;
  for (int t = 0; t < numTiles; ++t) {
    const int kTile = t * k_tile;
    const int aK = kTile + ty; // K-index of the A element this thread loads
    const int bK = kTile + tx; // K-index of the B element this thread loads

    // Global loads are coalesced (consecutive tx -> consecutive address).
    As[ty][tx] = (row < M && aK < K) ? A[((size_t)aK * M) + row] : 0.0F;
    Bs[tx][ty] = (bK < K && col < N) ? B[((size_t)col * K) + bK] : 0.0F;

    __syncthreads(); // (1) everyone must finish loading before any read

#pragma unroll
    for (int k = 0; k < k_tile; ++k) acc += As[k][tx] * Bs[k][ty];

    // (2) everyone must finish reading before the next iteration overwrites
    // the tiles
    __syncthreads();
  }

  if (row < M && col < N) C[((size_t)col * M) + row] = acc;
}

// Register-blocked (thread-tiled) SGEMM, column-major: C = A * B.
//   A: MxK   B: KxN   C: MxN, all column-major (element (r,c) at c*lead + r).
// A block computes a BM x BN output tile; each THREAD computes a TM x TN
// sub-tile of it, holding those TM*TN partial sums in registers and updating
// them with rank-1 outer products. The point: one shared-memory read now
// feeds TM (or TN) fused multiply-adds instead of one.
template<int BM, int BN, int BK, int TM, int TN>
__global__ void
matmul_regtiled(int M, int N, int K, const float* __restrict__ A,
    const float* __restrict__ B, float* __restrict__ C) {
  const int blockRow = blockIdx.y * BM;   // this block's output-row origin
  const int blockCol = blockIdx.x * BN;   // ... and output-col origin
  const int threadRow = threadIdx.y * TM; // this thread's sub-tile origin
  const int threadCol = threadIdx.x * TN; //     (within the block tile)

  __shared__ float As[BK][BM]; // As[k][m] = A[blockRow+m, kSlab+k]
  __shared__ float Bs[BK][BN]; // Bs[k][n] = B[kSlab+k, blockCol+n]

  float acc[TM][TN];
#pragma unroll
  for (int i = 0; i < TM; ++i)
#pragma unroll
    for (int j = 0; j < TN; ++j) acc[i][j] = 0.0F;

  const auto tid =
      static_cast<int>(threadIdx.y * blockDim.x) +
      static_cast<int>(threadIdx.x);
  const int numThreads = (BM / TM) * (BN / TN);

  for (int kSlab = 0; kSlab < K; kSlab += BK) {
    // Cooperatively stage the A and B tiles into shared memory.
    for (int p = tid; p < BM * BK; p += numThreads) {
      int m = p % BM;
      int k = p / BM;
      int row = blockRow + m;
      int col = kSlab + k;
      As[k][m] = (row < M && col < K) ? A[((size_t)col * M) + row] : 0.0F;
    }
    for (int p = tid; p < BK * BN; p += numThreads) {
      int n = p % BN;
      int k = p / BN;
      int row = kSlab + k;
      int col = blockCol + n;
      Bs[k][n] = (row < K && col < N) ? B[((size_t)col * K) + row] : 0.0F;
    }
    __syncthreads();

// Accumulate this slab as a sequence of outer products.
#pragma unroll
    for (int k = 0; k < BK; ++k) {
      float regA[TM];
      float regB[TN];
#pragma unroll
      for (int i = 0; i < TM; ++i) regA[i] = As[k][threadRow + i];
#pragma unroll
      for (int j = 0; j < TN; ++j) regB[j] = Bs[k][threadCol + j];
#pragma unroll
      for (int i = 0; i < TM; ++i)
#pragma unroll
        for (int j = 0; j < TN; ++j) acc[i][j] += regA[i] * regB[j];
    }
    __syncthreads();
  }

// Write the TM x TN sub-tile back to C.
#pragma unroll
  for (int i = 0; i < TM; ++i) {
    int row = blockRow + threadRow + i;
    if (row >= M) continue;
#pragma unroll
    for (int j = 0; j < TN; ++j) {
      int col = blockCol + threadCol + j;
      if (col < N) C[((size_t)col * M) + row] = acc[i][j];
    }
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
  std::vector<float> h_C_tiled(count);
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

  // ---------- Stupid kernel ----------
  dim3 stupid_block(32, 32);
  dim3 stupid_grid(ceil_div(n, stupid_block.x), ceil_div(n, stupid_block.y));

  matmul_stupid<<<stupid_grid, stupid_block>>>(n, n, n, d_A, d_B,
      d_C); // warm-up
  *cuda_timer::synchronize();

  if (auto timer = cuda_timer{ms}; true) {
    matmul_stupid<<<stupid_grid, stupid_block>>>(n, n, n, d_A, d_B, d_C);
    *cuda_last_status{}; // check for launch errors
  }

  std::println("stupid : {:8.2f} ms   {:9.1f} GFLOP/s", ms,
      gflops(n, n, n, ms));

  // ---------- Naive kernel ----------
  dim3 naive_block(32, 32);
  dim3 naive_grid(ceil_div(n, naive_block.x), ceil_div(n, naive_block.y));

  matmul_naive<<<naive_grid, naive_block>>>(n, n, n, d_A, d_B, d_C); // warm-up
  *cuda_timer::synchronize();

  if (auto timer = cuda_timer{ms}; true) {
    matmul_naive<<<naive_grid, naive_block>>>(n, n, n, d_A, d_B, d_C);
    *cuda_last_status{}; // check for launch errors
  }

  std::println("naive  : {:8.2f} ms   {:9.1f} GFLOP/s", ms,
      gflops(n, n, n, ms));

  // ---------- Tiled kernel ----------
  dim3 tiled_block(k_tile, k_tile);
  dim3 tiled_grid(ceil_div(n, k_tile), ceil_div(n, k_tile));

  matmul_tiled<<<tiled_grid, tiled_block>>>(n, n, n, d_A, d_B, d_C);
  *cuda_timer::synchronize();

  if (auto timer = cuda_timer{ms}; true) {
    matmul_tiled<<<tiled_grid, tiled_block>>>(n, n, n, d_A, d_B, d_C);
    *cuda_last_status{}; // check for launch errors
  }
  *d_C.store(h_C_tiled);

  std::println("tiled  : {:8.2f} ms   {:9.1f} GFLOP/s", ms,
      gflops(n, n, n, ms));

  // ---------- Register-tiled kernel ----------
  constexpr int BM = 64;
  constexpr int BN = 64;
  constexpr int BK = 8;
  constexpr int TM = 4;
  constexpr int TN = 4;
  dim3 block(BN / TN, BM / TM); // (16,16) = 256 threads
  dim3 grid((n + BN - 1) / BN, (n + BM - 1) / BM);

  matmul_regtiled<BM, BN, BK, TM, TN><<<grid, block>>>(n, n, n, d_A, d_B, d_C);
  *cuda_timer::synchronize();

  if (auto timer = cuda_timer{ms}; true) {
    matmul_regtiled<BM, BN, BK, TM, TN>
        <<<grid, block>>>(n, n, n, d_A, d_B, d_C);
    *cuda_last_status{}; // check for launch errors
  }
  *d_C.store(h_C_tiled);

  std::println("regtile: {:8.2f} ms   {:9.1f} GFLOP/s", ms,
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
    double a = h_C_tiled[i];
    double b = h_C_cublas[i];
    double denom = fabs(b) > 1e-6 ? fabs(b) : 1e-6;
    maxRel = fmax(maxRel, fabs(a - b) / denom);
  }
  std::println("max relative diff (tiled vs cuBLAS): {:.2e}", maxRel);

  return 0;
}
