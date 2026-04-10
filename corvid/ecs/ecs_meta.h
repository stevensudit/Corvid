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

#include <optional>
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
// NOLINTBEGIN(readability-redundant-typename)
template<typename Storage, typename... Cs>
inline constexpr bool has_all_components_v =
    (tuple_contains_v<Cs, typename Storage::tuple_t> && ...);
// NOLINTEND(readability-redundant-typename)

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
// NOLINTBEGIN(readability-redundant-typename)
inline constexpr size_t component_match_count_v =
    (static_cast<size_t>(std::is_same_v<C, typename Storages::component_t>) +
        ...);
// NOLINTEND(readability-redundant-typename)

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
// NOLINTBEGIN(readability-redundant-typename)
template<typename C, typename... Storages>
inline constexpr size_t tag_match_count_v =
    (static_cast<size_t>(std::is_same_v<C, typename Storages::tag_t>) + ...);
// NOLINTEND(readability-redundant-typename)

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

// Helper: append T to Tuple only if T is not already present.
template<typename T, typename Tuple>
struct tuple_append_unique;
template<typename T, typename... Ts>
struct tuple_append_unique<T, std::tuple<Ts...>> {
  using type = std::conditional_t<(std::is_same_v<T, Ts> || ...),
      std::tuple<Ts...>, std::tuple<Ts..., T>>;
};

// Recursively accumulate types from SrcTuples into AccTuple, skipping
// duplicates. Handles both type-list and empty-tuple cases.
template<typename AccTuple, typename... SrcTuples>
struct tuple_union_impl {
  using type = AccTuple;
};
template<typename AccTuple, typename Head, typename... Tail, typename... Rest>
struct tuple_union_impl<AccTuple, std::tuple<Head, Tail...>, Rest...> {
  using type = typename tuple_union_impl<
      typename tuple_append_unique<Head, AccTuple>::type, std::tuple<Tail...>,
      Rest...>::type;
};
template<typename AccTuple, typename... Rest>
struct tuple_union_impl<AccTuple, std::tuple<>, Rest...> {
  using type = typename tuple_union_impl<AccTuple, Rest...>::type;
};

// Deduplicated union of component types across all `Tuples`. For example,
// `tuple_union_t<tuple<A,B,C>, tuple<A,D,E>>` yields `tuple<A,B,C,D,E>`.
template<typename... Tuples>
using tuple_union_t = typename tuple_union_impl<std::tuple<>, Tuples...>::type;

// 0-based index of T in Tuple. Fails to compile if T is not present.
template<typename T, typename Tuple, size_t I = 0>
struct tuple_index_impl;
template<typename T, size_t I>
struct tuple_index_impl<T, std::tuple<>, I> {
  static_assert(dependent_false_v<T>, "type not found in tuple");
};
template<typename T, typename Head, typename... Tail, size_t I>
struct tuple_index_impl<T, std::tuple<Head, Tail...>, I>
    : std::conditional_t<std::is_same_v<T, Head>,
          std::integral_constant<size_t, I>,
          tuple_index_impl<T, std::tuple<Tail...>, I + 1>> {};

template<typename T, typename Tuple>
inline constexpr size_t tuple_index_v = tuple_index_impl<T, Tuple>::value;

// Transform `std::tuple<C0, C1, ...>` to
// `std::tuple<std::optional<C0>, std::optional<C1>, ...>`.
template<typename Tuple>
struct wrap_optionals;
template<typename... Cs>
struct wrap_optionals<std::tuple<Cs...>> {
  using type = std::tuple<std::optional<Cs>...>;
};
template<typename Tuple>
using wrap_optionals_t = typename wrap_optionals<Tuple>::type;

}}} // namespace corvid::ecs::ecs_metas
