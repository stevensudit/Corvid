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
#include <cassert>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "../enums/bool_enums.h"
#include "../infra/exception_firewalls.h"
#include "../math/arithmetic.h"
#include "archetype_storage_base.h"
#include "storage_iterator.h"

namespace corvid { inline namespace ecs {
inline namespace mono_archetype_storages {

#pragma region mono_archetype_storage

// Packed single-component storage with O(1) lookup through `entity_registry`.
//
// Maps entity IDs to densely packed component records using swap-and-pop for
// removal. The entity registry's `location_t.ndx` stores each entity's index
// in this class's vector, enabling O(1) access by entity ID while
// centralizing the management of these IDs.
//
// Derives from `archetype_storage_base` with a single-element tuple. Provides
// a contiguous iterator (the underlying `components_` vector is a plain
// `std::vector`), a `row_view` with both `component<T>` and implicit
// `const component_t&` conversion, and a component-first `erase_if`.
//
// Template parameters:
//  REG - `entity_registry` instantiation. Provides types.
//  C   - Component type. Must be movable (removal uses swap-and-pop).
//  TAG - Optional tag type (default: `void`). Use a distinct tag to create
//        multiple structurally identical storages that are nevertheless
//        different types and can coexist in the same `archetype_scene<>`
//        tuple.
template<typename REG, typename C, typename TAG = void>
class mono_archetype_storage final
    : public archetype_storage_base<mono_archetype_storage<REG, C, TAG>, REG,
          std::tuple<C>> {
#pragma region Types

  using base_t = archetype_storage_base<mono_archetype_storage<REG, C, TAG>,
      REG, std::tuple<C>>;

public:
  using tag_t = TAG;
  using component_t = C;

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
  using typename base_t::tuple_t;
  using base_t::size;
  using base_t::clear;
  using base_t::contains;

  using component_allocator_type =
      std::allocator_traits<allocator_type>::template rebind_alloc<C>;

#pragma endregion
#pragma region Construction

  // Default-constructed instances can only be assigned to.
  mono_archetype_storage() noexcept = default;

  // Construct bound to `registry` with the given `store_id`.
  //
  // The `store_id` is not permitted to be `store_id_t::invalid` or
  // `store_id_t{0}` (staging). If `policy` is `allocation_policy::eager` and
  // `limit` is not the sentinel unlimited value, reserves capacity for
  // `limit` entities up front.
  explicit mono_archetype_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid,
      allocation_policy policy = allocation_policy::lazy)
      : base_t{registry, store_id, limit},
        components_{component_allocator_type{registry.get_allocator()}} {
    if ((policy == allocation_policy::eager) && (limit_ != *id_t::invalid))
      reserve(limit_);
  }

  mono_archetype_storage(mono_archetype_storage&&) noexcept = default;
  ~mono_archetype_storage() {
    try_or_terminate([&] { clear(); });
  }

  mono_archetype_storage& operator=(mono_archetype_storage&& other) noexcept {
    if (this == &other) return *this;
    clear();
    base_t::operator=(std::move(other));
    // base_t::operator= moves only the base sub-object; other.components_ is
    // unaffected, so this access is safe (false positive).
    // NOLINTNEXTLINE(bugprone-use-after-move)
    components_ = std::move(other.components_);
    return *this;
  }

#pragma endregion
#pragma region Swap

  // Swap with another storage.
  void swap(mono_archetype_storage& other) noexcept {
    base_t::do_swap_base(other);
    components_.swap(other.components_);
  }

  friend void
  swap(mono_archetype_storage& lhs, mono_archetype_storage& rhs) noexcept {
    lhs.swap(rhs);
  }

#pragma endregion
#pragma region Capacity

  // Reduce memory usage to fit current size.
  void shrink_to_fit() {
    components_.shrink_to_fit();
    ids_.shrink_to_fit();
  }

  // Reserve space for at least `new_cap` components.
  //
  // Requests beyond the entity limit are clamped to it.
  void reserve(size_type new_cap) {
    const auto cap = static_cast<size_t>(std::min(new_cap, limit_));
    components_.reserve(cap);
    ids_.reserve(cap);
  }

  // Return current capacity (minimum across the component and ID vectors).
  [[nodiscard]] size_type capacity() const noexcept {
    auto min_cap = std::min(components_.capacity(), ids_.capacity());
    return saturate_cast<size_type>(min_cap);
  }

#pragma endregion
#pragma region Insertion

  // Add a component for a new entity, returning its handle or an invalid
  // handle on failure.
  //
  // Component-first convenience overload of the base's metadata-first
  // `add_new`. `metadata` is by value so this overload outranks the base's
  // forwarding pack on component-first calls, and the constraint removes it
  // when the two roles have the same type and the ordering would be
  // ambiguous. Move-only components must use the metadata-first form.
  [[nodiscard]] handle_t
  add_new(const component_t& component, metadata_t metadata = {})
  requires(!std::is_same_v<component_t, metadata_t>)
  {
    return base_t::add_new(metadata, component);
  }

  using base_t::add_new;

#pragma endregion
#pragma region Conditional removal

  // Erase components for which `pred(component, id)` returns true.
  //
  // Returns count erased. Component-first convenience for
  // `erase_if_component<component_t>`. Predicate shape: `(const component_t&
  // comp, id_t id) -> bool`.
  size_type erase_if(auto pred) {
    return base_t::template erase_if_component<component_t>(std::move(pred));
  }

#pragma endregion
#pragma region row_view

  // Read-only view of a single entity's row; see
  // `single_component_row_view`.
  using row_view = single_component_row_view<component_t, id_t>;

#pragma endregion
#pragma region Element access

  // Mutable access: returns `component_t&` directly.
  [[nodiscard]] component_t& operator[](id_t id) {
    assert(contains(id));
    return components_[registry_->get_location(id).ndx];
  }

  // Const access: returns `row_view` for uniform migrate-compatible access.
  [[nodiscard]] row_view operator[](id_t id) const {
    assert(contains(id));
    const auto ndx = registry_->get_location(id).ndx;
    return {components_[ndx], ids_[ndx]};
  }

  // Mutable access by entity ID, with checking.
  [[nodiscard]] component_t& at(id_t id) {
    if (!contains(id)) throw std::out_of_range{"entity not in this storage"};
    return components_[registry_->get_location(id).ndx];
  }

  // Const access by entity ID, with checking.
  [[nodiscard]] row_view at(id_t id) const {
    if (!contains(id)) throw std::out_of_range{"entity not in this storage"};
    const auto ndx = registry_->get_location(id).ndx;
    return {components_[ndx], ids_[ndx]};
  }

  // Access component by handle, with checking.
  [[nodiscard]] component_t& at(handle_t handle) {
    if (!contains(handle))
      throw std::invalid_argument{
          "invalid handle or entity not in this storage"};
    return (*this)[handle.id()];
  }

  [[nodiscard]] row_view at(handle_t handle) const {
    if (!contains(handle))
      throw std::invalid_argument{
          "invalid handle or entity not in this storage"};
    return (*this)[handle.id()];
  }

#pragma endregion
#pragma region Iteration

  // Contiguous iterators over components; see `contiguous_storage_iterator`.
  using iterator =
      contiguous_storage_iterator<mono_archetype_storage, access::as_mutable>;
  using const_iterator =
      contiguous_storage_iterator<mono_archetype_storage, access::as_const>;

  [[nodiscard]] iterator begin() noexcept { return {this, 0}; }
  [[nodiscard]] iterator end() noexcept { return {this, size()}; }
  [[nodiscard]] const_iterator begin() const noexcept { return {this, 0}; }
  [[nodiscard]] const_iterator end() const noexcept { return {this, size()}; }
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

#pragma endregion
#pragma region Implementation
private:
  using base_t::registry_;
  using base_t::store_id_;
  using base_t::limit_;
  using base_t::ids_;

  // Grant `archetype_storage_base` and its nested types access to the CRTP
  // customization points, and the shared iterators access to the vectors.
  friend base_t;
  friend base_t::add_guard;
  friend base_t::row_lens;
  friend base_t::row_view;
  friend iterator;
  friend const_iterator;

  // Append one component row (called by the base's `add(id_t, ...)`).
  template<typename... Args>
  bool do_add_components(Args&&... args) {
    components_.push_back(std::forward<Args>(args)...);
    return true;
  }

  // Access the component by type (called by `row_wrapper::component<C>` and
  // `erase_if_component<C>`).
  template<typename T>
  [[nodiscard]] decltype(auto)
  do_get_component(this auto& self, size_type ndx) {
    static_assert(std::is_same_v<T, component_t>,
        "mono_archetype_storage only has one component type");
    return self.components_[ndx];
  }

  // Access the component by zero-based tuple index (must be 0).
  template<size_t Index>
  [[nodiscard]] decltype(auto)
  do_get_component_by_index(this auto& self, size_type ndx) {
    static_assert(Index == 0,
        "mono_archetype_storage only has one component (index 0)");
    return self.components_[ndx];
  }

  // Return all components as a single-element tuple of references.
  [[nodiscard]] decltype(auto)
  do_make_components_tuple(this auto& self, size_type ndx) {
    return std::tuple<decltype(self.components_[ndx])>{self.components_[ndx]};
  }

  // Swap element at `ndx` with the last element and pop. Updates the
  // swapped-in entity's registry location.
  bool do_swap_and_pop(size_type ndx) {
    assert(size());
    const auto last = static_cast<size_type>(components_.size() - 1);
    if (ndx != last) {
      std::swap(components_[ndx], components_[last]);
      std::swap(ids_[ndx], ids_[last]);
      // Update the swapped-in entity's index in the registry.
      if (registry_) registry_->set_location(ids_[ndx], {store_id_, ndx});
    }
    components_.pop_back();
    ids_.pop_back();
    return true;
  }

  // Clear all component data (called by
  // `archetype_storage_base::do_drop_all`).
  bool do_clear_storage() {
    components_.clear();
    return true;
  }

  // Roll back component storage to `new_size` (called by `add_guard` on
  // exception).
  bool do_resize_storage(size_type new_size) {
    components_.resize(new_size);
    return true;
  }

#pragma endregion
#pragma region Data members
private:
  std::vector<C, component_allocator_type> components_;

#pragma endregion
};

#pragma endregion
}}} // namespace corvid::ecs::mono_archetype_storages
