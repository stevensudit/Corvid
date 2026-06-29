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
#include <utility>

#include "./sdl_event.h"
#include "./sdl_window.h"
#include "../math/one_euro_filter.h"

// Ground-driving camera input state and the SDL event handler that maintains
// it. The sibling of `fly_input`: the same mouse-look and wheel handling, but
// the avatar drives on the ground under gravity instead of free-flying, so the
// vertical fly keys are gone and Space is a jump. `handle` is a `pump_events`
// handler; the state is applied to the avatar by the caller each frame.

namespace corvid::sdl {

#pragma region drive_input

// The driving avatar's input state for a frame: which planar movement keys are
// held, whether mouse-look is active, this frame's accumulated look delta and
// wheel scroll, and a one-shot jump latched on the Space press. `handle` folds
// SDL events into it; `look` and `dolly` clear their accumulators as they
// consume them, and `take_jump` consumes the latched jump.
struct drive_input {
  bool forward = false;
  bool back = false;
  bool left = false;
  bool right = false;
  bool fast = false;

  // Look and scroll tuning, with defaults a viewer may override.
  // `look_sensitivity` scales raw mouse counts to radians; `scroll_step` is
  // the head dolly per wheel notch. `look_filter` de-jitters the mouse-look: a
  // One Euro Filter that smooths heavily at the low, steady speeds where
  // per-frame jitter shows and eases off as the mouse speeds up so a fast
  // flick stays responsive (its arguments are the at-rest smoothing time
  // constant in milliseconds and `beta`, how fast that smoothing relaxes with
  // speed).
  float look_sensitivity = 0.0025F;
  float scroll_step = 1.0F;
  // The speed multiple Run (Shift) commands over Walk; `movement` scales the
  // target by it.
  float run_multiplier = 5.0F;
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

  // A jump latched on the Space key-down edge and held until `take_jump`
  // consumes it, so a press is never missed between frames. Key auto-repeat is
  // ignored, so holding Space does not re-latch within a single press.
  bool jump = false;

  // Fold one event into this state.
  //
  // The right button toggles `looking` and captures the cursor through `win`,
  // mouse motion accumulates the look delta while looking, the wheel
  // accumulates scroll, the movement keys set their held flags, and Space
  // latches a jump.
  //
  // Returns whether it consumed the event, for `||` composition: the movement
  // keys, Space, and the right button are consumed; other keys (such as
  // Escape) are left for the pump, and other mouse buttons for another
  // handler.
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
      // Last press wins on an opposing pair (forward/back, left/right):
      // pressing one clears the other so a fresh press always takes over
      // rather than canceling against a still-held opposite. Re-pressing the
      // cleared key takes it back.
      switch (key.key) {
      case sdl_keycode::w:
        forward = key.down;
        if (key.down) back = false;
        return true;
      case sdl_keycode::s:
        back = key.down;
        if (key.down) forward = false;
        return true;
      case sdl_keycode::a:
        left = key.down;
        if (key.down) right = false;
        return true;
      case sdl_keycode::d:
        right = key.down;
        if (key.down) left = false;
        return true;
      case sdl_keycode::space:
        // Latch on the press edge only; the rig consumes it via `take_jump`.
        if (key.down && !key.repeat) jump = true;
        return true;
      case sdl_keycode::lshift: fast = key.down; return true;
      default: return false;
      }
    }

    default: return false;
    }
  }

  // Force-release the held movement keys and the latched jump. Call this when
  // a UI overlay takes the keyboard mid-hold: the matching key-up is delivered
  // to the overlay, not to `handle`, so without this a held key would stick
  // and drive the avatar until it is pressed and released again.
  void release_keys() {
    forward = back = left = right = fast = false;
    jump = false;
  }

  // The target ground velocity for this frame as (forward, sideways) in the
  // heading frame, sprint-scaled and capped to one speed so a diagonal is no
  // faster than a cardinal: holding both aims at standard speed in the
  // direction between them. The rig eases its own velocity toward this with
  // momentum (see `avatar_rig::move`); there is no vertical, gravity owns it.
  [[nodiscard]] std::pair<float, float> movement(
      float speed_multiplier = 1.0F) const {
    const float speed = speed_multiplier * (fast ? run_multiplier : 1.0F);
    float forward_move = (forward ? speed : 0.0F) - (back ? speed : 0.0F);
    float sideways_move = (right ? speed : 0.0F) - (left ? speed : 0.0F);
    if (const float planar = std::hypot(forward_move, sideways_move);
        planar > speed)
    {
      const float scale = speed / planar;
      forward_move *= scale;
      sideways_move *= scale;
    }
    return {forward_move, sideways_move};
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

  // The head dolly from this frame's wheel scroll: an impulse (not scaled by
  // frame time) of `scroll_step` per notch.
  [[nodiscard]] float dolly() {
    const auto scaled_wheel = wheel * scroll_step;
    wheel = 0.0F;
    return scaled_wheel;
  }

  // Consume the latched jump: returns whether a jump was requested since the
  // last call and clears it, so each Space press fires exactly one jump.
  [[nodiscard]] bool take_jump() {
    const bool jumped = jump;
    jump = false;
    return jumped;
  }
};

#pragma endregion

} // namespace corvid::sdl
