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

// d3d11.h pulls windows.h's min/max macros; keep them out.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>

#include <iterator>

#include "./com_ptr.h"
#include "./hr_status.h"

namespace corvid::win32::d3d {

#pragma region d3d11_device

// RAII for a Direct3D 11 device and its immediate context.
//
// We own this device; the swapchain and every GPU resource are created from
// it.
class d3d11_device {
public:
#pragma region Construction

  d3d11_device() {
    const D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0};
    hr_status{
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            levels, std::size(levels), D3D11_SDK_VERSION, device_.put(),
            nullptr, context_.put())}
        .or_throw();
  }

  // Pinned: the bundled immediate context is single-threaded and must not be
  // shared. Deleting the move ops also deletes the implicit copy ops, so a
  // future copyable com_ptr can't silently make this copyable.
  d3d11_device(d3d11_device&&) = delete;
  d3d11_device& operator=(d3d11_device&&) = delete;

#pragma endregion
#pragma region Accessors

  // The device is a free-threaded D3D interface for a GPU, acting as a factory
  // for GPU resources and the owner of resources shared across its contexts.
  [[nodiscard]] ID3D11Device* device() const noexcept { return device_.get(); }

  // The context is the single-threaded interface for issuing operations such
  // as copies, compute dispatches, and rendering to the GPU.
  [[nodiscard]] ID3D11DeviceContext* context() const noexcept {
    return context_.get();
  }

#pragma endregion
#pragma region Data members
private:
  com_ptr<ID3D11Device> device_;
  com_ptr<ID3D11DeviceContext> context_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::win32::d3d
