// Unit test for corvid::sdl::pump_events (corvid/sdl/frame_loop.h): events
// pushed onto a headless queue are drained through the pump, which maps the
// window-level ones to a frame_action and hands the rest to the handler. The
// events subsystem needs no display, so this runs without a window.
//
// SDL polls in batches behind a sentinel, so an event pushed after a pump that
// returned early (quit, menu) surfaces only on the following poll. Each
// section therefore pushes everything it needs before pumping.

#include <vector>

#include "corvid/sdl/frame_loop.h"
#include "corvid/sdl/sdl_subsystem.h"
#include "catch2_main.h"

using namespace corvid;
using corvid::sdl::frame_action;
using corvid::sdl::pump_events;
using corvid::sdl::sdl_event;
using corvid::sdl::sdl_event_type;
using corvid::sdl::sdl_init_flags;
using corvid::sdl::sdl_keycode;
using corvid::sdl::sdl_subsystem;

namespace {

// Push one event of `type` onto the queue.
void push(SDL_EventType type) {
  SDL_Event raw{};
  raw.type = type;
  REQUIRE(SDL_PushEvent(&raw));
}

// Push a key-down for `key`.
void push_key(sdl_keycode key) {
  SDL_Event raw{};
  raw.type = SDL_EVENT_KEY_DOWN;
  raw.key.key = *key;
  raw.key.down = true;
  REQUIRE(SDL_PushEvent(&raw));
}

// A handler that consumes nothing.
bool ignore(const sdl_event&) { return false; }

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region pump_events

TEST_CASE("pump_events maps window-level events to actions", "[sdl]") {
  const sdl_subsystem sdl{sdl_init_flags::events};
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

  SECTION("an empty queue proceeds") {
    CHECK(pump_events(ignore) == frame_action::proceed);
  }

  SECTION("quit quits") {
    push(SDL_EVENT_QUIT);
    CHECK(pump_events(ignore) == frame_action::quit);
  }

  SECTION("a close request quits") {
    push(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
    CHECK(pump_events(ignore) == frame_action::quit);
  }

  SECTION("a resize storm coalesces to one resize and drains the queue") {
    push(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    push(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    push(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    CHECK(pump_events(ignore) == frame_action::resize);
    CHECK(pump_events(ignore) == frame_action::proceed);
  }

  SECTION("other keys do not open the menu") {
    push_key(sdl_keycode::w);
    CHECK(pump_events(ignore) == frame_action::proceed);
  }

  SECTION("Escape opens the menu") {
    push_key(sdl_keycode::escape);
    CHECK(pump_events(ignore) == frame_action::menu);
  }

  SECTION("quit wins over a pending resize") {
    push(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    push(SDL_EVENT_QUIT);
    CHECK(pump_events(ignore) == frame_action::quit);
  }
}

TEST_CASE("pump_events lets the handler consume events first", "[sdl]") {
  const sdl_subsystem sdl{sdl_init_flags::events};
  SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

  std::vector<sdl_event_type> seen;
  const auto record = [&](const sdl_event& ev) {
    seen.push_back(ev.type());
    return false;
  };
  const auto swallow_escape = [&](const sdl_event& ev) {
    seen.push_back(ev.type());
    return ev.type() == sdl_event_type::key_down &&
           ev.key().key == sdl_keycode::escape;
  };

  SECTION("every event reaches the handler before the pump") {
    push_key(sdl_keycode::w);
    push(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    push(SDL_EVENT_QUIT);
    CHECK(pump_events(record) == frame_action::quit);
    REQUIRE(seen.size() == 3);
    CHECK(seen[0] == sdl_event_type::key_down);
    CHECK(seen[1] == sdl_event_type::window_pixel_size_changed);
    CHECK(seen[2] == sdl_event_type::quit);
  }

  SECTION("a consumed Escape never opens the menu") {
    push_key(sdl_keycode::escape);
    CHECK(pump_events(swallow_escape) == frame_action::proceed);
    CHECK(seen.size() == 1);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
