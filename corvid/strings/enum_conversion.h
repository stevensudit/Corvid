// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
  bool succeeded{};
  for (auto piece = trim(extract_piece(sv, "+")); !piece.empty();
      piece = trim(extract_piece(sv, "+")))
  {
    if (!registry::enum_spec_v<E>.lookup(ev, piece)) return false;
    // Use operator syntax to avoid ADL issues.
    corvid::enums::bitmask::operator|=(e, ev);
    succeeded = true;
  }
  return succeeded;
}
} // namespace details

// Extract enum out of a `std::string_view`, setting output parameter.
//
// Works for unscoped and scoped enums, including bitmask and sequential.
// Correctly round-trips with `enum_as_string`.
//
// In general, expects a single value, which may be numeric or named. This may
// be padded with spaces and/or terminated with  a comma, period, or semicolon.
// For BitMaskEnum, instead of a single value, it expects one or more values
// separated by plus signs.
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
  auto whole = trim(extract_piece(sv, ",.;"));
  if (whole.empty()) {
    sv = save_sv;
    return false;
  }
  bool succeeded;
  if constexpr (bitmask::BitmaskEnum<E>)
    succeeded = details::help_extract_enum(e, whole);
  else if constexpr (ScopedEnum<E>)
    succeeded = registry::enum_spec_v<E>.lookup(e, whole);
  else
    succeeded = registry::details::lookup_helper_wrapper(e, whole);

  if (!succeeded) {
    sv = save_sv;
    return false;
  }

  return true;
}

// Extract enum from a `std::string_view`, returning it as `std::optional`.
//
// On success, returns optional with value, and removes parsed characters
// from the string view.
//
// On failure, returns optional without value and leaves string view unchanged.
template<StdEnum E>
constexpr std::optional<E> extract_enum(std::string_view& sv) {
  E e;
  return extract_enum(e, sv) ? std::make_optional(e) : std::nullopt;
}

// Parse enum from copy of a `std::string_view`, returning it as
// `std::optional`. Fails if there are any unparsed characters.
//
// On success, returns optional with value.
//
// On failure, returns optional without value.
template<StdEnum E>
constexpr std::optional<E> parse_enum(std::string_view sv) {
  E e;
  return extract_enum(e, sv) && sv.empty()
             ? std::make_optional(e)
             : std::nullopt;
}

// Parse enum from copy of a `std::string_view` with a `default_value`.
// Fails if there are any unparsed characters.
//
// On success, returns parsed value.
//
// On failure, returns `default_value`.
template<StdEnum E>
constexpr E parse_enum(std::string_view sv, E default_value) {
  E e;
  return (extract_enum(e, sv) && sv.empty()) ? e : default_value;
}

}}} // namespace corvid::strings::conversion::cvt_enum
