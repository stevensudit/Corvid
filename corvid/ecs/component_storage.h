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
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "entity_registry.h"

namespace corvid { inline namespace ecs { inline namespace component_storages {

// Packed component storage with O(1) lookup through `entity_registry`.
//
// Maps entity IDs to densely-packed component records using swap-and-pop for
// removal. The entity registry's `location_t.ndx` stores each entity's index
// in this class's vector, enabling O(1) access by entity ID while
// centralizing the management of these IDs.
//
// The intention is for components sharing a common `entity_registry` to be
// stored in any of the `component_storage` instances specialized on that
// registry's type. Each of them must have a distinct `store_id` (although this
// is not enforced within this class), and it must not be
// `store_id_t::invalid` or `store_id_t{0}`.
//
// Note that this class stores IDs instead of handles because these it already
// owns the entities, so any checks would be redundant. We still check the
// validity of handles passed to us.
//
// Template parameters:
//  C        - Component type. Must be trivially copyable.
//  Registry - `entity_registry` instantiation. Provides id_t, store_id_t,
//             size_type, location_t, and handle_t.
template<typename C, typename Registry>
class component_storage {
public:
  using component_t = C;
  using registry_t = Registry;
  using id_t = typename Registry::id_t;
  using size_type = typename Registry::size_type;
  using store_id_t = typename Registry::store_id_t;
  using location_t = typename Registry::location_t;
  using handle_t = typename Registry::handle_t;
  using metadata_t = typename Registry::metadata_t;
  using allocator_type = typename Registry::allocator_type;
  using component_allocator_type =
      typename std::allocator_traits<allocator_type>::template rebind_alloc<C>;
  using id_allocator_type = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<id_t>;

  static_assert(std::is_trivially_copyable_v<C>,
      "Component type must be trivially copyable");

  // Constructors.

  // Default-constructed instances can only be assigned to.
  component_storage() noexcept = default;

  explicit component_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid, bool do_reserve = false)
      : registry_{&registry}, store_id_{store_id}, limit_{limit},
        components_{component_allocator_type{registry.get_allocator()}},
        ids_{id_allocator_type{registry.get_allocator()}} {
    if (store_id == store_id_t::invalid || store_id == store_id_t{})
      throw std::invalid_argument("store_id must be a valid non-zero value");
    if (do_reserve && limit_ != *id_t::invalid) reserve(limit_);
  }

  component_storage(component_storage&&) noexcept = default;
  component_storage& operator=(component_storage&&) noexcept = default;

  ~component_storage() { clear(); }

  // Swap with another storage.
  void swap(component_storage& other) noexcept {
    using std::swap;
    swap(registry_, other.registry_);
    swap(store_id_, other.store_id_);
    swap(limit_, other.limit_);
    components_.swap(other.components_);
    ids_.swap(other.ids_);
  }

  friend void swap(component_storage& a, component_storage& b) noexcept {
    a.swap(b);
  }

  // Maximum number of components allowed in this storage.
  //
  // Insertion fails when this limit is reached. Defaults to the maximum
  // representable value (effectively unlimited).
  [[nodiscard]] size_type limit() const noexcept { return limit_; }

  // Set a new component limit. Returns true on success, false if the current
  // size exceeds the new limit.
  [[nodiscard]] bool set_limit(size_type new_limit) {
    if (new_limit < components_.size()) return false;
    limit_ = new_limit;
    return true;
  }

  // Reduce memory usage to fit current size.
  void shrink_to_fit() {
    components_.shrink_to_fit();
    ids_.shrink_to_fit();
  }

  // Add a component for a new entity, returning the new entity's ID or
  // `id_t::invalid` on failure.
  id_t add_new(const C& component, const metadata_t& metadata = {}) {
    auto id = registry_->create_id(location_t{store_id_t{}}, metadata);
    if (id == id_t::invalid) return id_t::invalid;
    if (!add(id, component)) {
      registry_->erase(id);
      return id_t::invalid;
    }
    return id;
  }

