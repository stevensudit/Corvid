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

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>

#include <cuda_runtime.h>

#include "../enums/sequence_enum.h"
#include "../strings/string_literals.h"
#include "./cuda_handle.cuh"
#include "./cuda_status.cuh"

// CUDA memory management.
//
// CUDA allows you to allocate and free device memory, giving you a pointer
// that you can't dereference on the host. Instead, you explicitly copy between
// host and device memory.
//
// This is wrapped as `cuda_buffer<T>`, an owning, fixed-size array of `T` in
// device memory that knows its size, so it is the moral equivalent of a
// `std::vector` that cannot grow, or of `thrust::device_vector`.

namespace corvid::cuda {

#pragma region memcpy_kind

// Enum to wrap `cudaMemcpyKind`.
//
// NOLINTNEXTLINE(performance-enum-size)
enum class memcpy_kind : std::underlying_type_t<cudaMemcpyKind> {
  host_to_host = cudaMemcpyHostToHost,         // 0
  host_to_device = cudaMemcpyHostToDevice,     // 1
  device_to_host = cudaMemcpyDeviceToHost,     // 2
  device_to_device = cudaMemcpyDeviceToDevice, // 3
  inferred = cudaMemcpyDefault,                // 4
};

consteval auto corvid_enum_spec(memcpy_kind*) {
  return corvid::enums::sequence::make_sequence_enum_spec<memcpy_kind,
      "host_to_host,host_to_device,device_to_host,device_to_device,"
      "inferred">();
}

#pragma endregion
#pragma region cuda_buffer

// Owning, move-only RAII handle to an uninitialized block of `count` objects
// of type `T` in CUDA device memory.
template<typename T>
class cuda_buffer: public cuda_handle<T*, cudaFree, const_propagation::deep> {
  using base = cuda_handle<T*, cudaFree, const_propagation::deep>;
  static_assert(std::is_trivially_copyable_v<T>,
      "cuda_buffer<T> requires a trivially copyable T: device memory is "
      "copied "
      "as raw bytes and never constructed.");

public:
#pragma region Construction

  explicit cuda_buffer(std::nullptr_t) noexcept : base{nullptr}, size_{} {}

  // Allocate, but do not initialize, device memory for `count` objects of
  // type `T`, or throw.
  explicit cuda_buffer(size_t count = 1UZ)
      : cuda_buffer{make(count, on_failure::raise), count} {}

  // Allocate, or return a failed instance.
  //
  // Check with `operator bool`, and follow up with `cuda_last_status{}`.
  [[nodiscard]] static cuda_buffer try_create(size_t count = 1UZ) {
    return cuda_buffer{make(count, on_failure::ignore), count};
  }

#pragma endregion
#pragma region Accessors

  // The number of objects allocated.
  [[nodiscard]] size_t size() const noexcept { return size_; }

#pragma endregion
#pragma region Transfer

  // Store memory from the CUDA device into the host buffer.
  //
  // Copies the lesser of `count` and the allocation.
  [[nodiscard]] cuda_last_status
  store(T* host_ptr, size_t count = strings::npos) const {
    return copy(host_ptr, this->get(), std::min(count, size_),
        memcpy_kind::device_to_host);
  }
  // Store into `host_span`, which must not exceed the allocation.
  [[nodiscard]] cuda_last_status store(std::span<T> host_span) const {
    assert(host_span.size() <= size_);
    return store(host_span.data(), host_span.size());
  }
  // Store a single object into `host_ref`.
  [[nodiscard]] cuda_last_status store(T& host_ref) const {
    return store(&host_ref, 1);
  }
  // Store into every element of `host_array`, which must not exceed the
  // allocation.
  template<size_t N>
  [[nodiscard]] cuda_last_status store(T (&host_array)[N]) const {
    assert(N <= size_);
    return store(host_array, N);
  }

  // Load device memory from the host buffer at `host_ptr`.
  //
  // Copies the lesser of `count` and the allocation, so the default loads
  // the whole allocation and a zero `count` loads nothing.
  [[nodiscard]] cuda_last_status
  load(const T* host_ptr, size_t count = strings::npos) {
    return copy(this->get(), host_ptr, std::min(count, size_),
        memcpy_kind::host_to_device);
  }
  // Load from `host_span`, which must not exceed the allocation.
  [[nodiscard]] cuda_last_status load(std::span<const T> host_span) {
    assert(host_span.size() <= size_);
    return load(host_span.data(), host_span.size());
  }
  // Load a single object from `host_ref`.
  [[nodiscard]] cuda_last_status load(const T& host_ref) {
    return load(&host_ref, 1);
  }
  // Load every element of `host_array`, which must not exceed the allocation.
  template<size_t N>
  [[nodiscard]] cuda_last_status load(const T (&host_array)[N]) {
    assert(N <= size_);
    return load(host_array, N);
  }

  // Load every element of `device`, another buffer, which must not exceed the
  // allocation.
  [[nodiscard]] cuda_last_status load(const cuda_buffer& device) {
    assert(device.size() <= size_);
    return copy(this->get(), device.get(), device.size(),
        memcpy_kind::device_to_device);
  }

#pragma endregion
#pragma region Helpers

  // Copy `count` objects of type `T`, where `kind` identifies which parameter
  // is host or device.
  [[nodiscard]] static cuda_last_status
  copy(T* dest_ptr, const T* src_ptr, size_t count, memcpy_kind kind) {
    return cuda_last_status{cudaMemcpy(dest_ptr, src_ptr, count * sizeof(T),
        static_cast<cudaMemcpyKind>(kind))};
  }

private:
  cuda_buffer(T* ptr, size_t count) noexcept : base{ptr}, size_{count} {}

  // Allocate device memory for `count` objects of type `T`, failing per
  // `policy` when the byte count would overflow or the allocation fails.
  [[nodiscard]] static T* make(size_t count, on_failure policy) {
    if (count > std::numeric_limits<size_t>::max() / sizeof(T)) {
      if (policy == on_failure::raise)
        throw std::runtime_error{"cuda_buffer byte count overflows size_t"};
      return nullptr;
    }
    constexpr auto malloc_overload = [](T** ptr, size_t bytes) {
      return cudaMalloc(ptr, bytes);
    };
    return base::template create<malloc_overload>(policy, count * sizeof(T));
  }

#pragma endregion
#pragma region Data members
private:
  size_t size_ = 1UZ;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
