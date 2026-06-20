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
#include <cstdint>
#include <utility>

#include "../enums/bitmask_enum.h"
#include "./sdl_common.h"
#include "./sdl_status.h"

namespace corvid::sdl {

#pragma region sdl_window_flags

// Bitmask wrapper for `SDL_WindowFlags`: the creation and state flags for an
// SDL window. Mirrors the full `SDL_WINDOW_*` set, so it covers both the flags
// passed at creation and the ones SDL reports as window state.
// NOLINTNEXTLINE(performance-enum-size)
enum class sdl_window_flags : std::uint64_t {
  none = 0,
  fullscreen = SDL_WINDOW_FULLSCREEN,
  opengl = SDL_WINDOW_OPENGL,
  occluded = SDL_WINDOW_OCCLUDED,
  hidden = SDL_WINDOW_HIDDEN,
  borderless = SDL_WINDOW_BORDERLESS,
  resizable = SDL_WINDOW_RESIZABLE,
  minimized = SDL_WINDOW_MINIMIZED,
  maximized = SDL_WINDOW_MAXIMIZED,
  mouse_grabbed = SDL_WINDOW_MOUSE_GRABBED,
  input_focus = SDL_WINDOW_INPUT_FOCUS,
  mouse_focus = SDL_WINDOW_MOUSE_FOCUS,
  external = SDL_WINDOW_EXTERNAL,
  modal = SDL_WINDOW_MODAL,
  high_pixel_density = SDL_WINDOW_HIGH_PIXEL_DENSITY,
  mouse_capture = SDL_WINDOW_MOUSE_CAPTURE,
  mouse_relative_mode = SDL_WINDOW_MOUSE_RELATIVE_MODE,
  always_on_top = SDL_WINDOW_ALWAYS_ON_TOP,
  utility = SDL_WINDOW_UTILITY,
  tooltip = SDL_WINDOW_TOOLTIP,
  popup_menu = SDL_WINDOW_POPUP_MENU,
  keyboard_grabbed = SDL_WINDOW_KEYBOARD_GRABBED,
  fill_document = SDL_WINDOW_FILL_DOCUMENT,
  vulkan = SDL_WINDOW_VULKAN,
  metal = SDL_WINDOW_METAL,
  transparent = SDL_WINDOW_TRANSPARENT,
  not_focusable = SDL_WINDOW_NOT_FOCUSABLE,
};

consteval auto corvid_enum_spec(sdl_window_flags*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<sdl_window_flags,
      "not_focusable, transparent, metal, vulkan, -, -, -, -, -, -, "
      "fill_document, keyboard_grabbed, popup_menu, tooltip, utility, "
      "always_on_top, mouse_relative_mode, mouse_capture, high_pixel_density, "
      "modal, external, mouse_focus, input_focus, mouse_grabbed, maximized, "
      "minimized, resizable, borderless, hidden, occluded, opengl, "
      "fullscreen">();
}

#pragma endregion
#pragma region SDL window

// RAII for an SDL window.
class sdl_window {
public:
#pragma region Construction

  sdl_window(const char* title, int width, int height,
      sdl_window_flags flags = sdl_window_flags::none)
      : window_{sdl_status::or_throw(
            SDL_CreateWindow(title, width, height, *flags))} {}

  sdl_window(const sdl_window&) = delete;
  sdl_window& operator=(const sdl_window&) = delete;

  sdl_window(sdl_window&& other) noexcept
      : window_{std::exchange(other.window_, nullptr)} {}
  sdl_window& operator=(sdl_window&& other) noexcept {
    if (this != &other) {
      destroy();
      window_ = std::exchange(other.window_, nullptr);
    }
    return *this;
  }

  ~sdl_window() { destroy(); }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] SDL_Window* get() const noexcept { return window_; }
  operator SDL_Window*() const noexcept { return window_; }

  // The OS-native window handle.
  [[nodiscard]] void* native_handle() const noexcept {
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window_),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
  }

  // Set the smallest size, in screen coordinates, the window can be resized
  // to.
  [[nodiscard]] sdl_status set_minimum_size(int width, int height) noexcept {
    return SDL_SetWindowMinimumSize(window_, width, height);
  }

#pragma endregion
#pragma region Helpers
private:
  void destroy() {
    if (window_) SDL_DestroyWindow(window_);
  }

#pragma endregion
#pragma region Data members
private:
  SDL_Window* window_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::sdl
