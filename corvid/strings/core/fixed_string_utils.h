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
#include "strings_shared.h"
#include "trimming.h"
#include "fixed_string.h"

namespace corvid::strings { inline namespace fixed {

#pragma region fixed_split

// Split fixed string by delimiter, returning array of string views, optionally
// trimming by whitespace.
//
// Splits `W` into views using delimiter characters `D`. `WS` specifies
// characters to trim from each piece; pass "" to disable trimming. `D` is a
// set of delimiter characters, not a multi-character token. The delimiter must
// be non-empty, as enforced below.
template<strings::fixed_string W, strings::fixed_string D = ",",
    strings::fixed_string WS = "">
consteval auto fixed_split() {
  constexpr auto whole = W.view();
  constexpr auto delim = D.view();
  static_assert(delim.size(), "Delimiter cannot be empty");
  constexpr auto n = std::count_if(whole.begin(), whole.end(), [&](char c) {
    return delim.find(c) != delim.npos;
  });
  std::array<std::string_view, n + 1> result;

  auto w = whole;
  constexpr auto ws = WS.view();
  for (size_t i = 0; i != result.size(); ++i) {
    auto pos = w.find_first_of(delim);
    result[i] = w.substr(0, pos);
    if (ws.size()) result[i] = strings::trim(result[i], ws);
    // When npos, substr goes to end, so this works fine.
    w = w.substr(pos + 1);
  }

  return result;
}

#pragma endregion
#pragma region fixed_split_trim

// Split fixed string by delimiter, returning array of string views, trimming
// by specified whitespace.
//
// Splits `W` using delimiter characters, trimming pieces with the `WS` set.
template<strings::fixed_string W, strings::fixed_string WS = " ",
    strings::fixed_string D = ",">
consteval auto fixed_split_trim() {
  return fixed_split<W, D, WS>();
}

#pragma endregion
#pragma region fixed_replaced

// Return a copy of `W` with every occurrence of `F` replaced by `T`. The
// length is unchanged, so this is handy for swapping a delimiter, such as
// turning a comma-delimited list into a null-delimited one.
template<strings::fixed_string W, char F, char T>
consteval auto fixed_replaced() {
  constexpr std::size_t n = W.size();
  char buf[n + 1]{};
  for (std::size_t ndx = 0; ndx != n; ++ndx)
    buf[ndx] = W[ndx] == F ? T : W[ndx];
  return strings::basic_fixed_string{buf,
      std::integral_constant<std::size_t, n>{}};
}

#pragma endregion
#pragma region fixed_split_cstr

// Split a fixed string into null-terminated `cstring_view`s.
//
// Like `fixed_split`, but the pieces are `cstring_view`s instead of
// `string_view`s. `W` is split on the single delimiter character `D`. Each
// piece can only be terminated if the delimiter is itself a terminator, so the
// delimiter is replaced by '\0' to give null-delimited storage. Those views
// must outlive the call, so the storage is materialized as the template
// parameter `Nulled` (a template-parameter object with static storage), not a
// local. `Nulled` is an implementation detail; do not pass it explicitly.
template<strings::fixed_string W, char D = ',',
    strings::fixed_string Nulled = fixed_replaced<W, D, '\0'>()>
consteval auto fixed_split_cstr() {
  constexpr auto whole = Nulled.view();
  constexpr auto n = std::count(whole.begin(), whole.end(), '\0');
  std::array<cstring_view, n + 1> result;

  size_t start = 0;
  for (size_t i = 0; i != result.size(); ++i) {
    result[i] = cstring_view{Nulled.data() + start};
    auto pos = whole.find('\0', start);
    start = pos == whole.npos ? whole.size() : pos + 1;
  }

  return result;
}

#pragma endregion

}} // namespace corvid::strings::fixed
