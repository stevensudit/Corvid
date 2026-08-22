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
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <cuda_runtime.h>
#include <cublas_v2.h>

#include "./cuda_handle.cuh"
#include "./cuda_ptr.cuh"
#include "../strings/cstring_view.h"
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

// RAII owner of the cuBLAS library handle that every cuBLAS call takes.
//
// The constructor throws on failure; `try_create` returns a null handle
// instead.
class cublas_handle: public cuda_handle<cublasHandle_t, cublasDestroy> {
public:
#pragma region Construction

  cublas_handle() : cublas_handle{make(on_failure::raise)} {}

  // Create a handle, or return a failed instance.
  // Check with `operator bool`, and follow up with `cublas_last_status{}`.
  [[nodiscard]] static cublas_handle try_create() {
    return cublas_handle{make(on_failure::ignore)};
  }

#pragma endregion
#pragma region Multiply

  // General Matrix Multiply (GEMM) wrapper.
  //
  // C = alpha * op(A) * op(B) + beta * C
  //
  // Where op(X) is X or its transpose. C, A, and B are column-major (cuBLAS
  // default) and every leading dimension is `n`.

  // Simple square multiply.
  cublas_last_status multiply(int n, float alpha, const cuda_ptr<float>& A,
      const cuda_ptr<float>& B, float beta, cuda_ptr<float>& C,
      cublas_operation opA = cublas_operation::none,
      cublas_operation opB = cublas_operation::none) const {
    return cublasSgemm(handle_, as_raw(opA), as_raw(opB), n, n, n, &alpha, A,
        n, B, n, &beta, C, n);
  }

#pragma endregion
#pragma region Helpers
private:
  explicit cublas_handle(cublasHandle_t handle) noexcept
      : cuda_handle{handle} {}

  static cublasHandle_t make(on_failure policy) {
    return create<cublasCreate, cublas_last_status>(policy);
  }

  [[nodiscard]] static cublasOperation_t as_raw(cublas_operation op) {
    return static_cast<cublasOperation_t>(op);
  }

#pragma endregion
};
#pragma endregion

} // namespace corvid::cuda
