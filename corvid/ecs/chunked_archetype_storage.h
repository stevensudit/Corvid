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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "../meta/forward_like.h"

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
// which is why ChunkSize is required to be a positive power of two.
//
// An archetype is a storage unit defined by a fixed set of component types,
// where each component type is stored densely in arrays and rows across those
// arrays correspond to entities. Each component type is represented as a POD
// struct, usually containing floats, IDs, or small fixed-size arrays.
//
// Template parameters:
//  Registry  - `entity_registry` instantiation. Provides types.
//  CsTuple   - Tuple of component types. Each must be trivially copyable.
//  ChunkSize - Entities per chunk. Must be a positive power of two.
//              Default: 16. Tune so that one chunk fills a cache line
//              (e.g., ChunkSize = 64 / sizeof(largest_component_type)).
template<typename Registry, typename CsTuple, size_t ChunkSize = 16>
class chunked_archetype_storage;

template<typename Registry, typename... Cs, size_t ChunkSize>
class chunked_archetype_storage<Registry, std::tuple<Cs...>, ChunkSize> {
public:
  using tuple_t = std::tuple<Cs...>;
  using registry_t = Registry;
  using id_t = typename registry_t::id_t;
  using handle_t = typename registry_t::handle_t;
  using size_type = typename registry_t::size_type;
  using store_id_t = typename registry_t::store_id_t;
  using location_t = typename registry_t::location_t;
  using metadata_t = typename registry_t::metadata_t;
  using allocator_type = typename registry_t::allocator_type;

  static constexpr size_t chunk_size_v = ChunkSize;

  static_assert((chunk_size_v & (chunk_size_v - 1)) == 0 && chunk_size_v > 0,
      "ChunkSize must be a positive power of two");

  // Array of chunk_size_v elements for a single component type within a chunk.
  template<typename C>
  using chunk_t = std::array<C, chunk_size_v>;

  // Each chunk holds chunk_size_v slots for every component type.
  using chunk_tuple_t = std::tuple<chunk_t<Cs>...>;

  using chunk_allocator_t = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<chunk_tuple_t>;
  using id_allocator_t = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<id_t>;

  using chunk_vector_t = std::vector<chunk_tuple_t, chunk_allocator_t>;
  using id_vector_t = std::vector<id_t, id_allocator_t>;

  static_assert(sizeof...(Cs) >= 0);

  // Lightweight, non-owning handle to a single entity's components.
  //
  // Stores a pointer to the owning storage and a flat logical index. Each
  // component access decomposes that index into (chunk_ndx, element_ndx) via
  // chunk_coords(). Remains valid as long as no structural mutations
  // (add/remove/erase) occur on the owning storage.
  //
  // `IsConst = false`  ->  row_lens  (mutable access)
  // `IsConst = true`   ->  row_view  (read-only access)
  template<bool IsConst = false>
  class row_wrapper {
  public:
    static constexpr bool writeable_v = !IsConst;
    using owner_t = std::conditional_t<writeable_v, chunked_archetype_storage,
        const chunked_archetype_storage>;

    // Constructor.
    row_wrapper() = default;
    row_wrapper(const row_wrapper&) = default;
    row_wrapper(row_wrapper&&) = default;

    row_wrapper& operator=(const row_wrapper&) = default;
    row_wrapper& operator=(row_wrapper&&) = default;

    // Expose owner accessor.
    [[nodiscard]] decltype(auto) get_owner(this auto&& self) noexcept {
      return forward_like<decltype(self)>(*self.owner_);
    }

    // Get index and ID.
    [[nodiscard]] size_type index() const noexcept { return ndx_; }
    [[nodiscard]] id_t id() const { return owner_->ids_[ndx_]; }

