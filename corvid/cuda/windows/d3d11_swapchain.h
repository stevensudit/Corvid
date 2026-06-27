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
#include <dxgi1_5.h>

#include "../../enums/bitmask_enum.h"
#include "../../meta/crossplatform.h"
#include "./com_ptr.h"
#include "./d3d11_device.h"
#include "./hr_status.h"

namespace corvid::win32::d3d {

#pragma region d3d11_bind_flag

// Bitmask wrapper for `D3D11_BIND_FLAG`.
// NOLINTNEXTLINE(performance-enum-size)
enum class d3d11_bind_flag : unsigned {
  none = 0,
  vertex_buffer = D3D11_BIND_VERTEX_BUFFER,
  index_buffer = D3D11_BIND_INDEX_BUFFER,
  constant_buffer = D3D11_BIND_CONSTANT_BUFFER,
  shader_resource = D3D11_BIND_SHADER_RESOURCE,
  stream_output = D3D11_BIND_STREAM_OUTPUT,
  render_target = D3D11_BIND_RENDER_TARGET,
  depth_stencil = D3D11_BIND_DEPTH_STENCIL,
  unordered_access = D3D11_BIND_UNORDERED_ACCESS,
  decoder = D3D11_BIND_DECODER,
  video_encoder = D3D11_BIND_VIDEO_ENCODER,
};

consteval auto corvid_enum_spec(d3d11_bind_flag*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<d3d11_bind_flag,
      "video_encoder,decoder,-,unordered_access,depth_stencil,render_"
      "target,stream_output,shader_resource,constant_buffer,index_buffer,"
      "vertex_buffer">();
}

#pragma endregion
#pragma region d3d11_swapchain

// RAII for a flip-model DXGI swapchain bound to a Win32 window.
//
// Created from a `d3d11_device` and the Win32 `HWND`, sized to the window's
// client area, double-buffered, `FLIP_DISCARD`.
//
// When the window resizes, call `resize()`; to rebind to a new device after a
// lost device, call `reset()`.
class d3d11_swapchain {
public:
#pragma region Construction

  // An empty swapchain holding no buffers; call `reset` to build it for a
  // device and window.
  d3d11_swapchain() = default;

  explicit d3d11_swapchain(const d3d11_device& device, HWND hwnd) {
    reset(device, hwnd).or_throw();
  }

  // Release the current swapchain (if any) and rebuild it for the given device
  // and window.
  [[nodiscard]] hr_status reset(const d3d11_device& device, HWND hwnd) {
    back_buffer_.reset();
    swapchain_.reset();
    // D3D11's deferred destruction is flushed before the new one is created,
    // because DXGI binds at most one flip-model swapchain to an `HWND` at a
    // time and defers the teardown that would otherwise free it.
    if (context_) {
      context_->ClearState();
      context_->Flush();
    }
    device_ = device.device();
    context_ = device.context();
    hwnd_ = hwnd;
    // Tearing (flip-model uncapped present) is opt-in: query it once and, when
    // present, create the swapchain with the flag so a later `present(0)` can
    // run without the refresh-rate cap. Without it, a flip-model swapchain
    // stays locked to vblank even at sync interval 0.
    const com_ptr<IDXGIFactory2> factory = device.make_factory();
    tearing_ = supports_tearing(factory.get());
    const DXGI_SWAP_CHAIN_DESC1 desc{
        .Width = 0,
        .Height = 0,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .Stereo = FALSE,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = swap_chain_flags(),
    };
    if (hr_status st{factory->CreateSwapChainForHwnd(device_, hwnd, &desc,
            nullptr, nullptr, swapchain_.put())};
        !st)
      return st;
    if (hr_status st{acquire_back_buffer()}; !st) return st;
    window_width_ = buffer_width_;
    window_height_ = buffer_height_;
    return hr_status{S_OK};
  }

#pragma endregion
#pragma region Accessors

  // The backbuffer texture handle.
  //
  // After presenting, the runtime rotates the buffers so that this handle now
  // points to the new back buffer.
  [[nodiscard]] const com_ptr<ID3D11Texture2D>& back_buffer() const noexcept {
    return back_buffer_;
  }

  // Backbuffer dimensions in pixels: the size CUDA and the kernel render to.
  // Cached, because only `resize` changes it.
  template<typename T = UINT>
  [[nodiscard]] T buffer_width() const noexcept {
    return static_cast<T>(buffer_width_);
  }
  template<typename T = UINT>
  [[nodiscard]] T buffer_height() const noexcept {
    return static_cast<T>(buffer_height_);
  }

  // Window client-area size in pixels, 0 when minimized or collapsed. Updated
  // by `resize`, so it holds the size as of the last resize check.
  template<typename T = UINT>
  [[nodiscard]] T window_width() const noexcept {
    return static_cast<T>(window_width_);
  }
  template<typename T = UINT>
  [[nodiscard]] T window_height() const noexcept {
    return static_cast<T>(window_height_);
  }

  // The window this swapchain presents to.
  [[nodiscard]] HWND hwnd() const noexcept { return hwnd_; }

#pragma endregion
#pragma region Factory

