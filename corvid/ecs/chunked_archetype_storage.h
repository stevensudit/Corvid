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

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "archetype_storage_base.h"

namespace corvid { inline namespace ecs {
inline namespace chunked_archetype_storages {

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
//  TUPLE     - Tuple of component types. Each must be trivially copyable.
//  CHUNKSZ   - Entities per chunk. Must be a positive power of two.
//              Default: 16. Tune so that one chunk fills a cache line
//              (e.g., CHUNKSZ = 64 / sizeof(largest_component_type)).
//  TAG       - Optional tag type (default: `void`). Use a distinct tag to
//              create multiple structurally-identical storages that are
//              nevertheless different types and can coexist in the same
//              `scene<>` tuple.
template<typename REG, typename TUPLE, size_t CHUNKSZ = 16,
    typename TAG = void>
class chunked_archetype_storage;

template<typename REG, typename... Cs, size_t CHUNKSZ, typename TAG>
class chunked_archetype_storage<REG, std::tuple<Cs...>, CHUNKSZ, TAG>
    : public archetype_storage_base<
          chunked_archetype_storage<REG, std::tuple<Cs...>, CHUNKSZ, TAG>, REG,
          std::tuple<Cs...>> {
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
  using storage_base_t = typename base_t::storage_base_t;
  using storage_base_t::registry_;
  using storage_base_t::store_id_;
  using storage_base_t::limit_;
  using storage_base_t::ids_;

  using tag_t = TAG;

  static constexpr size_t chunk_size_v = CHUNKSZ;

  static_assert((chunk_size_v & (chunk_size_v - 1)) == 0 && chunk_size_v > 0,
      "CHUNKSZ must be a positive power of two");

  // Array of chunk_size_v elements for a single component type within a chunk.
  template<typename C>
  using chunk_t = std::array<C, chunk_size_v>;

  // Each chunk holds chunk_size_v slots for every component type.
  using chunk_tuple_t = std::tuple<chunk_t<Cs>...>;

  using chunk_allocator_t = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<chunk_tuple_t>;

  using chunk_vector_t = std::vector<chunk_tuple_t, chunk_allocator_t>;

  // Constructors.

  // Default-constructed storage has no registry binding. Assign from a
  // fully-constructed instance before calling any mutation methods.
  chunked_archetype_storage() = default;

  // Construct bound to `registry` with the given `store_id`. `store_id` must
  // not be `store_id_t::invalid` or `store_id_t{0}` (staging). If
  // `do_reserve` is true and `limit` is not the sentinel unlimited value,
  // reserves capacity for `limit` entities up front.
  explicit chunked_archetype_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid, bool do_reserve = false)
      : base_t{&registry, store_id, limit,
            id_allocator_t{registry.get_allocator()}},
        chunks_{chunk_allocator_t{registry.get_allocator()}} {
    if (do_reserve && limit_ != *id_t::invalid) reserve(limit_);
  }

  chunked_archetype_storage(const chunked_archetype_storage&) = delete;
  chunked_archetype_storage(chunked_archetype_storage&&) noexcept = default;

  ~chunked_archetype_storage() { clear(); }

  chunked_archetype_storage& operator=(
      const chunked_archetype_storage&) = delete;
  chunked_archetype_storage& operator=(
      chunked_archetype_storage&&) noexcept = default;

  // Swap.
  void swap(chunked_archetype_storage& other) noexcept {
    base_t::do_swap_base(other);
    chunks_.swap(other.chunks_);
  }

  friend void swap(chunked_archetype_storage& lhs,
      chunked_archetype_storage& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
  }

  // Shrink the chunk vector and IDs to fit their current sizes.
  void shrink_to_fit() {
    chunks_.shrink_to_fit();
    ids_.shrink_to_fit();
  }

  // Reserve capacity for at least `new_cap` entities. The chunk vector is
  // rounded up to `ceil(new_cap / chunk_size_v)` whole chunks; IDs is
  // reserved exactly.
  void reserve(size_type new_cap) {
    const auto n = static_cast<size_t>(new_cap);
    chunks_.reserve((n + chunk_size_v - 1) / chunk_size_v);
    ids_.reserve(n);
  }

  // Return the current entity capacity. Governed by IDs capacity(); the chunk
  // vector may hold slightly more slots due to chunk-boundary rounding.
  [[nodiscard]] size_type capacity() const noexcept {
    return static_cast<size_type>(ids_.capacity());
  }

private:
  // Grant the base chain and row wrappers access to private customization
  // points.
  friend base_t;
  friend typename base_t::storage_base_t;
  friend typename base_t::add_guard;
  friend row_lens;
  friend row_view;

  // Append one row of components into the correct chunk slot (called by
  // base's `add(id_t, ...)`).
  template<typename... Args>
  void do_add_components(Args&&... args) {
    const auto [chunk_ndx, element_ndx] = chunk_coords(size());
    if (element_ndx == 0) chunks_.emplace_back();
    auto& chunk = chunks_.back();
    ((void)(std::get<chunk_t<Cs>>(chunk)[element_ndx] =
                std::forward<Args>(args)),
        ...);
  }

  // Roll back chunks_ to the number needed for `new_size` entities (called
  // by base's `add_guard` on exception).
  void do_resize_storage(size_type new_size) {
    const auto chunk_count =
        (static_cast<size_t>(new_size) + chunk_size_v - 1) / chunk_size_v;
    chunks_.resize(chunk_count);
  }

  // Decompose a flat logical index into a `(chunk_index, element_index)` pair.
  static constexpr std::pair<size_t, size_t> chunk_coords(
      size_type ndx) noexcept {
    const auto n = static_cast<size_t>(ndx);
    return {n / chunk_size_v, n % chunk_size_v};
  }

  // Swap the elements (all component slots and the ID) at two logical indices.
  void do_swap_elements(size_type left_ndx, size_type right_ndx) noexcept {
    const auto [left_chunk_ndx, left_element_ndx] = chunk_coords(left_ndx);
    const auto [right_chunk_ndx, right_element_ndx] = chunk_coords(right_ndx);
    (std::swap(
         std::get<chunk_t<Cs>>(chunks_[left_chunk_ndx])[left_element_ndx],
         std::get<chunk_t<Cs>>(chunks_[right_chunk_ndx])[right_element_ndx]),
        ...);
    std::swap(ids_[left_ndx], ids_[right_ndx]);
  }

  // Swap element at `ndx` with the last element, pop the last slot, and drop
  // the last chunk if it is now empty. Updates the displaced entity's registry
  // location.
  void do_swap_and_pop(size_type ndx) {
    const auto last = size() - 1;
    if (ndx != last) {
      do_swap_elements(ndx, last);
      if (registry_)
        registry_->set_location(ids_[ndx], {store_id_, ndx});
    }
    ids_.pop_back();
    // The last chunk becomes empty when the element we just removed was in its
    // first slot (slot 0), i.e. `ids_.size()` is now a multiple of
    // `chunk_size_v`.
    if (ids_.size() % chunk_size_v == 0) chunks_.pop_back();
  }

  // Clear chunk storage (called by base's `do_remove_all`).
  void do_clear_storage() { chunks_.clear(); }

  // Customization points called by base's `do_remove_erase_if_component` and
  // by `row_wrapper`'s `component()` accessors.

  template<typename C>
  [[nodiscard]] decltype(auto)
  do_get_component(this auto& self, size_type ndx) noexcept {
    const auto [chunk_ndx, element_ndx] = chunk_coords(ndx);
    return std::get<chunk_t<C>>(self.chunks_[chunk_ndx])[element_ndx];
  }

  template<size_t Index>
  [[nodiscard]] decltype(auto)
  do_get_component_by_index(this auto& self, size_type ndx) noexcept {
    const auto [chunk_ndx, element_ndx] = chunk_coords(ndx);
    return std::get<Index>(self.chunks_[chunk_ndx])[element_ndx];
  }

  [[nodiscard]] auto
  do_make_components_tuple(this auto& self, size_type ndx) noexcept {
    const auto [chunk_ndx, element_ndx] = chunk_coords(ndx);
    // NOLINTBEGIN(clang-analyzer-core.NullDereference)
    return std::apply(
        [&](auto&&... arrs) {
          return std::tuple<decltype(arrs[element_ndx])&...>{
              arrs[element_ndx]...};
        },
        self.chunks_[chunk_ndx]);
    // NOLINTEND(clang-analyzer-core.NullDereference)
  }

private:
  // AoSoA storage: one chunk per K entities, each chunk a tuple of arrays.
  chunk_vector_t chunks_{};
};

}}} // namespace corvid::ecs::chunked_archetype_storages
