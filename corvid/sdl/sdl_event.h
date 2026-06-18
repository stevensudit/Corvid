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
#include <cassert>
#include <optional>

#include "../enums/enum_conversion.h"
#include "../enums/sequence_enum.h"

#include "./sdl_common.h"
#include "./sdl_event_data_type.h"
#include "./sdl_event_type.h"

namespace corvid::sdl {

#pragma region sdl_display_id_type

// Type-safe handle for an `SDL_DisplayID`. Registered like the ECS ID enums:
// the values are runtime-assigned, so there are no names, and it stringifies
// as its number.
enum class sdl_display_id_type : Uint32 {};
consteval auto corvid_enum_spec(sdl_display_id_type*) {
  return corvid::enums::sequence::make_sequence_enum_spec<sdl_display_id_type,
      "">();
}

#pragma endregion
#pragma region sdl_event

// Cleaned-up payload of a display event, with the shared header dropped (it is
// already on `sdl_event`) and the raw `SDL_DisplayID` wrapped. The two data
// fields are event-dependent; see the `SDL_EVENT_DISPLAY_*` documentation.
struct sdl_display_event {
  sdl_display_id_type display_id;
  Sint32 data1;
  Sint32 data2;
};

namespace details {

// Classify a raw `SDL_Event` by which union member it populates. Registered
// user-range events are `user`; payload-less and unknown types are `common`.
[[nodiscard]] inline sdl_event_data_type classify(
    const SDL_Event& event) noexcept {
  switch (event.type) {
  case SDL_EVENT_DISPLAY_ORIENTATION:
  case SDL_EVENT_DISPLAY_ADDED:
  case SDL_EVENT_DISPLAY_REMOVED:
  case SDL_EVENT_DISPLAY_MOVED:
  case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED:
  case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
  case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED:
  case SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED:
    return sdl_event_data_type::display;

  case SDL_EVENT_WINDOW_SHOWN:
  case SDL_EVENT_WINDOW_HIDDEN:
  case SDL_EVENT_WINDOW_EXPOSED:
  case SDL_EVENT_WINDOW_MOVED:
  case SDL_EVENT_WINDOW_RESIZED:
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
  case SDL_EVENT_WINDOW_METAL_VIEW_RESIZED:
  case SDL_EVENT_WINDOW_MINIMIZED:
  case SDL_EVENT_WINDOW_MAXIMIZED:
  case SDL_EVENT_WINDOW_RESTORED:
  case SDL_EVENT_WINDOW_MOUSE_ENTER:
  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
  case SDL_EVENT_WINDOW_FOCUS_LOST:
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
  case SDL_EVENT_WINDOW_HIT_TEST:
  case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
  case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
  case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
  case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
  case SDL_EVENT_WINDOW_OCCLUDED:
  case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
  case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
  case SDL_EVENT_WINDOW_DESTROYED:
  case SDL_EVENT_WINDOW_HDR_STATE_CHANGED: return sdl_event_data_type::window;

  case SDL_EVENT_KEYBOARD_ADDED:
  case SDL_EVENT_KEYBOARD_REMOVED: return sdl_event_data_type::kdevice;

  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP: return sdl_event_data_type::key;

  case SDL_EVENT_TEXT_EDITING: return sdl_event_data_type::edit;

  case SDL_EVENT_TEXT_EDITING_CANDIDATES:
    return sdl_event_data_type::edit_candidates;

  case SDL_EVENT_TEXT_INPUT: return sdl_event_data_type::text;

  case SDL_EVENT_MOUSE_ADDED:
  case SDL_EVENT_MOUSE_REMOVED: return sdl_event_data_type::mdevice;

  case SDL_EVENT_MOUSE_MOTION: return sdl_event_data_type::motion;

  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP: return sdl_event_data_type::button;

  case SDL_EVENT_MOUSE_WHEEL: return sdl_event_data_type::wheel;

  case SDL_EVENT_JOYSTICK_ADDED:
  case SDL_EVENT_JOYSTICK_REMOVED:
  case SDL_EVENT_JOYSTICK_UPDATE_COMPLETE: return sdl_event_data_type::jdevice;

  case SDL_EVENT_JOYSTICK_AXIS_MOTION: return sdl_event_data_type::jaxis;

  case SDL_EVENT_JOYSTICK_BALL_MOTION: return sdl_event_data_type::jball;

  case SDL_EVENT_JOYSTICK_HAT_MOTION: return sdl_event_data_type::jhat;

  case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
  case SDL_EVENT_JOYSTICK_BUTTON_UP: return sdl_event_data_type::jbutton;

  case SDL_EVENT_JOYSTICK_BATTERY_UPDATED:
    return sdl_event_data_type::jbattery;

  case SDL_EVENT_GAMEPAD_ADDED:
  case SDL_EVENT_GAMEPAD_REMOVED:
  case SDL_EVENT_GAMEPAD_REMAPPED:
  case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE:
  case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
    return sdl_event_data_type::gdevice;

  case SDL_EVENT_GAMEPAD_AXIS_MOTION: return sdl_event_data_type::gaxis;

  case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
  case SDL_EVENT_GAMEPAD_BUTTON_UP: return sdl_event_data_type::gbutton;

  case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
  case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
  case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: return sdl_event_data_type::gtouchpad;

  case SDL_EVENT_GAMEPAD_SENSOR_UPDATE: return sdl_event_data_type::gsensor;

  case SDL_EVENT_AUDIO_DEVICE_ADDED:
  case SDL_EVENT_AUDIO_DEVICE_REMOVED:
  case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
    return sdl_event_data_type::adevice;

  case SDL_EVENT_CAMERA_DEVICE_ADDED:
  case SDL_EVENT_CAMERA_DEVICE_REMOVED:
  case SDL_EVENT_CAMERA_DEVICE_APPROVED:
  case SDL_EVENT_CAMERA_DEVICE_DENIED: return sdl_event_data_type::cdevice;

  case SDL_EVENT_SENSOR_UPDATE: return sdl_event_data_type::sensor;

  case SDL_EVENT_QUIT: return sdl_event_data_type::quit;

  case SDL_EVENT_FINGER_DOWN:
  case SDL_EVENT_FINGER_UP:
  case SDL_EVENT_FINGER_MOTION:
  case SDL_EVENT_FINGER_CANCELED: return sdl_event_data_type::tfinger;

  case SDL_EVENT_PINCH_BEGIN:
  case SDL_EVENT_PINCH_UPDATE:
  case SDL_EVENT_PINCH_END: return sdl_event_data_type::pinch;

  case SDL_EVENT_PEN_PROXIMITY_IN:
  case SDL_EVENT_PEN_PROXIMITY_OUT: return sdl_event_data_type::pproximity;

  case SDL_EVENT_PEN_DOWN:
  case SDL_EVENT_PEN_UP: return sdl_event_data_type::ptouch;

  case SDL_EVENT_PEN_MOTION: return sdl_event_data_type::pmotion;

  case SDL_EVENT_PEN_BUTTON_DOWN:
  case SDL_EVENT_PEN_BUTTON_UP: return sdl_event_data_type::pbutton;

  case SDL_EVENT_PEN_AXIS: return sdl_event_data_type::paxis;

  case SDL_EVENT_RENDER_TARGETS_RESET:
  case SDL_EVENT_RENDER_DEVICE_RESET:
  case SDL_EVENT_RENDER_DEVICE_LOST: return sdl_event_data_type::render;

  case SDL_EVENT_DROP_FILE:
  case SDL_EVENT_DROP_TEXT:
  case SDL_EVENT_DROP_BEGIN:
  case SDL_EVENT_DROP_COMPLETE:
  case SDL_EVENT_DROP_POSITION: return sdl_event_data_type::drop;

  case SDL_EVENT_CLIPBOARD_UPDATE: return sdl_event_data_type::clipboard;

  default:
    if (event.type >= SDL_EVENT_USER) return sdl_event_data_type::user;
    return sdl_event_data_type::common;
  }
}

} // namespace details

// Union-aware wrapper for an `SDL_Event`. Holds the raw event but presents it
// in C++ terms: `type()` is the registered event enum, `data_type()` names the
// active union member, and `timestamp()` reads the header every event shares.
//
// To read an event's payload, switch on `type()` and call the typed accessor
// for its shape (e.g. `get_display()`), which returns a cleaned-up struct and
// asserts the shape matches. Shapes without a typed accessor yet are reached
// through `raw()`, the transitional escape hatch into the underlying union.
class sdl_event {
public:
#pragma region Construction

