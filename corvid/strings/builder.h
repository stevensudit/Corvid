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
#include <string>
#include <type_traits>

#include "../meta/bool_enums.h"
#include "../meta/concepts.h"
#include "delimiting.h"

namespace corvid::strings { inline namespace building {

#pragma region String concatenation functions

// Variadic concatenation, so that `a += b += c` can be written without the
// parentheses that `std::string` would otherwise need.

// A part that `std::string::operator+=` can take: anything convertible to
// `std::string_view`, or a single `char`. Other integrals are excluded so
// that an `int` is never silently appended as a character.
template<typename T>
concept Concatenable = StringViewConvertible<T> || Char<T>;

// Append each of `parts`, in order, to `target`, returning it.
constexpr auto&
concat_to(std::string& target, const Concatenable auto&... parts) {
  ((target += parts), ...);
  return target;
}

// Return the concatenation of `parts`, in order, as a new string.
[[nodiscard]] constexpr std::string concat(const Concatenable auto&... parts) {
  std::string target;
  concat_to(target, parts...);
  return target;
}

// Append `head`, then each of `tail` preceded by `d`, to `target`, returning
// it.
constexpr auto& concat_with_to(std::string& target, delim d,
    const Concatenable auto& head, const Concatenable auto&... tail) {
  target += head;
  if constexpr (sizeof...(tail) > 0) { ((target += d, target += tail), ...); }
  return target;
}

// Return `head`, then each of `tail` preceded by `d`, as a new string.
[[nodiscard]] constexpr std::string concat_with(delim d,
    const Concatenable auto& head, const Concatenable auto&... tail) {
  std::string target;
  concat_with_to(target, d, head, tail...);
  return target;
}

#pragma endregion
#pragma region builder

// String builder with a chainable `operator<<`.
//
// Where `std::string::operator+=` cannot be chained without parentheses,
// `builder{} << "a" << 'b' << sv` appends in order.
//
//  The ownership parameter selects the target: `unique` builds into a string
//  the builder owns, while `shared` builds into a `std::string&` supplied at
//  construction, so an existing buffer can be appended to in place. Either
//  way, `str()` exposes the string being built.
template<ownership_type Ownership>
class basic_builder {
  static constexpr bool owning = (Ownership == ownership_type::unique);

public:
  basic_builder() noexcept
  requires owning
  = default;

  explicit basic_builder(std::string& target) noexcept
  requires(!owning)
      : target_{target} {}

  // Append `part`.
  basic_builder& operator<<(const Concatenable auto& part) {
    target_ += part;
    return *this;
  }

  // The string being built.
  [[nodiscard]] std::string& str() noexcept { return target_; }
  [[nodiscard]] const std::string& str() const noexcept { return target_; }

private:
  std::conditional_t<owning, std::string, std::string&> target_;
};

// Builder that owns the string it builds.
using builder = basic_builder<ownership_type::unique>;

// Builder that appends to a caller-owned string.
using shared_builder = basic_builder<ownership_type::shared>;

#pragma endregion

}} // namespace corvid::strings::building
