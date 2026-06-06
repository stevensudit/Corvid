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
#include "../strings/strings_shared.h"
#include "../strings/trimming.h"
#include "../strings/splitting.h"
#include "../strings/conversion.h"
#include "enum_registry.h"

namespace corvid::strings { inline namespace conversion {
inline namespace cvt_enum {

#pragma region From enum

// Append enum to `target`. Returns `target`.
constexpr auto& append_enum(AppendTarget auto& target, ScopedEnum auto e) {
  return registry::enum_spec_v<decltype(e)>.append(target, e);
}

// Return enum as string.
constexpr std::string enum_as_string(ScopedEnum auto t) {
  std::string target;
  return append_enum(target, t);
}

#pragma endregion
#pragma region To enum

namespace details {
// Extract any enum from `sv`. Scoped enums (sequential and bitmask) dispatch
// to the registered spec's `lookup`, which the bitmask spec extends to parse
// "a + b + c" combinations. Plain (unscoped) enums accept numeric input only.
constexpr bool help_extract_enum(StdEnum auto& e, std::string_view sv) {
  using E = std::remove_cvref_t<decltype(e)>;
  e = {};
  if (sv.empty()) return false;
  if constexpr (ScopedEnum<E>)
    return registry::enum_spec_v<E>.lookup(e, sv);
  else
    return registry::details::lookup_helper_wrapper(e, sv);
}

} // namespace details

// Convert enum from a `std::string_view`.
//
// Works for unscoped and scoped enums, including bitmask and sequential.
// Correctly round-trips with `enum_as_string`.
//
// In general, expects a single value, which may be numeric or named. This must
// not be padded with spaces and/or terminated with a comma, period, or
// semicolon. For BitMaskEnum, instead of a single value, it expects one or
// more values separated by plus signs.
//
// When clipping is enabled, if numerical value is out of range, then it fails.
//
// On success, sets output value and returns true.
// On failure, clears output value and returns false.
constexpr bool convert_enum(StdEnum auto& e, std::string_view sv) {
  return details::help_extract_enum(e, sv);
}

// Convert enum from a `std::string_view` with text, not digits.
constexpr bool convert_text_enum(StdEnum auto& e, std::string_view sv) {
  e = {};
  if (sv.empty() || (sv[0] >= '0' && sv[0] <= '9')) return false;
  return details::help_extract_enum(e, sv);
}

// Extract enum out of a `std::string_view`, setting output parameter.
//
// Works for unscoped and scoped enums, including bitmask and sequential.
// Correctly round-trips with `enum_as_string`.
//
// In general, expects a single value, which may be numeric or named. This may
// be padded with spaces and/or terminated with a comma, period, or semicolon.
// For BitMaskEnum, instead of a single value, it expects one or more values
// separated by plus signs.
//
// When clipping is enabled, if numerical value is out of range, then it fails.
//
// On success, sets output value, removes parsed characters from the string
// view, and returns true.
//
// On failure, clears output value, leaves parsed characters in the string
// view, and returns false.
constexpr bool extract_enum(StdEnum auto& e, std::string_view& sv) {
  using E = std::remove_cvref_t<decltype(e)>;
  e = E{};
  auto new_sv = sv;
  auto whole = trim(extract_piece(new_sv, ",.;"));
  if (whole.empty()) return false;
  if (!details::help_extract_enum(e, whole)) return false;
  sv = new_sv;
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

#pragma endregion
}}} // namespace corvid::strings::conversion::cvt_enum

#pragma region operator<<

// Append scoped enum to `os`.
//
// No need to define this for old-style enums because they're treated as
// aliases for their underlying type, which is already supported.
auto& operator<<(std::ostream& os, corvid::ScopedEnum auto t) {
  return corvid::strings::append_enum(os, t);
}

#pragma endregion
