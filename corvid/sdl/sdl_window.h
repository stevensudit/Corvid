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
#include <utility>

#include "./sdl_common.h"
#include "./sdl_status.h"

namespace corvid::sdl {

#pragma region SDL window

// RAII for an SDL window.
class sdl_window {
public:
#pragma region Construction

  sdl_window(const char* title, int width, int height)
      : window_{
            sdl_status::or_throw(SDL_CreateWindow(title, width, height, 0))} {}

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
