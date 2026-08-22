// Unit test for corvid::sdl::camera_input (corvid/sdl/camera_input.h):
// synthesized events fold into the held-key, mouse-look, and wheel state;
// `movement`, `look`, and `dolly` read it back. `handle` takes a window for
// the cursor capture, so a hidden one is created.

#include <cmath>
#include <numbers>

#include "corvid/sdl/camera_input.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "catch2_main.h"

using namespace corvid;
using corvid::sdl::camera_input;
using corvid::sdl::sdl_event;
using corvid::sdl::sdl_init_flags;
using corvid::sdl::sdl_keycode;
using corvid::sdl::sdl_subsystem;
using corvid::sdl::sdl_window;
using corvid::sdl::sdl_window_flags;

namespace {

sdl_event key_event(sdl_keycode key, bool down) {
  SDL_Event raw{};
  raw.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  raw.key.key = *key;
  raw.key.down = down;
  return sdl_event{raw};
}

sdl_event button_event(Uint8 button, bool down) {
  SDL_Event raw{};
  raw.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
  raw.button.button = button;
  raw.button.down = down;
  return sdl_event{raw};
}

sdl_event motion_event(float xrel, float yrel) {
  SDL_Event raw{};
  raw.type = SDL_EVENT_MOUSE_MOTION;
  raw.motion.xrel = xrel;
  raw.motion.yrel = yrel;
  return sdl_event{raw};
}

sdl_event wheel_event(float y) {
  SDL_Event raw{};
  raw.type = SDL_EVENT_MOUSE_WHEEL;
  raw.wheel.y = y;
  return sdl_event{raw};
}

constexpr auto eps = 1.0e-6F;

bool near(float a, float b) { return std::fabs(a - b) < eps; }

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Keys

TEST_CASE("camera_input folds key events into held flags", "[sdl]") {
  const sdl_subsystem sdl;
  sdl_window win{"camera_input_test", 64, 64, sdl_window_flags::hidden};
  camera_input input;

  SECTION("each movement key sets and clears its flag") {
    CHECK(input.handle(key_event(sdl_keycode::w, true), win));
    CHECK(input.forward);
    CHECK(input.handle(key_event(sdl_keycode::w, false), win));
    CHECK_FALSE(input.forward);

    CHECK(input.handle(key_event(sdl_keycode::a, true), win));
    CHECK(input.left);
    CHECK(input.handle(key_event(sdl_keycode::space, true), win));
    CHECK(input.up);
    CHECK(input.handle(key_event(sdl_keycode::lctrl, true), win));
    CHECK(input.down);
    CHECK(input.handle(key_event(sdl_keycode::lshift, true), win));
    CHECK(input.fast);
  }

  SECTION("last press wins on an opposing pair") {
    CHECK(input.handle(key_event(sdl_keycode::w, true), win));
    CHECK(input.handle(key_event(sdl_keycode::s, true), win));
    CHECK(input.back);
    CHECK_FALSE(input.forward);
    // Re-pressing the cleared key takes it back.
    CHECK(input.handle(key_event(sdl_keycode::w, true), win));
    CHECK(input.forward);
    CHECK_FALSE(input.back);
    // Releasing the other does not disturb it.
    CHECK(input.handle(key_event(sdl_keycode::s, false), win));
    CHECK(input.forward);
  }

  SECTION("other keys are left for another handler") {
    CHECK_FALSE(input.handle(key_event(sdl_keycode::escape, true), win));
    CHECK_FALSE(input.handle(key_event(sdl_keycode::f1, false), win));
  }

  SECTION("release_keys clears every held key") {
    CHECK(input.handle(key_event(sdl_keycode::w, true), win));
    CHECK(input.handle(key_event(sdl_keycode::d, true), win));
    CHECK(input.handle(key_event(sdl_keycode::space, true), win));
    CHECK(input.handle(key_event(sdl_keycode::lshift, true), win));
    input.release_keys();
    CHECK_FALSE(input.forward);
    CHECK_FALSE(input.right);
    CHECK_FALSE(input.up);
    CHECK_FALSE(input.fast);
  }
}

#pragma endregion
#pragma region Movement

TEST_CASE("camera_input movement is capped planar plus a free vertical",
    "[sdl]") {
  camera_input input;

  SECTION("nothing held moves nothing") {
    const auto [fwd, side, up] = input.movement();
    CHECK(fwd == 0.0F);
    CHECK(side == 0.0F);
    CHECK(up == 0.0F);
  }

  SECTION("a cardinal moves at full speed") {
    input.back = true;
    const auto [fwd, side, up] = input.movement(2.0F);
    CHECK(fwd == -2.0F);
    CHECK(side == 0.0F);
  }

  SECTION("a diagonal is capped to one speed") {
    input.forward = true;
    input.right = true;
    const auto [fwd, side, up] = input.movement();
    CHECK(near(fwd, std::numbers::sqrt2_v<float> / 2.0F));
    CHECK(near(side, std::numbers::sqrt2_v<float> / 2.0F));
    CHECK(near(std::hypot(fwd, side), 1.0F));
  }

  SECTION("the vertical is separate and uncapped by the planar") {
    input.forward = true;
    input.right = true;
    input.up = true;
    const auto [fwd, side, up] = input.movement();
    CHECK(up == 1.0F);
    input.up = false;
    input.down = true;
    CHECK(std::get<2>(input.movement()) == -1.0F);
  }

  SECTION("Shift scales by run_multiplier") {
    input.forward = true;
    input.fast = true;
    input.run_multiplier = 3.0F;
    const auto [fwd, side, up] = input.movement(2.0F);
    CHECK(fwd == 6.0F);
  }
}

#pragma endregion
#pragma region Mouse

TEST_CASE("camera_input folds mouse events into look and scroll", "[sdl]") {
  const sdl_subsystem sdl;
  sdl_window win{"camera_input_test", 64, 64, sdl_window_flags::hidden};
  camera_input input;

  SECTION("motion is ignored and unconsumed until the right button holds") {
    CHECK_FALSE(input.handle(motion_event(3.0F, 4.0F), win));
    CHECK(input.look_dx == 0.0F);
    CHECK(input.look_dy == 0.0F);

    CHECK(input.handle(button_event(SDL_BUTTON_RIGHT, true), win));
    CHECK(input.looking);
    CHECK(input.handle(motion_event(3.0F, 4.0F), win));
    CHECK(input.handle(motion_event(1.0F, -1.0F), win));
    CHECK(input.look_dx == 4.0F);
    CHECK(input.look_dy == 3.0F);

    CHECK(input.handle(button_event(SDL_BUTTON_RIGHT, false), win));
    CHECK_FALSE(input.looking);
  }

  SECTION("other buttons are left for another handler") {
    CHECK_FALSE(input.handle(button_event(SDL_BUTTON_LEFT, true), win));
    CHECK_FALSE(input.handle(button_event(SDL_BUTTON_MIDDLE, false), win));
    CHECK_FALSE(input.looking);
  }

  SECTION("look returns a scaled, y-flipped rotation and clears the delta") {
    CHECK(input.handle(button_event(SDL_BUTTON_RIGHT, true), win));
    CHECK(input.handle(motion_event(10.0F, 20.0F), win));
    const auto [yaw, pitch] = input.look(1.0F / 60.0F);
    // The filter's first sample passes through unsmoothed.
    CHECK(near(yaw, 10.0F * input.look_sensitivity));
    CHECK(near(pitch, -20.0F * input.look_sensitivity));
    CHECK(input.look_dx == 0.0F);
    CHECK(input.look_dy == 0.0F);
  }

  SECTION("look while not looking returns nothing and drops any delta") {
    input.look_dx = 5.0F;
    input.look_dy = 5.0F;
    const auto [yaw, pitch] = input.look(1.0F / 60.0F);
    CHECK(yaw == 0.0F);
    CHECK(pitch == 0.0F);
    CHECK(input.look_dx == 0.0F);
    CHECK(input.look_dy == 0.0F);
  }

  SECTION("the wheel accumulates and dolly consumes it") {
    CHECK(input.handle(wheel_event(1.0F), win));
    CHECK(input.handle(wheel_event(2.0F), win));
    input.scroll_step = 0.5F;
    CHECK(input.dolly() == 1.5F);
    CHECK(input.dolly() == 0.0F);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
