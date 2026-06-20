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
#pragma region sdl_window_id_type

// Type-safe handle for an `SDL_WindowID`.
enum class sdl_window_id_type : Uint32 {};
consteval auto corvid_enum_spec(sdl_window_id_type*) {
  return corvid::enums::sequence::make_sequence_enum_spec<sdl_window_id_type,
      "">();
}

#pragma endregion
#pragma region sdl_keycode

// Wrapper for `SDL_Keycode`, mirroring the full `SDLK_*` set.
enum class sdl_keycode : std::uint32_t {
  unknown = SDLK_UNKNOWN,
  backspace = SDLK_BACKSPACE,
  tab = SDLK_TAB,
  return_key = SDLK_RETURN,
  escape = SDLK_ESCAPE,
  space = SDLK_SPACE,
  exclaim = SDLK_EXCLAIM,
  dblapostrophe = SDLK_DBLAPOSTROPHE,
  hash = SDLK_HASH,
  dollar = SDLK_DOLLAR,
  percent = SDLK_PERCENT,
  ampersand = SDLK_AMPERSAND,
  apostrophe = SDLK_APOSTROPHE,
  leftparen = SDLK_LEFTPAREN,
  rightparen = SDLK_RIGHTPAREN,
  asterisk = SDLK_ASTERISK,
  plus = SDLK_PLUS,
  comma = SDLK_COMMA,
  minus = SDLK_MINUS,
  period = SDLK_PERIOD,
  slash = SDLK_SLASH,
  num0 = SDLK_0,
  num1 = SDLK_1,
  num2 = SDLK_2,
  num3 = SDLK_3,
  num4 = SDLK_4,
  num5 = SDLK_5,
  num6 = SDLK_6,
  num7 = SDLK_7,
  num8 = SDLK_8,
  num9 = SDLK_9,
  colon = SDLK_COLON,
  semicolon = SDLK_SEMICOLON,
  less = SDLK_LESS,
  equals = SDLK_EQUALS,
  greater = SDLK_GREATER,
  question = SDLK_QUESTION,
  at = SDLK_AT,
  leftbracket = SDLK_LEFTBRACKET,
  backslash = SDLK_BACKSLASH,
  rightbracket = SDLK_RIGHTBRACKET,
  caret = SDLK_CARET,
  underscore = SDLK_UNDERSCORE,
  grave = SDLK_GRAVE,
  a = SDLK_A,
  b = SDLK_B,
  c = SDLK_C,
  d = SDLK_D,
  e = SDLK_E,
  f = SDLK_F,
  g = SDLK_G,
  h = SDLK_H,
  i = SDLK_I,
  j = SDLK_J,
  k = SDLK_K,
  l = SDLK_L,
  m = SDLK_M,
  n = SDLK_N,
  o = SDLK_O,
  p = SDLK_P,
  q = SDLK_Q,
  r = SDLK_R,
  s = SDLK_S,
  t = SDLK_T,
  u = SDLK_U,
  v = SDLK_V,
  w = SDLK_W,
  x = SDLK_X,
  y = SDLK_Y,
  z = SDLK_Z,
  leftbrace = SDLK_LEFTBRACE,
  pipe = SDLK_PIPE,
  rightbrace = SDLK_RIGHTBRACE,
  tilde = SDLK_TILDE,
  delete_key = SDLK_DELETE,
  plusminus = SDLK_PLUSMINUS,
  left_tab = SDLK_LEFT_TAB,
  level5_shift = SDLK_LEVEL5_SHIFT,
  multi_key_compose = SDLK_MULTI_KEY_COMPOSE,
  lmeta = SDLK_LMETA,
  rmeta = SDLK_RMETA,
  lhyper = SDLK_LHYPER,
  rhyper = SDLK_RHYPER,
  capslock = SDLK_CAPSLOCK,
  f1 = SDLK_F1,
  f2 = SDLK_F2,
  f3 = SDLK_F3,
  f4 = SDLK_F4,
  f5 = SDLK_F5,
  f6 = SDLK_F6,
  f7 = SDLK_F7,
  f8 = SDLK_F8,
  f9 = SDLK_F9,
  f10 = SDLK_F10,
  f11 = SDLK_F11,
  f12 = SDLK_F12,
  printscreen = SDLK_PRINTSCREEN,
  scrolllock = SDLK_SCROLLLOCK,
  pause = SDLK_PAUSE,
  insert = SDLK_INSERT,
  home = SDLK_HOME,
  pageup = SDLK_PAGEUP,
  end = SDLK_END,
  pagedown = SDLK_PAGEDOWN,
  right = SDLK_RIGHT,
  left = SDLK_LEFT,
  down = SDLK_DOWN,
  up = SDLK_UP,
  numlockclear = SDLK_NUMLOCKCLEAR,
  kp_divide = SDLK_KP_DIVIDE,
  kp_multiply = SDLK_KP_MULTIPLY,
  kp_minus = SDLK_KP_MINUS,
  kp_plus = SDLK_KP_PLUS,
  kp_enter = SDLK_KP_ENTER,
  kp_1 = SDLK_KP_1,
  kp_2 = SDLK_KP_2,
  kp_3 = SDLK_KP_3,
  kp_4 = SDLK_KP_4,
  kp_5 = SDLK_KP_5,
  kp_6 = SDLK_KP_6,
  kp_7 = SDLK_KP_7,
  kp_8 = SDLK_KP_8,
  kp_9 = SDLK_KP_9,
  kp_0 = SDLK_KP_0,
  kp_period = SDLK_KP_PERIOD,
  application = SDLK_APPLICATION,
  power = SDLK_POWER,
  kp_equals = SDLK_KP_EQUALS,
  f13 = SDLK_F13,
  f14 = SDLK_F14,
  f15 = SDLK_F15,
  f16 = SDLK_F16,
  f17 = SDLK_F17,
  f18 = SDLK_F18,
  f19 = SDLK_F19,
  f20 = SDLK_F20,
  f21 = SDLK_F21,
  f22 = SDLK_F22,
  f23 = SDLK_F23,
  f24 = SDLK_F24,
  execute = SDLK_EXECUTE,
  help = SDLK_HELP,
  menu = SDLK_MENU,
  select = SDLK_SELECT,
  stop = SDLK_STOP,
  again = SDLK_AGAIN,
  undo = SDLK_UNDO,
  cut = SDLK_CUT,
  copy = SDLK_COPY,
  paste = SDLK_PASTE,
  find = SDLK_FIND,
  mute = SDLK_MUTE,
  volumeup = SDLK_VOLUMEUP,
  volumedown = SDLK_VOLUMEDOWN,
  kp_comma = SDLK_KP_COMMA,
  kp_equalsas400 = SDLK_KP_EQUALSAS400,
  alterase = SDLK_ALTERASE,
  sysreq = SDLK_SYSREQ,
  cancel = SDLK_CANCEL,
  clear = SDLK_CLEAR,
  prior = SDLK_PRIOR,
  return2 = SDLK_RETURN2,
  separator = SDLK_SEPARATOR,
  out = SDLK_OUT,
  oper = SDLK_OPER,
  clearagain = SDLK_CLEARAGAIN,
  crsel = SDLK_CRSEL,
  exsel = SDLK_EXSEL,
  kp_00 = SDLK_KP_00,
  kp_000 = SDLK_KP_000,
  thousandsseparator = SDLK_THOUSANDSSEPARATOR,
  decimalseparator = SDLK_DECIMALSEPARATOR,
  currencyunit = SDLK_CURRENCYUNIT,
  currencysubunit = SDLK_CURRENCYSUBUNIT,
  kp_leftparen = SDLK_KP_LEFTPAREN,
  kp_rightparen = SDLK_KP_RIGHTPAREN,
  kp_leftbrace = SDLK_KP_LEFTBRACE,
  kp_rightbrace = SDLK_KP_RIGHTBRACE,
  kp_tab = SDLK_KP_TAB,
  kp_backspace = SDLK_KP_BACKSPACE,
  kp_a = SDLK_KP_A,
  kp_b = SDLK_KP_B,
  kp_c = SDLK_KP_C,
  kp_d = SDLK_KP_D,
  kp_e = SDLK_KP_E,
  kp_f = SDLK_KP_F,
  kp_xor = SDLK_KP_XOR,
  kp_power = SDLK_KP_POWER,
  kp_percent = SDLK_KP_PERCENT,
  kp_less = SDLK_KP_LESS,
  kp_greater = SDLK_KP_GREATER,
  kp_ampersand = SDLK_KP_AMPERSAND,
  kp_dblampersand = SDLK_KP_DBLAMPERSAND,
  kp_verticalbar = SDLK_KP_VERTICALBAR,
  kp_dblverticalbar = SDLK_KP_DBLVERTICALBAR,
  kp_colon = SDLK_KP_COLON,
  kp_hash = SDLK_KP_HASH,
  kp_space = SDLK_KP_SPACE,
  kp_at = SDLK_KP_AT,
  kp_exclam = SDLK_KP_EXCLAM,
  kp_memstore = SDLK_KP_MEMSTORE,
  kp_memrecall = SDLK_KP_MEMRECALL,
  kp_memclear = SDLK_KP_MEMCLEAR,
  kp_memadd = SDLK_KP_MEMADD,
  kp_memsubtract = SDLK_KP_MEMSUBTRACT,
  kp_memmultiply = SDLK_KP_MEMMULTIPLY,
  kp_memdivide = SDLK_KP_MEMDIVIDE,
  kp_plusminus = SDLK_KP_PLUSMINUS,
  kp_clear = SDLK_KP_CLEAR,
  kp_clearentry = SDLK_KP_CLEARENTRY,
  kp_binary = SDLK_KP_BINARY,
  kp_octal = SDLK_KP_OCTAL,
  kp_decimal = SDLK_KP_DECIMAL,
  kp_hexadecimal = SDLK_KP_HEXADECIMAL,
  lctrl = SDLK_LCTRL,
  lshift = SDLK_LSHIFT,
  lalt = SDLK_LALT,
  lgui = SDLK_LGUI,
  rctrl = SDLK_RCTRL,
  rshift = SDLK_RSHIFT,
  ralt = SDLK_RALT,
  rgui = SDLK_RGUI,
  mode = SDLK_MODE,
  sleep = SDLK_SLEEP,
  wake = SDLK_WAKE,
  channel_increment = SDLK_CHANNEL_INCREMENT,
  channel_decrement = SDLK_CHANNEL_DECREMENT,
  media_play = SDLK_MEDIA_PLAY,
  media_pause = SDLK_MEDIA_PAUSE,
  media_record = SDLK_MEDIA_RECORD,
  media_fast_forward = SDLK_MEDIA_FAST_FORWARD,
  media_rewind = SDLK_MEDIA_REWIND,
  media_next_track = SDLK_MEDIA_NEXT_TRACK,
  media_previous_track = SDLK_MEDIA_PREVIOUS_TRACK,
  media_stop = SDLK_MEDIA_STOP,
  media_eject = SDLK_MEDIA_EJECT,
  media_play_pause = SDLK_MEDIA_PLAY_PAUSE,
  media_select = SDLK_MEDIA_SELECT,
  ac_new = SDLK_AC_NEW,
  ac_open = SDLK_AC_OPEN,
  ac_close = SDLK_AC_CLOSE,
  ac_exit = SDLK_AC_EXIT,
  ac_save = SDLK_AC_SAVE,
  ac_print = SDLK_AC_PRINT,
  ac_properties = SDLK_AC_PROPERTIES,
  ac_search = SDLK_AC_SEARCH,
  ac_home = SDLK_AC_HOME,
  ac_back = SDLK_AC_BACK,
  ac_forward = SDLK_AC_FORWARD,
  ac_stop = SDLK_AC_STOP,
  ac_refresh = SDLK_AC_REFRESH,
  ac_bookmarks = SDLK_AC_BOOKMARKS,
  softleft = SDLK_SOFTLEFT,
  softright = SDLK_SOFTRIGHT,
  call = SDLK_CALL,
  endcall = SDLK_ENDCALL,
};

