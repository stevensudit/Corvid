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

#include "./com_ptr.h"
#include "./d3d11_device.h"
#include "./d3d11_status.h"

namespace corvid::d3d {

#pragma region d3d11_swapchain

// RAII for a flip-model DXGI swapchain bound to a Win32 window, together with
// the render-target view of its current backbuffer.
//
// Created from a `d3d11_device` and the window's `HWND` (which SDL supplies
// for its window), sized to the window's client area, double-buffered,
// `FLIP_DISCARD`.
//
// The viewer's window is not resizable, so the swapchain is sized once and
// never rebuilt; `ResizeBuffers` handling lands when a resizable window needs
// it.
class d3d11_swapchain {
public:
#pragma region Construction

  explicit d3d11_swapchain(const d3d11_device& device, HWND hwnd) {
    // Reach the factory that owns the device's adapter; a swapchain must come
    // from the device's own DXGI factory.
    com_ptr<IDXGIDevice> dxgi_device;
    hr_status{device.device()->QueryInterface(IID_PPV_ARGS(dxgi_device.put()))}
        .or_throw();
    com_ptr<IDXGIAdapter> adapter;
    hr_status{dxgi_device->GetAdapter(adapter.put())}.or_throw();
    com_ptr<IDXGIFactory2> factory;
    hr_status{adapter->GetParent(IID_PPV_ARGS(factory.put()))}.or_throw();

    // Width and height left zero: CreateSwapChainForHwnd takes them from the
    // window's client area.
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    hr_status{factory->CreateSwapChainForHwnd(device.device(), hwnd, &desc,
                  nullptr, nullptr, swapchain_.put())}
        .or_throw();

    com_ptr<ID3D11Texture2D> backbuffer;
    hr_status{swapchain_->GetBuffer(0, IID_PPV_ARGS(backbuffer.put()))}
        .or_throw();
    hr_status{device.device()->CreateRenderTargetView(backbuffer.get(),
                  nullptr, rtv_.put())}
        .or_throw();
  }

#pragma endregion
#pragma region Accessors

  // Render-target view of the current backbuffer, to clear or bind as the
  // output target.
  [[nodiscard]] ID3D11RenderTargetView* render_target() const noexcept {
    return rtv_.get();
  }

#pragma endregion
#pragma region Present

  // Present the backbuffer. `sync_interval` 0 presents uncapped, the
  // flip-model default for this viewer, where the present is never the
  // bottleneck; 1 and up sync to that many vertical blanks.
  [[nodiscard]] hr_status present(UINT sync_interval = 0) {
    return hr_status{swapchain_->Present(sync_interval, 0)};
  }

#pragma endregion
#pragma region Data members
private:
  com_ptr<IDXGISwapChain1> swapchain_;
  com_ptr<ID3D11RenderTargetView> rtv_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::d3d
