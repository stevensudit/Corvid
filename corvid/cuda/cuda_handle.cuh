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

#include <utility>
#include <stdexcept>

// Generic ownership of a CUDA handle.

namespace corvid::cuda {

#pragma region cuda_handle

// Move-only RAII owner of a CUDA handle of type `H`, released by calling
// `Destroy` on it (a function such as `cudaFreeArray` or
// `cudaDestroySurfaceObject` that takes the handle).
//
// The empty state is `H{}`, which is a null pointer for the pointer-shaped
// handles and `0` for the opaque integer handles like `cudaSurfaceObject_t`,
// so one definition serves both.
//
// It adopts an already-created handle and never allocates or creates, because
// that step varies per resource and belongs to the wrapper. Use it either way:
// embed it as a member and forward `get`, so a type owns a handle by rule of
// zero; or derive from it, as `cuda_array_3d` does, where the derived
// constructor makes the handle and passes it down, and the
// derived type inherits `get` and the conversion to `H`, so it passes wherever
// the raw handle is expected and only adds its own construction and methods.
template<typename H, auto Destroy>
class cuda_handle {
public:
#pragma region Construction

  cuda_handle() noexcept = default;
  explicit cuda_handle(H handle) noexcept : handle_{handle} {}

  cuda_handle(const cuda_handle&) = delete;
  cuda_handle& operator=(const cuda_handle&) = delete;

  cuda_handle(cuda_handle&& other) noexcept
      : handle_{std::exchange(other.handle_, H{})} {}
  cuda_handle& operator=(cuda_handle&& other) noexcept {
    if (this != &other) {
      destroy();
      handle_ = std::exchange(other.handle_, H{});
    }
    return *this;
  }
  ~cuda_handle() { destroy(); }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] H get() const noexcept { return handle_; }
  [[nodiscard]] operator H() const noexcept { return handle_; }

  [[nodiscard]] bool ok() const noexcept { return handle_ != H{}; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  H operator*() const {
    if (handle_ == H{})
      throw std::runtime_error{"dereferencing null cuda_handle"};
    return handle_;
  }

#pragma endregion
#pragma region Helpers
private:
  void destroy() const noexcept {
    if (handle_) Destroy(handle_);
  }

#pragma endregion
#pragma region Data members
protected:
  H handle_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
