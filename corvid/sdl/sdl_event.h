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
#include <cstdint>

#include "../enums/enum_conversion.h"
#include "../enums/sequence_enum.h"

#include "./sdl_common.h"

namespace corvid::sdl {

#pragma region sdl_event_type

// Enum to wrap `SDL_EventType`.
enum class sdl_event_type : std::uint16_t {
  // Application (0x100).
  quit = SDL_EVENT_QUIT,
  terminating = SDL_EVENT_TERMINATING,
  low_memory = SDL_EVENT_LOW_MEMORY,
  will_enter_background = SDL_EVENT_WILL_ENTER_BACKGROUND,
  did_enter_background = SDL_EVENT_DID_ENTER_BACKGROUND,
  will_enter_foreground = SDL_EVENT_WILL_ENTER_FOREGROUND,
  did_enter_foreground = SDL_EVENT_DID_ENTER_FOREGROUND,
  locale_changed = SDL_EVENT_LOCALE_CHANGED,
  system_theme_changed = SDL_EVENT_SYSTEM_THEME_CHANGED,
  // Display (0x151).
  display_orientation = SDL_EVENT_DISPLAY_ORIENTATION,
  display_added = SDL_EVENT_DISPLAY_ADDED,
  display_removed = SDL_EVENT_DISPLAY_REMOVED,
  display_moved = SDL_EVENT_DISPLAY_MOVED,
  display_desktop_mode_changed = SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED,
  display_current_mode_changed = SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED,
  display_content_scale_changed = SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED,
  display_usable_bounds_changed = SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED,
  // Window (0x202).
  window_shown = SDL_EVENT_WINDOW_SHOWN,
  window_hidden = SDL_EVENT_WINDOW_HIDDEN,
  window_exposed = SDL_EVENT_WINDOW_EXPOSED,
  window_moved = SDL_EVENT_WINDOW_MOVED,
  window_resized = SDL_EVENT_WINDOW_RESIZED,
  window_pixel_size_changed = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED,
  window_metal_view_resized = SDL_EVENT_WINDOW_METAL_VIEW_RESIZED,
  window_minimized = SDL_EVENT_WINDOW_MINIMIZED,
  window_maximized = SDL_EVENT_WINDOW_MAXIMIZED,
  window_restored = SDL_EVENT_WINDOW_RESTORED,
  window_mouse_enter = SDL_EVENT_WINDOW_MOUSE_ENTER,
  window_mouse_leave = SDL_EVENT_WINDOW_MOUSE_LEAVE,
  window_focus_gained = SDL_EVENT_WINDOW_FOCUS_GAINED,
  window_focus_lost = SDL_EVENT_WINDOW_FOCUS_LOST,
  window_close_requested = SDL_EVENT_WINDOW_CLOSE_REQUESTED,
  window_hit_test = SDL_EVENT_WINDOW_HIT_TEST,
  window_iccprof_changed = SDL_EVENT_WINDOW_ICCPROF_CHANGED,
  window_display_changed = SDL_EVENT_WINDOW_DISPLAY_CHANGED,
  window_display_scale_changed = SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED,
  window_safe_area_changed = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED,
  window_occluded = SDL_EVENT_WINDOW_OCCLUDED,
  window_enter_fullscreen = SDL_EVENT_WINDOW_ENTER_FULLSCREEN,
  window_leave_fullscreen = SDL_EVENT_WINDOW_LEAVE_FULLSCREEN,
  window_destroyed = SDL_EVENT_WINDOW_DESTROYED,
  window_hdr_state_changed = SDL_EVENT_WINDOW_HDR_STATE_CHANGED,
  // Keyboard (0x300).
  key_down = SDL_EVENT_KEY_DOWN,
  key_up = SDL_EVENT_KEY_UP,
  text_editing = SDL_EVENT_TEXT_EDITING,
  text_input = SDL_EVENT_TEXT_INPUT,
  keymap_changed = SDL_EVENT_KEYMAP_CHANGED,
  keyboard_added = SDL_EVENT_KEYBOARD_ADDED,
  keyboard_removed = SDL_EVENT_KEYBOARD_REMOVED,
  text_editing_candidates = SDL_EVENT_TEXT_EDITING_CANDIDATES,
  screen_keyboard_shown = SDL_EVENT_SCREEN_KEYBOARD_SHOWN,
  screen_keyboard_hidden = SDL_EVENT_SCREEN_KEYBOARD_HIDDEN,
  // Mouse (0x400).
  mouse_motion = SDL_EVENT_MOUSE_MOTION,
  mouse_button_down = SDL_EVENT_MOUSE_BUTTON_DOWN,
  mouse_button_up = SDL_EVENT_MOUSE_BUTTON_UP,
  mouse_wheel = SDL_EVENT_MOUSE_WHEEL,
  mouse_added = SDL_EVENT_MOUSE_ADDED,
  mouse_removed = SDL_EVENT_MOUSE_REMOVED,
  // Joystick (0x600).
  joystick_axis_motion = SDL_EVENT_JOYSTICK_AXIS_MOTION,
  joystick_ball_motion = SDL_EVENT_JOYSTICK_BALL_MOTION,
  joystick_hat_motion = SDL_EVENT_JOYSTICK_HAT_MOTION,
  joystick_button_down = SDL_EVENT_JOYSTICK_BUTTON_DOWN,
  joystick_button_up = SDL_EVENT_JOYSTICK_BUTTON_UP,
  joystick_added = SDL_EVENT_JOYSTICK_ADDED,
  joystick_removed = SDL_EVENT_JOYSTICK_REMOVED,
  joystick_battery_updated = SDL_EVENT_JOYSTICK_BATTERY_UPDATED,
  joystick_update_complete = SDL_EVENT_JOYSTICK_UPDATE_COMPLETE,
  // Gamepad (0x650).
  gamepad_axis_motion = SDL_EVENT_GAMEPAD_AXIS_MOTION,
  gamepad_button_down = SDL_EVENT_GAMEPAD_BUTTON_DOWN,
  gamepad_button_up = SDL_EVENT_GAMEPAD_BUTTON_UP,
  gamepad_added = SDL_EVENT_GAMEPAD_ADDED,
  gamepad_removed = SDL_EVENT_GAMEPAD_REMOVED,
  gamepad_remapped = SDL_EVENT_GAMEPAD_REMAPPED,
  gamepad_touchpad_down = SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN,
  gamepad_touchpad_motion = SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION,
  gamepad_touchpad_up = SDL_EVENT_GAMEPAD_TOUCHPAD_UP,
  gamepad_sensor_update = SDL_EVENT_GAMEPAD_SENSOR_UPDATE,
  gamepad_update_complete = SDL_EVENT_GAMEPAD_UPDATE_COMPLETE,
  gamepad_steam_handle_updated = SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED,
  // Touch (0x700).
  finger_down = SDL_EVENT_FINGER_DOWN,
  finger_up = SDL_EVENT_FINGER_UP,
  finger_motion = SDL_EVENT_FINGER_MOTION,
  finger_canceled = SDL_EVENT_FINGER_CANCELED,
  // Pinch (0x710).
  pinch_begin = SDL_EVENT_PINCH_BEGIN,
  pinch_update = SDL_EVENT_PINCH_UPDATE,
  pinch_end = SDL_EVENT_PINCH_END,
  // Clipboard (0x900).
  clipboard_update = SDL_EVENT_CLIPBOARD_UPDATE,
  // Drag and drop (0x1000).
  drop_file = SDL_EVENT_DROP_FILE,
  drop_text = SDL_EVENT_DROP_TEXT,
  drop_begin = SDL_EVENT_DROP_BEGIN,
  drop_complete = SDL_EVENT_DROP_COMPLETE,
  drop_position = SDL_EVENT_DROP_POSITION,
  // Audio hotplug (0x1100).
  audio_device_added = SDL_EVENT_AUDIO_DEVICE_ADDED,
  audio_device_removed = SDL_EVENT_AUDIO_DEVICE_REMOVED,
  audio_device_format_changed = SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED,
  // Sensor (0x1200).
  sensor_update = SDL_EVENT_SENSOR_UPDATE,
  // Pen (0x1300).
  pen_proximity_in = SDL_EVENT_PEN_PROXIMITY_IN,
  pen_proximity_out = SDL_EVENT_PEN_PROXIMITY_OUT,
  pen_down = SDL_EVENT_PEN_DOWN,
  pen_up = SDL_EVENT_PEN_UP,
  pen_button_down = SDL_EVENT_PEN_BUTTON_DOWN,
  pen_button_up = SDL_EVENT_PEN_BUTTON_UP,
  pen_motion = SDL_EVENT_PEN_MOTION,
  pen_axis = SDL_EVENT_PEN_AXIS,
  // Camera hotplug (0x1400).
  camera_device_added = SDL_EVENT_CAMERA_DEVICE_ADDED,
  camera_device_removed = SDL_EVENT_CAMERA_DEVICE_REMOVED,
  camera_device_approved = SDL_EVENT_CAMERA_DEVICE_APPROVED,
  camera_device_denied = SDL_EVENT_CAMERA_DEVICE_DENIED,
  // Render (0x2000).
  render_targets_reset = SDL_EVENT_RENDER_TARGETS_RESET,
  render_device_reset = SDL_EVENT_RENDER_DEVICE_RESET,
  render_device_lost = SDL_EVENT_RENDER_DEVICE_LOST,
  // Reserved for private platforms (0x4000).
  private0 = SDL_EVENT_PRIVATE0,
  private1 = SDL_EVENT_PRIVATE1,
  private2 = SDL_EVENT_PRIVATE2,
  private3 = SDL_EVENT_PRIVATE3,
  // Internal (0x7F00).
  poll_sentinel = SDL_EVENT_POLL_SENTINEL,
  // User-event range start (0x8000).
  user = SDL_EVENT_USER,
  // Bounding sentinel (0xFFFF).
  last = SDL_EVENT_LAST,
};

consteval auto corvid_enum_spec(sdl_event_type*) {
  return corvid::enums::sequence::make_sequence_enum_spec<sdl_event_type,
      "256,quit,terminating,low_memory,will_enter_background,did_enter_"
      "background,will_enter_foreground,did_enter_foreground,locale_changed,"
      "system_theme_changed|337,display_orientation,display_added,display_"
      "removed,display_moved,display_desktop_mode_changed,display_current_"
      "mode_changed,display_content_scale_changed,display_usable_bounds_"
      "changed|514,window_shown,window_hidden,window_exposed,window_moved,"
      "window_resized,window_pixel_size_changed,window_metal_view_resized,"
      "window_minimized,window_maximized,window_restored,window_mouse_enter,"
      "window_mouse_leave,window_focus_gained,window_focus_lost,"
      "window_close_requested,window_hit_test,window_iccprof_changed,window_"
      "display_changed,window_display_scale_changed,window_safe_area_changed,"
      "window_occluded,window_enter_fullscreen,window_leave_fullscreen,window_"
      "destroyed,window_hdr_state_changed|768,key_down,key_up,text_editing,"
      "text_input,keymap_changed,keyboard_added,keyboard_removed,text_editing_"
      "candidates,screen_keyboard_shown,screen_keyboard_hidden|1024,mouse_"
      "motion,mouse_button_down,mouse_button_up,mouse_wheel,mouse_added,mouse_"
      "removed|1536,joystick_axis_motion,joystick_ball_motion,joystick_hat_"
      "motion,joystick_button_down,joystick_button_up,joystick_added,joystick_"
      "removed,joystick_battery_updated,joystick_update_complete|1616,gamepad_"
      "axis_motion,gamepad_button_down,gamepad_button_up,gamepad_added,"
      "gamepad_removed,gamepad_remapped,gamepad_touchpad_down,gamepad_"
      "touchpad_motion,gamepad_touchpad_up,gamepad_sensor_update,gamepad_"
      "update_complete,gamepad_steam_handle_updated|1792,finger_down,finger_"
      "up,finger_motion,finger_canceled|1808,pinch_begin,pinch_update,pinch_"
      "end|2304,clipboard_update|4096,drop_file,drop_text,drop_begin,drop_"
      "complete,drop_position|4352,audio_device_added,audio_device_removed,"
      "audio_device_format_changed|4608,sensor_update|4864,pen_proximity_in,"
      "pen_proximity_out,pen_down,pen_up,pen_button_down,pen_button_up,pen_"
      "motion,pen_axis|5120,camera_device_added,camera_device_removed,camera_"
      "device_approved,camera_device_denied|8192,render_targets_reset,render_"
      "device_reset,render_device_lost|16384,private0,private1,private2,"
      "private3|32512,poll_sentinel|32768,user|65535,last">();
}

#pragma endregion
#pragma region sdl_event_data_type

// Which member of the `SDL_Event` union carries an event's payload, hence
// which typed `sdl_event` accessor applies. Coarser than `sdl_event_type`:
// `key_down` and `key_up` are both `key`. Header-only events, including
// `quit`, are `common`.
enum class sdl_event_data_type : std::uint8_t {
  common,
  display,
  window,
  kdevice,
  key,
  edit,
  edit_candidates,
  text,
  mdevice,
  motion,
  button,
  wheel,
  jdevice,
  jaxis,
  jball,
  jhat,
  jbutton,
  jbattery,
  gdevice,
  gaxis,
  gbutton,
  gtouchpad,
  gsensor,
  adevice,
  cdevice,
  sensor,
  user,
  tfinger,
  pinch,
  pproximity,
  ptouch,
  pmotion,
  pbutton,
  paxis,
  render,
  drop,
  clipboard,
};

consteval auto corvid_enum_spec(sdl_event_data_type*) {
  return corvid::enums::sequence::make_sequence_enum_spec<sdl_event_data_type,
      "common,display,window,kdevice,key,edit,edit_candidates,text,mdevice,"
      "motion,button,wheel,jdevice,jaxis,jball,jhat,jbutton,jbattery,gdevice,"
      "gaxis,gbutton,gtouchpad,gsensor,adevice,cdevice,sensor,user,"
      "tfinger,pinch,pproximity,ptouch,pmotion,pbutton,paxis,render,drop,"
      "clipboard">();
}

#pragma endregion
#pragma region sdl_display_id_type

// Type-safe handle for an `SDL_DisplayID`.
enum class sdl_display_id_type : Uint32 {};
consteval auto corvid_enum_spec(sdl_display_id_type*) {
  return corvid::enums::sequence::make_sequence_enum_spec<sdl_display_id_type,
      "">();
}

#pragma endregion
#pragma region sdl_event

// Cleaned payload of a display event. `data1`/`data2` are event-specific; see
// the `SDL_EVENT_DISPLAY_*` documentation.
struct sdl_display_event {
  sdl_display_id_type display_id;
  Sint32 data1;
  Sint32 data2;
};

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

