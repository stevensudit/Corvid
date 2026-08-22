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

#pragma region sdl_init_flags

// Bitmask wrapper for `SDL_InitFlags`: which SDL subsystems to initialize.
//
// `audio`, `video`, `joystick`, `sensor`, and `camera` each imply `events`,
// and `gamepad` implies `joystick`. `video` must be initialized on the main
// thread. `events` alone is headless, for tests and tools without a display.
enum class sdl_init_flags : uint32_t {
  none = 0,
  audio = SDL_INIT_AUDIO,
  video = SDL_INIT_VIDEO,
  joystick = SDL_INIT_JOYSTICK,
  haptic = SDL_INIT_HAPTIC,
  gamepad = SDL_INIT_GAMEPAD,
  events = SDL_INIT_EVENTS,
  sensor = SDL_INIT_SENSOR,
  camera = SDL_INIT_CAMERA,
};

consteval auto corvid_enum_spec(sdl_init_flags*) {
  return corvid::enums::bitmask::make_bitmask_enum_spec<sdl_init_flags,
      "camera, sensor, events, gamepad, haptic, -, -, joystick, -, -, -, "
      "video, audio, -, -, -, -">();
}

#pragma endregion
#pragma region SDL subsystem

// RAII for SDL's process-wide lifetime: `SDL_SetMainReady` (because we own
// `main`) and `SDL_Init` in the constructor, `SDL_Quit` in the destructor.
class sdl_subsystem {
public:
#pragma region Construction

  // Initialize the subsystems in `flags`, or throw.
  explicit sdl_subsystem(sdl_init_flags flags = sdl_init_flags::video) {
    SDL_SetMainReady();
    sdl_status{SDL_Init(*flags)}.or_throw();
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
  bool live_ = true;

#pragma endregion
};

#pragma endregion

} // namespace corvid::sdl
