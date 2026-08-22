// Unit test for corvid::sdl::sdl_window and sdl_subsystem (corvid/sdl/
// sdl_window.h, sdl_subsystem.h): the window and init flag enums round-trip
// through their bitmask registrations (every named single bit, which also
// verifies each spec string is bit-aligned across its gaps, plus a combined
// value and an unknown name), and a hidden window is created, queried, and
// moved.

#include <string_view>
#include <utility>

#include "corvid/enums/enum_conversion.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "corvid/sdl/sdl_window.h"
#include "catch2_main.h"

using namespace corvid;
using corvid::sdl::sdl_init_flags;
using corvid::sdl::sdl_subsystem;
using corvid::sdl::sdl_window;
using corvid::sdl::sdl_window_flags;

namespace {

struct flag_case {
  sdl_window_flags value;
  std::string_view name;
};

constexpr flag_case flag_cases[]{
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

struct init_case {
  sdl_init_flags value;
  std::string_view name;
};

constexpr init_case init_cases[]{
    {sdl_init_flags::audio, "audio"},
    {sdl_init_flags::video, "video"},
    {sdl_init_flags::joystick, "joystick"},
    {sdl_init_flags::haptic, "haptic"},
    {sdl_init_flags::gamepad, "gamepad"},
    {sdl_init_flags::events, "events"},
    {sdl_init_flags::sensor, "sensor"},
    {sdl_init_flags::camera, "camera"},
};

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region sdl_window_flags

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

#pragma endregion
#pragma region sdl_init_flags

TEST_CASE("sdl_init_flags string round-trip", "[sdl][enums]") {
  for (const auto& c : init_cases) {
    CAPTURE(c.name);
    CHECK(enum_as_string(c.value) == c.name);
    sdl_init_flags parsed{};
    CHECK(convert_enum(parsed, c.name));
    CHECK(parsed == c.value);
  }

  const auto combo = sdl_init_flags::video | sdl_init_flags::gamepad;
  CHECK(enum_as_string(combo) == "gamepad + video");
  sdl_init_flags parsed{};
  CHECK(convert_enum(parsed, "video + gamepad"));
  CHECK(parsed == combo);
  CHECK_FALSE(convert_enum(parsed, "not_a_subsystem"));
}

#pragma endregion
#pragma region sdl_window

TEST_CASE("sdl_window creates, queries, and moves", "[sdl]") {
  const sdl_subsystem sdl;
  sdl_window win{"sdl_window_test", 64, 64, sdl_window_flags::hidden};
  REQUIRE(win.get());
  CHECK(win.native_handle());
  CHECK(win.set_minimum_size(32, 32));
  CHECK(win.set_relative_mouse_mode(false));

  // Moving transfers the handle and leaves the source empty.
  auto* const raw = win.get();
  sdl_window moved = std::move(win);
  CHECK(moved.get() == raw);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK_FALSE(win.get());
  sdl_window assigned{"sdl_window_test 2", 32, 32, sdl_window_flags::hidden};
  assigned = std::move(moved);
  CHECK(assigned.get() == raw);
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK_FALSE(moved.get());
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
