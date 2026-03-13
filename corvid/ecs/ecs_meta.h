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

// Always-false helper for `static_assert` in templates, avoiding reliance on
// `sizeof(C) == 0` which requires `C` to be a complete type.
template<typename>
inline constexpr bool dependent_false_v = false;

// 0-based index into `Storages...` of the first storage whose
// `component_t == C`. Fails to compile if no storage matches.
template<typename C, size_t I, typename... Storages>
struct find_component_storage_index_impl;

template<typename C, size_t I, typename First, typename... Rest>
struct find_component_storage_index_impl<C, I, First, Rest...>
    : std::conditional_t<std::is_same_v<C, typename First::component_t>,
          std::integral_constant<size_t, I>,
          find_component_storage_index_impl<C, I + 1, Rest...>> {};

template<typename C, size_t I>
struct find_component_storage_index_impl<C, I> {
  static_assert(dependent_false_v<C>,
      "no storage in the scene has `component_t` equal to `C`");
};

// 0-based index into `Storages...` of the storage whose `component_t == C`.
template<typename C, typename... Storages>
inline constexpr size_t find_component_storage_index_v =
    find_component_storage_index_impl<C, 0, Storages...>::value;

// Number of storages in `Storages...` whose `component_t == C`.
template<typename C, typename... Storages>
inline constexpr size_t component_match_count_v =
    (static_cast<size_t>(std::is_same_v<C, typename Storages::component_t>) +
        ...);

// 0-based index into `Storages...` of the first storage with `tag_t == TAG`.
// Fails to compile if no storage matches.
template<typename TAG, size_t I, typename... Storages>
struct find_storage_by_tag_impl;

template<typename TAG, size_t I, typename First, typename... Rest>
struct find_storage_by_tag_impl<TAG, I, First, Rest...>
    : std::conditional_t<std::is_same_v<TAG, typename First::tag_t>,
          std::integral_constant<size_t, I>,
          find_storage_by_tag_impl<TAG, I + 1, Rest...>> {};

template<typename TAG, size_t I>
struct find_storage_by_tag_impl<TAG, I> {
  static_assert(dependent_false_v<TAG>,
      "no storage in the scene has `tag_t` equal to `TAG`");
};

// 0-based index into `Storages...` of the storage with `tag_t == TAG`.
template<typename TAG, typename... Storages>
inline constexpr size_t find_storage_by_tag_index_v =
    find_storage_by_tag_impl<TAG, 0, Storages...>::value;

// Number of storages in `Storages...` whose `tag_t == C`.
template<typename C, typename... Storages>
inline constexpr size_t tag_match_count_v =
    (static_cast<size_t>(std::is_same_v<C, typename Storages::tag_t>) + ...);

// 0-based index into `Storages...` for selector `C`. Resolution order:
//   1. If exactly one storage has `component_t == C`, use it.
//   2. Else if exactly one storage has `tag_t == C`, use it (the returned
//      reference type is that storage's `component_t`, not `C`).
//   3. Otherwise fail to compile: `C` is ambiguous or absent.
template<typename C, typename... Storages>
consteval size_t storage_index_for_impl() noexcept {
  constexpr size_t nc = component_match_count_v<C, Storages...>;
  constexpr size_t nt = tag_match_count_v<C, Storages...>;
  if constexpr (nc == 1) {
    return find_component_storage_index_v<C, Storages...>;
  } else if constexpr (nc == 0 && nt == 1) {
    return find_storage_by_tag_index_v<C, Storages...>;
  } else if constexpr (nc > 1) {
    static_assert(dependent_false_v<C>,
        "`C` matches multiple storages by `component_t`; pass the storage's "
        "`tag_t` instead to select the specific storage");
    return 0;
  } else {
    static_assert(dependent_false_v<C>,
        "`C` matches no storage by `component_t` and does not uniquely match "
        "any storage by `tag_t`");
    return 0;
  }
}

template<typename C, typename... Storages>
inline constexpr size_t storage_index_for_v =
    storage_index_for_impl<C, Storages...>();

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
