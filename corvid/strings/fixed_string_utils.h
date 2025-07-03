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
#include "fixed_string.h"

namespace corvid::strings { inline namespace fixed {

// Split fixed string by delimiter, returning array of string views, optionally
// trimming by whitespace.
template<strings::fixed_string W, strings::fixed_string D = ",",
    strings::fixed_string WS = "">
consteval auto fixed_split() {
  constexpr auto whole = W.view();
  constexpr auto delim = D.view();
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
    w = w.substr(pos + 1);
  }

  return result;
}

// Split fixed string by delimiter, returning array of string views, trimming
// by specified whitespace.
template<strings::fixed_string W, strings::fixed_string WS = " ",
    strings::fixed_string D = ",">
consteval auto fixed_split_trim() {
  return fixed_split<W, D, WS>();
}

// TODO: Consider writing a version of split that also does a search/replace in
// parallel, as by calling search_and_replace.  Might even take a list of froms
// and to's.

}} // namespace corvid::strings::fixed
