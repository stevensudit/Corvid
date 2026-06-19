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

#include "./com_ptr.h"
#include "./d3d11_device.h"
#include "./hr_status.h"

namespace corvid::win32::d3d {

#pragma region d3d11_swapchain

// RAII for a flip-model DXGI swapchain bound to a Win32 window.
//
// Created from a `d3d11_device` and the Win32 `HWND` (which SDL supplies for
// its window), sized to the window's client area, double-buffered,
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

    // Width and height left zero: `CreateSwapChainForHwnd` takes them from the
    // window's client area.
    DXGI_SWAP_CHAIN_DESC1 desc{
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = {.Count = 1},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
    };
    hr_status{factory->CreateSwapChainForHwnd(device.device(), hwnd, &desc,
                  nullptr, nullptr, swapchain_.put())}
        .or_throw();

    hr_status{swapchain_->GetBuffer(0, IID_PPV_ARGS(back_buffer_.put()))}
        .or_throw();
    D3D11_TEXTURE2D_DESC tex_desc{};
    back_buffer_->GetDesc(&tex_desc);
    width_ = tex_desc.Width;
    height_ = tex_desc.Height;
  }

#pragma endregion
#pragma region Accessors

  // The backbuffer texture, for CUDA interop registration. Stable across
  // `Present` in D3D11 flip model: the runtime rotates buffers underneath.
  [[nodiscard]] ID3D11Texture2D* back_buffer() const noexcept {
    return back_buffer_.get();
  }

  // Backbuffer dimensions in pixels (the window's client area at creation).
  [[nodiscard]] UINT width() const noexcept { return width_; }
  [[nodiscard]] UINT height() const noexcept { return height_; }

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
  com_ptr<ID3D11Texture2D> back_buffer_;
  UINT width_{};
  UINT height_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::win32::d3d