consteval auto corvid_enum_spec(sdl_keycode*) {
  return corvid::enums::sequence::make_sequence_enum_spec<sdl_keycode,
      "0,unknown|8,backspace,tab|13,return_key|27,escape|32,space,exclaim,"
      "dblapostrophe,hash,dollar,percent,ampersand,apostrophe,leftparen,"
      "rightparen,asterisk,plus,comma,minus,period,slash,num0,num1,num2,num3,"
      "num4,num5,num6,num7,num8,num9,colon,semicolon,less,equals,greater,"
      "question,at|91,leftbracket,backslash,rightbracket,caret,underscore,"
      "grave,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q,r,s,t,u,v,w,x,y,z,leftbrace,"
      "pipe,rightbrace,tilde,delete_key|177,plusminus|536870913,left_tab,"
      "level5_shift,multi_key_compose,lmeta,rmeta,lhyper,rhyper|1073741881,"
      "capslock,f1,f2,f3,f4,f5,f6,f7,f8,f9,f10,f11,f12,printscreen,scrolllock,"
      "pause,insert,home,pageup,,end,pagedown,right,left,down,up,numlockclear,"
      "kp_divide,kp_multiply,kp_minus,kp_plus,kp_enter,kp_1,kp_2,kp_3,kp_4,kp_"
      "5,kp_6,kp_7,kp_8,kp_9,kp_0,kp_period,,application,power,kp_equals,f13,"
      "f14,f15,f16,f17,f18,f19,f20,f21,f22,f23,f24,execute,help,menu,select,"
      "stop,again,undo,cut,copy,paste,find,mute,volumeup,volumedown|"
      "1073741957,kp_comma,kp_equalsas400|1073741977,alterase,sysreq,cancel,"
      "clear,prior,return2,separator,out,oper,clearagain,crsel,exsel|"
      "1073742000,kp_00,kp_000,thousandsseparator,decimalseparator,"
      "currencyunit,currencysubunit,kp_leftparen,kp_rightparen,kp_leftbrace,"
      "kp_rightbrace,kp_tab,kp_backspace,kp_a,kp_b,kp_c,kp_d,kp_e,kp_f,kp_xor,"
      "kp_power,kp_percent,kp_less,kp_greater,kp_ampersand,kp_dblampersand,kp_"
      "verticalbar,kp_dblverticalbar,kp_colon,kp_hash,kp_space,kp_at,kp_"
      "exclam,kp_memstore,kp_memrecall,kp_memclear,kp_memadd,kp_memsubtract,"
      "kp_memmultiply,kp_memdivide,kp_plusminus,kp_clear,kp_clearentry,kp_"
      "binary,kp_octal,kp_decimal,kp_hexadecimal,,,lctrl,lshift,lalt,lgui,"
      "rctrl,rshift,ralt,rgui|1073742081,mode,sleep,wake,channel_increment,"
      "channel_decrement,media_play,media_pause,media_record,media_fast_"
      "forward,media_rewind,media_next_track,media_previous_track,media_stop,"
      "media_eject,media_play_pause,media_select,ac_new,ac_open,ac_close,ac_"
      "exit,ac_save,ac_print,ac_properties,ac_search,ac_home,ac_back,ac_"
      "forward,ac_stop,ac_refresh,ac_bookmarks,softleft,softright,call,"
      "endcall">();
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

// Payload of a window event.
//
// `data1`/`data2` are event-specific; for `window_pixel_size_changed` they are
// the new width and height in pixels.
struct sdl_window_event {
  sdl_window_id_type window_id;
  Sint32 data1;
  Sint32 data2;
};

// Payload of a mouse-wheel event.
//
//  `x`/`y` are the scroll deltas; mouse_x`/`mouse_y` are the cursor position
//  when it fired.
struct sdl_wheel_event {
  float x;
  float y;
  float mouse_x;
  float mouse_y;
};

// Payload of a mouse-motion event.
//
// `x`/`y` are the cursor position; `xrel`/`yrel` are the movement since the
// previous event; `left_held` is whether the left button was down during the
// motion.
struct sdl_motion_event {
  float x;
  float y;
  float xrel;
  float yrel;
  bool left_held;
};

// Payload of a keyboard event.
//
// `key` is the virtual key; `down` is true on a press and false on a release;
// `repeat` marks an auto-repeat press.
struct sdl_key_event {
  sdl_keycode key;
  bool down;
  bool repeat;
};

// Union-aware wrapper for an `SDL_Event`.
//
// Holds the raw event but presents it safely and in C++ terms: `type()` is the
// registered event enum, `data_type()` names the active union member.
//
// To read an event's payload, switch on `type()` and call the typed accessor
// for its shape (e.g. `get_display()`), which returns a cleaned-up struct and
// asserts the shape matches.
class sdl_event {
public:
#pragma region Construction

  sdl_event() noexcept = default;

  explicit sdl_event(const SDL_Event& event) noexcept
      : event_{event}, data_type_{classify(event)} {}

  // Pull the next event from SDL's queue, wrapped. Call only on the main
  // thread.
  [[nodiscard]] static sdl_event poll() noexcept {
    SDL_Event event;
    if (!SDL_PollEvent(&event)) return {};
    return sdl_event{event};
  }

#pragma endregion
#pragma region Header accessors

  // Specific event type.
  [[nodiscard]] sdl_event_type type() const noexcept {
    return static_cast<sdl_event_type>(event_.type);
  }
  // Type of data payload.
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

  [[nodiscard]] sdl_window_event get_window() const {
    assert(data_type_ == sdl_event_data_type::window);
    return {sdl_window_id_type{event_.window.windowID}, event_.window.data1,
        event_.window.data2};
  }

  [[nodiscard]] sdl_wheel_event get_wheel() const {
    assert(data_type_ == sdl_event_data_type::wheel);
    return {event_.wheel.x, event_.wheel.y, event_.wheel.mouse_x,
        event_.wheel.mouse_y};
  }

  [[nodiscard]] sdl_motion_event get_motion() const {
    assert(data_type_ == sdl_event_data_type::motion);
    return {event_.motion.x, event_.motion.y, event_.motion.xrel,
        event_.motion.yrel, (event_.motion.state & SDL_BUTTON_LMASK) != 0};
  }

  [[nodiscard]] sdl_key_event get_key() const {
    assert(data_type_ == sdl_event_data_type::key);
    return {static_cast<sdl_keycode>(event_.key.key), event_.key.down,
        event_.key.repeat};
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
