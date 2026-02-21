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

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "../meta/forward_like.h"

namespace corvid { inline namespace ecs { inline namespace archetype_storages {

// Packed archetype components storage with O(1) lookup through
// `entity_registry`.
//
// Maps entity IDs to densely-packed sets of component records using
// swap-and-pop for removal. The entity registry's `location_t.ndx` stores each
// entity's index in this class's vectors, enabling O(1) access by entity ID
// while centralizing the management of these IDs.
//
// An archetype is a storage unit defined by a fixed set of component types,
// where each component type is stored in its own dense array and rows across
// those arrays correspond to entities. Each component type is represented as a
// POD struct, usually containing floats, IDs, or small fixed-size arrays.
//
// The intention is for archetypes sharing a common `entity_registry` to be
// stored in any of the `archetype_storage` or `component_storage` instances
// specialized on that registry's type. Each of them must have a distinct
// `store_id` (although this is not enforced within this class), and it must
// not be `store_id_t::invalid` or `store_id_t{0}`.
//
// Note that, despite specializing on a tuple of values, it does not
// physically store a vector of tuples (AoS). Rather, the initial
// implementation uses a tuple of vectors (SoA). Future versions may use more
// sophisticated storage techniques (e.g., AoSoA chunks) to improve cache
// performance.

// The `Registry` template parameter provides `id_t`, `size_type`,
// `store_id_t`, `location_t`. An `ids_` vector in parallel with the component
// vectors tracks which entity occupies each row, enabling O(1) lookup of
// entity IDs and allowing swap-and-pop to update the registry so the displaced
// entity knows its new index.
//
// Note that this class stores IDs instead of handles because it already owns
// the entities, so any checks would be redundant. We still check the validity
// of handles passed to us.
//
// Template parameters:
//  Registry - `entity_registry` instantiation. Provides types.
//  CsTuple  - Tuple of component types. Each must be trivially copyable.
template<typename Registry, typename CsTuple>
class archetype_storage;

template<typename Registry, typename... Cs>
class archetype_storage<Registry, std::tuple<Cs...>> {
public:
  using tuple_t = std::tuple<Cs...>;
  using registry_t = Registry;
  using id_t = typename Registry::id_t;
  using handle_t = typename Registry::handle_t;
  using size_type = typename Registry::size_type;
  using store_id_t = typename Registry::store_id_t;
  using location_t = typename Registry::location_t;
  using metadata_t = typename Registry::metadata_t;
  using allocator_type = typename Registry::allocator_type;

  template<typename T>
  using component_allocator_t =
      typename std::allocator_traits<allocator_type>::template rebind_alloc<T>;

  using id_allocator_t = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<id_t>;

  template<typename T>
  using component_vector_t = std::vector<T, component_allocator_t<T>>;

  using id_vector_t = std::vector<id_t, id_allocator_t>;

  static_assert(sizeof...(Cs) >= 0);

  // Row wrapper for accessing all components at a given index.
  template<bool IsConst = false>
  class row_wrapper {
  public:
    static constexpr bool writeable_v = !IsConst;
    using owner_t = std::conditional_t<writeable_v, archetype_storage,
        const archetype_storage>;

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
      return std::get<component_vector_t<C>>(owner_->components_)[ndx_];
    }
    template<typename C>
    [[nodiscard]] const C& component() const noexcept {
      return std::get<component_vector_t<C>>(owner_->components_)[ndx_];
    }

    // Access component by index.
    template<std::size_t Index>
    requires(writeable_v)
    [[nodiscard]] auto& component() noexcept {
      return std::get<Index>(owner_->components_)[ndx_];
    }
    template<std::size_t Index>
    [[nodiscard]] const auto& component() const noexcept {
      return std::get<Index>(owner_->components_)[ndx_];
    }

    // Access all components as a tuple of mutable values.
    [[nodiscard]] auto components() noexcept
    requires(writeable_v)
    {
      return std::apply(
          [&](auto&&... vecs) {
            return std::tuple<decltype(vecs[ndx_])&...>{vecs[ndx_]...};
          },
          owner_->components_);
    }

    // Access all components as a tuple of const values.
    [[nodiscard]] auto components() const noexcept {
      return std::apply(
          [&](auto&&... vecs) {
            return std::tuple<
                const std::remove_reference_t<decltype(vecs[ndx_])>&...>{
                vecs[ndx_]...};
          },
          owner_->components_);
    }

  private:
    owner_t* owner_{};
    size_type ndx_{};

    explicit row_wrapper(owner_t& owner, size_type ndx)
        : owner_{&owner}, ndx_{ndx} {}

