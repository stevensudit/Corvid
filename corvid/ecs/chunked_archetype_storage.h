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

namespace corvid { inline namespace ecs { inline namespace chunked_archetype_storages {

// AoSoA archetype component storage with O(1) lookup through
// `entity_registry`.
//
// Organises entities into fixed-size chunks so that all component arrays for
// a given chunk of entities reside in a contiguous memory block, improving
// cache utilisation when iterating over multiple components together.
//
// Physical layout:
//   std::vector< std::tuple< std::array<C0,K>, std::array<C1,K>, ... > >
//
// Entity at logical index `ndx` lives in chunk `ndx / K` at slot `ndx % K`.
// When K is a power of two the compiler reduces both to a shift and a mask,
// which is why ChunkSize is required to be a positive power of two.
//
// The public interface is identical to `archetype_storage` so the two can be
// used interchangeably.  `add_new`, `add`, `remove`, `remove_all`, `erase`,
// `erase_if`, `erase_if_component`, `clear`, `contains`, `size`, `empty`,
// `store_id`, `limit`, `set_limit`, `reserve`, `capacity`, `shrink_to_fit`,
// `operator[]`, `begin`/`end`/`cbegin`/`cend`, and `swap` are all present.
//
// Template parameters:
//  Registry  - `entity_registry` instantiation.  Provides types.
//  CsTuple   - Tuple of component types.  Each must be trivially copyable.
//  ChunkSize - Entities per chunk.  Must be a positive power of two (default 16).

template<typename Registry, typename CsTuple, std::size_t ChunkSize = 16>
class chunked_archetype_storage;

template<typename Registry, typename... Cs, std::size_t ChunkSize>
class chunked_archetype_storage<Registry, std::tuple<Cs...>, ChunkSize> {
public:
  static_assert((ChunkSize & (ChunkSize - 1)) == 0 && ChunkSize > 0,
      "ChunkSize must be a positive power of two");

  using tuple_t = std::tuple<Cs...>;
  using registry_t = Registry;
  using id_t = typename Registry::id_t;
  using handle_t = typename Registry::handle_t;
  using size_type = typename Registry::size_type;
  using store_id_t = typename Registry::store_id_t;
  using location_t = typename Registry::location_t;
  using metadata_t = typename Registry::metadata_t;
  using allocator_type = typename Registry::allocator_type;

  // Each chunk holds ChunkSize slots for every component type.
  using chunk_t = std::tuple<std::array<Cs, ChunkSize>...>;

  using chunk_allocator_t =
      typename std::allocator_traits<allocator_type>::template rebind_alloc<
          chunk_t>;
  using id_allocator_t = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<id_t>;

  using chunk_vector_t = std::vector<chunk_t, chunk_allocator_t>;
  using id_vector_t = std::vector<id_t, id_allocator_t>;

  static_assert(sizeof...(Cs) >= 0);

  // Row wrapper for accessing all components at a given logical index.
  template<bool IsConst = false>
  class row_wrapper {
  public:
    static constexpr bool writeable_v = !IsConst;
    using owner_t = std::conditional_t<IsConst, const chunked_archetype_storage,
        chunked_archetype_storage>;

    row_wrapper() = default;
    row_wrapper(const row_wrapper&) = default;
    row_wrapper(row_wrapper&&) = default;
    row_wrapper& operator=(const row_wrapper&) = default;
    row_wrapper& operator=(row_wrapper&&) = default;

    [[nodiscard]] decltype(auto) get_owner(this auto&& self) noexcept {
      return forward_like<decltype(self)>(*self.owner_);
    }

    [[nodiscard]] size_type index() const noexcept { return ndx_; }
    [[nodiscard]] id_t id() const { return owner_->ids_[ndx_]; }

    // Access component by type.
    template<typename C>
    requires(writeable_v)
    [[nodiscard]] C& component() noexcept {
      const auto ci = static_cast<std::size_t>(ndx_) / ChunkSize;
      const auto ei = static_cast<std::size_t>(ndx_) % ChunkSize;
      return std::get<std::array<C, ChunkSize>>(owner_->chunks_[ci])[ei];
    }
    template<typename C>
    [[nodiscard]] const C& component() const noexcept {
      const auto ci = static_cast<std::size_t>(ndx_) / ChunkSize;
      const auto ei = static_cast<std::size_t>(ndx_) % ChunkSize;
      return std::get<std::array<C, ChunkSize>>(owner_->chunks_[ci])[ei];
    }

