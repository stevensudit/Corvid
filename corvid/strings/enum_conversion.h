// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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
#include "strings_shared.h"
#include "trimming.h"
#include "conversion.h"
#include "../enums/sequence_enum.h"
#include "../enums/bitmask_enum.h"

namespace corvid::strings { inline namespace conversion {
inline namespace cvt_enum {

namespace details {
constexpr bool
help_extract_enum(bitmask::BitmaskEnum auto& e, std::string_view& sv) {
  using E = std::remove_cvref_t<decltype(e)>;
  E ev{};
  for (auto piece = trim(extract_piece(sv, "+")); !piece.empty();
       piece = trim(extract_piece(sv, "+")))
  {
    if (!registry::enum_spec_v<E>.lookup(ev, piece)) return false;
    // TODO: Why does `e |= ev` not work except when it does?
    e = static_cast<E>(meta::as_underlying(e) | meta::as_underlying(ev));
  }
  return true;
}
} // namespace details

// Extract enum out of a `std::string_view`, setting output parameter.
//
// When clipping is enabled, if the value is out of range, then it fails.
//
// On success, sets output value, removes parsed characters from the string
// view, and returns true.
//
// On failure, leaves the parameters unchanged and returns false.
constexpr bool extract_enum(StdEnum auto& e, std::string_view& sv) {
  using E = std::remove_cvref_t<decltype(e)>;
  e = E{};
  auto save_sv = sv;
  auto whole = trim(extract_piece(sv, ",;"));
  bool succeeded;
  if constexpr (bitmask::BitmaskEnum<E>)
    succeeded = details::help_extract_enum(e, whole);
  else
    succeeded = registry::enum_spec_v<E>.lookup(e, whole);

  if (!succeeded) {
    sv = save_sv;
    return false;
  }

  return true;
}

// TODO: Flesh this out with all the other variants, including parse.

}}} // namespace corvid::strings::conversion::cvt_enum
