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

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "archetype_storage_base.h"

namespace corvid { inline namespace ecs { inline namespace archetype_storages {

// Packed archetype components storage with O(1) lookup through
// `entity_registry`.
//
// Maps entity IDs to densely-packed sets of component records using
// swap-and-pop for removal. The entity registry's `location_t.ndx` stores each
// entity's index in this class's vectors, enabling O(1) access by entity ID
// while centralizing the management of these IDs.
//
// Physical layout:
//   std::tuple< std::vector<C0>, std::vector<C1>, ... >
//
// Note that, despite specializing on a tuple of values, it does not
// physically store a vector of tuples (AoS). Rather, it uses a tuple of
// vectors (SoA). For an AoSoA alternative with the same public interface, see
// `chunked_archetype_storage` in `chunked_archetype_storage.h`.
//
// The `REG` template parameter provides `id_t`, `size_type`, `store_id_t`,
// `location_t`. An ID vector in parallel with the component vectors tracks
// which entity occupies each row, enabling O(1) lookup of entity IDs and
// allowing swap-and-pop to update the registry so the displaced entity knows
// its new index.
//
// Template parameters:
//  REG      - `entity_registry` instantiation. Provides types.
//  TUPLE    - Tuple of component types. Each must be trivially copyable.
//  TAG      - Optional tag type (default: `void`). Use a distinct tag to
//             create multiple structurally-identical storages that are
//             nevertheless different types and can coexist in the same
//             `scene<>` tuple.
template<typename REG, typename TUPLE, typename TAG = void>
class archetype_storage;

template<typename REG, typename... Cs, typename TAG>
class archetype_storage<REG, std::tuple<Cs...>, TAG>
    : public archetype_storage_base<
          archetype_storage<REG, std::tuple<Cs...>, TAG>, REG,
          std::tuple<Cs...>> {
  using base_t = archetype_storage_base<
      archetype_storage<REG, std::tuple<Cs...>, TAG>, REG, std::tuple<Cs...>>;

public:
  using tag_t = TAG;

  // Inherit all type aliases from the base.
  using typename base_t::tuple_t;
  using typename base_t::registry_t;
  using typename base_t::id_t;
  using typename base_t::handle_t;
  using typename base_t::size_type;
  using typename base_t::store_id_t;
  using typename base_t::location_t;
  using typename base_t::metadata_t;
  using typename base_t::allocator_type;
  using typename base_t::id_allocator_t;
  using typename base_t::id_vector_t;
  using typename base_t::row_view;
  using typename base_t::row_lens;
  using typename base_t::iterator;
  using typename base_t::const_iterator;
  using base_t::size;
  using base_t::clear;
  using storage_base_t = typename base_t::storage_base_t;
  using storage_base_t::registry_;
  using storage_base_t::store_id_;
  using storage_base_t::limit_;
  using storage_base_t::ids_;

  template<typename T>
  using component_allocator_t =
      typename std::allocator_traits<allocator_type>::template rebind_alloc<T>;

  template<typename T>
  using component_vector_t = std::vector<T, component_allocator_t<T>>;

  // Constructors.

  // Default-constructed storage has no registry binding. Assign from a
  // fully-constructed instance before calling any mutation methods.
  archetype_storage() = default;

  // Construct with a custom allocator but without binding to a registry.
  // Useful for staged initialization.
  explicit archetype_storage(const allocator_type& alloc)
      : base_t{id_allocator_t{alloc}}, components_{make_components(alloc)} {}

  // Construct bound to `registry` with the given `store_id`. `store_id` must
  // not be `store_id_t::invalid` or `store_id_t{0}` (staging). If
  // `do_reserve` is true and `limit` is not the sentinel unlimited value,
  // reserves capacity for `limit` entities up front.
  explicit archetype_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid, bool do_reserve = false)
      : base_t{&registry, store_id, limit,
            id_allocator_t{registry.get_allocator()}},
        components_{make_components(registry.get_allocator())} {
    if (do_reserve && limit_ != *id_t::invalid) reserve(limit_);
  }

  archetype_storage(const archetype_storage&) = delete;
  archetype_storage(archetype_storage&&) noexcept = default;

  ~archetype_storage() { clear(); }

  archetype_storage& operator=(const archetype_storage&) = delete;
  archetype_storage& operator=(archetype_storage&&) noexcept = default;