    friend class archetype_storage;
  };

  // Read-only row view.
  using row_view = row_wrapper<true>;

  // Mutable row lens.
  using row_lens = row_wrapper<false>;

  // Iterator over archetype. Dereferencing yields a `row_wrapper` whose `id()`
  // returns the entity ID at the current position.
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
    using owner_t = std::conditional_t<writeable_v, archetype_storage,
        const archetype_storage>;

  public:
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
    friend class archetype_storage;
  };

  using iterator = row_iterator<false>;
  using const_iterator = row_iterator<true>;

public:
  // Constructors.

  // Default-constructed instances can only be assigned to.
  archetype_storage() = default;
  explicit archetype_storage(const allocator_type& alloc)
      : components_{make_components(alloc)}, ids_{id_allocator_t{alloc}} {}

  explicit archetype_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid, bool do_reserve = false)
      : components_{make_components(registry.get_allocator())},
        registry_{&registry}, store_id_{store_id}, limit_{limit},
        ids_{id_allocator_t{registry.get_allocator()}} {
    if (do_reserve && limit_ != *id_t::invalid) reserve(limit_);
  }

  archetype_storage(const archetype_storage&) = delete;
  archetype_storage(archetype_storage&&) noexcept = default;
  archetype_storage& operator=(const archetype_storage&) = delete;
  archetype_storage& operator=(archetype_storage&&) noexcept = default;

  // Swap.
  void swap(archetype_storage& other) noexcept {
    using std::swap;
    swap(registry_, other.registry_);
    swap(store_id_, other.store_id_);
    swap(components_, other.components_);
    ids_.swap(other.ids_);
  }

  friend void swap(archetype_storage& lhs, archetype_storage& rhs) noexcept(
      noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
  }

  // Maximum number of components allowed in this storage.
  //
  // Insertion fails when this limit is reached. Defaults to the maximum
  // representable value (effectively unlimited).
  [[nodiscard]] size_type limit() const noexcept { return limit_; }

  // Set a new entity limit. Returns true on success, false if the current
  // size exceeds the new limit.
  [[nodiscard]] bool set_limit(size_type new_limit) {
    if (new_limit < ids_.size()) return false;
    limit_ = new_limit;
    return true;
  }

