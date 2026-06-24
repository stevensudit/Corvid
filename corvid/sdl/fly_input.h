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

#include <cmath>
#include <tuple>
#include <utility>

#include "./sdl_event.h"
#include "./sdl_window.h"
#include "../math/one_euro_filter.h"

// Free-fly camera input state and the SDL event handler that maintains it: the
// held movement keys, the mouse-look toggle, and the frame's accumulated look
// delta and wheel scroll. `handle` is a `pump_events` handler; the state is
// applied to a camera by the caller each frame.

namespace corvid::sdl {

#pragma region fly_input

// The free-fly camera's input state for a frame: which movement keys are held,
// whether mouse-look is active, and this frame's accumulated look delta and
// wheel scroll. `handle` folds SDL events into it; `look` and `dolly` clear
// their accumulators as they consume them.
struct fly_input {
  bool forward = false;
  bool back = false;
  bool left = false;
  bool right = false;
  bool up = false;
  bool down = false;
  bool fast = false;

  // Look and scroll tuning, with defaults a viewer may override.
  // `look_sensitivity` scales raw mouse counts to radians; `scroll_step` is
  // the forward dolly per wheel notch. `look_filter` de-jitters the
  // mouse-look: a One Euro Filter that smooths heavily at the low, steady
  // speeds where per-frame jitter shows and eases off as the mouse speeds up
  // so a fast flick stays responsive (its arguments are the at-rest smoothing
  // time constant in milliseconds and `beta`, how fast that smoothing relaxes
  // with speed).
  float look_sensitivity = 0.0025F;
  float scroll_step = 1.0F;
  one_euro_filter look_filter{60.0F, 0.001F};

  // Whether mouse-look is active: held while the right button is down, which
  // captures the cursor. Persists across frames.
  bool looking = false;

  // This frame's accumulated mouse-look delta (raw counts, gated on `looking`)
  // and wheel scroll, gathered by `handle` and cleared by `look` and `dolly`
  // as they consume them.
  float look_dx = 0.0F;
  float look_dy = 0.0F;
  float wheel = 0.0F;

  // Fold one event into this state.
  //
  // The right button toggles `looking` and captures the cursor through `win`,
  // mouse motion accumulates the look delta while looking, the wheel
  // accumulates scroll, and the movement keys set their held flags.
  //
  // Returns whether it consumed the event, for `||` composition: the movement
  // keys and the right button are consumed; other keys (such as Escape) are
  // left for the pump, and other mouse buttons for another handler.
  [[nodiscard]] bool handle(const sdl_event& ev, sdl_window& win) {
    switch (ev.type()) {
    case sdl_event_type::mouse_button_down:
    case sdl_event_type::mouse_button_up: {
      const auto button = ev.get_button();
      if (button.button == sdl_mouse_button::right) {
        looking = button.down;
        win.set_relative_mouse_mode(looking).or_throw();
        return true;
      }
      return false;
    }

    case sdl_event_type::mouse_motion: {
      if (looking) {
        const auto motion = ev.get_motion();
        look_dx += motion.xrel;
        look_dy += motion.yrel;
      }
      return true;
    }

    case sdl_event_type::mouse_wheel: {
      wheel += ev.get_wheel().y;
      return true;
    }

    case sdl_event_type::key_down:
    case sdl_event_type::key_up: {
      const auto key = ev.get_key();
      switch (key.key) {
      case sdl_keycode::w: forward = key.down; return true;
      case sdl_keycode::s: back = key.down; return true;
      case sdl_keycode::a: left = key.down; return true;
      case sdl_keycode::d: right = key.down; return true;
      case sdl_keycode::space: up = key.down; return true;
      case sdl_keycode::lctrl: down = key.down; return true;
      case sdl_keycode::lshift: fast = key.down; return true;
      default: return false;
      }
    }

    default: return false;
    }
  }

  // Force-release the held movement keys. Call this when a UI overlay takes
  // the keyboard mid-hold: the matching key-up is delivered to the overlay,
  // not to `handle`, so without this a held key would stick and drive the
  // camera until it is pressed and released again.
  void release_keys() {
    forward = back = left = right = up = down = fast = false;
  }

  // The camera-relative movement for this frame as (forward, sideways, up),
  // each scaled by frame time. The planar (forward, sideways) pair is capped
  // to one speed, so a diagonal is no faster than a cardinal: holding both
  // moves at standard speed in the direction between them. The vertical is
  // separate.
  [[nodiscard]] std::tuple<float, float, float>
  movement(float dt, float speed_multiplier = 1.0F) const {
    const float speed = speed_multiplier * dt * (fast ? 3.0F : 1.0F);
    float forward_move = (forward ? speed : 0.0F) - (back ? speed : 0.0F);
    float sideways_move = (right ? speed : 0.0F) - (left ? speed : 0.0F);
    const float upward_move = (up ? speed : 0.0F) - (down ? speed : 0.0F);
    if (const float planar = std::hypot(forward_move, sideways_move);
        planar > speed)
    {
      const float scale = speed / planar;
      forward_move *= scale;
      sideways_move *= scale;
    }
    return {forward_move, sideways_move, upward_move};
  }

  // Smooth this frame's accumulated mouse-look delta and return it as a
  // (yaw, pitch) rotation in radians, scaled by `look_sensitivity` with the
  // screen-y axis flipped. When not looking, forgets the filter's carried
  // state and returns no rotation, so the look neither glides on after release
  // nor fires on the next grab.
  [[nodiscard]] std::pair<float, float> look(float dt) {
    if (!looking) {
      look_filter.reset();
      look_dx = 0.0F;
      look_dy = 0.0F;
      return {0.0F, 0.0F};
    }
    look_filter.smooth(dt, look_dx, look_dy);
    const auto scaled_dx = look_dx * look_sensitivity;
    const auto scaled_dy = look_dy * look_sensitivity;
    look_dx = 0.0F;
    look_dy = 0.0F;
    return {scaled_dx, -scaled_dy};
  }

  // The forward camera dolly from this frame's wheel scroll: an impulse (not
  // scaled by frame time) of `scroll_step` per notch.
  [[nodiscard]] float dolly() {
    const auto scaled_wheel = wheel * scroll_step;
    wheel = 0.0F;
    return scaled_wheel;
  }
};

#pragma endregion

} // namespace corvid::sdl
