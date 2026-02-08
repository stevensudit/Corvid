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
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "entity_registry.h"

namespace corvid { inline namespace ecs {
inline namespace component_storage_impl {

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
// `store_id_t::invalid`. It should probably not be `store_id_t{0}`, either, as
// that has a use as placeholder.
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

  static_assert(std::is_trivially_copyable_v<C>,
      "Component type must be trivially copyable");

  component_storage() noexcept = default;
  component_storage(component_storage&&) noexcept = default;
  component_storage& operator=(component_storage&&) noexcept = default;
  component_storage(registry_t& registry, store_id_t store_id)
      : registry_{&registry}, store_id_{store_id} {}

  void swap(component_storage& other) noexcept {
    using std::swap;
    swap(registry_, other.registry_);
    swap(store_id_, other.store_id_);
    components_.swap(other.components_);
    handles_.swap(other.handles_);
  }

  friend void swap(component_storage& a, component_storage& b) noexcept {
    a.swap(b);
  }

  // TODO: Consider changing this scheme to require setting the location_id to
  // 0 before adding. Essentially, 0 means it's not invalid (hence not
  // deleted), but it's also not owned by any storage yet. This prevents
  // stealing from a component but also lets you have an entity that you can
  // pass around and add to any one place. Doing so imprints the storage_id,
  // assigning ownership to that storage.

  // Add a component for an entity. The entity's location must already be set
  // to this storage with `ndx=invalid` (i.e., assigned but not yet stored).
  // Returns success flag.
  [[nodiscard]] bool add(id_t id, const C& component) {
    return add(registry_->get_handle(id), component);
  }

  // Add a component for an entity. The entity's location must already be set
  // to this storage with `ndx=invalid` (i.e., assigned but not yet stored).
  // Returns success flag.
  [[nodiscard]] bool add(handle_t handle, const C& component) {
    const auto& loc = registry_->get_location(handle);
    if (loc.store_id != store_id_ || loc.ndx != *store_id_t::invalid)
      return false;

    const auto ndx = components_.size();
    components_.push_back(component);
    handles_.push_back(handle);
    registry_->set_location(handle.get_id(), {store_id_, ndx});
    return true;
  }

  // Remove a component, moving the entity to `new_store_id` with ndx=invalid.
  // When `new_store_id` is `store_id_t::invalid`, this erases the entity from
  // the registry entirely (see `erase`).
  bool remove(id_t id, store_id_t new_store_id) {
    if (!contains(id)) return false;
    const auto ndx = registry_->get_location(id).ndx;
    do_swap_and_pop(ndx);
    registry_->set_location(id, {new_store_id, *store_id_t::invalid});
    return true;
  }

  // TODO: Add `remove` overload taking handle.

  // TODO: Add a remove_all that takes a new_store_id and removes all
  // components from this storage, like clear() does, except it sets the
  // location of each to (new_store_id, invalid_ndx). In fact, clear should
  // just call here with storage_id = storage_id_.

  // Remove a component and erase the entity from the registry.
  bool erase(id_t id) { return remove(id, store_id_t::invalid); }

  // TODO: Add `erase` overload taking handle.

  // Check whether an entity has a component in this storage.
  [[nodiscard]] bool contains(id_t id) const {
    if (!registry_->is_valid(id)) return false;
    auto loc = registry_->get_location(id);
    return loc.store_id == store_id_ && loc.ndx != *store_id_t::invalid;
  }

  // TODO: Add `contains` overload taking handle.

  // Access component by entity ID. Entity must be valid and in this storage.
  [[nodiscard]]
  decltype(auto) operator[](this auto& self, id_t id) noexcept {
    assert(self.contains(id));
    const auto ndx = self.registry_->get_location(id).ndx;
    return self.components_[ndx];
  }

  // Access component by entity ID, with checking.
  // TODO: Change this to use `handle`.
  [[nodiscard]]
  decltype(auto) at(this auto& self, id_t id) {
    if (!self.contains(id))
      throw std::out_of_range("entity not in this storage");
    const auto ndx = self.registry_->get_location(id).ndx;
    return self.components_[ndx];
  }

  // Return the number of components in this storage.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(components_.size());
  }

  // Check whether this storage is empty.
  [[nodiscard]] bool empty() const noexcept { return components_.empty(); }

  // Expose this storage's ID.
  [[nodiscard]] store_id_t store_id() const noexcept { return store_id_; }

  // Remove all components. Entities are not removed from the registry, though
  // their location ndx is set to invalid.
  void clear() {
    // TODO: Rewrite this as a call to remove_all with store_id_t::invalid.
    for (const auto& h : handles_)
      registry_->set_location(h, {store_id_, *store_id_t::invalid});

    components_.clear();
    handles_.clear();
  }

  // Reserve space for at least `new_cap` components.
  void reserve(size_type new_cap) {
    components_.reserve(new_cap);
    handles_.reserve(new_cap);
  }

private:
  // Swap element at `ndx` with the last element and pop. Updates the swapped
  // entity's ndx in the registry.
  void do_swap_and_pop(size_type ndx) {
    const auto last = static_cast<size_type>(components_.size() - 1);
    if (ndx != last) {
      std::swap(components_[ndx], components_[last]);
      std::swap(handles_[ndx], handles_[last]);
      // Update the swapped-in entity's index in the registry.
      const auto swapped_id = handles_[ndx].get_id();
      registry_->set_location(swapped_id, {store_id_, ndx});
    }
    components_.pop_back();
    handles_.pop_back();
  }

private:
  registry_t* registry_{nullptr};
  store_id_t store_id_{store_id_t::invalid};
  std::vector<C> components_;
  std::vector<handle_t> handles_;
};
}}} // namespace corvid::ecs::component_storage_impl
