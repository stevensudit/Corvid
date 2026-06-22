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

// The DX11 backend uses full D3D11 types; pull them in (keeping windows.h's
// min/max macros out) before the backend header, which only forward-declares
// them.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_sdl3.h"

#include "../../sdl/sdl_event.h"
#include "./com_ptr.h"

namespace corvid::cuda {

#pragma region imgui_overlay

// RAII owner of the Dear ImGui context and its SDL3 and D3D11 backends, for
// drawing a UI overlay on top of a `cuda_d3d11_presenter` frame.
//
// One instance owns the whole ImGui lifecycle: construct it once, feed it
// every SDL event with `process_event`, open a frame with `begin_frame`, build
// the UI with ImGui calls, and `render` it onto the backbuffer between the
// presenter's copy and present (drive that through the presenter's overlay
// overload). Each frame must pair one `begin_frame` with one `render`.
//
// Non-copyable and non-movable: it holds the single global ImGui context. The
// device and context are borrowed, so the presenter that owns them must
// outlive the overlay.
class imgui_overlay {
public:
#pragma region Construction

  explicit imgui_overlay(SDL_Window* window, ID3D11Device* device,
      ID3D11DeviceContext* context)
      : device_{device}, context_{context} {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // Scale the whole UI up: the default 13px font is small on a high-DPI
    // display. A global scale keeps it asset-free (no bundled TTF); swap in a
    // larger font later if the upscaled bitmap reads too soft.
    io.FontGlobalScale = 1.5F;
    // Don't persist window layout to imgui.ini: a tuning panel should open at
    // its configured default placement every run, not wherever it last sat.
    io.IniFilename = nullptr;
    ImGui_ImplSDL3_InitForD3D(window);
    ImGui_ImplDX11_Init(device, context);
  }

  imgui_overlay(const imgui_overlay&) = delete;
  imgui_overlay& operator=(const imgui_overlay&) = delete;
  imgui_overlay(imgui_overlay&&) = delete;
  imgui_overlay& operator=(imgui_overlay&&) = delete;

  ~imgui_overlay() {
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
  }

#pragma endregion
#pragma region Frame

  // Feed one SDL event to the backend so ImGui tracks input.
  //
  // Forward every event, even while the panel is closed, so capture state
  // stays current.
  void process_event(const sdl::sdl_event& ev) {
    ImGui_ImplSDL3_ProcessEvent(&ev.raw());
  }

  // Open a new ImGui frame; build the UI after this and before `render`.
  void begin_frame() {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
  }

  // Draw the built UI onto `back_buffer`.
  //
  // Builds a render target view of the backbuffer fresh (it rotates each
  // present), so pass the presenter's live `back_buffer` each frame.
  void render(ID3D11Texture2D* back_buffer) {
    ImGui::Render();
    win32::com_ptr<ID3D11RenderTargetView> rtv;
    device_->CreateRenderTargetView(back_buffer, nullptr, rtv.put());
    if (!rtv) return;
    ID3D11RenderTargetView* rtv_raw = rtv.get();
    context_->OMSetRenderTargets(1, &rtv_raw, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  }

#pragma endregion
#pragma region Accessors

  // Whether ImGui is consuming the mouse or keyboard this frame, so the game
  // can leave that input to the panel.
  [[nodiscard]] bool wants_mouse() const {
    return ImGui::GetIO().WantCaptureMouse;
  }
  [[nodiscard]] bool wants_keyboard() const {
    return ImGui::GetIO().WantCaptureKeyboard;
  }

#pragma endregion
#pragma region Data members
private:
  ID3D11Device* device_;
  ID3D11DeviceContext* context_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
