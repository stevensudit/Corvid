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
      "video_encoder,decoder,decoder,unordered_access,depth_stencil,render_"
      "target,stream_output, shader_resource,constant_buffer,index_buffer,"
      "vertex_buffer">();
}

#pragma endregion
#pragma region d3d11_swapchain

// RAII for a flip-model DXGI swapchain bound to a Win32 window.
//
// Created from a `d3d11_device` and the Win32 `HWND`, sized to the window's
// client area, double-buffered, `FLIP_DISCARD`.
//
// When the window resizes, call `resize()`.
class d3d11_swapchain {
public:
#pragma region Construction

  explicit d3d11_swapchain(const d3d11_device& device, HWND hwnd)
      : device_{device.device()}, context_{device.context()}, hwnd_{hwnd} {
    DXGI_SWAP_CHAIN_DESC1 desc{
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
        .Flags = 0,
    };
    hr_status{device.make_factory()->CreateSwapChainForHwnd(device_, hwnd,
                  &desc, nullptr, nullptr, swapchain_.put())}
        .or_throw();

    acquire_back_buffer().or_throw();
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

  // Backbuffer dimensions in pixels (derived from the window's client area).
  [[nodiscard]] UINT width() const noexcept { return width_; }
  [[nodiscard]] UINT height() const noexcept { return height_; }

#pragma endregion
#pragma region Factory

  // Create a texture matching the backbuffer that can be written to and then
  // used with `fill_back_buffer`.
  [[nodiscard]] com_ptr<ID3D11Texture2D> create_matching_texture(
      d3d11_bind_flag bind_flags) const {
    D3D11_TEXTURE2D_DESC desc = back_buffer_desc();
    desc.BindFlags = *bind_flags;
    com_ptr<ID3D11Texture2D> texture;
    hr_status{device_->CreateTexture2D(&desc, nullptr, texture.put())}
        .or_throw();
    return texture;
  }

#pragma endregion
#pragma region Present

  // Copy a resource into the backbuffer through the immediate context.
  //
  // The source must be a valid `CopyResource` peer of the backbuffer (same
  // size, format, and sample count); `create_matching_texture` builds one.
  void fill_back_buffer(ID3D11Resource* source) {
    context_->CopyResource(back_buffer_, source);
  }

  // Present the backbuffer. `sync_interval` is the number of vertical blanks
  // to wait for; 0 presents uncapped.
  [[nodiscard]] hr_status present(UINT sync_interval = 1) {
    return hr_status{swapchain_->Present(sync_interval, 0)};
  }

#pragma endregion
#pragma region Resize

  // Rebuild the buffers to the window's current client area. Returns `S_FALSE`
  // when no-op.
  [[nodiscard]] hr_status resize() {
    RECT client{};
    if (!GetClientRect(hwnd_, &client)) return hr_status{S_OK};
    const auto w = static_cast<UINT>(client.right - client.left);
    const auto h = static_cast<UINT>(client.bottom - client.top);
    if (w == 0 || h == 0) return hr_status{S_FALSE};            // minimized
    if (w == width_ && h == height_) return hr_status{S_FALSE}; // unchanged

    back_buffer_.reset();
    if (hr_status st{
            swapchain_->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0)};
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
    if (hr_status st{
            swapchain_->GetBuffer(0, IID_PPV_ARGS(back_buffer_.put()))};
        !st)
      return st;

    const auto tex_desc = back_buffer_desc();
    width_ = tex_desc.Width;
    height_ = tex_desc.Height;
    return hr_status{S_OK};
  }

#pragma endregion
#pragma region Data members
private:
  com_ptr<ID3D11Device> device_;
  com_ptr<ID3D11DeviceContext> context_;
  HWND hwnd_;
  com_ptr<IDXGISwapChain1> swapchain_;
  com_ptr<ID3D11Texture2D> back_buffer_;
  UINT width_{};
  UINT height_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::win32::d3d
