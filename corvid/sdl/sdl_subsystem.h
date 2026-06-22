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

#pragma region SDL subsystem

// RAII for SDL's process-wide lifetime: `SDL_SetMainReady` (because we own
// `main`) and `SDL_Init` in the constructor, `SDL_Quit` in the destructor.
// Move-only.
//
// Initializes the video subsystem only for now; an `SDL_InitFlags` parameter,
// wrapped as a Corvid enum, lands with the rest of the SDL enums.
class sdl_subsystem {
public:
#pragma region Construction

  sdl_subsystem() {
    SDL_SetMainReady();
    sdl_status{SDL_Init(SDL_INIT_VIDEO)}.or_throw();
  }

  sdl_subsystem(const sdl_subsystem&) = delete;
  sdl_subsystem& operator=(const sdl_subsystem&) = delete;

  sdl_subsystem(sdl_subsystem&& other) noexcept
      : live_{std::exchange(other.live_, false)} {}
  sdl_subsystem& operator=(sdl_subsystem&& other) noexcept {
    if (this != &other) {
      destroy();
      live_ = std::exchange(other.live_, false);
    }
    return *this;
  }
  ~sdl_subsystem() { destroy(); }

#pragma endregion
#pragma region Helpers
private:
  void destroy() const {
    if (live_) SDL_Quit();
  }

#pragma endregion
#pragma region Data members
private:
  bool live_{true};

#pragma endregion
};

#pragma endregion

} // namespace corvid::sdl
