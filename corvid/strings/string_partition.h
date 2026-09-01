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
#include <string_view>

#include "../meta/concepts.h"
#include "string_literals.h"

namespace corvid::strings { inline namespace partitioning {

#pragma region string_partition_base

// Result storage shared by `string_partition` and `string_rpartition`.
//
// `head` is the part before the separator, `sep` is the separator occurrence
// itself, and `tail` is the part after it. All three are views into the
// partitioned string, laid end to end, so their concatenation reconstructs
// it; they remain valid only as long as the underlying string does. Since a
// found `sep` is never empty, `!sep.empty()` distinguishes found from
// not-found.
template<CharType CharT = char>
struct string_partition_base {
#pragma region Member types

  using char_t = CharT;
  using view_t = std::basic_string_view<CharT>;

#pragma endregion
#pragma region Data members

  view_t head;
  view_t sep;
  view_t tail;

#pragma endregion
#pragma region Helpers
protected:
  // Set the three views around the separator found at `pos`.
  // NOLINTNEXTLINE(bugprone-exception-escape): substr positions are in-bounds.
  constexpr void do_found(view_t whole, size_t pos, size_t sep_size) noexcept {
    head = whole.substr(0, pos);
    sep = whole.substr(pos, sep_size);
    tail = whole.substr(pos + sep_size);
  }

#pragma endregion
};

#pragma endregion
#pragma region string_partition

// Three-way partition of a string around the first occurrence of a
// separator.
//
// Modeled on Python `str.partition`. Construct on a view of the whole string
// and a separator, then read the inherited `head`, `sep`, and `tail` views.
//
// When the separator is not found, all of the input lands in `head`, and the
// other two views are empty but still anchored in the input, at its end. An
// empty separator is never found, matching `token_parser`; Python raises
// instead, but that is not our way.
template<CharType CharT = char>
struct string_partition: string_partition_base<CharT> {
  using base = string_partition_base<CharT>;
  using view_t = base::view_t;

  // Partition `whole` around the first occurrence of `separator`.
  // NOLINTNEXTLINE(bugprone-exception-escape): substr positions are in-bounds.
  explicit constexpr string_partition(view_t whole,
      view_t separator) noexcept {
    const auto pos = separator.empty() ? npos : whole.find(separator);
    if (pos == npos) {
      this->head = whole;
      this->sep = this->tail = whole.substr(whole.size());
    } else {
      this->do_found(whole, pos, separator.size());
    }
  }
};

// Deduce the code unit from any string-like arguments.
template<StringViewLike S, StringViewLike T>
string_partition(const S&, const T&) -> string_partition<char_type_of_t<S>>;

#pragma endregion
#pragma region string_rpartition

// Three-way partition of a string around the last occurrence of a separator.
//
// Modeled on Python `str.rpartition`; see `string_partition` for the shared
// semantics. When the separator is not found, all of the input lands in
// `tail`, and the other two views are empty but still anchored in the input,
// at its start.
template<CharType CharT = char>
struct string_rpartition: string_partition_base<CharT> {
  using base = string_partition_base<CharT>;
  using view_t = base::view_t;

  // Partition `whole` around the last occurrence of `separator`.
  // NOLINTNEXTLINE(bugprone-exception-escape): substr positions are in-bounds.
  explicit constexpr string_rpartition(view_t whole,
      view_t separator) noexcept {
    const auto pos = separator.empty() ? npos : whole.rfind(separator);
    if (pos == npos) {
      this->head = this->sep = whole.substr(0, 0);
      this->tail = whole;
    } else {
      this->do_found(whole, pos, separator.size());
    }
  }
};

// Deduce the code unit from any string-like arguments.
template<StringViewLike S, StringViewLike T>
string_rpartition(const S&, const T&) -> string_rpartition<char_type_of_t<S>>;

#pragma endregion

}} // namespace corvid::strings::partitioning