    // Access component by type.
    template<typename C>
    requires(writeable_v)
    [[nodiscard]] C& component() noexcept {
      const auto [chunk_ndx, element_ndx] = owner_t::chunk_coords(ndx_);
      return std::get<chunk_t<C>>(owner_->chunks_[chunk_ndx])[element_ndx];
    }
    template<typename C>
    [[nodiscard]] const C& component() const noexcept {
      const auto [chunk_ndx, element_ndx] = owner_t::chunk_coords(ndx_);
      return std::get<chunk_t<C>>(owner_->chunks_[chunk_ndx])[element_ndx];
    }

    // Access component by index.
    template<size_t Index>
    requires(writeable_v)
    [[nodiscard]] auto& component() noexcept {
      const auto [chunk_ndx, element_ndx] = owner_t::chunk_coords(ndx_);
      return std::get<Index>(owner_->chunks_[chunk_ndx])[element_ndx];
    }
    template<size_t Index>
    [[nodiscard]] const auto& component() const noexcept {
      const auto [chunk_ndx, element_ndx] = owner_t::chunk_coords(ndx_);
      return std::get<Index>(owner_->chunks_[chunk_ndx])[element_ndx];
    }

    // Access all components as a tuple of mutable references.
    [[nodiscard]] auto components() noexcept
    requires(writeable_v)
    {
      const auto [chunk_ndx, element_ndx] = owner_t::chunk_coords(ndx_);
      return std::apply(
          [&](auto&&... arrs) {
            return std::tuple<decltype(arrs[element_ndx])&...>{
                arrs[element_ndx]...};
          },
          owner_->chunks_[chunk_ndx]);
    }

    // Access all components as a tuple of const references.
    [[nodiscard]] auto components() const noexcept {
      const auto [chunk_ndx, element_ndx] = owner_t::chunk_coords(ndx_);
      return std::apply(
          [&](auto&&... arrs) {
            return std::tuple<const std::remove_reference_t<
                decltype(arrs[element_ndx])>&...>{arrs[element_ndx]...};
          },
          owner_->chunks_[chunk_ndx]);
    }

  private:
    owner_t* owner_{};
    size_type ndx_{};

    explicit row_wrapper(owner_t& owner, size_type ndx)
        : owner_{&owner}, ndx_{ndx} {}

    friend class chunked_archetype_storage;
  };

  // Read-only row view.
  using row_view = row_wrapper<true>;

  // Mutable row lens.
  using row_lens = row_wrapper<false>;

  // Bidirectional iterator over a chunked_archetype_storage.
  //
  // Dereferencing yields a row_wrapper (row_lens or row_view) whose `id()`
  // returns the entity at the current position. Invalidated by any structural
  // mutation (add/remove/erase) on the storage.
  //
  // NOTE: Do not erase entities during range-for; use erase_if or
  // erase_if_component instead.
  template<bool IsConst = false>
  class row_iterator {
  public:
    static constexpr bool writeable_v = !IsConst;
    using iterator_category = std::bidirectional_iterator_tag;
    using iterator_concept = std::bidirectional_iterator_tag;
    using value_type = std::conditional_t<writeable_v, row_lens, row_view>;
    using difference_type = std::ptrdiff_t;
    using reference = value_type&;
    using pointer = value_type*;
    using owner_t = std::conditional_t<writeable_v, chunked_archetype_storage,
        const chunked_archetype_storage>;

    row_iterator() = default;
    row_iterator(const row_iterator&) = default;
    row_iterator(row_iterator&&) = default;
    row_iterator& operator=(const row_iterator&) = default;
    row_iterator& operator=(row_iterator&&) = default;

    [[nodiscard]] reference operator*() const noexcept { return row_; }
    [[nodiscard]] pointer operator->() const noexcept { return &row_; }

    row_iterator& operator++() noexcept {
      ++row_.ndx_;
      return *this;
    }
    row_iterator operator++(int) noexcept {
      auto out = *this;
      ++*this;
      return out;
    }
    row_iterator& operator--() noexcept {
      --row_.ndx_;
      return *this;
    }
    row_iterator operator--(int) noexcept {
      auto out = *this;
      --*this;
      return out;
    }

