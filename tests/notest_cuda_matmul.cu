#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cuda_runtime.h>
#include <cublas_v2.h>

#define CUDA_CHECK(call)                                                      \
  do {                                                                        \
    cudaError_t err = (call);                                                 \
    if (err != cudaSuccess) {                                                 \
      fprintf(stderr, "CUDA error %s:%d: %s\n", __FILE__, __LINE__,           \
          cudaGetErrorString(err));                                           \
      exit(EXIT_FAILURE);                                                     \
    }                                                                         \
  } while (0)

#define CUBLAS_CHECK(call)                                                    \
  do {                                                                        \
    cublasStatus_t st = (call);                                               \
    if (st != CUBLAS_STATUS_SUCCESS) {                                        \
      fprintf(stderr, "cuBLAS error %s:%d: status %d\n", __FILE__, __LINE__,  \
          (int)st);                                                           \
      exit(EXIT_FAILURE);                                                     \
    }                                                                         \
  } while (0)

// Naive SGEMM. Each thread computes ONE element of C (M x N, row-major).
// It walks the entire K dimension, reading a full row of A and a full column
// of B straight from global memory. No staging, no reuse. This is the floor.
__global__ void
matmul_naive(int M, int N, int K, const float* A, const float* B, float* C) {
  int col = (blockIdx.x * blockDim.x) + threadIdx.x; // 2D index now
  int row = (blockIdx.y * blockDim.y) + threadIdx.y;
  if (row < M && col < N) {
    float acc = 0.0F;
    for (int k = 0; k < K; ++k)
      acc += A[(row * K) + k] * B[(k * N) + col]; // row-major addressing
    C[(row * N) + col] = acc;
  }
}

static double gflops(double M, double N, double K, double ms) {
  // matmul does 2*M*N*K flops: one multiply + one add per inner-loop step
  return (2.0 * M * N * K) / (ms / 1000.0) / 1e9;
}

int main() {
  const int n = 4096; // square: M = N = K = n
  const size_t bytes = (size_t)n * n * sizeof(float);

  float* h_A = (float*)malloc(bytes);
  float* h_B = (float*)malloc(bytes);
  float* h_C_naive = (float*)malloc(bytes);
  float* h_C_cublas = (float*)malloc(bytes);
  for (int i = 0; i < n * n; ++i) {
    h_A[i] = (i % 7) * 0.1f;
    h_B[i] = (i % 5) * 0.2f;
  }

  float *d_A, *d_B, *d_C;
  CUDA_CHECK(cudaMalloc(&d_A, bytes));
  CUDA_CHECK(cudaMalloc(&d_B, bytes));
  CUDA_CHECK(cudaMalloc(&d_C, bytes));
  CUDA_CHECK(cudaMemcpy(d_A, h_A, bytes, cudaMemcpyHostToDevice));
  CUDA_CHECK(cudaMemcpy(d_B, h_B, bytes, cudaMemcpyHostToDevice));

  cudaEvent_t start, stop;
  CUDA_CHECK(cudaEventCreate(&start));
  CUDA_CHECK(cudaEventCreate(&stop));
  float ms = 0.0f;

  // ---------- Naive kernel ----------
  dim3 block(16, 16); // 256 threads, 2D
  dim3 grid((n + block.x - 1) / block.x, (n + block.y - 1) / block.y);

  matmul_naive<<<grid, block>>>(n, n, n, d_A, d_B, d_C); // warm-up
  CUDA_CHECK(cudaDeviceSynchronize());

  CUDA_CHECK(cudaEventRecord(start));
  matmul_naive<<<grid, block>>>(n, n, n, d_A, d_B, d_C);
  CUDA_CHECK(cudaEventRecord(stop));
  CUDA_CHECK(cudaEventSynchronize(stop));
  CUDA_CHECK(cudaGetLastError());
  CUDA_CHECK(cudaEventElapsedTime(&ms, start, stop));
  CUDA_CHECK(cudaMemcpy(h_C_naive, d_C, bytes, cudaMemcpyDeviceToHost));
  printf("naive  : %8.2f ms   %9.1f GFLOP/s\n", ms, gflops(n, n, n, ms));

  // ---------- cuBLAS reference ----------
  // THE FOOTGUN: cuBLAS is COLUMN-major; our arrays are ROW-major. A row-major
  // matrix reinterpreted as column-major is its transpose. So to get row-major
  // C = A*B, we ask cuBLAS for C^T = B^T * A^T -- which, with both operands
  // already "transposed" by the layout mismatch, is just sgemm(B, A), no
  // explicit transpose flags. (Square here, so all leading dims are n.)
  cublasHandle_t handle;
  CUBLAS_CHECK(cublasCreate(&handle));
  const float alpha = 1.0f, beta = 0.0f;

  CUBLAS_CHECK(cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha,
      d_B, n, d_A, n, &beta, d_C, n)); // warm-up
  CUDA_CHECK(cudaDeviceSynchronize());

  CUDA_CHECK(cudaEventRecord(start));
  CUBLAS_CHECK(cublasSgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, n, n, n, &alpha,
      d_B, n, d_A, n, &beta, d_C, n));
  CUDA_CHECK(cudaEventRecord(stop));
  CUDA_CHECK(cudaEventSynchronize(stop));
  CUDA_CHECK(cudaEventElapsedTime(&ms, start, stop));
  CUDA_CHECK(cudaMemcpy(h_C_cublas, d_C, bytes, cudaMemcpyDeviceToHost));
  printf("cuBLAS : %8.2f ms   %9.1f GFLOP/s\n", ms, gflops(n, n, n, ms));

  // ---------- Cross-check: the two answers must agree ----------
  // Validates both your kernel AND that the column-major incantation is right.
  double maxRel = 0.0;
  for (int i = 0; i < n * n; ++i) {
    double a = h_C_naive[i], b = h_C_cublas[i];
    double denom = fabs(b) > 1e-6 ? fabs(b) : 1e-6;
    maxRel = fmax(maxRel, fabs(a - b) / denom);
  }
  printf("max relative diff (naive vs cuBLAS): %.2e\n", maxRel);

  cublasDestroy(handle);
  cudaFree(d_A);
  cudaFree(d_B);
  cudaFree(d_C);
  free(h_A);
  free(h_B);
  free(h_C_naive);
  free(h_C_cublas);
  return 0;
}
