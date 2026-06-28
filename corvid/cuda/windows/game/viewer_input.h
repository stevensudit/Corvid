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

#include "../imgui_overlay.h"
#include "../../../sdl/drive_input.h"
#include "../../../sdl/sdl_event.h"
#include "../../../sdl/sdl_window.h"

// Event routing for the voxel viewer: classify input events and fold the dig
// button into the game state, sharing one event stream between the ImGui
// config panel and the game.

namespace corvid::cuda {

#pragma region Input

// Whether an event carries mouse or keyboard input, so an open config panel
// can swallow the input ImGui captured instead of letting the game act on it.
[[nodiscard]] inline bool is_mouse_event(const sdl::sdl_event& ev) {
  switch (ev.data_type()) {
  case sdl::sdl_event_data_type::motion:
  case sdl::sdl_event_data_type::button:
  case sdl::sdl_event_data_type::wheel: return true;
  default: return false;
  }
}
[[nodiscard]] inline bool is_keyboard_event(const sdl::sdl_event& ev) {
  switch (ev.data_type()) {
  case sdl::sdl_event_data_type::key:
  case sdl::sdl_event_data_type::text:
  case sdl::sdl_event_data_type::edit: return true;
  default: return false;
  }
}

// The player's active tool, selected by the number keys. `none` is value 0
// (no tool: no reticle, the dig brush disabled). More beams (fill, drag) will
// take the next values as they are built.
enum class active_tool : std::uint8_t { none, dig };

// Fold the tool-select number keys into `tool`: 1 toggles the dig projection
// (press to equip, press again to put it away). Auto-repeat is ignored so a
// held key does not flutter the toggle. Returns whether it consumed the event.
[[nodiscard]] inline bool
handle_tool_select(const sdl::sdl_event& ev, active_tool& tool) {
  if (ev.type() != sdl::sdl_event_type::key_down) return false;
  const auto key = ev.get_key();
  if (key.repeat) return false;
  if (key.key == sdl::sdl_keycode::num1) {
    tool = (tool == active_tool::dig) ? active_tool::none : active_tool::dig;
    return true;
  }
  return false;
}

// Fold the left mouse button into the `digging` flag, for composing after
// `handle_fly`. Returns whether it consumed the event.
[[nodiscard]] inline bool handle_dig(const sdl::sdl_event& ev, bool& digging) {
  switch (ev.type()) {
  case sdl::sdl_event_type::mouse_button_down:
  case sdl::sdl_event_type::mouse_button_up: {
    const auto button = ev.get_button();
    if (button.button == sdl::sdl_mouse_button::left) {
      digging = button.down;
      return true;
    }
    return false;
  }

  default: return false;
  }
}

// Route one frame event. ImGui always sees it, so its capture state stays
// current.
//
// While the config panel is open, ImGui swallows the input it captured (a
// slider drag, a focused field) so the game does not also react; other events
// still reach the game handlers. Returns whether it consumed the event,
// leaving Escape for the pump to toggle the panel.
[[nodiscard]] inline bool handle_viewer_event(const sdl::sdl_event& ev,
    imgui_overlay& imgui, bool show_config, sdl::drive_input& input,
    sdl::sdl_window& win, bool& digging, active_tool& tool) {
  imgui.process_event(ev);
  if (show_config) {
    if (is_mouse_event(ev) && imgui.wants_mouse()) return true;
    if (is_keyboard_event(ev) && imgui.wants_keyboard()) return true;
  }
  return input.handle(ev, win) || handle_dig(ev, digging) ||
         handle_tool_select(ev, tool);
}

#pragma endregion

} // namespace corvid::cuda
