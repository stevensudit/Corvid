// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
#include <concepts>
#include <cstddef>
#include <stdexcept>
#include <type_traits>

#include <cuda_runtime.h>
#include <cublas_v2.h>

#include "../strings/cstring_view.h"
#include "./cuda_handle.cuh"
#include "./cuda_buffer.cuh"
#include "./cuda_status.cuh"

// Wrappers for cuBLAS, the CUDA Basic Linear Algebra Subprograms library.

namespace corvid::cuda {

#pragma region cublas_status

// Enum to wrap `cublasStatus_t`.
// NOLINTNEXTLINE(performance-enum-size)
enum class cublas_status : std::underlying_type_t<cublasStatus_t> {
  success = CUBLAS_STATUS_SUCCESS,
  not_initialized = CUBLAS_STATUS_NOT_INITIALIZED,
  alloc_failed = CUBLAS_STATUS_ALLOC_FAILED,
  invalid_value = CUBLAS_STATUS_INVALID_VALUE,
  arch_mismatch = CUBLAS_STATUS_ARCH_MISMATCH,
  mapping_error = CUBLAS_STATUS_MAPPING_ERROR,
  execution_failed = CUBLAS_STATUS_EXECUTION_FAILED,
  internal_error = CUBLAS_STATUS_INTERNAL_ERROR,
  not_supported = CUBLAS_STATUS_NOT_SUPPORTED,
  license_error = CUBLAS_STATUS_LICENSE_ERROR
};

#pragma endregion
#pragma region Operation

// Wrapper for `cublasOperation_t`.
// NOLINTNEXTLINE(performance-enum-size)
enum class cublas_operation : std::underlying_type_t<cublasOperation_t> {
  none = CUBLAS_OP_N,
  transpose = CUBLAS_OP_T,
  conjugate_transpose = CUBLAS_OP_C
};

#pragma endregion
#pragma region last_cublas_status

class cublas_last_status {
public:
  cublas_last_status() : value_{cublas_status::success} {}
  cublas_last_status(cublasStatus_t status)
      : value_{static_cast<cublas_status>(status)} {}

  [[nodiscard]] bool ok() const { return value_ == cublas_status::success; }
  [[nodiscard]] explicit operator bool() const { return ok(); }

  [[nodiscard]] cublas_status status() const { return value_; }

  [[nodiscard]] cstring_view message() const {
    return cublasGetStatusString(as_raw(value_));
  }

  // Throw `status` as a `std::runtime_error`. cuBLAS has no thread-wide last
  // error, so the status must be passed.
  [[noreturn]] static void raise(const cublas_last_status status) {
    throw std::runtime_error{status.message().c_str()};
  }

  // NOLINTNEXTLINE(modernize-use-nodiscard)
  bool or_throw() const {
    if (value_ != cublas_status::success)
      throw std::runtime_error{cublasGetStatusString(as_raw(value_))};
    return true;
  }
  bool operator*() const {
    or_throw();
    return true;
  }

  [[nodiscard]] static cublasStatus_t as_raw(cublas_status status) {
    return static_cast<cublasStatus_t>(status);
  }

private:
  cublas_status value_{};
};

#pragma endregion
#pragma region cublas_handle

// An element type that `multiply` has a `gemm` overload for.
//
// cuBLAS also has GEMM routines for `__half`, `cuComplex`, and
// `cuDoubleComplex`, which this wrapper does not cover.
template<typename T>
concept GemmElement = std::same_as<T, float> || std::same_as<T, double>;

// RAII owner of the cuBLAS library handle that every cuBLAS call takes.
class cublas_handle: public cuda_handle<cublasHandle_t, cublasDestroy> {
public:
#pragma region Construction

  explicit cublas_handle(std::nullptr_t) noexcept : cuda_handle{nullptr} {}

  // Create a handle, or throw.
  cublas_handle() : cublas_handle{make(on_failure::raise)} {}

  // Create a handle, or return a failed instance.
  //
  // Check with `operator bool`, and follow up with `cublas_last_status{}`.
  [[nodiscard]] static cublas_handle try_create() {
    return cublas_handle{make(on_failure::ignore)};
  }

#pragma endregion
#pragma region Multiply