    // Access component by index.
    template<std::size_t Index>
    requires(writeable_v)
    [[nodiscard]] auto& component() noexcept {
      const auto ci = static_cast<std::size_t>(ndx_) / ChunkSize;
      const auto ei = static_cast<std::size_t>(ndx_) % ChunkSize;
      return std::get<Index>(owner_->chunks_[ci])[ei];
    }
    template<std::size_t Index>
    [[nodiscard]] const auto& component() const noexcept {
      const auto ci = static_cast<std::size_t>(ndx_) / ChunkSize;
      const auto ei = static_cast<std::size_t>(ndx_) % ChunkSize;
      return std::get<Index>(owner_->chunks_[ci])[ei];
    }

    // Access all components as a tuple of mutable references.
    [[nodiscard]] auto components() noexcept
    requires(writeable_v)
    {
      const auto ci = static_cast<std::size_t>(ndx_) / ChunkSize;
      const auto ei = static_cast<std::size_t>(ndx_) % ChunkSize;
      return std::apply(
          [&](auto&&... arrs) {
            return std::tuple<decltype(arrs[ei])&...>{arrs[ei]...};
          },
          owner_->chunks_[ci]);
    }

    // Access all components as a tuple of const references.
    [[nodiscard]] auto components() const noexcept {
      const auto ci = static_cast<std::size_t>(ndx_) / ChunkSize;
      const auto ei = static_cast<std::size_t>(ndx_) % ChunkSize;
      return std::apply(
          [&](auto&&... arrs) {
            return std::tuple<
                const std::remove_reference_t<decltype(arrs[ei])>&...>{
                arrs[ei]...};
          },
          owner_->chunks_[ci]);
    }

  private:
    owner_t* owner_ = nullptr;
    size_type ndx_ = 0;

    explicit row_wrapper(owner_t& owner, size_type ndx)
        : owner_{&owner}, ndx_{ndx} {}

    friend class chunked_archetype_storage;
  };

  // Read-only row view.
  using row_view = row_wrapper<true>;

  // Mutable row lens.
  using row_lens = row_wrapper<false>;

  // Bidirectional iterator over rows.
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

  // Default-constructed instances can only be assigned to.
  chunked_archetype_storage() = default;
  explicit chunked_archetype_storage(const allocator_type& alloc)
      : chunks_{chunk_allocator_t{alloc}}, ids_{id_allocator_t{alloc}} {}

  explicit chunked_archetype_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid, bool do_reserve = false)
      : chunks_{chunk_allocator_t{registry.get_allocator()}},
        registry_{&registry}, store_id_{store_id}, limit_{limit},
        ids_{id_allocator_t{registry.get_allocator()}} {
    if (do_reserve && limit_ != *id_t::invalid) reserve(limit_);
  }

  chunked_archetype_storage(const chunked_archetype_storage&) = delete;
  chunked_archetype_storage(chunked_archetype_storage&&) noexcept = default;
  chunked_archetype_storage& operator=(const chunked_archetype_storage&) =
      delete;
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

  // Maximum number of entities allowed in this storage.
  [[nodiscard]] size_type limit() const noexcept { return limit_; }

  // Set a new entity limit. Returns true on success, false if current size
  // exceeds the new limit.
  [[nodiscard]] bool set_limit(size_type new_limit) {
    if (new_limit < ids_.size()) return false;
    limit_ = new_limit;
    return true;
  }

  // Shrink chunk vector and ids_ to fit their current sizes.
  void shrink_to_fit() {
    chunks_.shrink_to_fit();
    ids_.shrink_to_fit();
  }

  // Add components for a new entity, returning its handle or an invalid
  // handle on failure.
  template<typename... Args>
  [[nodiscard]] handle_t add_new(const metadata_t& metadata, Args&&... args) {
    auto owner = registry_->create_owner(location_t{store_id_t{}}, metadata);
    if (!owner || !add(owner.id(), std::forward<Args>(args)...)) return {};
    return owner.release();
  }

