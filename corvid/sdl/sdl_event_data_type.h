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
#include <cstdint>

#include "../enums/enum_conversion.h"
#include "../enums/sequence_enum.h"

namespace corvid::sdl {

#pragma region sdl_event_data_type

// Which member of the `SDL_Event` union an event populates, and so which typed
// accessor on `sdl_event` is valid. Distinct from `sdl_event_type`: many event
// types share one payload shape (key_down and key_up are both `key`), so this
// is the coarser, by-shape discriminant. The names track the union members of
// `SDL_Event` one-for-one.
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
  quit,
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

// Register as a dense sequence enum starting at 0; the names track the
// enumerators above in order.
consteval auto corvid_enum_spec(sdl_event_data_type*) {
  return corvid::enums::sequence::make_sequence_enum_spec<sdl_event_data_type,
      "common,display,window,kdevice,key,edit,edit_candidates,text,mdevice,"
      "motion,button,wheel,jdevice,jaxis,jball,jhat,jbutton,jbattery,gdevice,"
      "gaxis,gbutton,gtouchpad,gsensor,adevice,cdevice,sensor,quit,user,"
      "tfinger,pinch,pproximity,ptouch,pmotion,pbutton,paxis,render,drop,"
      "clipboard">();
}

#pragma endregion

} // namespace corvid::sdl
