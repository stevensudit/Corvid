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

#include "./sdl_event.h"

// A generic per-frame event pump for an interactive window: it drains the
// frame's SDL events, handles the window-level ones itself (close, resize,
// menu), and delegates the rest to a caller-supplied handler.

namespace corvid::sdl {

#pragma region frame_action

// What the event pump asks the frame loop to do next: keep rendering, rebuild
// for a resize, open the menu, or quit.
enum class frame_action : std::uint8_t { proceed, resize, menu, quit };

#pragma endregion
#pragma region pump_events

// Drain this frame's events through `handle`, a `bool(const sdl_event&)` that
// returns whether it consumed the event, and report the resulting
// `frame_action`.
//
// The pump owns the window-level events: a close request quits, a pixel-size
// change asks for a resize (coalesced, so a drag's event storm becomes one
// rebuild before the frame), and Escape opens the menu.
//
// Events `handle` does not consume fall through to that handling. Compose
// handlers with `||`, which stops at the first consumer, e.g. `return
// handle_look(ev) || handle_keys(ev);`.
template<typename Handler>
[[nodiscard]] frame_action pump_events(Handler handle) {
  auto action = frame_action::proceed;
  while (auto ev = sdl_event::poll()) {
    if (handle(ev)) continue;
    switch (ev.type()) {
    case sdl_event_type::quit:
    case sdl_event_type::window_close_requested: return frame_action::quit;

    case sdl_event_type::window_pixel_size_changed:
      action = frame_action::resize;
      break;

    case sdl_event_type::key_down:
      if (ev.get_key().key == sdl_keycode::escape) return frame_action::menu;
      break;

    default: break;
    }
  }
  return action;
}

#pragma endregion

} // namespace corvid::sdl
