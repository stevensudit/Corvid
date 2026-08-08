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

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

#include "../enums/bool_enums.h"
#include "../infra/exception_firewalls.h"
#include "../math/arithmetic.h"
#include "archetype_storage_base.h"

namespace corvid { inline namespace ecs {
inline namespace chunked_archetype_storages {

#pragma region chunked_archetype_storage

// AoSoA archetype component storage with O(1) lookup through
// `entity_registry`.
//
// Drop-in alternative to `archetype_storage`: the public interface is
// identical, so the two are interchangeable. Prefer this class when systems
// iterate densely over multiple components per entity, as the AoSoA layout
// keeps all component data for a slice of `chunk_size_v` entities contiguous
// in memory.
//
// Physical layout:
//   std::vector< std::tuple< std::array<C0,K>, std::array<C1,K>, ... > >
//
// Entity at logical index `ndx` lives in chunk `ndx / K` at slot `ndx % K`.
// When K is a power of two the compiler reduces both to a shift and a mask,
// which is why `ChunkSize` is required to be a positive power of two.
//
// Template parameters:
//  REG       - `entity_registry` instantiation. Provides types.
//  TUPLE     - Tuple of component types. Each must be default-constructible
//              and assignable. Intended for cheap value-type components:
//              vacated slots retain stale values until their chunk is
//              dropped.
//  CHUNKSZ   - Entities per chunk. Must be a positive power of two.
//              Default: 16. To avoid false sharing when threads are
//              partitioned by chunk, size each per-component slice to at
//              least one cache line: `CHUNKSZ >= (64 /
//              sizeof(smallest_component_type))`.
//  TAG       - Optional tag type (default: `void`). Use a distinct tag to
//              create multiple structurally identical storages that are
//              nevertheless different types and can coexist in the same
//              `archetype_scene<>` tuple.
template<typename REG, typename TUPLE, size_t CHUNKSZ = 16UZ,
    typename TAG = void>
class chunked_archetype_storage;

template<typename REG, typename... Cs, size_t CHUNKSZ, typename TAG>
class chunked_archetype_storage<REG, std::tuple<Cs...>, CHUNKSZ, TAG> final
    : public archetype_storage_base<
          chunked_archetype_storage<REG, std::tuple<Cs...>, CHUNKSZ, TAG>, REG,
          std::tuple<Cs...>> {
#pragma region Types

  using base_t = archetype_storage_base<
      chunked_archetype_storage<REG, std::tuple<Cs...>, CHUNKSZ, TAG>, REG,
      std::tuple<Cs...>>;

public:
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

  using tag_t = TAG;

  static constexpr size_t chunk_size_v = CHUNKSZ;

  static_assert(std::has_single_bit(chunk_size_v),
      "CHUNKSZ must be a positive power of two");

  // Array of chunk_size_v elements for a single component type within a chunk.
  template<typename C>
  using chunk_t = std::array<C, chunk_size_v>;

  // Each chunk holds chunk_size_v slots for every component type.
  using chunk_tuple_t = std::tuple<chunk_t<Cs>...>;

  using chunk_allocator_t = std::allocator_traits<
      allocator_type>::template rebind_alloc<chunk_tuple_t>;

  using chunk_vector_t = std::vector<chunk_tuple_t, chunk_allocator_t>;

#pragma endregion
#pragma region Construction

  // Default-constructed storage has no registry binding. Assign from a
  // fully constructed instance before calling any mutation methods.
  chunked_archetype_storage() = default;

  // Construct bound to `registry` with the given `store_id`.
  //
  // The `store_id` is not permitted to be `store_id_t::invalid` or
  // `store_id_t{0}` (staging). If `policy` is `allocation_policy::eager` and
  // `limit` is not the sentinel unlimited value, reserves capacity for
  // `limit` entities up front.
  explicit chunked_archetype_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid,
      allocation_policy policy = allocation_policy::lazy)
      : base_t{registry, store_id, limit},
        chunks_{chunk_allocator_t{registry.get_allocator()}} {
    if ((policy == allocation_policy::eager) && (limit_ != *id_t::invalid))
      reserve(limit_);
  }

  chunked_archetype_storage(const chunked_archetype_storage&) = delete;
  chunked_archetype_storage(chunked_archetype_storage&&) noexcept = default;

  ~chunked_archetype_storage() {
    try_or_terminate([&] { clear(); });
  }

  chunked_archetype_storage& operator=(
      const chunked_archetype_storage&) = delete;
  chunked_archetype_storage& operator=(
      chunked_archetype_storage&&) noexcept = default;

#pragma endregion
#pragma region Swap

  void swap(chunked_archetype_storage& other) noexcept {
    base_t::do_swap_base(other);
    chunks_.swap(other.chunks_);
  }

  friend void swap(chunked_archetype_storage& lhs,
      chunked_archetype_storage& rhs) noexcept {
    lhs.swap(rhs);
  }

#pragma endregion
#pragma region Capacity

  // Shrink the chunk vector and IDs to fit their current sizes.
  void shrink_to_fit() {
    chunks_.shrink_to_fit();
    ids_.shrink_to_fit();
  }

  // Reserve capacity for at least `new_cap` entities.
  //
  // Requests beyond the entity limit are clamped to it. The chunk vector is
  // rounded up to whole chunks; IDs is reserved exactly.
  void reserve(size_type new_cap) {
    const auto n = static_cast<size_t>(std::min(new_cap, limit_));
    chunks_.reserve(ceil_div(n, chunk_size_v));
    ids_.reserve(n);
  }

  // Return the current entity capacity: the number of entities the storage
  // can hold without reallocating, which is the minimum of the ID capacity
  // and the whole-chunk capacity.
  [[nodiscard]] size_type capacity() const noexcept {
    auto min_cap =
        std::min(ids_.capacity(), chunks_.capacity() * chunk_size_v);
    if constexpr (sizeof(size_type) < sizeof(size_t)) {
      constexpr auto max_cap = std::numeric_limits<size_type>::max();
      if (min_cap > max_cap) return max_cap;
    }
    return static_cast<size_type>(min_cap);
  }

#pragma endregion
#pragma region Implementation
private:
  using base_t::registry_;
  using base_t::store_id_;
  using base_t::limit_;
  using base_t::ids_;

  // Grant the base chain and row wrappers access to private customization
  // points.
  friend base_t;
  friend base_t::add_guard;
  friend row_lens;
  friend row_view;

  // Append one row of components into the correct chunk slot (called by
  // base's `add(id_t, ...)`).
  template<typename... Args>
  bool do_add_components(Args&&... args) {
    const auto [chunk_ndx, element_ndx] = chunk_coords(size());
    if (element_ndx == 0) chunks_.emplace_back();
    auto& chunk = chunks_.back();
    ((void)(std::get<chunk_t<Cs>>(chunk)[element_ndx] =
                std::forward<Args>(args)),
        ...);
    return true;
  }

  // Roll back chunks_ to the number needed for `new_size` entities (called
  // by base's `add_guard` on exception).
  bool do_resize_storage(size_type new_size) {
    chunks_.resize(ceil_div(static_cast<size_t>(new_size), chunk_size_v));
    return true;
  }

  // Decompose a flat logical index into a `(chunk_index, element_index)` pair.
  static constexpr std::pair<size_t, size_t> chunk_coords(
      size_type ndx) noexcept {
    const auto n = static_cast<size_t>(ndx);
    return {n / chunk_size_v, n % chunk_size_v};
  }

  // Swap the elements (all component slots and the ID) at two logical indices.
  bool do_swap_elements(size_type left_ndx, size_type right_ndx) {
    const auto [left_chunk_ndx, left_element_ndx] = chunk_coords(left_ndx);
    const auto [right_chunk_ndx, right_element_ndx] = chunk_coords(right_ndx);
    (std::swap(
         std::get<chunk_t<Cs>>(chunks_[left_chunk_ndx])[left_element_ndx],
         std::get<chunk_t<Cs>>(chunks_[right_chunk_ndx])[right_element_ndx]),
        ...);
    std::swap(ids_[left_ndx], ids_[right_ndx]);
    return true;
  }

  // Swap element at `ndx` with the last element, pop the last slot, and drop
  // the last chunk if it is now empty. Updates the displaced entity's registry
  // location.
  bool do_swap_and_pop(size_type ndx) {
    assert(size());
    const auto last = size() - 1;
    if (ndx != last) {
      do_swap_elements(ndx, last);
      if (registry_) registry_->set_location(ids_[ndx], {store_id_, ndx});
    }
    ids_.pop_back();
    // The last chunk becomes empty when the element we just removed was in its
    // first slot (slot 0), i.e. `ids_.size()` is now a multiple of
    // `chunk_size_v`.
    if (ids_.size() % chunk_size_v == 0) chunks_.pop_back();
    return true;
  }

  // Clear chunk storage (called by base's `do_remove_erase_all`).
  bool do_clear_storage() {
    chunks_.clear();
    return true;
  }

  // Customization points called by base's `do_remove_erase_if_component` and
  // by `row_wrapper`'s `component` accessors.

  template<typename C>
  [[nodiscard]] decltype(auto)
  do_get_component(this auto& self, size_type ndx) {
    const auto [chunk_ndx, element_ndx] = chunk_coords(ndx);
    return std::get<chunk_t<C>>(self.chunks_[chunk_ndx])[element_ndx];
  }

  template<size_t Index>
  [[nodiscard]] decltype(auto)
  do_get_component_by_index(this auto& self, size_type ndx) {
    const auto [chunk_ndx, element_ndx] = chunk_coords(ndx);
    return std::get<Index>(self.chunks_[chunk_ndx])[element_ndx];
  }

  [[nodiscard]] auto do_make_components_tuple(this auto& self, size_type ndx) {
    const auto [chunk_ndx, element_ndx] = chunk_coords(ndx);
    // NOLINTBEGIN(clang-analyzer-core.NullDereference)
    return std::apply(
        [&](auto&&... arrs) {
          return std::tuple<decltype(arrs[element_ndx])...>{
              arrs[element_ndx]...};
        },
        self.chunks_[chunk_ndx]);
    // NOLINTEND(clang-analyzer-core.NullDereference)
  }

#pragma endregion
#pragma region Data members
private:
  // AoSoA storage: one chunk per K entities, each chunk a tuple of arrays.
  chunk_vector_t chunks_;

#pragma endregion
};

#pragma endregion
}}} // namespace corvid::ecs::chunked_archetype_storages
