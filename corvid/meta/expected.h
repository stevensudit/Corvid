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
#include <cassert>
#include <expected>
#include <utility>

namespace corvid { inline namespace meta { inline namespace expectations {

// Helpers for `std::expected`.
//
// C++23 offers no sugar for propagating a failure out of a function whose
// return type is an `expected` over a different value type, so call sites
// must unpack the error and repack it by hand. These helpers name those
// casts.
//
//   std::expected<token, parse_error> t = lex(src);
//   if (!t) return as_unexpected(t);  // as std::expected<tree, parse_error>

#pragma region as_unexpected

// Extract the error of a failed `expected`, recast as a `std::unexpected`
// that can convert to an `expected` over any value type.
//
// Precondition: `r` holds an error.
template<typename T, typename E>
[[nodiscard]] constexpr std::unexpected<E>
as_unexpected(const std::expected<T, E>& r) {
  assert(!r.has_value());
  return std::unexpected{r.error()};
}
template<typename T, typename E>
[[nodiscard]] constexpr std::unexpected<E>
as_unexpected(std::expected<T, E>&& r) {
  assert(!r.has_value());
  return std::unexpected{std::move(r).error()};
}

#pragma endregion

}}} // namespace corvid::meta::expectations
