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
#include <utility>

#include <cuda_runtime.h>
#include <cublas_v2.h>

#include "./cuda_status.cuh"
#include "./cuda_ptr.cuh"

// Wrappers for cuBLAS, the CUDA Basic Linear Algebra Subprograms library.

namespace corvid::cuda {

#pragma region cublas_status

// Enum to wrap `cublasStatus_t`.
enum class cublas_status : std::uint8_t {
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
enum class cublas_operation : std::uint8_t {
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
  [[nodiscard]] bool operator!() const { return !ok(); }

  [[nodiscard]] cublas_status status() const { return value_; }

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
  cublas_status value_;
};

#pragma endregion
#pragma region cublas_handle

// Wrapper for cuBLAS handle, which is required for all cuBLAS calls. Provides
// RAII.

class cublas_handle {
public:
#pragma region Construction

  cublas_handle() : handle_{create()} {}

  cublas_handle(const cublas_handle&) = delete;
  cublas_handle& operator=(const cublas_handle&) = delete;

  cublas_handle(cublas_handle&& other) noexcept : handle_{other.handle_} {
    other.handle_ = nullptr;
  }
  cublas_handle& operator=(cublas_handle&& other) noexcept {
    if (this != &other) {
      destroy();
      handle_ = other.handle_;
      other.handle_ = nullptr;
    }
    return *this;
  }

  ~cublas_handle() { destroy(); }

#pragma endregion
#pragma region Accessors

  // Return address of device pointer; cannot be dereferenced on the host.
  [[nodiscard]] cublasHandle_t device_handle() const noexcept {
    return handle_;
  }
  [[nodiscard]] operator cublasHandle_t() const noexcept { return handle_; }
  void operator*() const {
    if (!handle_) throw std::runtime_error{"dereferencing null cublas_handle"};
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
  static cublasHandle_t create() {
    cublasHandle_t handle;
    if (!cublas_last_status{cublasCreate(&handle)}.ok()) return nullptr;
    return handle;
  }

  void destroy() {
    if (handle_) {
      cublasDestroy(handle_);
      handle_ = nullptr;
    }
  }

  [[nodiscard]] static cublasOperation_t as_raw(cublas_operation op) {
    return static_cast<cublasOperation_t>(op);
  }

#pragma endregion
#pragma region Data members
private:
  cublasHandle_t handle_{};

#pragma endregion
};
#pragma endregion

} // namespace corvid::cuda
