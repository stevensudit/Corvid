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

// d3d11.h/dxgi1_2.h pull windows.h's min/max macros; keep them out.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <dxgi1_2.h>

#include <iterator>

#include "./com_ptr.h"
#include "./hr_status.h"

namespace corvid::win32::d3d {

#pragma region d3d11_device

// RAII handle to a Direct3D 11 device and its immediate context.
//
// The immediate context is single-threaded: sharing a handle does not make
// concurrent use of it safe.
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

#pragma endregion
#pragma region Accessors

  // The device is a free-threaded D3D interface for a GPU, acting as a factory
  // for GPU resources and the owner of resources shared across its contexts.
  [[nodiscard]] const com_ptr<ID3D11Device>& device() const noexcept {
    return device_;
  }

  // The context is the single-threaded interface for issuing operations such
  // as copies, compute dispatches, and rendering to the GPU.
  [[nodiscard]] const com_ptr<ID3D11DeviceContext>& context() const noexcept {
    return context_;
  }

  // Reach the DXGI factory that owns this device's adapter.
  //
  // An object built from this device, such as a swapchain, must come from the
  // device's own factory.
  [[nodiscard]] com_ptr<IDXGIFactory2> make_factory() const {
    com_ptr<IDXGIDevice> dxgi_device;
    hr_status{device_->QueryInterface(IID_PPV_ARGS(dxgi_device.put()))}
        .or_throw();
    com_ptr<IDXGIAdapter> adapter;
    hr_status{dxgi_device->GetAdapter(adapter.put())}.or_throw();
    com_ptr<IDXGIFactory2> factory;
    hr_status{adapter->GetParent(IID_PPV_ARGS(factory.put()))}.or_throw();
    return factory;
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