  // Shrink all component vectors and ids_ to fit their size.
  void shrink_to_fit() {
    for_each_component([](auto& vec) { vec.shrink_to_fit(); });
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

  // Add components for an entity. Returns success flag.
  template<typename... Args>
  [[nodiscard]] bool add(id_t id, Args&&... args) {
    static_assert(sizeof...(Args) == sizeof...(Cs));
    const auto& loc = registry_->get_location(id);
    if (loc.store_id != store_id_t{}) return false;
    const auto ndx = size();
    if (ndx >= limit_) return false;
    const size_t cap = ndx + 1;
    ids_.reserve(cap);
    for_each_component([&](auto& vec) { vec.reserve(cap); });
    ids_.push_back(id);
    // Note: `for_each_component` would only complicate this.
    (std::get<component_vector_t<Cs>>(components_)
            .emplace_back(std::forward<Args>(args)),
        ...);
    registry_->set_location(id, {store_id_, ndx});
    return true;
  }

  // Add components for an entity. Returns success flag.
  template<typename... Args>
  [[nodiscard]] bool add(handle_t handle, Args&&... args) {
    if (!registry_->is_valid(handle)) return false;
    return add(handle.id(), std::forward<Args>(args)...);
  }

  // Remove a component by ID, moving the entity to `store_id_t{0}`.
  // Returns success flag.
  bool remove(id_t id) { return do_remove_erase(id, store_id_t{}); }

  // Remove a component by handle, moving the entity to `store_id_t{0}`.
  // Returns success flag.
  bool remove(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), store_id_t{});
  }

  // Remove all components, moving all entities to `store_id_t{0}`.
  void remove_all() { do_remove_all(store_id_t{}); }

  // Remove a component by ID and erase the entity from the registry.
  // Returns success flag.
  bool erase(id_t id) { return do_remove_erase(id, store_id_t::invalid); }

  // Remove a component by handle and erase the entity from the registry.
  // Returns success flag.
  bool erase(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), store_id_t::invalid);
  }

  // Erase archetypes for which `pred(component, id)` returns true for the
  // selected component type. Returns count erased.
  template<typename C>
  size_type erase_if_component(auto pred) {
    return do_erase_if_component(std::get<component_vector_t<C>>(components_),
        std::move(pred));
  }

  // Erase archetypes for which `pred(component, id)` returns true for the
  // selected component index. Returns count erased.
  template<std::size_t Index>
  size_type erase_if_component(auto pred) {
    return do_erase_if_component(std::get<Index>(components_),
        std::move(pred));
  }

  // Erase archetypes for which `pred(row_view)` returns true.
  // Returns count erased.
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

  // Remove all archetypes. Entities are removed from the registry.
  void clear() { do_remove_all(store_id_t::invalid); }

  // Check whether an entity has an archetype in this storage, by ID.
  [[nodiscard]] bool contains(id_t id) const {
    if (!registry_->is_valid(id)) return false;
    const auto loc = registry_->get_location(id);
    return loc.store_id == store_id_;
  }

  // Check whether an entity has an archetype in this storage, by handle.
  [[nodiscard]] bool contains(handle_t handle) const {
    if (!registry_->is_valid(handle)) return false;
    return contains(handle.id());
  }

  // Return the number of elements in this storage.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(std::get<0>(components_).size());
  }

  // Return whether storage is empty.
  [[nodiscard]] bool empty() const noexcept {
    return std::get<0>(components_).empty();
  }

  // Expose this storage's ID.
  [[nodiscard]] store_id_t store_id() const noexcept { return store_id_; }

  // Reserve capacity for at least `new_cap` elements.
  void reserve(size_type new_cap) {
    const auto cap = static_cast<std::size_t>(new_cap);
    for_each_component([&](auto& vec) { vec.reserve(cap); });
    ids_.reserve(cap);
  }

  // Return current capacity (minimum across all component vectors and ids_).
  [[nodiscard]] size_type capacity() const noexcept {
    std::size_t min_cap = ids_.capacity();
    std::apply(
        [&](const auto&... vecs) {
          ((min_cap = std::min(min_cap, vecs.capacity())), ...);
        },
        components_);
    return static_cast<size_type>(min_cap);
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
  // Swap elements at left_ndx and right_ndx, including their IDs.
  void do_swap_elements(size_type left_ndx, size_type right_ndx) noexcept {
    for_each_component([&](auto& vec) {
      std::swap(vec[left_ndx], vec[right_ndx]);
    });
    std::swap(ids_[left_ndx], ids_[right_ndx]);
  }

  // Swap element at `ndx` with the last element and pop. Updates the swapped
  // entity's ndx in the registry.
  void do_swap_and_pop(size_type ndx) {
    const auto last = size() - 1;
    if (ndx != last) {
      do_swap_elements(ndx, last);
      if (registry_) registry_->set_location(ids_[ndx], {store_id_, ndx});
    }
    for_each_component([&](auto& vec) { vec.pop_back(); });
    ids_.pop_back();
  }

  bool do_remove_erase(id_t id, store_id_t new_store_id) {
    if (!contains(id)) return false;
    do_swap_and_pop(registry_->get_location(id).ndx);
    registry_->set_location(id, {new_store_id, *store_id_t::invalid});
    return true;
  }

  void do_remove_all(store_id_t new_store_id) {
    for (const auto id : ids_) registry_->set_location(id, {new_store_id});
    for_each_component([](auto& vec) { vec.clear(); });
    ids_.clear();
  }

  template<typename ComponentVec, typename Pred>
  size_type do_erase_if_component(ComponentVec& components, Pred&& pred) {
    size_type cnt = 0;
    for (size_type ndx{}; ndx < components.size();) {
      if (pred(components[ndx], ids_[ndx])) {
        const auto removed_id = ids_[ndx];
        do_swap_and_pop(ndx);
        registry_->set_location(removed_id, {store_id_t::invalid});
        ++cnt;
      } else
        ++ndx;
    }
    return cnt;
  }

private:
  static std::tuple<component_vector_t<Cs>...> make_components(
      const allocator_type& alloc) {
    return std::tuple<component_vector_t<Cs>...>{
        component_vector_t<Cs>{component_allocator_t<Cs>{alloc}}...};
  }

  template<typename F>
  void for_each_component(F&& f) {
    std::apply([&](auto&... vecs) { (f(vecs), ...); }, components_);
  }

  // SoA storage: a tuple of vectors, one per component type.
  std::tuple<component_vector_t<Cs>...> components_{};

  // Registry pointer and store ID for location updates on swap_and_pop.
  registry_t* registry_{nullptr};
  store_id_t store_id_{store_id_t::invalid};
  size_type limit_{*id_t::invalid};

  // Entity IDs corresponding to each component row.
  id_vector_t ids_{};
};
}}} // namespace corvid::ecs::archetype_storages

// TODO: Test how well it fits into stable_ids. We'll at least need to offer a
// way to detect swap_elements and make use of it.