  // Add a component for an entity. Returns success flag.
  //
  // See comment for `add(handle_t, const C&)`.
  [[nodiscard]] bool add(id_t id, const C& component) {
    const auto& loc = registry_->get_location(id);
    if (loc.store_id != store_id_t{}) return false;
    const auto ndx = components_.size();
    if (ndx >= limit_) return false;
    components_.push_back(component);
    ids_.push_back(id);
    registry_->set_location(id, {store_id_, ndx});
    return true;
  }

  // Add a component for an entity. Returns success flag.
  //
  // The entity's location must be set be set to `store_id_t{0}` and its `ndx`
  // doesn't matter, although it would normally be `*store_id_t::invalid`. As a
  // result of being added, the registry will update the location in the
  // registry to match its `storage_id_` and the correct `ndx`.
  [[nodiscard]] bool add(handle_t handle, const C& component) {
    if (!registry_->is_valid(handle)) return false;
    return add(handle.id(), component);
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

  // Erase components for which `pred(component, id)` returns true. Returns
  // count erased.
  size_type erase_if(auto pred) {
    size_type cnt = 0;
    for (size_type ndx{}; ndx < components_.size();) {
      if (pred(components_[ndx], ids_[ndx])) {
        const auto removed_id = ids_[ndx];
        do_swap_and_pop(ndx);
        registry_->set_location(removed_id, {store_id_t::invalid});
        ++cnt;
      } else
        ++ndx;
    }
    return cnt;
  }

  // Remove all components. Entities are removed from the registry.
  void clear() { do_remove_all(store_id_t::invalid); }

  // Check whether an entity has a component in this storage, by ID.
  [[nodiscard]] bool contains(id_t id) const {
    if (!registry_->is_valid(id)) return false;
    const auto loc = registry_->get_location(id);
    return loc.store_id == store_id_;
  }

  // Check whether an entity has a component in this storage, by handle.
  [[nodiscard]] bool contains(handle_t handle) const {
    if (!registry_->is_valid(handle)) return false;
    return contains(handle.id());
  }

  // Access component by entity ID. Entity must be valid and in this storage.
  [[nodiscard]]
  decltype(auto) operator[](this auto& self, id_t id) noexcept {
    assert(self.contains(id));
    const auto ndx = self.registry_->get_location(id).ndx;
    return self.components_[ndx];
  }

  // Access component by entity ID, with checking.
  [[nodiscard]]
  decltype(auto) at(this auto& self, id_t id) {
    if (!self.contains(id))
      throw std::out_of_range("entity not in this storage");
    const auto ndx = self.registry_->get_location(id).ndx;
    return self.components_[ndx];
  }

  // Access component by handle, with checking.
  [[nodiscard]]
  decltype(auto) at(this auto& self, handle_t handle) {
    if (!self.contains(handle))
      throw std::invalid_argument(
          "invalid handle or entity not in this storage");
    return self[handle.id()];
  }

  // Return the number of components in this storage.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(components_.size());
  }

  // Check whether this storage is empty.
  [[nodiscard]] bool empty() const noexcept { return components_.empty(); }

  // Expose this storage's ID.
  [[nodiscard]] store_id_t store_id() const noexcept { return store_id_; }

  // Reserve space for at least `new_cap` components.
  void reserve(size_type new_cap) {
    components_.reserve(new_cap);
    ids_.reserve(new_cap);
  }

  // Iterator over components. Dereferencing yields a `component_t` reference;
  // `id()` returns the entity ID at the current position.
  template<bool IsConst>
  class iterator_t {
    using storage_ptr = std::conditional_t<IsConst, const component_storage*,
        component_storage*>;

  public:
    using iterator_category = std::random_access_iterator_tag;
    using value_type = C;
    using difference_type = std::ptrdiff_t;
    using reference = std::conditional_t<IsConst, const C&, C&>;
    using pointer = std::conditional_t<IsConst, const C*, C*>;

