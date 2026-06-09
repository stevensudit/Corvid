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

#include <cassert>
#include <cstddef>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include <cuda_runtime.h>

#include "./cuda_status.cuh"

namespace corvid::cuda {

// CUDA memory management.
//
// CUDA allows you to allocate and free device memory, giving you a pointer
// that you can't dereference on the host; instead, you explicitly copy to or
// from it.
//
// This is wrapped as `cuda_ptr<T>`, which is the moral equivalent to
// `std::unique_ptr`, providing RAII and

#pragma region details

namespace details {
// Allocate CUDA device memory for `count` objects of type `T`, and return a
// pointer to the allocated memory. Returns `nullptr` on failure.
template<typename T>
T* cuda_malloc(size_t count) {
  if (count > std::numeric_limits<size_t>::max() / sizeof(T)) return nullptr;
  T* ptr = nullptr;
  cuda_last_status status{cudaMalloc(&ptr, count * sizeof(T))};
  if (!status) return nullptr;
  return ptr;
}

// Free CUDA device memory allocated by `cuda_malloc`. Does nothing if `ptr` is
// `nullptr`.
template<typename T>
void cuda_free(T* ptr) {
  if (!ptr) return;
  cudaFree(ptr);
}

// Copy `count` objects of type `T`, where `kind` identifies which parameter is
// host or device.
template<typename T>
cuda_last_status
cuda_copy(T* dest_ptr, const T* src_ptr, size_t count, cudaMemcpyKind kind) {
  return cuda_last_status{
      cudaMemcpy(dest_ptr, src_ptr, count * sizeof(T), kind)};
}

} // namespace details

#pragma endregion details
#pragma region cuda_ptr

// Owning, move-only RAII handle to an uninitialized block of `count` objects
// of type `T` in CUDA device memory.
//
// The constructor allocates, leaving the value null on failure.
template<typename T>
class cuda_ptr {
  static_assert(std::is_trivially_copyable_v<T>,
      "cuda_ptr<T> requires a trivially copyable T: device memory is copied "
      "as raw bytes and never constructed.");

public:
#pragma region Construction

  // Allocates but does not initialize device memory for `count` objects of
  // type `T`.
  explicit cuda_ptr(size_t count = 1)
      : ptr_{details::cuda_malloc<T>(count)}, count_{count} {}

  cuda_ptr(const cuda_ptr&) = delete;
  cuda_ptr& operator=(const cuda_ptr&) = delete;

  cuda_ptr(cuda_ptr&& other) noexcept
      : ptr_{std::exchange(other.ptr_, nullptr)},
        count_{std::exchange(other.count_, 0)} {}
  cuda_ptr& operator=(cuda_ptr&& other) noexcept {
    if (this != &other) {
      details::cuda_free(ptr_);
      ptr_ = std::exchange(other.ptr_, nullptr);
      count_ = std::exchange(other.count_, 0);
    }
    return *this;
  }

  ~cuda_ptr() { details::cuda_free(ptr_); }

#pragma endregion Construction
#pragma region Status

  [[nodiscard]] bool ok() const { return ptr_ != nullptr; }
  [[nodiscard]] explicit operator bool() const { return ok(); }
  [[nodiscard]] bool operator!() const { return !ok(); }

#pragma endregion Status
#pragma region Transfer

  // Store memory from the CUDA device into the host buffer. Copies `count`
  // objects, or the whole allocation when `count` is defaulted.
  [[nodiscard]] cuda_last_status store(T* host_ptr, size_t count = 0) const {
    if (count == 0) count = count_;
    assert(count <= count_ && "store array size exceeds allocated count");
    return details::cuda_copy(host_ptr, ptr_, count, cudaMemcpyDeviceToHost);
  }
  [[nodiscard]] cuda_last_status store(std::span<T> host_span) {
    return store(host_span.data(), host_span.size());
  }
  // Store a single object into `host_ref`.
  [[nodiscard]] cuda_last_status store(T& host_ref) const {
    return store(&host_ref, 1);
  }
  // Store into every element of `host_array` (`N` objects).
  template<size_t N>
  [[nodiscard]] cuda_last_status store(T (&host_array)[N]) const {
    return store(host_array, N);
  }

  // Load device memory from the host buffer at `host_ptr`. Copies `count`
  // objects, or the whole allocation when `count` is defaulted.
  [[nodiscard]] cuda_last_status load(const T* host_ptr, size_t count = 0) {
    if (count == 0) count = count_;
    assert(count <= count_ && "load array size exceeds allocated count");
    return details::cuda_copy(ptr_, host_ptr, count, cudaMemcpyHostToDevice);
  }
  [[nodiscard]] cuda_last_status load(std::span<const T> host_span) {
    return load(host_span.data(), host_span.size());
  }
  // Load a single object from `host_ref`.
  [[nodiscard]] cuda_last_status load(const T& host_ref) {
    return load(&host_ref, 1);
  }
  // Load every element of `host_array` (`N` objects).
  template<size_t N>
  [[nodiscard]] cuda_last_status load(const T (&host_array)[N]) {
    return load(host_array, N);
  }

  // TODO: Add overloads that take a `cuda_ptr` to do device-to-device.

#pragma endregion Transfer
#pragma region Accessors

  // Return address of device pointer; cannot be dereferenced on the host.
  [[nodiscard]] T* device_ptr() const noexcept { return ptr_; }
  [[nodiscard]] operator T*() const noexcept { return ptr_; }

#pragma endregion Accessors
#pragma region Data members
private:
  T* ptr_;
  size_t count_;

#pragma endregion Data members
};

#pragma endregion cuda_ptr

} // namespace corvid::cuda
