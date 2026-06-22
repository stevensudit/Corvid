// Unit test for corvid::sdl::sdl_window_flags (corvid/sdl/sdl_window.h):
// exercises the bitmask-enum registration wrapping SDL's SDL_WINDOW_* flags.
// Round-trips every named single-bit flag, which also verifies the spec string
// is bit-aligned across the 22-27 gap, plus a combined value and an unknown
// name.

#include <string_view>

#include "corvid/enums/enum_conversion.h"
#include "corvid/sdl/sdl_window.h"
#include "catch2_main.h"

using namespace corvid;
using corvid::sdl::sdl_window_flags;

namespace {

struct flag_case {
  sdl_window_flags value;
  std::string_view name;
};

constexpr flag_case flag_cases[] = {
    {sdl_window_flags::fullscreen, "fullscreen"},
    {sdl_window_flags::opengl, "opengl"},
    {sdl_window_flags::occluded, "occluded"},
    {sdl_window_flags::hidden, "hidden"},
    {sdl_window_flags::borderless, "borderless"},
    {sdl_window_flags::resizable, "resizable"},
    {sdl_window_flags::minimized, "minimized"},
    {sdl_window_flags::maximized, "maximized"},
    {sdl_window_flags::mouse_grabbed, "mouse_grabbed"},
    {sdl_window_flags::input_focus, "input_focus"},
    {sdl_window_flags::mouse_focus, "mouse_focus"},
    {sdl_window_flags::external, "external"},
    {sdl_window_flags::modal, "modal"},
    {sdl_window_flags::high_pixel_density, "high_pixel_density"},
    {sdl_window_flags::mouse_capture, "mouse_capture"},
    {sdl_window_flags::mouse_relative_mode, "mouse_relative_mode"},
    {sdl_window_flags::always_on_top, "always_on_top"},
    {sdl_window_flags::utility, "utility"},
    {sdl_window_flags::tooltip, "tooltip"},
    {sdl_window_flags::popup_menu, "popup_menu"},
    {sdl_window_flags::keyboard_grabbed, "keyboard_grabbed"},
    {sdl_window_flags::fill_document, "fill_document"},
    {sdl_window_flags::vulkan, "vulkan"},
    {sdl_window_flags::metal, "metal"},
    {sdl_window_flags::transparent, "transparent"},
    {sdl_window_flags::not_focusable, "not_focusable"},
};

} // namespace

TEST_CASE("sdl_window_flags single-flag string round-trip", "[sdl][enums]") {
  for (const auto& c : flag_cases) {
    CAPTURE(c.name);
    CHECK(enum_as_string(c.value) == c.name); // enum -> string
    sdl_window_flags parsed{};
    CHECK(convert_enum(parsed, c.name)); // string -> enum
    CHECK(parsed == c.value);
  }
}

TEST_CASE("sdl_window_flags combined and unknown handling", "[sdl][enums]") {
  // Combined flags print in registration order (high bit first), joined with
  // " + "; parsing is order-independent.
  const auto combo =
      sdl_window_flags::borderless | sdl_window_flags::resizable;
  CHECK(enum_as_string(combo) == "resizable + borderless");
  sdl_window_flags parsed{};
  CHECK(convert_enum(parsed, "borderless + resizable"));
  CHECK(parsed == combo);

  // An unknown name does not parse.
  sdl_window_flags unused{};
  CHECK_FALSE(convert_enum(unused, "not_a_flag"));
}
