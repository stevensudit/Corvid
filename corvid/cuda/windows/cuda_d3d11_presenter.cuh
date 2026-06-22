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
#include <concepts>
#include <utility>

#include "../../math/arithmetic.h"
#include "./com_ptr.h"
#include "./cuda_d3d11_interop.cuh"
#include "./d3d11_device.h"
#include "./d3d11_swapchain.h"

namespace corvid::cuda {

#pragma region cuda_d3d11_presenter

// A presentation pipeline for CUDA-rendered frames: a D3D11 device, its
// flip-model swapchain, and a grow-only render texture registered for CUDA
// surface writes.
//
// A frame maps the render target, lets a CUDA kernel write it in place, copies
// it to the backbuffer, and presents: all without leaving the GPU. The render
// target is sized to a grow-only capacity (each axis a 256-pixel multiple)
// held at or above the live size, so an ordinary resize neither reallocates it
// nor re-registers it with CUDA. A lost device is rebuilt in place.
//
// Drive a frame with `render`, or compose `target` and `present` yourself for
// more control. Call `resize` when the window's client area changes.
class cuda_d3d11_presenter {
public:
#pragma region Types

  template<typename T>
  using com_ptr = win32::com_ptr<T>;
  using hr_status = win32::hr_status;
  using d3d11_swapchain = corvid::win32::d3d::d3d11_swapchain;
  using d3d11_device = win32::d3d::d3d11_device;
  using d3d11_bind_flag = win32::d3d::d3d11_bind_flag;

#pragma endregion
#pragma region Construction

  explicit cuda_d3d11_presenter(HWND hwnd) : swapchain_{device_, hwnd} {
    ensure_target().or_throw();
  }

  cuda_d3d11_presenter(const cuda_d3d11_presenter&) = delete;
  cuda_d3d11_presenter& operator=(const cuda_d3d11_presenter&) = delete;
  cuda_d3d11_presenter(cuda_d3d11_presenter&&) = delete;
  cuda_d3d11_presenter& operator=(cuda_d3d11_presenter&&) = delete;

#pragma endregion
#pragma region Accessors

  // Backbuffer size in pixels: what the kernel renders to. Never 0.
  template<typename T = UINT>
  [[nodiscard]] T buffer_width() const noexcept {
    return swapchain_.buffer_width<T>();
  }
  template<typename T = UINT>
  [[nodiscard]] T buffer_height() const noexcept {
    return swapchain_.buffer_height<T>();
  }

  // Window client-area size in pixels, 0 when minimized or collapsed.
  template<typename T = UINT>
  [[nodiscard]] T window_width() const noexcept {
    return swapchain_.window_width<T>();
  }
  template<typename T = UINT>
  [[nodiscard]] T window_height() const noexcept {
    return swapchain_.window_height<T>();
  }

  // The CUDA-registered render target to map for a frame with a
  // `cuda_d3d11_mapping`.
  //
  // `render` does this for you; reach for it directly only to drive the
  // mapping, drawing, and presenting manually.
  [[nodiscard]] const cuda_d3d11_resource& target() const noexcept {
    return cuda_target_;
  }

  // The D3D11 device and its immediate context, for initializing an overlay's
  // rendering backend (e.g. Dear ImGui). Borrowed: valid for the presenter's
  // lifetime.
  [[nodiscard]] ID3D11Device* device() const noexcept {
    return device_.device().get();
  }
  [[nodiscard]] ID3D11DeviceContext* context() const noexcept {
    return device_.context().get();
  }

  // The live backbuffer, to build an overlay's render target view from.
  //
  // It rotates each present and is recreated on resize, so an overlay should
  // rebuild its view from this each frame rather than cache it.
  [[nodiscard]] ID3D11Texture2D* back_buffer() const noexcept {
    return swapchain_.back_buffer().get();
  }

#pragma endregion
#pragma region Frame

