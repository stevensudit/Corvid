// Unit test for corvid::sdl::sdl_event / sdl_event::poll (corvid/sdl/
// sdl_event.h): exercises the union-aware wrapper. The classify switch is
// covered with at least one event type per data shape (plus first/last of the
// big display and window blocks, where a mis-grouped case label would slip
// through); the registered enums round-trip; the header accessors and the
// display payload accessor copy through; and a headless push/poll case drives
// `poll`.

#include <cstdint>
#include <string_view>

#include "corvid/enums/enum_conversion.h"
#include "corvid/sdl/sdl_event.h"
#include "catch2_main.h"

using namespace corvid;
using corvid::sdl::sdl_display_id_type;
using corvid::sdl::sdl_event;
using corvid::sdl::sdl_event_data_type;
using corvid::sdl::sdl_event_type;
using corvid::sdl::sdl_keycode;
using corvid::sdl::sdl_window_id_type;

namespace {

// Wrap an SDL_Event of the given type and check its classified data shape.
void check_data_type(SDL_EventType type, sdl_event_data_type expected) {
  CAPTURE(type);
  SDL_Event raw{};
  raw.type = type;
  CHECK(sdl_event{raw}.data_type() == expected);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

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

TEST_CASE("sdl_keycode string round-trip", "[sdl][event][enums]") {
  struct keycode_case {
    sdl_keycode value;
    std::uint32_t code;
    std::string_view name;
  };
  // Exhaustive: every registered keycode round-trips value, code, and name.
  constexpr keycode_case cases[] = {
      {sdl_keycode::unknown, 0U, "unknown"},
      {sdl_keycode::backspace, 8U, "backspace"},
      {sdl_keycode::tab, 9U, "tab"},
      {sdl_keycode::return_key, 13U, "return_key"},
      {sdl_keycode::escape, 27U, "escape"},
      {sdl_keycode::space, 32U, "space"},
      {sdl_keycode::exclaim, 33U, "exclaim"},
      {sdl_keycode::dblapostrophe, 34U, "dblapostrophe"},
      {sdl_keycode::hash, 35U, "hash"},
      {sdl_keycode::dollar, 36U, "dollar"},
      {sdl_keycode::percent, 37U, "percent"},
      {sdl_keycode::ampersand, 38U, "ampersand"},
      {sdl_keycode::apostrophe, 39U, "apostrophe"},
      {sdl_keycode::leftparen, 40U, "leftparen"},
      {sdl_keycode::rightparen, 41U, "rightparen"},
      {sdl_keycode::asterisk, 42U, "asterisk"},
      {sdl_keycode::plus, 43U, "plus"},
      {sdl_keycode::comma, 44U, "comma"},
      {sdl_keycode::minus, 45U, "minus"},
      {sdl_keycode::period, 46U, "period"},
      {sdl_keycode::slash, 47U, "slash"},
      {sdl_keycode::num0, 48U, "num0"},
      {sdl_keycode::num1, 49U, "num1"},
      {sdl_keycode::num2, 50U, "num2"},
      {sdl_keycode::num3, 51U, "num3"},
      {sdl_keycode::num4, 52U, "num4"},
      {sdl_keycode::num5, 53U, "num5"},
      {sdl_keycode::num6, 54U, "num6"},
      {sdl_keycode::num7, 55U, "num7"},
      {sdl_keycode::num8, 56U, "num8"},
      {sdl_keycode::num9, 57U, "num9"},
      {sdl_keycode::colon, 58U, "colon"},
      {sdl_keycode::semicolon, 59U, "semicolon"},
      {sdl_keycode::less, 60U, "less"},
      {sdl_keycode::equals, 61U, "equals"},
      {sdl_keycode::greater, 62U, "greater"},
      {sdl_keycode::question, 63U, "question"},
      {sdl_keycode::at, 64U, "at"},
      {sdl_keycode::leftbracket, 91U, "leftbracket"},
      {sdl_keycode::backslash, 92U, "backslash"},
      {sdl_keycode::rightbracket, 93U, "rightbracket"},
      {sdl_keycode::caret, 94U, "caret"},
      {sdl_keycode::underscore, 95U, "underscore"},
      {sdl_keycode::grave, 96U, "grave"},
      {sdl_keycode::a, 97U, "a"},
      {sdl_keycode::b, 98U, "b"},
      {sdl_keycode::c, 99U, "c"},
      {sdl_keycode::d, 100U, "d"},
      {sdl_keycode::e, 101U, "e"},
      {sdl_keycode::f, 102U, "f"},
      {sdl_keycode::g, 103U, "g"},
      {sdl_keycode::h, 104U, "h"},
      {sdl_keycode::i, 105U, "i"},
      {sdl_keycode::j, 106U, "j"},
      {sdl_keycode::k, 107U, "k"},
      {sdl_keycode::l, 108U, "l"},
      {sdl_keycode::m, 109U, "m"},
      {sdl_keycode::n, 110U, "n"},
      {sdl_keycode::o, 111U, "o"},
      {sdl_keycode::p, 112U, "p"},
      {sdl_keycode::q, 113U, "q"},
      {sdl_keycode::r, 114U, "r"},
      {sdl_keycode::s, 115U, "s"},
      {sdl_keycode::t, 116U, "t"},
      {sdl_keycode::u, 117U, "u"},
      {sdl_keycode::v, 118U, "v"},
      {sdl_keycode::w, 119U, "w"},
      {sdl_keycode::x, 120U, "x"},
      {sdl_keycode::y, 121U, "y"},
      {sdl_keycode::z, 122U, "z"},
      {sdl_keycode::leftbrace, 123U, "leftbrace"},
      {sdl_keycode::pipe, 124U, "pipe"},
      {sdl_keycode::rightbrace, 125U, "rightbrace"},
      {sdl_keycode::tilde, 126U, "tilde"},
      {sdl_keycode::delete_key, 127U, "delete_key"},
      {sdl_keycode::plusminus, 177U, "plusminus"},
      {sdl_keycode::left_tab, 536870913U, "left_tab"},
      {sdl_keycode::level5_shift, 536870914U, "level5_shift"},
      {sdl_keycode::multi_key_compose, 536870915U, "multi_key_compose"},
      {sdl_keycode::lmeta, 536870916U, "lmeta"},
      {sdl_keycode::rmeta, 536870917U, "rmeta"},
      {sdl_keycode::lhyper, 536870918U, "lhyper"},
      {sdl_keycode::rhyper, 536870919U, "rhyper"},
      {sdl_keycode::capslock, 1073741881U, "capslock"},
      {sdl_keycode::f1, 1073741882U, "f1"},
      {sdl_keycode::f2, 1073741883U, "f2"},
      {sdl_keycode::f3, 1073741884U, "f3"},
      {sdl_keycode::f4, 1073741885U, "f4"},
      {sdl_keycode::f5, 1073741886U, "f5"},
      {sdl_keycode::f6, 1073741887U, "f6"},
      {sdl_keycode::f7, 1073741888U, "f7"},
      {sdl_keycode::f8, 1073741889U, "f8"},
      {sdl_keycode::f9, 1073741890U, "f9"},
      {sdl_keycode::f10, 1073741891U, "f10"},
      {sdl_keycode::f11, 1073741892U, "f11"},
      {sdl_keycode::f12, 1073741893U, "f12"},
      {sdl_keycode::printscreen, 1073741894U, "printscreen"},
      {sdl_keycode::scrolllock, 1073741895U, "scrolllock"},
      {sdl_keycode::pause, 1073741896U, "pause"},
      {sdl_keycode::insert, 1073741897U, "insert"},
      {sdl_keycode::home, 1073741898U, "home"},
      {sdl_keycode::pageup, 1073741899U, "pageup"},
      {sdl_keycode::end, 1073741901U, "end"},
      {sdl_keycode::pagedown, 1073741902U, "pagedown"},
      {sdl_keycode::right, 1073741903U, "right"},
      {sdl_keycode::left, 1073741904U, "left"},
      {sdl_keycode::down, 1073741905U, "down"},
      {sdl_keycode::up, 1073741906U, "up"},
      {sdl_keycode::numlockclear, 1073741907U, "numlockclear"},
      {sdl_keycode::kp_divide, 1073741908U, "kp_divide"},
      {sdl_keycode::kp_multiply, 1073741909U, "kp_multiply"},
      {sdl_keycode::kp_minus, 1073741910U, "kp_minus"},
      {sdl_keycode::kp_plus, 1073741911U, "kp_plus"},
      {sdl_keycode::kp_enter, 1073741912U, "kp_enter"},
      {sdl_keycode::kp_1, 1073741913U, "kp_1"},
      {sdl_keycode::kp_2, 1073741914U, "kp_2"},
      {sdl_keycode::kp_3, 1073741915U, "kp_3"},
      {sdl_keycode::kp_4, 1073741916U, "kp_4"},
      {sdl_keycode::kp_5, 1073741917U, "kp_5"},
      {sdl_keycode::kp_6, 1073741918U, "kp_6"},
      {sdl_keycode::kp_7, 1073741919U, "kp_7"},
      {sdl_keycode::kp_8, 1073741920U, "kp_8"},
      {sdl_keycode::kp_9, 1073741921U, "kp_9"},
      {sdl_keycode::kp_0, 1073741922U, "kp_0"},
      {sdl_keycode::kp_period, 1073741923U, "kp_period"},
      {sdl_keycode::application, 1073741925U, "application"},
      {sdl_keycode::power, 1073741926U, "power"},
      {sdl_keycode::kp_equals, 1073741927U, "kp_equals"},
      {sdl_keycode::f13, 1073741928U, "f13"},
      {sdl_keycode::f14, 1073741929U, "f14"},
      {sdl_keycode::f15, 1073741930U, "f15"},
      {sdl_keycode::f16, 1073741931U, "f16"},
      {sdl_keycode::f17, 1073741932U, "f17"},
      {sdl_keycode::f18, 1073741933U, "f18"},
      {sdl_keycode::f19, 1073741934U, "f19"},
      {sdl_keycode::f20, 1073741935U, "f20"},
      {sdl_keycode::f21, 1073741936U, "f21"},
      {sdl_keycode::f22, 1073741937U, "f22"},
      {sdl_keycode::f23, 1073741938U, "f23"},
      {sdl_keycode::f24, 1073741939U, "f24"},
      {sdl_keycode::execute, 1073741940U, "execute"},
      {sdl_keycode::help, 1073741941U, "help"},
      {sdl_keycode::menu, 1073741942U, "menu"},
      {sdl_keycode::select, 1073741943U, "select"},
      {sdl_keycode::stop, 1073741944U, "stop"},
      {sdl_keycode::again, 1073741945U, "again"},
      {sdl_keycode::undo, 1073741946U, "undo"},
      {sdl_keycode::cut, 1073741947U, "cut"},
      {sdl_keycode::copy, 1073741948U, "copy"},
      {sdl_keycode::paste, 1073741949U, "paste"},
      {sdl_keycode::find, 1073741950U, "find"},
      {sdl_keycode::mute, 1073741951U, "mute"},
      {sdl_keycode::volumeup, 1073741952U, "volumeup"},
      {sdl_keycode::volumedown, 1073741953U, "volumedown"},
      {sdl_keycode::kp_comma, 1073741957U, "kp_comma"},
      {sdl_keycode::kp_equalsas400, 1073741958U, "kp_equalsas400"},
      {sdl_keycode::alterase, 1073741977U, "alterase"},
      {sdl_keycode::sysreq, 1073741978U, "sysreq"},
      {sdl_keycode::cancel, 1073741979U, "cancel"},
      {sdl_keycode::clear, 1073741980U, "clear"},
      {sdl_keycode::prior, 1073741981U, "prior"},
      {sdl_keycode::return2, 1073741982U, "return2"},
      {sdl_keycode::separator, 1073741983U, "separator"},
      {sdl_keycode::out, 1073741984U, "out"},
      {sdl_keycode::oper, 1073741985U, "oper"},
      {sdl_keycode::clearagain, 1073741986U, "clearagain"},
      {sdl_keycode::crsel, 1073741987U, "crsel"},
      {sdl_keycode::exsel, 1073741988U, "exsel"},
      {sdl_keycode::kp_00, 1073742000U, "kp_00"},
      {sdl_keycode::kp_000, 1073742001U, "kp_000"},
      {sdl_keycode::thousandsseparator, 1073742002U, "thousandsseparator"},
      {sdl_keycode::decimalseparator, 1073742003U, "decimalseparator"},
      {sdl_keycode::currencyunit, 1073742004U, "currencyunit"},
      {sdl_keycode::currencysubunit, 1073742005U, "currencysubunit"},
      {sdl_keycode::kp_leftparen, 1073742006U, "kp_leftparen"},
      {sdl_keycode::kp_rightparen, 1073742007U, "kp_rightparen"},
      {sdl_keycode::kp_leftbrace, 1073742008U, "kp_leftbrace"},
      {sdl_keycode::kp_rightbrace, 1073742009U, "kp_rightbrace"},
      {sdl_keycode::kp_tab, 1073742010U, "kp_tab"},
      {sdl_keycode::kp_backspace, 1073742011U, "kp_backspace"},
      {sdl_keycode::kp_a, 1073742012U, "kp_a"},
      {sdl_keycode::kp_b, 1073742013U, "kp_b"},
      {sdl_keycode::kp_c, 1073742014U, "kp_c"},
      {sdl_keycode::kp_d, 1073742015U, "kp_d"},
      {sdl_keycode::kp_e, 1073742016U, "kp_e"},
      {sdl_keycode::kp_f, 1073742017U, "kp_f"},
      {sdl_keycode::kp_xor, 1073742018U, "kp_xor"},
      {sdl_keycode::kp_power, 1073742019U, "kp_power"},
      {sdl_keycode::kp_percent, 1073742020U, "kp_percent"},
      {sdl_keycode::kp_less, 1073742021U, "kp_less"},
      {sdl_keycode::kp_greater, 1073742022U, "kp_greater"},
      {sdl_keycode::kp_ampersand, 1073742023U, "kp_ampersand"},
      {sdl_keycode::kp_dblampersand, 1073742024U, "kp_dblampersand"},
      {sdl_keycode::kp_verticalbar, 1073742025U, "kp_verticalbar"},
      {sdl_keycode::kp_dblverticalbar, 1073742026U, "kp_dblverticalbar"},
      {sdl_keycode::kp_colon, 1073742027U, "kp_colon"},
      {sdl_keycode::kp_hash, 1073742028U, "kp_hash"},
      {sdl_keycode::kp_space, 1073742029U, "kp_space"},
      {sdl_keycode::kp_at, 1073742030U, "kp_at"},
      {sdl_keycode::kp_exclam, 1073742031U, "kp_exclam"},
      {sdl_keycode::kp_memstore, 1073742032U, "kp_memstore"},
      {sdl_keycode::kp_memrecall, 1073742033U, "kp_memrecall"},
      {sdl_keycode::kp_memclear, 1073742034U, "kp_memclear"},
      {sdl_keycode::kp_memadd, 1073742035U, "kp_memadd"},
      {sdl_keycode::kp_memsubtract, 1073742036U, "kp_memsubtract"},
      {sdl_keycode::kp_memmultiply, 1073742037U, "kp_memmultiply"},
      {sdl_keycode::kp_memdivide, 1073742038U, "kp_memdivide"},
      {sdl_keycode::kp_plusminus, 1073742039U, "kp_plusminus"},
      {sdl_keycode::kp_clear, 1073742040U, "kp_clear"},
      {sdl_keycode::kp_clearentry, 1073742041U, "kp_clearentry"},
      {sdl_keycode::kp_binary, 1073742042U, "kp_binary"},
      {sdl_keycode::kp_octal, 1073742043U, "kp_octal"},
      {sdl_keycode::kp_decimal, 1073742044U, "kp_decimal"},
      {sdl_keycode::kp_hexadecimal, 1073742045U, "kp_hexadecimal"},
      {sdl_keycode::lctrl, 1073742048U, "lctrl"},
      {sdl_keycode::lshift, 1073742049U, "lshift"},
      {sdl_keycode::lalt, 1073742050U, "lalt"},
      {sdl_keycode::lgui, 1073742051U, "lgui"},
      {sdl_keycode::rctrl, 1073742052U, "rctrl"},
      {sdl_keycode::rshift, 1073742053U, "rshift"},
      {sdl_keycode::ralt, 1073742054U, "ralt"},
      {sdl_keycode::rgui, 1073742055U, "rgui"},
      {sdl_keycode::mode, 1073742081U, "mode"},
      {sdl_keycode::sleep, 1073742082U, "sleep"},
      {sdl_keycode::wake, 1073742083U, "wake"},
      {sdl_keycode::channel_increment, 1073742084U, "channel_increment"},
      {sdl_keycode::channel_decrement, 1073742085U, "channel_decrement"},
      {sdl_keycode::media_play, 1073742086U, "media_play"},
      {sdl_keycode::media_pause, 1073742087U, "media_pause"},
      {sdl_keycode::media_record, 1073742088U, "media_record"},
      {sdl_keycode::media_fast_forward, 1073742089U, "media_fast_forward"},
      {sdl_keycode::media_rewind, 1073742090U, "media_rewind"},
      {sdl_keycode::media_next_track, 1073742091U, "media_next_track"},
      {sdl_keycode::media_previous_track, 1073742092U, "media_previous_track"},
      {sdl_keycode::media_stop, 1073742093U, "media_stop"},
      {sdl_keycode::media_eject, 1073742094U, "media_eject"},
      {sdl_keycode::media_play_pause, 1073742095U, "media_play_pause"},
      {sdl_keycode::media_select, 1073742096U, "media_select"},
      {sdl_keycode::ac_new, 1073742097U, "ac_new"},
      {sdl_keycode::ac_open, 1073742098U, "ac_open"},
      {sdl_keycode::ac_close, 1073742099U, "ac_close"},
      {sdl_keycode::ac_exit, 1073742100U, "ac_exit"},
      {sdl_keycode::ac_save, 1073742101U, "ac_save"},
      {sdl_keycode::ac_print, 1073742102U, "ac_print"},
      {sdl_keycode::ac_properties, 1073742103U, "ac_properties"},
      {sdl_keycode::ac_search, 1073742104U, "ac_search"},
      {sdl_keycode::ac_home, 1073742105U, "ac_home"},
      {sdl_keycode::ac_back, 1073742106U, "ac_back"},
      {sdl_keycode::ac_forward, 1073742107U, "ac_forward"},
      {sdl_keycode::ac_stop, 1073742108U, "ac_stop"},
      {sdl_keycode::ac_refresh, 1073742109U, "ac_refresh"},
      {sdl_keycode::ac_bookmarks, 1073742110U, "ac_bookmarks"},
      {sdl_keycode::softleft, 1073742111U, "softleft"},
      {sdl_keycode::softright, 1073742112U, "softright"},
      {sdl_keycode::call, 1073742113U, "call"},
      {sdl_keycode::endcall, 1073742114U, "endcall"},
  };
  for (const auto& c : cases) {
    CAPTURE(c.name);
    CHECK(*c.value == c.code);
    CHECK(enum_as_string(c.value) == c.name);
    sdl_keycode parsed{};
    CHECK(convert_enum(parsed, c.name));
    CHECK(parsed == c.value);
  }
}

TEST_CASE("sdl_event get_wheel copies the cleaned payload", "[sdl][event]") {
  SDL_Event raw{};
  raw.type = SDL_EVENT_MOUSE_WHEEL;
  raw.wheel.x = 1.5F;
  raw.wheel.y = -2.0F;
  raw.wheel.mouse_x = 100.0F;
  raw.wheel.mouse_y = 200.0F;
  const sdl_event ev{raw};
  REQUIRE(ev.data_type() == sdl_event_data_type::wheel);
  const auto wheel = ev.get_wheel();
  CHECK(wheel.x == 1.5F);
  CHECK(wheel.y == -2.0F);
  CHECK(wheel.mouse_x == 100.0F);
  CHECK(wheel.mouse_y == 200.0F);
}

TEST_CASE("sdl_event get_motion copies the cleaned payload", "[sdl][event]") {
  SDL_Event raw{};
  raw.type = SDL_EVENT_MOUSE_MOTION;
  raw.motion.x = 10.0F;
  raw.motion.y = 20.0F;
  raw.motion.xrel = -3.0F;
  raw.motion.yrel = 4.0F;
  raw.motion.state = SDL_BUTTON_LMASK;
  REQUIRE(sdl_event{raw}.data_type() == sdl_event_data_type::motion);
  const auto motion = sdl_event{raw}.get_motion();
  CHECK(motion.x == 10.0F);
  CHECK(motion.y == 20.0F);
  CHECK(motion.xrel == -3.0F);
  CHECK(motion.yrel == 4.0F);
  CHECK(motion.left_held);

  // Without the left button down, left_held is false.
  raw.motion.state = SDL_BUTTON_RMASK;
  CHECK_FALSE(sdl_event{raw}.get_motion().left_held);
}

TEST_CASE("sdl_event get_key copies the cleaned payload", "[sdl][event]") {
  SDL_Event raw{};
  raw.type = SDL_EVENT_KEY_DOWN;
  raw.key.key = SDLK_LEFT;
  raw.key.down = true;
  raw.key.repeat = true;
  const sdl_event ev{raw};
  REQUIRE(ev.data_type() == sdl_event_data_type::key);
  const auto key = ev.get_key();
  CHECK(key.key == sdl_keycode::left);
  CHECK(key.down);
  CHECK(key.repeat);
}

TEST_CASE("sdl_event get_window copies the cleaned payload", "[sdl][event]") {
  SDL_Event raw{};
  raw.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
  raw.window.windowID = 9;
  raw.window.data1 = 1280;
  raw.window.data2 = 720;
  const sdl_event ev{raw};
  REQUIRE(ev.data_type() == sdl_event_data_type::window);
  const auto window = ev.get_window();
  CHECK(window.window_id == sdl_window_id_type{9});
  CHECK(window.data1 == 1280);
  CHECK(window.data2 == 720);
}

// NOLINTEND(readability-function-cognitive-complexity)
