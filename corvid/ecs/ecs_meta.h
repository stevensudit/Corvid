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

#include <tuple>
#include <type_traits>
#include <utility>

namespace corvid { inline namespace ecs { inline namespace ecs_metas {

// True if `T` appears at least once in `Tuple`.
template<typename T, typename Tuple>
struct tuple_contains;
template<typename T, typename... Ts>
struct tuple_contains<T, std::tuple<Ts...>>
    : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};
template<typename T, typename Tuple>
inline constexpr bool tuple_contains_v = tuple_contains<T, Tuple>::value;

// True if `Storage::tuple_t` contains every type in `Cs...`.
template<typename Storage, typename... Cs>
inline constexpr bool has_all_components_v =
    (tuple_contains_v<Cs, typename Storage::tuple_t> && ...);

// Returns a copy of component `C` from `row` if `SrcTuple` contains `C`,
// otherwise returns a value-initialized `C{}`.
//
// Used during archetype migration to carry over matching components and
// default-construct any that the source archetype doesn't have.
template<typename C, typename SrcTuple>
[[nodiscard]] C get_or_default(const auto& row) {
  if constexpr (tuple_contains_v<C, SrcTuple>)
    return C{row.template component<C>()};
  else
    return C{};
}

}}} // namespace corvid::ecs::ecs_metas