  // Create a texture of the given size, otherwise sharing the backbuffer's
  // format and sample count, so it is a valid `fill_back_buffer` source.
  //
  // The size may exceed the backbuffer (`fill_back_buffer` copies only the
  // live backbuffer-sized region), which is how a grow-only render target
  // avoids reallocating on every resize.
  [[nodiscard]] com_ptr<ID3D11Texture2D>
  create_texture(UINT width, UINT height, d3d11_bind_flag bind_flags) const {
    D3D11_TEXTURE2D_DESC desc = back_buffer_desc();
    desc.Width = width;
    desc.Height = height;
    desc.BindFlags = *bind_flags;
    com_ptr<ID3D11Texture2D> texture;
    hr_status{device_->CreateTexture2D(&desc, nullptr, texture.put())}
        .or_throw();
    return texture;
  }

  // Create a texture matching the backbuffer that can be written to and then
  // used with `fill_back_buffer`.
  [[nodiscard]] com_ptr<ID3D11Texture2D> create_matching_texture(
      d3d11_bind_flag bind_flags) const {
    return create_texture(buffer_width_, buffer_height_, bind_flags);
  }

#pragma endregion
#pragma region Present

  // Copy a source texture's top-left, backbuffer-sized region into the
  // backbuffer through the immediate context.
  //
  // The source must share the backbuffer's format and sample count and be at
  // least its size; `create_texture` / `create_matching_texture` build one. A
  // larger source is allowed: only the live `buffer_width` x `buffer_height`
  // region is copied, so a grow-only render target can stay bound across
  // resizes.
  void fill_back_buffer(ID3D11Resource* source) {
    const D3D11_BOX box{
        .left = 0,
        .top = 0,
        .front = 0,
        .right = buffer_width_,
        .bottom = buffer_height_,
        .back = 1,
    };
    context_->CopySubresourceRegion(back_buffer_, 0, 0, 0, 0, source, 0, &box);
  }

  // Present the backbuffer. `sync_interval` is the number of vertical blanks
  // to wait for; 0 presents uncapped. The tearing present flag is paired with
  // sync interval 0 and tearing support, since DXGI rejects it otherwise; only
  // then does an uncapped present actually run past the refresh rate.
  [[nodiscard]] hr_status present(int sync_interval = 1) {
    const UINT flags =
        (sync_interval == 0 && tearing_)
            ? static_cast<UINT>(DXGI_PRESENT_ALLOW_TEARING)
            : 0U;
    return hr_status{swapchain_->Present(sync_interval, flags)};
  }

  // Whether a status from `present` or `resize` signals a lost device, which
  // invalidates the device and everything built from it. Recovery is to
  // recreate the device and rebuild downstream, not to retry the call.
  [[nodiscard]] static bool is_device_lost(const hr_status& status) noexcept {
    return status.value() == DXGI_ERROR_DEVICE_REMOVED ||
           status.value() == DXGI_ERROR_DEVICE_RESET;
  }

#pragma endregion
#pragma region Resize

  // Rebuild the buffers to the window's current client area, recording that
  // area as the window size. Returns `S_FALSE` when no-op.
  [[nodiscard]] hr_status resize() {
    RECT client{};
    if (!GetClientRect(hwnd_, &client)) return hr_status{S_FALSE};
    window_width_ = client.right - client.left;
    window_height_ = client.bottom - client.top;
    if (window_width_ == 0 || window_height_ == 0)
      return hr_status{S_FALSE}; // no client area
    if (window_width_ == buffer_width_ && window_height_ == buffer_height_)
      return hr_status{S_FALSE}; // unchanged

    back_buffer_.reset();
    if (hr_status st{swapchain_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN,
            swap_chain_flags())};
        !st)
      return st;
    return acquire_back_buffer();
  }

#pragma endregion
#pragma region Description

  // The backbuffer's full texture desc: the template a matching texture is
  // built from.
  [[nodiscard]] D3D11_TEXTURE2D_DESC back_buffer_desc() const noexcept {
    D3D11_TEXTURE2D_DESC desc{};
    back_buffer_->GetDesc(&desc);
    return desc;
  }

#pragma endregion
#pragma region Helpers
private:
  // Acquire back buffer 0 and read its dimensions.
  [[nodiscard]] hr_status acquire_back_buffer() {
    hr_status st{swapchain_->GetBuffer(0, IID_PPV_ARGS(back_buffer_.put()))};
    if (!st) return st;

    const auto tex_desc = back_buffer_desc();
    buffer_width_ = tex_desc.Width;
    buffer_height_ = tex_desc.Height;
    return st;
  }

  // The swapchain creation/resize flags: the tearing flag when supported, kept
  // in one place so creation and every `ResizeBuffers` stay consistent (DXGI
  // requires the resize to preserve the original flags).
  [[nodiscard]] UINT swap_chain_flags() const noexcept {
    return tearing_ ? static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)
                    : 0U;
  }

  // Whether the DXGI factory supports flip-model tearing. Needs IDXGIFactory5
  // (DXGI 1.5+); false on older runtimes or adapters that lack it.
  [[nodiscard]] static bool supports_tearing(IDXGIFactory2* factory) {
    com_ptr<IDXGIFactory5> factory5;
    if (FAILED(factory->QueryInterface(IID_PPV_ARGS(factory5.put()))))
      return false;
    BOOL allowed = FALSE;
    if (FAILED(factory5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed))))
      return false;
    return allowed != FALSE;
  }

#pragma endregion
#pragma region Data members
private:
  com_ptr<ID3D11Device> device_;
  com_ptr<ID3D11DeviceContext> context_;
  HWND hwnd_{};
  com_ptr<IDXGISwapChain1> swapchain_;
  com_ptr<ID3D11Texture2D> back_buffer_;
  UINT buffer_width_{};
  UINT buffer_height_{};
  UINT window_width_{};
  UINT window_height_{};
  bool tearing_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::win32::d3d
