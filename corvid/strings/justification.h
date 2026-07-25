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
#include <cstddef>
#include <string>

#include "../meta/concepts.h"
#include "../meta/padding.h"
#include "string_literals.h"

namespace corvid::strings { inline namespace justification {

// Justification
//
// Fixed-width field justification, following Python's `ljust`, `rjust`,
// `center`, and `zfill`. Each returns a copy of the input padded with `fill`
// out to `width`, or an unpadded copy when the input is already that wide.
//
// The names describe where the content goes, not where the padding does:
// `ljust` left-justifies the content, so the fill lands on the right.

#pragma region Justify

// Return `s` left-justified in a field of `width`, Python `ljust`-style.
template<StringViewLike S>
[[nodiscard]] constexpr auto ljust(const S& s, size_t width,
    char_type_of_t<S> fill = char_type_of_t<S>{' '}) {
  using C = char_type_of_t<S>;
  const auto sv = as_view(s);
  const auto pad = (width > sv.size()) ? width - sv.size() : 0;
  std::basic_string<C> r;
  r.reserve(sv.size() + pad);
  r.append(sv);
  r.append(pad, fill);
  return r;
}

// Return `s` right-justified in a field of `width`, Python `rjust`-style.
template<StringViewLike S>
[[nodiscard]] constexpr auto rjust(const S& s, size_t width,
    char_type_of_t<S> fill = char_type_of_t<S>{' '}) {
  using C = char_type_of_t<S>;
  const auto sv = as_view(s);
  const auto pad = (width > sv.size()) ? width - sv.size() : 0;
  std::basic_string<C> r;
  r.reserve(sv.size() + pad);
  r.append(pad, fill);
  r.append(sv);
  return r;
}

// Return `s` centered in a field of `width`, Python `center`-style.
//
// When the padding is odd, the extra fill goes on the right, matching
// `std::format`'s `^` alignment and this library's `calc_padding`. Python's
// `center` instead picks the side from the parities of the width and margin,
// so `center("ab", 5)` is " ab  " here but "  ab " in Python.
template<StringViewLike S>
[[nodiscard]] constexpr auto center(const S& s, size_t width,
    char_type_of_t<S> fill = char_type_of_t<S>{' '}) {
  using C = char_type_of_t<S>;
  const auto sv = as_view(s);
  const auto [lead, trail] = calc_padding(aligned::center, sv.size(), width);
  std::basic_string<C> r;
  r.reserve(sv.size() + lead + trail);
  r.append(lead, fill);
  r.append(sv);
  r.append(trail, fill);
  return r;
}

// Return `s` right-justified in a field of `width`, filling with zeros after
// any leading sign, Python `zfill`-style.
//
// A leading '+' or '-' stays in front and the zeros go after it. There is no
// fill parameter; use `rjust` for arbitrary fill.
template<StringViewLike S>
[[nodiscard]] constexpr auto zfill(const S& s, size_t width) {
  using C = char_type_of_t<S>;
  const auto sv = as_view(s);
  const bool has_sign =
      !sv.empty() && (sv.front() == C{'+'} || sv.front() == C{'-'});
  const auto pad = (width > sv.size()) ? width - sv.size() : 0;
  std::basic_string<C> r;
  r.reserve(sv.size() + pad);
  if (has_sign) r.push_back(sv.front());
  r.append(pad, C{'0'});
  r.append(sv.substr(has_sign ? 1 : 0));
  return r;
}

#pragma endregion

}} // namespace corvid::strings::justification
