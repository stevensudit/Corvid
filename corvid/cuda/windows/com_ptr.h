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

// unknwn.h pulls windows.h's min/max macros; keep them out.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <unknwn.h>

#include <concepts>
#include <utility>

namespace corvid::d3d {

#pragma region com_ptr

// RAII handle to a COM interface.
//
// The moral equivalent of `std::unique_ptr` for COM: the destructor calls
// `Release`, so one reference is held for the handle's lifetime.
//
// Construction adopts an existing reference without calling `AddRef`, matching
// the COM convention that a creating call (`Create*`, `QueryInterface`,
// `GetBuffer`) hands back a reference the caller already owns. Use `put` to
// receive such a reference into an out-parameter.
template<std::derived_from<IUnknown> T>
class com_ptr {
public:
#pragma region Construction

  com_ptr() noexcept = default;

  // Adopt an already-owned reference, without an `AddRef`.
  explicit com_ptr(T* ptr) noexcept : ptr_{ptr} {}

  com_ptr(const com_ptr&) = delete;
  com_ptr& operator=(const com_ptr&) = delete;

  com_ptr(com_ptr&& other) noexcept
      : ptr_{std::exchange(other.ptr_, nullptr)} {}
  com_ptr& operator=(com_ptr&& other) noexcept {
    if (this != &other) {
      reset();
      ptr_ = std::exchange(other.ptr_, nullptr);
    }
    return *this;
  }
  ~com_ptr() { reset(); }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] T* get() const noexcept { return ptr_; }
  T* operator->() const noexcept { return ptr_; }
  [[nodiscard]] explicit operator bool() const noexcept { return ptr_; }

  // Release any held reference and return the address of the now-null pointer,
  // to receive a fresh reference from a COM out-parameter, e.g.
  // `device->CreateRenderTargetView(tex, nullptr, rtv.put())`. Wrap with
  // `IID_PPV_ARGS` for the `void**`-typed `QueryInterface`-style calls.
  T** put() noexcept {
    reset();
    return &ptr_;
  }

  // Release any held reference now, leaving the handle null.
  void reset() noexcept {
    if (ptr_) {
      ptr_->Release();
      ptr_ = nullptr;
    }
  }

#pragma endregion
#pragma region Data members
private:
  T* ptr_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::d3d
