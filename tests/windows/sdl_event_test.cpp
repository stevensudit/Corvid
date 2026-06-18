// Unit test for corvid::sdl::sdl_event / sdl_event::poll (corvid/sdl/
// sdl_event.h): exercises the union-aware wrapper. The classify switch is
// covered with at least one event type per data shape (plus first/last of the
// big display and window blocks, where a mis-grouped case label would slip
// through); the registered enums round-trip; the header accessors and the
// display payload accessor copy through; and a headless push/poll case drives
// `poll`.

#include "corvid/enums/enum_conversion.h"
#include "corvid/sdl/sdl_event.h"
#include "catch2_main.h"

using namespace corvid::enums;
using corvid::sdl::sdl_display_id_type;
using corvid::sdl::sdl_event;
using corvid::sdl::sdl_event_data_type;
using corvid::sdl::sdl_event_type;

namespace {

// Wrap an SDL_Event of the given type and check its classified data shape.
void check_data_type(SDL_EventType type, sdl_event_data_type expected) {
  CAPTURE(type);
  SDL_Event raw{};
  raw.type = type;
  CHECK(sdl_event{raw}.data_type() == expected);
}

} // namespace

TEST_CASE("sdl_event classifies every union member", "[sdl][event]") {
  using dt = sdl_event_data_type;
  check_data_type(SDL_EVENT_DISPLAY_ORIENTATION, dt::display);
  check_data_type(SDL_EVENT_DISPLAY_USABLE_BOUNDS_CHANGED, dt::display);
  check_data_type(SDL_EVENT_WINDOW_SHOWN, dt::window);
  check_data_type(SDL_EVENT_WINDOW_HDR_STATE_CHANGED, dt::window);
  check_data_type(SDL_EVENT_KEYBOARD_ADDED, dt::kdevice);
  check_data_type(SDL_EVENT_KEY_DOWN, dt::key);
  check_data_type(SDL_EVENT_KEY_UP, dt::key);
  check_data_type(SDL_EVENT_TEXT_EDITING, dt::edit);
  check_data_type(SDL_EVENT_TEXT_EDITING_CANDIDATES, dt::edit_candidates);
  check_data_type(SDL_EVENT_TEXT_INPUT, dt::text);
  check_data_type(SDL_EVENT_MOUSE_ADDED, dt::mdevice);
  check_data_type(SDL_EVENT_MOUSE_MOTION, dt::motion);
  check_data_type(SDL_EVENT_MOUSE_BUTTON_DOWN, dt::button);
  check_data_type(SDL_EVENT_MOUSE_BUTTON_UP, dt::button);
  check_data_type(SDL_EVENT_MOUSE_WHEEL, dt::wheel);
  check_data_type(SDL_EVENT_JOYSTICK_ADDED, dt::jdevice);
  check_data_type(SDL_EVENT_JOYSTICK_AXIS_MOTION, dt::jaxis);
  check_data_type(SDL_EVENT_JOYSTICK_BALL_MOTION, dt::jball);
  check_data_type(SDL_EVENT_JOYSTICK_HAT_MOTION, dt::jhat);
  check_data_type(SDL_EVENT_JOYSTICK_BUTTON_DOWN, dt::jbutton);
  check_data_type(SDL_EVENT_JOYSTICK_BATTERY_UPDATED, dt::jbattery);
  check_data_type(SDL_EVENT_GAMEPAD_ADDED, dt::gdevice);
  check_data_type(SDL_EVENT_GAMEPAD_AXIS_MOTION, dt::gaxis);
  check_data_type(SDL_EVENT_GAMEPAD_BUTTON_DOWN, dt::gbutton);
  check_data_type(SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, dt::gtouchpad);
  check_data_type(SDL_EVENT_GAMEPAD_SENSOR_UPDATE, dt::gsensor);
  check_data_type(SDL_EVENT_AUDIO_DEVICE_ADDED, dt::adevice);
  check_data_type(SDL_EVENT_CAMERA_DEVICE_ADDED, dt::cdevice);
  check_data_type(SDL_EVENT_SENSOR_UPDATE, dt::sensor);
  check_data_type(SDL_EVENT_FINGER_DOWN, dt::tfinger);
  check_data_type(SDL_EVENT_PINCH_BEGIN, dt::pinch);
  check_data_type(SDL_EVENT_PEN_PROXIMITY_IN, dt::pproximity);
  check_data_type(SDL_EVENT_PEN_DOWN, dt::ptouch);
  check_data_type(SDL_EVENT_PEN_MOTION, dt::pmotion);
  check_data_type(SDL_EVENT_PEN_BUTTON_DOWN, dt::pbutton);
  check_data_type(SDL_EVENT_PEN_AXIS, dt::paxis);
  check_data_type(SDL_EVENT_RENDER_TARGETS_RESET, dt::render);
  check_data_type(SDL_EVENT_DROP_FILE, dt::drop);
  check_data_type(SDL_EVENT_CLIPBOARD_UPDATE, dt::clipboard);

  // A registered user event (>= SDL_EVENT_USER) is `user`; header-only events
  // (quit and the application events) fall back to `common`.
  SDL_Event raw{};
  raw.type = SDL_EVENT_USER + 7;
  CHECK(sdl_event{raw}.data_type() == dt::user);
  check_data_type(SDL_EVENT_QUIT, dt::common);
  check_data_type(SDL_EVENT_TERMINATING, dt::common);
  check_data_type(SDL_EVENT_KEYMAP_CHANGED, dt::common);
}