  // Swap.
  void swap(archetype_storage& other) noexcept {
    base_t::do_swap_base(other);
    components_.swap(other.components_);
  }

  friend void swap(archetype_storage& lhs, archetype_storage& rhs) noexcept(
      noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
  }

  // Shrink all component vectors and IDs to fit their size.
  void shrink_to_fit() {
    for_each_component([](auto& vec) { vec.shrink_to_fit(); });
    ids_.shrink_to_fit();
  }

  // Reserve capacity for at least `new_cap` entities across all component
  // vectors and IDs.
  void reserve(size_type new_cap) {
    const auto cap = static_cast<size_t>(new_cap);
    for_each_component([&](auto& vec) { vec.reserve(cap); });
    ids_.reserve(cap);
  }

  // Return current capacity (minimum across all component vectors and IDs).
  [[nodiscard]] size_type capacity() const noexcept {
    size_t min_cap = ids_.capacity();
    std::apply(
        [&](const auto&... vecs) {
          ((min_cap = std::min(min_cap, vecs.capacity())), ...);
        },
        components_);
    return static_cast<size_type>(min_cap);
  }

private:
  // Grant the base chain and row wrappers access to private customization
  // points.
  friend base_t;
  friend typename base_t::storage_base_t;
  friend typename base_t::add_guard;
  friend row_lens;
  friend row_view;

  // Append one row of components (called by base's `add(id_t, ...)`).
  // Note: `for_each_component` would only complicate this.
  template<typename... Args>
  void do_add_components(Args&&... args) {
    (std::get<component_vector_t<Cs>>(components_)
            .emplace_back(std::forward<Args>(args)),
        ...);
  }

  // Roll back all component vectors to `new_size` (called by base's
  // `add_guard` on exception).
  void do_resize_storage(size_type new_size) {
    for_each_component([&](auto& vec) { vec.resize(new_size); });
  }

  // Swap elements at `left_ndx` and `right_ndx`, including their IDs.
  void do_swap_elements(size_type left_ndx, size_type right_ndx) noexcept {
    for_each_component([&](auto& vec) {
      std::swap(vec[left_ndx], vec[right_ndx]);
    });
    std::swap(ids_[left_ndx], ids_[right_ndx]);
  }

  // Swap element at `ndx` with the last element and pop. Updates the displaced
  // entity's registry location.
  void do_swap_and_pop(size_type ndx) {
    const auto last = size() - 1;
    if (ndx != last) {
      do_swap_elements(ndx, last);
      if (registry_)
        registry_->set_location(ids_[ndx], {store_id_, ndx});
    }
    for_each_component([&](auto& vec) { vec.pop_back(); });
    ids_.pop_back();
  }

  // Clear all component vectors (called by base's `do_remove_all`).
  void do_clear_storage() {
    for_each_component([](auto& vec) { vec.clear(); });
  }

  // Customization points called by base's `do_remove_erase_if_component` and
  // by row_wrapper's `component()` accessors.

  template<typename C>
  [[nodiscard]] decltype(auto)
  do_get_component(this auto& self, size_type ndx) noexcept {
    return std::get<component_vector_t<C>>(self.components_)[ndx];
  }

  template<size_t Index>
  [[nodiscard]] decltype(auto)
  do_get_component_by_index(this auto& self, size_type ndx) noexcept {
    return std::get<Index>(self.components_)[ndx];
  }

  [[nodiscard]] auto
  do_make_components_tuple(this auto& self, size_type ndx) noexcept {
    return std::apply(
        [&](auto&&... vecs) {
          return std::tuple<decltype(vecs[ndx])&...>{vecs[ndx]...};
        },
        self.components_);
  }

  // Apply `f` to each component vector.
  template<typename F>
  void for_each_component(F&& f) {
    std::apply([&](auto&... vecs) { (f(vecs), ...); }, components_);
  }

  static std::tuple<component_vector_t<Cs>...> make_components(
      const allocator_type& alloc) {
    return std::tuple<component_vector_t<Cs>...>{
        component_vector_t<Cs>{component_allocator_t<Cs>{alloc}}...};
  }

private:
  // SoA storage: a tuple of vectors, one per component type.
  std::tuple<component_vector_t<Cs>...> components_{};
};

}}} // namespace corvid::ecs::archetype_storages