    [[nodiscard]] friend bool
    operator==(row_iterator lhs, row_iterator rhs) noexcept {
      return lhs.row_.ndx_ == rhs.row_.ndx_;
    }
    [[nodiscard]] friend bool
    operator!=(row_iterator lhs, row_iterator rhs) noexcept {
      return !(lhs == rhs);
    }

  private:
    mutable value_type row_{};

    explicit row_iterator(owner_t& owner, size_type ndx) : row_{owner, ndx} {}
    friend class chunked_archetype_storage;
  };

  using iterator = row_iterator<false>;
  using const_iterator = row_iterator<true>;

public:
  // Constructors.

  // Default-constructed storage has no registry binding. Assign from a
  // fully-constructed instance before calling any mutation methods.
  chunked_archetype_storage() = default;

  // Construct with a custom allocator but without binding to a registry.
  // Useful for staged initialization.
  explicit chunked_archetype_storage(const allocator_type& alloc)
      : chunks_{chunk_allocator_t{alloc}}, ids_{id_allocator_t{alloc}} {}

  // Construct bound to `registry` with the given `store_id`. `store_id` must
  // not be `store_id_t::invalid` or `store_id_t{0}` (staging). If
  // `do_reserve` is true and `limit` is not the sentinel unlimited value,
  // reserves capacity for `limit` entities up front.
  explicit chunked_archetype_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid, bool do_reserve = false)
      : chunks_{chunk_allocator_t{registry.get_allocator()}},
        registry_{&registry}, store_id_{store_id}, limit_{limit},
        ids_{id_allocator_t{registry.get_allocator()}} {
    if (do_reserve && limit_ != *id_t::invalid) reserve(limit_);
  }

  chunked_archetype_storage(const chunked_archetype_storage&) = delete;
  chunked_archetype_storage(chunked_archetype_storage&&) noexcept = default;
  chunked_archetype_storage& operator=(
      const chunked_archetype_storage&) = delete;
  chunked_archetype_storage& operator=(
      chunked_archetype_storage&&) noexcept = default;

  // Swap.
  void swap(chunked_archetype_storage& other) noexcept {
    using std::swap;
    swap(registry_, other.registry_);
    swap(store_id_, other.store_id_);
    chunks_.swap(other.chunks_);
    ids_.swap(other.ids_);
  }

  friend void swap(chunked_archetype_storage& lhs,
      chunked_archetype_storage& rhs) noexcept(noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
  }

  // Maximum number of entities allowed in this storage. Insertion fails when
  // this limit is reached. Defaults to the maximum representable value
  // (effectively unlimited).
  [[nodiscard]] size_type limit() const noexcept { return limit_; }

  // Set a new entity limit. Returns true on success, false if the current size
  // exceeds the new limit.
  [[nodiscard]] bool set_limit(size_type new_limit) {
    if (new_limit < ids_.size()) return false;
    limit_ = new_limit;
    return true;
  }

  // Shrink the chunk vector and ids_ to fit their current sizes. Non-binding
  // request; honored by all major standard library implementations.
  void shrink_to_fit() {
    chunks_.shrink_to_fit();
    ids_.shrink_to_fit();
  }

  // Atomically create an entity in the registry and insert it into this
  // storage. Returns the new entity's handle on success, or an invalid handle
  // if the registry refused creation or the storage limit would be exceeded.
  // Components are forwarded in the same order as the Cs... pack.
  template<typename... Args>
  [[nodiscard]] handle_t add_new(const metadata_t& metadata, Args&&... args) {
    auto owner = registry_->create_owner(location_t{store_id_t{}}, metadata);
    if (!owner || !add(owner.id(), std::forward<Args>(args)...)) return {};
    return owner.release();
  }

  // Insert components for an entity already in staging (store_id ==
  // store_id_t{}). Returns false if the entity is not in staging, is
  // invalid, or if the limit would be exceeded.
  template<typename... Args>
  [[nodiscard]] bool add(id_t id, Args&&... args) {
    static_assert(sizeof...(Args) == sizeof...(Cs));
    const auto& loc = registry_->get_location(id);
    if (loc.store_id != store_id_t{}) return false;
    const auto ndx = size();
    if (ndx >= limit_) return false;
    const auto [chunk_ndx, element_ndx] = chunk_coords(ndx);
    ids_.reserve(static_cast<size_t>(ndx) + 1);
    chunks_.reserve(chunk_ndx + 1);
    if (element_ndx == 0) chunks_.emplace_back();
    auto& chunk = chunks_.back();
    ((void)(std::get<chunk_t<Cs>>(chunk)[element_ndx] =
                std::forward<Args>(args)),
        ...);
    ids_.push_back(id);
    registry_->set_location(id, {store_id_, ndx});
    return true;
  }

  // Insert components for an entity by handle. Validates the handle before
  // delegating to add(id_t, ...). Returns false for an invalid or stale
  // handle.
  template<typename... Args>
  [[nodiscard]] bool add(handle_t handle, Args&&... args) {
    if (!registry_->is_valid(handle)) return false;
    return add(handle.id(), std::forward<Args>(args)...);
  }

  // Remove entity by ID, moving it back to staging. Returns success flag.
  bool remove(id_t id) { return do_remove_erase(id, store_id_t{}); }

  // Remove entity by handle, moving it back to staging. Returns success flag.
  bool remove(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), store_id_t{});
  }

  // Move all entities back to staging. Entities remain valid
  // (store_id == store_id_t{0}) after this call; contrast with clear().
  void remove_all() { do_remove_all(store_id_t{}); }

  // Swap-and-pop the entity out of storage and destroy it in the registry.
  // The entity ID becomes invalid after this call. Returns success flag.
  bool erase(id_t id) { return do_remove_erase(id, store_id_t::invalid); }

  // Erase by handle; validates the handle first. Returns success flag.
  bool erase(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), store_id_t::invalid);
  }

  // Erase entities for which `pred(comp, id)` returns true, where `comp` is a
  // mutable reference to the entity's component of type C and `id` is the
  // entity's ID. Uses swap-and-pop; the predicate must not structurally modify
  // the storage. All erased entities are destroyed in the registry. Returns
  // the count erased.
  template<typename C>
  size_type erase_if_component(auto pred) {
    size_type cnt = 0;
    for (size_type ndx{}; ndx < ids_.size();) {
      const auto [chunk_ndx, element_ndx] = chunk_coords(ndx);
      auto& comp = std::get<chunk_t<C>>(chunks_[chunk_ndx])[element_ndx];
      if (pred(comp, ids_[ndx])) {
        const auto removed_id = ids_[ndx];
        do_swap_and_pop(ndx);
        registry_->set_location(removed_id, {store_id_t::invalid});
        ++cnt;
      } else
        ++ndx;
    }
    return cnt;
  }

  // Overload that selects the component by zero-based tuple index rather than
  // by type. Useful when two component types in Cs... are identical. Delegates
  // to erase_if_component<C>.
  template<size_t Index>
  size_type erase_if_component(auto pred) {
    using C = std::tuple_element_t<Index, tuple_t>;
    return erase_if_component<C>(std::move(pred));
  }

  // Erase entities for which `pred(row)` returns true, where `row` is a
  // row_view giving const access to all components and the entity ID. Use
  // this overload when the predicate must inspect multiple component types
  // simultaneously. Uses swap-and-pop; the predicate must not structurally
  // modify the storage. All erased entities are destroyed in the registry.
  // Returns the count erased.
  size_type erase_if(auto pred) {
    size_type cnt = 0;
    row_view row{*this, {}};
    for (size_type ndx{}; ndx < ids_.size();) {
      row.ndx_ = ndx;
      if (pred(row)) {
        const auto removed_id = ids_[ndx];
        do_swap_and_pop(ndx);
        registry_->set_location(removed_id, {store_id_t::invalid});
        ++cnt;
      } else
        ++ndx;
    }
    return cnt;
  }

  // Destroy all entities in the registry and empty the storage. Contrast with
  // remove_all(), which returns entities to staging instead of destroying
  // them.
  void clear() { do_remove_all(store_id_t::invalid); }

  // Check whether an entity is in this storage, by ID.
  [[nodiscard]] bool contains(id_t id) const {
    if (!registry_->is_valid(id)) return false;
    return registry_->get_location(id).store_id == store_id_;
  }

  // Check whether an entity is in this storage, by handle.
  [[nodiscard]] bool contains(handle_t handle) const {
    if (!registry_->is_valid(handle)) return false;
    return contains(handle.id());
  }

  // Return the number of entities in this storage.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(ids_.size());
  }

  // Return whether storage is empty.
  [[nodiscard]] bool empty() const noexcept { return ids_.empty(); }

  // Expose this storage's ID.
  [[nodiscard]] store_id_t store_id() const noexcept { return store_id_; }

  // Reserve capacity for at least `new_cap` entities. The chunk vector is
  // rounded up to `ceil(new_cap / chunk_size_v)` whole chunks; ids_ is
  // reserved exactly.
  void reserve(size_type new_cap) {
    const auto n = static_cast<size_t>(new_cap);
    chunks_.reserve((n + chunk_size_v - 1) / chunk_size_v);
    ids_.reserve(n);
  }

  // Return the current entity capacity. Governed by ids_.capacity(); the chunk
  // vector may hold slightly more slots due to chunk-boundary rounding.
  [[nodiscard]] size_type capacity() const noexcept {
    return static_cast<size_type>(ids_.capacity());
  }

  // Return the row at logical index `ndx` as a row_lens (mutable) or row_view
  // (const). No bounds checking is performed.
  [[nodiscard]] row_lens operator[](size_type ndx) noexcept {
    return row_lens{*this, ndx};
  }
  [[nodiscard]] row_view operator[](size_type ndx) const noexcept {
    return row_view{*this, ndx};
  }

  // Bidirectional iteration over all entities in insertion order.
  [[nodiscard]] iterator begin() noexcept { return iterator{*this, 0}; }
  [[nodiscard]] iterator end() noexcept { return iterator{*this, size()}; }
  [[nodiscard]] const_iterator begin() const noexcept {
    return const_iterator{*this, 0};
  }
  [[nodiscard]] const_iterator end() const noexcept {
    return const_iterator{*this, size()};
  }
  [[nodiscard]] const_iterator cbegin() const noexcept {
    return const_iterator{*this, 0};
  }
  [[nodiscard]] const_iterator cend() const noexcept {
    return const_iterator{*this, size()};
  }

