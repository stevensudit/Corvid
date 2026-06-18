// Unit test for corvid::sdl::sdl_event_type (corvid/sdl/sdl_event_type.h):
// exercises the sparse sequence-enum registration of SDL's ~118 event types.
// Checks the first and last value of every block so a misaligned run in the
// spec string is caught, that enum<->string round-trips, that an in-gap value
// stringifies to its number, and that an unknown name fails to parse.

#include <cstdint>
#include <string_view>

#include "corvid/enums/enum_conversion.h"
#include "corvid/sdl/sdl_event_type.h"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::strings;
using corvid::sdl::sdl_event_type;

namespace {

struct event_case {
  sdl_event_type value;
  std::uint32_t code;
  std::string_view name;
};

// First and last of every sparse block; a run that starts one slot off would
// break the boundary values here.
constexpr event_case event_cases[] = {
    {sdl_event_type::quit, 256, "quit"},
    {sdl_event_type::system_theme_changed, 264, "system_theme_changed"},
    {sdl_event_type::display_orientation, 337, "display_orientation"},
    {sdl_event_type::display_usable_bounds_changed, 344,
        "display_usable_bounds_changed"},
    {sdl_event_type::window_shown, 514, "window_shown"},
    {sdl_event_type::window_hdr_state_changed, 538, "window_hdr_state_changed"},
    {sdl_event_type::key_down, 768, "key_down"},
    {sdl_event_type::screen_keyboard_hidden, 777, "screen_keyboard_hidden"},
    {sdl_event_type::mouse_motion, 1024, "mouse_motion"},
    {sdl_event_type::mouse_removed, 1029, "mouse_removed"},
    {sdl_event_type::joystick_axis_motion, 1536, "joystick_axis_motion"},
    {sdl_event_type::joystick_update_complete, 1544,
        "joystick_update_complete"},
    {sdl_event_type::gamepad_axis_motion, 1616, "gamepad_axis_motion"},
    {sdl_event_type::gamepad_steam_handle_updated, 1627,
        "gamepad_steam_handle_updated"},
    {sdl_event_type::finger_down, 1792, "finger_down"},
    {sdl_event_type::finger_canceled, 1795, "finger_canceled"},
    {sdl_event_type::pinch_begin, 1808, "pinch_begin"},
    {sdl_event_type::pinch_end, 1810, "pinch_end"},
    {sdl_event_type::clipboard_update, 2304, "clipboard_update"},
    {sdl_event_type::drop_file, 4096, "drop_file"},
    {sdl_event_type::drop_position, 4100, "drop_position"},
    {sdl_event_type::audio_device_added, 4352, "audio_device_added"},
    {sdl_event_type::audio_device_format_changed, 4354,
        "audio_device_format_changed"},
    {sdl_event_type::sensor_update, 4608, "sensor_update"},
    {sdl_event_type::pen_proximity_in, 4864, "pen_proximity_in"},
    {sdl_event_type::pen_axis, 4871, "pen_axis"},
    {sdl_event_type::camera_device_added, 5120, "camera_device_added"},
    {sdl_event_type::camera_device_denied, 5123, "camera_device_denied"},
    {sdl_event_type::render_targets_reset, 8192, "render_targets_reset"},
    {sdl_event_type::render_device_lost, 8194, "render_device_lost"},
    {sdl_event_type::private0, 16384, "private0"},
    {sdl_event_type::private3, 16387, "private3"},
    {sdl_event_type::poll_sentinel, 32512, "poll_sentinel"},
    {sdl_event_type::user, 32768, "user"},
    {sdl_event_type::last, 65535, "last"},
};

} // namespace

TEST_CASE("sdl_event_type values and string round-trip", "[sdl][enums]") {
  for (const auto& c : event_cases) {
    CAPTURE(c.name);
    CHECK(*c.value == c.code);                // underlying value
    CHECK(enum_as_string(c.value) == c.name); // enum -> string
    sdl_event_type parsed{};
    CHECK(convert_enum(parsed, c.name)); // string -> enum
    CHECK(parsed == c.value);
  }
}

TEST_CASE("sdl_event_type unnamed and unknown handling", "[sdl][enums]") {
  // 999 falls in a gap between blocks; it stringifies to its number.
  CHECK(enum_as_string(sdl_event_type{999}) == "999");

  // An unknown name does not parse.
  sdl_event_type unused{};
  CHECK_FALSE(convert_enum(unused, "not_a_real_event"));
}