TEST_CASE("sdl_event registered enums round-trip", "[sdl][event][enums]") {
  CHECK(enum_as_string(sdl_event_data_type::display) == "display");
  CHECK(enum_as_string(sdl_event_data_type::clipboard) == "clipboard");
  // No named display IDs: it stringifies as its number.
  CHECK(enum_as_string(sdl_display_id_type{5}) == "5");
}

TEST_CASE("sdl_event exposes the shared header", "[sdl][event]") {
  SDL_Event raw{};
  raw.type = SDL_EVENT_KEY_DOWN;
  raw.common.timestamp = 123456;
  const sdl_event ev{raw};
  CHECK(ev.type() == sdl_event_type::key_down);
  CHECK(ev.data_type() == sdl_event_data_type::key);
  CHECK(ev.timestamp() == 123456);
  CHECK(ev.raw().type == SDL_EVENT_KEY_DOWN); // escape hatch sees the union
}

TEST_CASE("sdl_event get_display copies the cleaned payload", "[sdl][event]") {
  SDL_Event raw{};
  raw.type = SDL_EVENT_DISPLAY_MOVED;
  raw.display.displayID = 5;
  raw.display.data1 = 100;
  raw.display.data2 = 200;
  const sdl_event ev{raw};
  REQUIRE(ev.data_type() == sdl_event_data_type::display);
  const auto d = ev.get_display();
  CHECK(d.display_id == sdl_display_id_type{5});
  CHECK(d.data1 == 100);
  CHECK(d.data2 == 200);
}

TEST_CASE("sdl_event::poll drains the queue through the wrapper",
    "[sdl][event]") {
  // SDL_INIT_EVENTS is headless (no display), so this runs without a window.
  REQUIRE(SDL_Init(SDL_INIT_EVENTS));

  // Nothing pushed yet: the queue is empty.
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
  CHECK_FALSE(sdl_event::poll());

  SDL_Event quit{};
  quit.type = SDL_EVENT_QUIT;
  REQUIRE(SDL_PushEvent(&quit));

  const auto ev = sdl_event::poll();
  REQUIRE(ev);
  CHECK(ev.type() == sdl_event_type::quit);
  CHECK_FALSE(sdl_event::poll()); // queue drained again

  SDL_Quit();
}