  // General Matrix Multiply (GEMM), `C = alpha * op(A) * op(B) + beta * C`.
  //
  // These are:
  //   m      rows of `op(A)` and of `C`
  //   n      columns of `op(B)` and of `C`
  //   k      columns of `op(A)` and rows of `op(B)`, the summed dimension
  //   alpha  scale applied to the product
  //   A      device matrix, `m` by `k` after `opA`
  //   lda    leading dimension of `A` as stored
  //   B      device matrix, `k` by `n` after `opB`
  //   ldb    leading dimension of `B` as stored
  //   beta   scale applied to the existing `C` before the product is added;
  //          zero ignores its contents, one accumulates onto them
  //   C      device matrix, `m` by `n`, read when `beta` is nonzero, written
  //   ldc    leading dimension of `C`
  //   opA    whether `A` is used as stored or transposed
  //   opB    likewise for `B`
  //
  // Every matrix is column-major, as cuBLAS defines it. `op(X)` is `X` as
  // stored when its flag is `none`, and the transpose of `X` as stored when
  // it is `transpose`. The dimensions describe the operands of the product:
  // `op(A)` is `m` by `k`, `op(B)` is `k` by `n`, and `C` is `m` by `n`. So a
  // buffer passed with `transpose` holds the other shape, `k` by `m` for `A`
  // and `n` by `k` for `B`.
  //
  // A leading dimension is the element distance between the starts of
  // consecutive columns of a buffer as stored, which the flags do not change.
  // For a packed buffer it is the stored row count: `lda` is `m` under `none`
  // and `k` under `transpose`, `ldb` is `k` or `n` likewise, and `ldc` is `m`.
  // A larger value addresses a block of a taller stored matrix.
  //
  // `alpha` scales the product. `beta` scales `C` before the product is
  // added, so zero leaves `C` unread and one accumulates onto it.
  template<GemmElement T>
  [[nodiscard]] cublas_last_status
  multiply(int m, int n, int k, std::type_identity_t<T> alpha,
      const cuda_buffer<T>& A, int lda, const cuda_buffer<T>& B, int ldb,
      std::type_identity_t<T> beta, cuda_buffer<T>& C, int ldc,
      cublas_operation opA = cublas_operation::none,
      cublas_operation opB = cublas_operation::none) const {
    return gemm(handle_, as_raw(opA), as_raw(opB), m, n, k, &alpha, A.get(),
        lda, B.get(), ldb, &beta, C.get(), ldc);
  }

  // GEMM over row-major matrices, `C = alpha * op(A) * op(B) + beta * C`.
  //
  // The same letters as `multiply`, read row-major: `op(A)` is `m` by `k`,
  // `op(B)` is `k` by `n`, `C` is `m` by `n`, and a leading dimension is the
  // element distance between the starts of consecutive rows of a buffer as
  // stored, which for a packed buffer is its column count.
  //
  // cuBLAS reads a row-major buffer as the transpose of the matrix it holds.
  // Transposing both sides of the product gives `C^T = (op(A) * op(B))^T`,
  // which is the same as `op(B)^T * op(A)^T`. So the call is `multiply` with
  // the operands swapped and `m` and `n` swapped, and it all cancels out to
  // give us a row-major result.
  template<GemmElement T>
  [[nodiscard]] cublas_last_status
  multiply_row_major(int m, int n, int k, std::type_identity_t<T> alpha,
      const cuda_buffer<T>& A, int lda, const cuda_buffer<T>& B, int ldb,
      std::type_identity_t<T> beta, cuda_buffer<T>& C, int ldc,
      cublas_operation opA = cublas_operation::none,
      cublas_operation opB = cublas_operation::none) const {
    return multiply(n, m, k, alpha, B, ldb, A, lda, beta, C, ldc, opB, opA);
  }

  // Square multiply, where every dimension and leading dimension is `n`.
  template<GemmElement T>
  [[nodiscard]] cublas_last_status
  multiply(int n, std::type_identity_t<T> alpha, const cuda_buffer<T>& A,
      const cuda_buffer<T>& B, std::type_identity_t<T> beta, cuda_buffer<T>& C,
      cublas_operation opA = cublas_operation::none,
      cublas_operation opB = cublas_operation::none) const {
    return multiply(n, n, n, alpha, A, n, B, n, beta, C, n, opA, opB);
  }

#pragma endregion
#pragma region Helpers
private:
  explicit cublas_handle(cublasHandle_t handle) noexcept
      : cuda_handle{handle} {}

  static cublasHandle_t make(on_failure policy) {
    return create<cublasCreate, cublas_last_status>(policy);
  }

  // The GEMM routine for each `GemmElement`, so `multiply` dispatches by
  // overload.
  static cublasStatus_t gemm(cublasHandle_t handle, cublasOperation_t opA,
      cublasOperation_t opB, int m, int n, int k, const float* alpha,
      const float* A, int lda, const float* B, int ldb, const float* beta,
      float* C, int ldc) {
    return cublasSgemm(handle, opA, opB, m, n, k, alpha, A, lda, B, ldb, beta,
        C, ldc);
  }
  static cublasStatus_t gemm(cublasHandle_t handle, cublasOperation_t opA,
      cublasOperation_t opB, int m, int n, int k, const double* alpha,
      const double* A, int lda, const double* B, int ldb, const double* beta,
      double* C, int ldc) {
    return cublasDgemm(handle, opA, opB, m, n, k, alpha, A, lda, B, ldb, beta,
        C, ldc);
  }

  [[nodiscard]] static cublasOperation_t as_raw(cublas_operation op) {
    return static_cast<cublasOperation_t>(op);
  }

#pragma endregion
};
#pragma endregion

} // namespace corvid::cuda
