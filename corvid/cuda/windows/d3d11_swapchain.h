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
      "video_encoder, decoder, -, unordered_access, depth_stencil, "
      "render_target, stream_output, shader_resource, constant_buffer, "
      "index_buffer, vertex_buffer">();
}

#pragma endregion
#pragma region d3d11_swapchain

// RAII for a flip-model DXGI swapchain bound to a Win32 window.
//
// Created from a `d3d11_device` and the Win32 `HWND`, sized to the window's
// client area, double-buffered, `FLIP_DISCARD`.
//
// The viewer's window is not resizable, so the swapchain is sized once and
// never rebuilt; `ResizeBuffers` handling lands when a resizable window needs
// it.
class d3d11_swapchain {
public:
#pragma region Construction

  explicit d3d11_swapchain(const d3d11_device& device, HWND hwnd)
      : device_{device.device()}, context_{device.context()} {
    // Width and height are taken from the window's client area.
    DXGI_SWAP_CHAIN_DESC1 desc{
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = {.Count = 1},
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = 2,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
    };
    hr_status{device.make_factory()->CreateSwapChainForHwnd(device_, hwnd,
                  &desc, nullptr, nullptr, swapchain_.put())}
        .or_throw();

    hr_status{swapchain_->GetBuffer(0, IID_PPV_ARGS(back_buffer_.put()))}
        .or_throw();

    const auto tex_desc = back_buffer_desc();
    width_ = tex_desc.Width;
    height_ = tex_desc.Height;
  }

#pragma endregion
#pragma region Accessors

  // The backbuffer texture handle, stable across `Present` in the D3D11 flip
  // model: the runtime rotates the buffers underneath this one handle.
  // Returned as the shared handle so a caller can copy it.
  [[nodiscard]] const com_ptr<ID3D11Texture2D>& back_buffer() const noexcept {
    return back_buffer_;
  }

  // Backbuffer dimensions in pixels (the window's client area at creation).
  [[nodiscard]] UINT width() const noexcept { return width_; }
  [[nodiscard]] UINT height() const noexcept { return height_; }

#pragma endregion
#pragma region Factory

  // Create a texture matching the backbuffer in size, format, and sample
  // count, so it is a valid `CopyResource` peer, with `bind_flags` for the
  // caller's own use of it.
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

  // Copy a resource into the backbuffer through the immediate context. The
  // source must be a valid `CopyResource` peer of the backbuffer (same size,
  // format, and sample count); `create_matching_texture` builds one.
  void fill_back_buffer(ID3D11Resource* source) {
    context_->CopyResource(back_buffer_, source);
  }

  // Present the backbuffer. `sync_interval` 0 presents uncapped, the
  // flip-model default for this viewer, where the present is never the
  // bottleneck; 1 and up sync to that many vertical blanks.
  [[nodiscard]] hr_status present(UINT sync_interval = 0) {
    return hr_status{swapchain_->Present(sync_interval, 0)};
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
#pragma region Data members
private:
  com_ptr<ID3D11Device> device_;
  com_ptr<ID3D11DeviceContext> context_;
  com_ptr<IDXGISwapChain1> swapchain_;
  com_ptr<ID3D11Texture2D> back_buffer_;
  UINT width_{};
  UINT height_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::win32::d3d