private:
  // Decompose a flat logical index into a (chunk_index, element_index) pair.
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
      if (registry_) registry_->set_location(ids_[ndx], {store_id_, ndx});
    }
    ids_.pop_back();
    // The last chunk becomes empty when the element we just removed was in its
    // first slot (slot 0), i.e. ids_.size() is now a multiple of chunk_size_v.
    if (ids_.size() % chunk_size_v == 0) chunks_.pop_back();
  }

  bool do_remove_erase(id_t id, store_id_t new_store_id) {
    if (!contains(id)) return false;
    do_swap_and_pop(registry_->get_location(id).ndx);
    registry_->set_location(id, {new_store_id, *store_id_t::invalid});
    return true;
  }

  void do_remove_all(store_id_t new_store_id) {
    for (const auto id : ids_) registry_->set_location(id, {new_store_id});
    chunks_.clear();
    ids_.clear();
  }

  // AoSoA storage: one chunk per K entities, each chunk a tuple of arrays.
  chunk_vector_t chunks_{};

  // Registry pointer and store ID for location updates on swap-and-pop.
  registry_t* registry_{nullptr};
  store_id_t store_id_{store_id_t::invalid};
  size_type limit_{*id_t::invalid};

  // Entity IDs corresponding to each logical row (flat, parallel to chunks).
  id_vector_t ids_{};
};

}}} // namespace corvid::ecs::chunked_archetype_storages