  explicit sdl_event(const SDL_Event& event) noexcept
      : event_{event}, data_type_{details::classify(event)} {}

#pragma endregion
#pragma region Header accessors

  [[nodiscard]] sdl_event_type type() const noexcept {
    return static_cast<sdl_event_type>(event_.type);
  }
  [[nodiscard]] sdl_event_data_type data_type() const noexcept {
    return data_type_;
  }
  [[nodiscard]] Uint64 timestamp() const noexcept {
    return event_.common.timestamp;
  }

  // Escape hatch into the raw union for shapes without a typed accessor yet.
  [[nodiscard]] const SDL_Event& raw() const noexcept { return event_; }

#pragma endregion
#pragma region Payload accessors

  // Display event payload. Narrow contract: the active shape must be
  // `display`.
  [[nodiscard]] sdl_display_event get_display() const {
    assert(data_type_ == sdl_event_data_type::display);
    return {static_cast<sdl_display_id_type>(event_.display.displayID),
        event_.display.data1, event_.display.data2};
  }

#pragma endregion
#pragma region Data members
private:
  SDL_Event event_;
  sdl_event_data_type data_type_;

#pragma endregion
};

// Pull the next event from SDL's queue, wrapped. Returns `nullopt` when the
// queue is empty (SDL_PollEvent returned false), so `while (auto ev =
// poll_event())` drains it. Like SDL_PollEvent, call only on the main thread.
[[nodiscard]] inline std::optional<sdl_event> poll_event() noexcept {
  SDL_Event event;
  if (!SDL_PollEvent(&event)) return std::nullopt;
  return sdl_event{event};
}

#pragma endregion

} // namespace corvid::sdl
