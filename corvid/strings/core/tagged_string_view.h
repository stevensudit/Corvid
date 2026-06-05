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
#include <string_view>

#include "string_view_wrapper.h"

namespace corvid { inline namespace inlinestrings {
inline namespace taggedstringview {

#pragma region tagged_string_view

// Tagged string view
//
// A `string_view_wrapper` child that adds nothing but type safety. It is
// distinguished by a `Tag` type (and `Char`), so views with different tags
// are distinct types that don't implicitly mix, and, unlike
// `opt_string_view`, it cannot be implicitly constructed from a raw
// `std::string_view`: the view-taking constructor is `explicit`, and there
// is no view-taking assignment. Conversion the other way (to
// `std::string_view`) stays implicit, as does comparison against any view.
//
// Having no invariant on its state, it restores the reslicing operations the
// CRTP base leaves to children (`substr`, `remove_prefix`, `remove_suffix`),
// with `substr` keeping the tag. That minimalism also makes it the natural
// vehicle for exercising the base's full contract in tests.
template<typename Tag, typename Char = char>
class tagged_string_view final
    : public string_view_wrapper<tagged_string_view<Tag, Char>, Char> {
  using wrapper = string_view_wrapper<tagged_string_view<Tag, Char>, Char>;

#pragma region Member types
public:
  using tag_type = Tag;
  using view_t = typename wrapper::view_t;
  using size_type = typename wrapper::size_type;
  using wrapper::npos;

#pragma endregion
#pragma region Construction

  constexpr tagged_string_view() noexcept = default;
  constexpr explicit tagged_string_view(view_t sv) noexcept : wrapper{sv} {}

#pragma endregion
#pragma region Reslicing

  // Safe because there is no state invariant to preserve. `substr` keeps the
  // tag; the in-place mutators match `std::string_view`.
  [[nodiscard]] constexpr tagged_string_view
  substr(size_type pos = 0, size_type n = npos) const {
    return tagged_string_view{this->sv_.substr(pos, n)};
  }
  constexpr void remove_prefix(size_type n) { this->sv_.remove_prefix(n); }
  constexpr void remove_suffix(size_type n) { this->sv_.remove_suffix(n); }

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::inlinestrings::taggedstringview