  sdl_event() noexcept = default;

  explicit sdl_event(const SDL_Event& event) noexcept
      : event_{event}, data_type_{classify(event)} {}

  // Pull the next event from SDL's queue, wrapped. Like `SDL_PollEvent`, call
  // only on the main thread.
  [[nodiscard]] static sdl_event poll() noexcept {
    SDL_Event event;
    if (!SDL_PollEvent(&event)) return {};
    return sdl_event{event};
  }

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

  // A default-constructed instance is invalid, signaling the lack of an event.
  [[nodiscard]] bool ok() const noexcept { return type() != sdl_event_type{}; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

#pragma endregion
#pragma region Payload accessors

  [[nodiscard]] sdl_display_event get_display() const {
    assert(data_type_ == sdl_event_data_type::display);
    return {sdl_display_id_type{event_.display.displayID},
        event_.display.data1, event_.display.data2};
  }

#pragma endregion
#pragma region Helpers
private:
  // Classify a raw `SDL_Event` by which union member it populates. Registered
  // user-range events are `user`; payload-less and unknown types are `common`.
  [[nodiscard]] static sdl_event_data_type classify(
      const SDL_Event& event) noexcept {
    switch (static_cast<sdl_event_type>(event.type)) {
    case sdl_event_type::display_orientation:
    case sdl_event_type::display_added:
    case sdl_event_type::display_removed:
    case sdl_event_type::display_moved:
    case sdl_event_type::display_desktop_mode_changed:
    case sdl_event_type::display_current_mode_changed:
    case sdl_event_type::display_content_scale_changed:
    case sdl_event_type::display_usable_bounds_changed:
      return sdl_event_data_type::display;

    case sdl_event_type::window_shown:
    case sdl_event_type::window_hidden:
    case sdl_event_type::window_exposed:
    case sdl_event_type::window_moved:
    case sdl_event_type::window_resized:
    case sdl_event_type::window_pixel_size_changed:
    case sdl_event_type::window_metal_view_resized:
    case sdl_event_type::window_minimized:
    case sdl_event_type::window_maximized:
    case sdl_event_type::window_restored:
    case sdl_event_type::window_mouse_enter:
    case sdl_event_type::window_mouse_leave:
    case sdl_event_type::window_focus_gained:
    case sdl_event_type::window_focus_lost:
    case sdl_event_type::window_close_requested:
    case sdl_event_type::window_hit_test:
    case sdl_event_type::window_iccprof_changed:
    case sdl_event_type::window_display_changed:
    case sdl_event_type::window_display_scale_changed:
    case sdl_event_type::window_safe_area_changed:
    case sdl_event_type::window_occluded:
    case sdl_event_type::window_enter_fullscreen:
    case sdl_event_type::window_leave_fullscreen:
    case sdl_event_type::window_destroyed:
    case sdl_event_type::window_hdr_state_changed:
      return sdl_event_data_type::window;

    case sdl_event_type::keyboard_added:
    case sdl_event_type::keyboard_removed: return sdl_event_data_type::kdevice;

    case sdl_event_type::key_down:
    case sdl_event_type::key_up: return sdl_event_data_type::key;

    case sdl_event_type::text_editing: return sdl_event_data_type::edit;

    case sdl_event_type::text_editing_candidates:
      return sdl_event_data_type::edit_candidates;

    case sdl_event_type::text_input: return sdl_event_data_type::text;

    case sdl_event_type::mouse_added:
    case sdl_event_type::mouse_removed: return sdl_event_data_type::mdevice;

    case sdl_event_type::mouse_motion: return sdl_event_data_type::motion;

    case sdl_event_type::mouse_button_down:
    case sdl_event_type::mouse_button_up: return sdl_event_data_type::button;

    case sdl_event_type::mouse_wheel: return sdl_event_data_type::wheel;

    case sdl_event_type::joystick_added:
    case sdl_event_type::joystick_removed:
    case sdl_event_type::joystick_update_complete:
      return sdl_event_data_type::jdevice;

    case sdl_event_type::joystick_axis_motion:
      return sdl_event_data_type::jaxis;

    case sdl_event_type::joystick_ball_motion:
      return sdl_event_data_type::jball;

    case sdl_event_type::joystick_hat_motion: return sdl_event_data_type::jhat;

    case sdl_event_type::joystick_button_down:
    case sdl_event_type::joystick_button_up:
      return sdl_event_data_type::jbutton;

    case sdl_event_type::joystick_battery_updated:
      return sdl_event_data_type::jbattery;

    case sdl_event_type::gamepad_added:
    case sdl_event_type::gamepad_removed:
    case sdl_event_type::gamepad_remapped:
    case sdl_event_type::gamepad_update_complete:
    case sdl_event_type::gamepad_steam_handle_updated:
      return sdl_event_data_type::gdevice;

    case sdl_event_type::gamepad_axis_motion:
      return sdl_event_data_type::gaxis;

    case sdl_event_type::gamepad_button_down:
    case sdl_event_type::gamepad_button_up:
      return sdl_event_data_type::gbutton;

    case sdl_event_type::gamepad_touchpad_down:
    case sdl_event_type::gamepad_touchpad_motion:
    case sdl_event_type::gamepad_touchpad_up:
      return sdl_event_data_type::gtouchpad;

    case sdl_event_type::gamepad_sensor_update:
      return sdl_event_data_type::gsensor;

    case sdl_event_type::audio_device_added:
    case sdl_event_type::audio_device_removed:
    case sdl_event_type::audio_device_format_changed:
      return sdl_event_data_type::adevice;

    case sdl_event_type::camera_device_added:
    case sdl_event_type::camera_device_removed:
    case sdl_event_type::camera_device_approved:
    case sdl_event_type::camera_device_denied:
      return sdl_event_data_type::cdevice;

    case sdl_event_type::sensor_update: return sdl_event_data_type::sensor;

    case sdl_event_type::finger_down:
    case sdl_event_type::finger_up:
    case sdl_event_type::finger_motion:
    case sdl_event_type::finger_canceled: return sdl_event_data_type::tfinger;

    case sdl_event_type::pinch_begin:
    case sdl_event_type::pinch_update:
    case sdl_event_type::pinch_end: return sdl_event_data_type::pinch;

    case sdl_event_type::pen_proximity_in:
    case sdl_event_type::pen_proximity_out:
      return sdl_event_data_type::pproximity;

    case sdl_event_type::pen_down:
    case sdl_event_type::pen_up: return sdl_event_data_type::ptouch;

    case sdl_event_type::pen_motion: return sdl_event_data_type::pmotion;

    case sdl_event_type::pen_button_down:
    case sdl_event_type::pen_button_up: return sdl_event_data_type::pbutton;

    case sdl_event_type::pen_axis: return sdl_event_data_type::paxis;

    case sdl_event_type::render_targets_reset:
    case sdl_event_type::render_device_reset:
    case sdl_event_type::render_device_lost:
      return sdl_event_data_type::render;

    case sdl_event_type::drop_file:
    case sdl_event_type::drop_text:
    case sdl_event_type::drop_begin:
    case sdl_event_type::drop_complete:
    case sdl_event_type::drop_position: return sdl_event_data_type::drop;

    case sdl_event_type::clipboard_update:
      return sdl_event_data_type::clipboard;

    default:
      if (event.type >= SDL_EVENT_USER) return sdl_event_data_type::user;
      return sdl_event_data_type::common;
    }
  }

#pragma endregion
#pragma region Data members
private:
  SDL_Event event_{};
  sdl_event_data_type data_type_{};

#pragma endregion
};

#pragma endregion

} // namespace corvid::sdl