    iterator_t() = default;

    [[nodiscard]] reference operator*() const {
      return storage_->components_[ndx_];
    }
    [[nodiscard]] pointer operator->() const {
      return &storage_->components_[ndx_];
    }
    [[nodiscard]] id_t id() const { return storage_->ids_[ndx_]; }

    iterator_t& operator++() {
      ++ndx_;
      return *this;
    }
    iterator_t operator++(int) {
      auto tmp = *this;
      ++ndx_;
      return tmp;
    }
    iterator_t& operator--() {
      --ndx_;
      return *this;
    }
    iterator_t operator--(int) {
      auto tmp = *this;
      --ndx_;
      return tmp;
    }

    iterator_t& operator+=(difference_type n) {
      ndx_ += n;
      return *this;
    }
    iterator_t& operator-=(difference_type n) {
      ndx_ -= n;
      return *this;
    }
    [[nodiscard]] iterator_t operator+(difference_type n) const {
      auto tmp = *this;
      return tmp += n;
    }
    [[nodiscard]] iterator_t operator-(difference_type n) const {
      auto tmp = *this;
      return tmp -= n;
    }
    [[nodiscard]] difference_type operator-(const iterator_t& o) const {
      return static_cast<difference_type>(ndx_) -
             static_cast<difference_type>(o.ndx_);
    }
    [[nodiscard]] reference operator[](difference_type n) const {
      return storage_->components_[ndx_ + n];
    }

    [[nodiscard]] friend iterator_t
    operator+(difference_type n, const iterator_t& it) {
      return it + n;
    }

    [[nodiscard]] bool operator==(const iterator_t& o) const = default;
    [[nodiscard]] auto operator<=>(const iterator_t& o) const {
      return ndx_ <=> o.ndx_;
    }

  private:
    storage_ptr storage_{};
    size_type ndx_{};

    iterator_t(storage_ptr s, size_type ndx) : storage_{s}, ndx_{ndx} {}
    friend class component_storage;
  };

  using iterator = iterator_t<false>;
  using const_iterator = iterator_t<true>;

  [[nodiscard]] iterator begin() noexcept { return {this, 0}; }
  [[nodiscard]] iterator end() noexcept { return {this, size()}; }
  [[nodiscard]] const_iterator begin() const noexcept { return {this, 0}; }
  [[nodiscard]] const_iterator end() const noexcept { return {this, size()}; }
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
  // Swap element at `ndx` with the last element and pop. Updates the swapped
  // entity's ndx in the registry.
  void do_swap_and_pop(size_type ndx) {
    const auto last = static_cast<size_type>(components_.size() - 1);
    if (ndx != last) {
      std::swap(components_[ndx], components_[last]);
      std::swap(ids_[ndx], ids_[last]);
      // Update the swapped-in entity's index in the registry.
      registry_->set_location(ids_[ndx], {store_id_, ndx});
    }
    components_.pop_back();
    ids_.pop_back();
  }

  // Remove a component, moving the entity to `new_store_id`. Returns success
  // flag.
  bool do_remove_erase(id_t id, store_id_t new_store_id) {
    if (!contains(id)) return false;
    do_swap_and_pop(registry_->get_location(id).ndx);
    registry_->set_location(id, {new_store_id, *store_id_t::invalid});
    return true;
  }

  void do_remove_all(store_id_t new_store_id) {
    for (const auto id : ids_) registry_->set_location(id, {new_store_id});
    components_.clear();
    ids_.clear();
  }

private:
  registry_t* registry_{nullptr};
  store_id_t store_id_{store_id_t::invalid};
  size_type limit_{*id_t::invalid};
  std::vector<C, component_allocator_type> components_;
  std::vector<id_t, id_allocator_type> ids_;
};
}}} // namespace corvid::ecs::component_storages