  // Add components for an entity already in staging. Returns success flag.
  template<typename... Args>
  [[nodiscard]] bool add(id_t id, Args&&... args) {
    static_assert(sizeof...(Args) == sizeof...(Cs));
    const auto& loc = registry_->get_location(id);
    if (loc.store_id != store_id_t{}) return false;
    const auto ndx = size();
    if (ndx >= limit_) return false;
    const auto ci = static_cast<std::size_t>(ndx) / ChunkSize;
    const auto ei = static_cast<std::size_t>(ndx) % ChunkSize;
    ids_.reserve(static_cast<std::size_t>(ndx) + 1);
    chunks_.reserve(ci + 1);
    if (ei == 0) chunks_.emplace_back();
    auto& chunk = chunks_.back();
    ((void)(std::get<std::array<Cs, ChunkSize>>(chunk)[ei] =
                std::forward<Args>(args)),
        ...);
    ids_.push_back(id);
    registry_->set_location(id, {store_id_, ndx});
    return true;
  }

  // Add components for an entity by handle. Returns success flag.
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

  // Move all entities back to staging.
  void remove_all() { do_remove_all(store_id_t{}); }

  // Remove entity by ID and erase it from the registry. Returns success flag.
  bool erase(id_t id) { return do_remove_erase(id, store_id_t::invalid); }

  // Remove entity by handle and erase it from the registry. Returns success
  // flag.
  bool erase(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), store_id_t::invalid);
  }

  // Erase entities for which `pred(component, id)` returns true, selected by
  // component type. Returns count erased.
  template<typename C>
  size_type erase_if_component(auto pred) {
    size_type cnt = 0;
    for (size_type ndx{}; ndx < ids_.size();) {
      const auto ci = static_cast<std::size_t>(ndx) / ChunkSize;
      const auto ei = static_cast<std::size_t>(ndx) % ChunkSize;
      auto& comp = std::get<std::array<C, ChunkSize>>(chunks_[ci])[ei];
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

  // Erase entities for which `pred(component, id)` returns true, selected by
  // component index. Returns count erased.
  template<std::size_t Index>
  size_type erase_if_component(auto pred) {
    using C = std::tuple_element_t<Index, tuple_t>;
    return erase_if_component<C>(std::move(pred));
  }

  // Erase entities for which `pred(row_view)` returns true. Returns count
  // erased.
  size_type erase_if(auto pred) {
    size_type cnt = 0;
    row_view row{*this, size_type{}};
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

  // Erase all entities from the registry.
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

  // Reserve capacity for at least `new_cap` entities (rounded up to a whole
  // number of chunks).
  void reserve(size_type new_cap) {
    const auto n = static_cast<std::size_t>(new_cap);
    chunks_.reserve((n + ChunkSize - 1) / ChunkSize);
    ids_.reserve(n);
  }

  // Return current entity capacity (min of chunk capacity in entities and
  // ids_ capacity).
  [[nodiscard]] size_type capacity() const noexcept {
    return static_cast<size_type>(
        std::min(chunks_.capacity() * ChunkSize, ids_.capacity()));
  }

  // Index.
  [[nodiscard]] row_lens operator[](size_type ndx) noexcept {
    return row_lens{*this, ndx};
  }
  [[nodiscard]] row_view operator[](size_type ndx) const noexcept {
    return row_view{*this, ndx};
  }

  // Iterators.
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
  // Swap the elements (all component slots and the ID) at two logical indices.
  void do_swap_elements(size_type left_ndx, size_type right_ndx) noexcept {
    const auto lci = static_cast<std::size_t>(left_ndx) / ChunkSize;
    const auto lei = static_cast<std::size_t>(left_ndx) % ChunkSize;
    const auto rci = static_cast<std::size_t>(right_ndx) / ChunkSize;
    const auto rei = static_cast<std::size_t>(right_ndx) % ChunkSize;
    (std::swap(std::get<std::array<Cs, ChunkSize>>(chunks_[lci])[lei],
         std::get<std::array<Cs, ChunkSize>>(chunks_[rci])[rei]),
        ...);
    std::swap(ids_[left_ndx], ids_[right_ndx]);
  }

  // Swap element at `ndx` with the last element, pop the last slot, and drop
  // the last chunk if it is now empty.  Updates the displaced entity's registry
  // location.
  void do_swap_and_pop(size_type ndx) {
    const auto last = size() - 1;
    if (ndx != last) {
      do_swap_elements(ndx, last);
      if (registry_) registry_->set_location(ids_[ndx], {store_id_, ndx});
    }
    ids_.pop_back();
    // The last chunk becomes empty when the element we just removed was in its
    // first slot (slot 0), i.e. ids_.size() is now a multiple of ChunkSize.
    if (ids_.size() % ChunkSize == 0) chunks_.pop_back();
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
