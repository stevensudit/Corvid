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

#include <cassert>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "archetype_storage_base.h"

namespace corvid { inline namespace ecs {
inline namespace mono_archetype_storages {

// Packed single-component storage with O(1) lookup through `entity_registry`.
//
// Maps entity IDs to densely-packed component records using swap-and-pop for
// removal. The entity registry's `location_t.ndx` stores each entity's index
// in this class's vector, enabling O(1) access by entity ID while
// centralizing the management of these IDs.
//
// Derives from `archetype_storage_base` with a single-element tuple. Provides
// a contiguous iterator (the underlying `components_` vector is a plain
// `std::vector`), a `row_view` with both `component<T>()` and implicit
// `const component_t&` conversion, and a component-first `erase_if`.
//
// Template parameters:
//  REG - `entity_registry` instantiation. Provides types.
//  C   - Component type. Must be trivially copyable.
//  TAG - Optional tag type (default: `void`). Use a distinct tag to create
//        multiple structurally-identical storages that are nevertheless
//        different types and can coexist in the same `archetype_scene<>`
//        tuple.
template<typename REG, typename C, typename TAG = void>
class mono_archetype_storage final
    : public archetype_storage_base<mono_archetype_storage<REG, C, TAG>, REG,
          std::tuple<C>> {
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

  static_assert(std::is_trivially_copyable_v<component_t>,
      "Component type must be trivially copyable");

  // Constructors.

  // Default-constructed instances can only be assigned to.
  mono_archetype_storage() noexcept = default;

  explicit mono_archetype_storage(registry_t& registry, store_id_t store_id,
      size_type limit = *id_t::invalid,
      allocation_policy policy = allocation_policy::lazy)
      : base_t{registry, store_id, limit},
        components_{component_allocator_type{registry.get_allocator()}} {
    if (policy == allocation_policy::eager && limit_ != *id_t::invalid)
      reserve(limit_);
  }

  mono_archetype_storage(mono_archetype_storage&&) noexcept = default;
  ~mono_archetype_storage() { clear(); }

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

  // Swap with another storage.
  void swap(mono_archetype_storage& other) noexcept {
    base_t::do_swap_base(other);
    components_.swap(other.components_);
  }

  friend void
  swap(mono_archetype_storage& lhs, mono_archetype_storage& rhs) noexcept {
    lhs.swap(rhs);
  }

  // Reduce memory usage to fit current size.
  void shrink_to_fit() {
    components_.shrink_to_fit();
    ids_.shrink_to_fit();
  }

  // Reserve space for at least `new_cap` components.
  void reserve(size_type new_cap) {
    components_.reserve(new_cap);
    ids_.reserve(new_cap);
  }

  // Add a component for a new entity, returning its handle or an invalid
  // handle on failure.
  [[nodiscard]] handle_t
  add_new(const component_t& component, const metadata_t& metadata = {}) {
    auto owner = registry_->create_owner(location_t{store_id_t{}}, metadata);
    if (!owner || !add(owner.id(), component)) return {};
    return owner.release();
  }

  // Metadata-first overload matching the archetype storage convention,
  // enabling use as a StorageSpec in `archetype_scene`.
  [[nodiscard]] handle_t add_new(const metadata_t& metadata,
      const component_t& component = component_t{}) {
    return add_new(component, metadata);
  }

  // Add a component for an entity. Returns success flag.
  //
  // ID must be valid. The entity's location must be `store_id_t{0}` (staging).
  // As a result of being added, the registry updates the entity's location to
  // this storage's `store_id_` and the correct `ndx`.
  [[nodiscard]] bool add(id_t id, const component_t& component) {
    const auto& loc = registry_->get_location(id);
    if (loc.store_id != store_id_t{}) return false;
    const auto ndx = size();
    if (ndx >= limit_) return false;
    typename base_t::add_guard guard{*this};
    components_.push_back(component);
    ids_.push_back(id);
    registry_->set_location(id, {store_id_, ndx});
    return guard.disarm();
  }

  // Add a component for an entity by handle. Returns success flag.
  [[nodiscard]] bool add(handle_t handle, const component_t& component) {
    if (!registry_->is_valid(handle)) return false;
    return add(handle.id(), component);
  }

  // Erase components for which `pred(component, id)` returns true. Returns
  // count erased.
  // Predicate shape: `(const component_t& comp, id_t id) -> bool`.
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

  // Read-only view of a single entity's row. Provides a `component<T>()`
  // accessor uniform with archetype storages (only valid for `T ==
  // component_t`), plus an implicit conversion to `const component_t&` for
  // backward compatibility.
  struct row_view {
    const component_t& value;
    id_t entity_id;

    [[nodiscard]] operator const component_t&() const noexcept {
      return value;
    }

    // Uniform accessor (component_t only).
    template<typename T>
    [[nodiscard]] const T& component() const noexcept {
      static_assert(std::is_same_v<T, component_t>,
          "mono_archetype_storage only has one component type");
      return value;
    }

    [[nodiscard]] id_t id() const noexcept { return entity_id; }
  };

  // Mutable access: returns `component_t&` directly.
  [[nodiscard]] component_t& operator[](id_t id) noexcept {
    assert(contains(id));
    return components_[registry_->get_location(id).ndx];
  }

  // Const access: returns `row_view` for uniform migrate-compatible access.
  [[nodiscard]] row_view operator[](id_t id) const noexcept {
    assert(contains(id));
    const auto ndx = registry_->get_location(id).ndx;
    return {components_[ndx], ids_[ndx]};
  }

  // Mutable access by entity ID, with checking.
  [[nodiscard]] component_t& at(id_t id) {
    if (!contains(id)) throw std::out_of_range("entity not in this storage");
    return components_[registry_->get_location(id).ndx];
  }

  // Const access by entity ID, with checking.
  [[nodiscard]] row_view at(id_t id) const {
    if (!contains(id)) throw std::out_of_range("entity not in this storage");
    const auto ndx = registry_->get_location(id).ndx;
    return {components_[ndx], ids_[ndx]};
  }

  // Access component by handle, with checking.
  [[nodiscard]] component_t& at(handle_t handle) {
    if (!contains(handle))
      throw std::invalid_argument(
          "invalid handle or entity not in this storage");
    return (*this)[handle.id()];
  }

  [[nodiscard]] row_view at(handle_t handle) const {
    if (!contains(handle))
      throw std::invalid_argument(
          "invalid handle or entity not in this storage");
    return (*this)[handle.id()];
  }

  // Contiguous iterator over components. Dereferencing yields a `component_t`
  // reference; `id()` returns the entity ID at the current position.
  template<access ACCESS>
  class iterator_t {
  public:
    static constexpr bool mutable_v = static_cast<bool>(ACCESS);
    using iterator_category = std::contiguous_iterator_tag;
    using iterator_concept = std::contiguous_iterator_tag;
    using value_type = component_t;
    using difference_type = std::ptrdiff_t;
    using reference =
        std::conditional_t<mutable_v, value_type&, const value_type&>;
    using pointer =
        std::conditional_t<mutable_v, value_type*, const value_type*>;
    using storage_ptr = std::conditional_t<mutable_v, mono_archetype_storage*,
        const mono_archetype_storage*>;

    iterator_t() = default;
    iterator_t(const iterator_t&) = default;
    iterator_t(iterator_t&&) = default;
    iterator_t& operator=(const iterator_t&) = default;
    iterator_t& operator=(iterator_t&&) = default;

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

    [[nodiscard]] bool operator==(const iterator_t& o) const {
      return ndx_ == o.ndx_;
    };
    [[nodiscard]] auto operator<=>(const iterator_t& o) const {
      return ndx_ <=> o.ndx_;
    }

  private:
    storage_ptr storage_{};
    size_type ndx_{};

    iterator_t(storage_ptr s, size_type ndx) : storage_{s}, ndx_{ndx} {}
    friend class mono_archetype_storage;
  };

  using iterator = iterator_t<access::as_mutable>;
  using const_iterator = iterator_t<access::as_const>;

  [[nodiscard]] iterator begin() noexcept { return {this, 0}; }
  [[nodiscard]] iterator end() noexcept { return {this, size()}; }
  [[nodiscard]] const_iterator begin() const noexcept { return {this, 0}; }
  [[nodiscard]] const_iterator end() const noexcept { return {this, size()}; }
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

private:
  using base_t::registry_;
  using base_t::store_id_;
  using base_t::limit_;
  using base_t::ids_;

  // Grant `archetype_storage_base` and its nested types access to the CRTP
  // customization points.
  friend base_t;
  friend base_t::add_guard;
  friend base_t::row_lens;
  friend base_t::row_view;

  // Append one component row (called by the base's `add(id_t, ...)`).
  template<typename... Args>
  void do_add_components(Args&&... args) {
    components_.push_back(std::forward<Args>(args)...);
  }

  // Access the component by type (called by `row_wrapper::component<C>()`
  // and `erase_if_component<C>()`).
  template<typename T>
  [[nodiscard]] decltype(auto)
  do_get_component(this auto& self, size_type ndx) noexcept {
    static_assert(std::is_same_v<T, component_t>,
        "mono_archetype_storage only has one component type");
    return self.components_[ndx];
  }

  // Access the component by zero-based tuple index (must be 0).
  template<size_t Index>
  [[nodiscard]] decltype(auto)
  do_get_component_by_index(this auto& self, size_type ndx) noexcept {
    static_assert(Index == 0,
        "mono_archetype_storage only has one component (index 0)");
    return self.components_[ndx];
  }

  // Return all components as a single-element tuple of references.
  [[nodiscard]] decltype(auto)
  do_make_components_tuple(this auto& self, size_type ndx) noexcept {
    return std::tuple<decltype(self.components_[ndx])>{self.components_[ndx]};
  }

  // Swap element at `ndx` with the last element and pop. Updates the
  // swapped-in entity's registry location.
  void do_swap_and_pop(size_type ndx) {
    assert(size());
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

  // Clear all component data (called by
  // `archetype_storage_base::do_drop_all`).
  void do_clear_storage() { components_.clear(); }

  // Roll back component storage to `new_size` (called by `add_guard` on
  // exception).
  void do_resize_storage(size_type new_size) { components_.resize(new_size); }

private:
  std::vector<C, component_allocator_type> components_;
};

}}} // namespace corvid::ecs::mono_archetype_storages