  // Resize the swapchain to the window's current client area and grow the
  // render target to fit, rebuilding a lost device in place.
  //
  // Returns a success status (`S_FALSE` when the size was unchanged), or a
  // genuine failure for the caller to handle.
  [[nodiscard]] hr_status resize() {
    auto st = swapchain_.resize();
    if (d3d11_swapchain::is_device_lost(st)) return recover_device();
    if (st && !st.is_false()) st = ensure_target();
    return st;
  }

  // Copy the render target into the backbuffer and present it.
  //
  // A lost device is rebuilt in place, dropping the frame so the next call
  // redraws on the fresh device.
  [[nodiscard]] hr_status present(int sync_interval = 1) {
    return present([] {}, sync_interval);
  }

  // As `present`, but run `overlay` to draw over the backbuffer between the
  // copy and the present, for an on-top UI.
  //
  // `overlay()` issues its D3D draws onto the live backbuffer; reach it via
  // `back_buffer`. The presenter stays UI-agnostic.
  [[nodiscard]] hr_status
  present(std::invocable auto&& overlay, int sync_interval = 1) {
    swapchain_.fill_back_buffer(render_texture_);
    overlay();
    auto st = swapchain_.present(sync_interval);
    if (d3d11_swapchain::is_device_lost(st)) st = recover_device();
    return st;
  }

  // Map the render target, hand its `cudaArray` and live size to `draw`, then
  // present.
  //
  // `draw(cudaArray_t array, UINT width, UINT height)` issues the CUDA work
  // that fills the array; the unmap before present is handled here.
  [[nodiscard]] hr_status
  render(std::invocable<cudaArray_t, int, int> auto&& draw,
      int sync_interval = 1) {
    return render(std::forward<decltype(draw)>(draw), [] {}, sync_interval);
  }

  // As `render(draw)`, plus the overlay step of the `present` overlay
  // overload: `overlay()` draws over the backbuffer before the present.
  [[nodiscard]] hr_status
  render(std::invocable<cudaArray_t, int, int> auto&& draw,
      std::invocable auto&& overlay, int sync_interval = 1) {
    if (cuda_d3d11_mapping map{cuda_target_})
      draw(map.array(), buffer_width(), buffer_height());
    return present(std::forward<decltype(overlay)>(overlay), sync_interval);
  }

#pragma endregion
#pragma region Helpers
private:
  // Grow the capacity-sized render texture to fit the live size, recreating it
  // and re-registering with CUDA only when it must grow.
  //
  // Never shrinks, so an ordinary resize neither reallocates nor re-registers
  // it.
  hr_status ensure_target() {
    const UINT need_w = round_up_to_multiple(buffer_width(), capacity_quantum);
    const UINT need_h =
        round_up_to_multiple(buffer_height(), capacity_quantum);
    if (render_texture_ && need_w <= cap_w_ && need_h <= cap_h_)
      return hr_status{S_FALSE};

    cap_w_ = std::max(cap_w_, need_w);
    cap_h_ = std::max(cap_h_, need_h);

    // unregister before releasing texture
    cuda_target_ = cuda_d3d11_resource{};
    render_texture_ = swapchain_.create_texture(cap_w_, cap_h_,
        d3d11_bind_flag::shader_resource);
    if (!render_texture_) return hr_status{E_FAIL};

    cuda_target_ = cuda_d3d11_resource{render_texture_};
    return hr_status{S_OK};
  }

  // Rebuild every GPU object after a lost device: a fresh device, the
  // swapchain rebound to it, and the render target recreated.
  hr_status recover_device() {
    cuda_target_ = cuda_d3d11_resource{};
    render_texture_ = com_ptr<ID3D11Texture2D>{};
    cap_w_ = 0;
    cap_h_ = 0;
    device_ = d3d11_device{};
    swapchain_.reset(device_, swapchain_.hwnd()).or_throw();
    return ensure_target();
  }

#pragma endregion
#pragma region Data members
private:
  d3d11_device device_;
  d3d11_swapchain swapchain_;
  com_ptr<ID3D11Texture2D> render_texture_;
  cuda_d3d11_resource cuda_target_;
  UINT cap_w_{};
  UINT cap_h_{};

  static constexpr UINT capacity_quantum = 256;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
