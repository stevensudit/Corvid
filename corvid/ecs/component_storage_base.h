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
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "component_index_policies.h"
#include "entity_registry.h"

// Forward declaration for friendship; defined in "component_scene.h".
namespace corvid { inline namespace ecs { inline namespace component_scenes {
class component_scene_base;
}}} // namespace corvid::ecs::component_scenes

namespace corvid { inline namespace ecs {
inline namespace component_storage_bases {

// CRTP base class for component-model entity storages.
//
// Mirrors `archetype_storage_base` structurally but uses a policy-based
// reverse index (entity_id -> packed_ndx) instead of the registry location
// field. The registry is used only for ID validity checks and bitmap updates.
//
// In the component model, an entity may occupy multiple storages
// simultaneously. The registry's `fixed_bitset` presence bitmap (one bit per
// `store_id_t`) tracks which storages hold each entity.
// `component_storage_base` sets and clears its bit in that bitmap on add and
// remove.
//
// No staging requirement: `add(id, ...)` accepts any valid entity, including
// those already in other storages. An entity enters staging (`store_id_t{0}`
// bit set) when removed from its last storage with `removal_mode::preserve`.
//
// Required customization points in `CHILD` (may be private; befriend `base_t`
// and `typename base_t::add_guard`):
//   `template<typename... Args>
//         void do_add_components(Args&&... args);`
//   `void do_swap_and_pop(size_type ndx);`  -- swaps component arrays only;
//         the base handles `ids_` and `reverse_index_`
//   `void do_clear_storage();`
//   `void do_resize_storage(size_type new_size);`
//
// Template parameters:
//   CHILD - The concrete derived class (CRTP).
//   REG   - `entity_registry` instantiation; must be a component-mode registry
//           (`is_component_v == true`, i.e., `OWN_COUNT >= 2`).
//   IDX   - Reverse-index policy (e.g., `flat_sparse_index<id_t>`).
template<typename CHILD, typename REG, typename IDX>
class component_storage_base {
public:
  static_assert(REG::is_component_v,
      "component_storage_base requires a component-mode registry "
      "(OWN_COUNT must be >= 2)");

  using derived_t = CHILD;
  using registry_t = REG;
  using id_t = registry_t::id_t;
  using handle_t = registry_t::handle_t;
  using size_type = registry_t::size_type;
  using store_id_t = registry_t::store_id_t;
  using metadata_t = registry_t::metadata_t;
  using allocator_type = registry_t::allocator_type;
  using id_allocator_t =
      std::allocator_traits<allocator_type>::template rebind_alloc<id_t>;
  using id_vector_t = std::vector<id_t, id_allocator_t>;
  using index_t = IDX;

  // Check whether entity `id` is currently in this storage (via bitmap).
  [[nodiscard]] bool contains(id_t id) const {
    if (!registry_->is_valid(id)) return false;
    return registry_->is_in_location(id, store_id_);
  }

  // Check whether entity referenced by `handle` is in this storage.
  [[nodiscard]] bool contains(handle_t handle) const {
    if (!registry_->is_valid(handle)) return false;
    return contains(handle.id());
  }

  // Return the number of entities in this storage.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(ids_.size());
  }

  // Return whether the storage is empty.
  [[nodiscard]] bool empty() const noexcept { return ids_.empty(); }

  // Return a read-only view of all entity IDs currently in this storage.
  [[nodiscard]] std::span<const id_t> entity_ids() const noexcept {
    return ids_;
  }

  // Expose this storage's `store_id_t`.
  [[nodiscard]] store_id_t store_id() const noexcept { return store_id_; }

  // Return the maximum number of entities allowed in this storage.
  [[nodiscard]] size_type limit() const noexcept { return limit_; }

  // Set a new entity limit. Returns false if the current size exceeds the new
  // limit.
  [[nodiscard]] bool set_limit(size_type new_limit) {
    if (new_limit < ids_.size()) return false;
    limit_ = new_limit;
    return true;
  }

  // Remove entity from this storage. Entity remains alive: moved to staging
  // if this was its only storage, otherwise remains in its other storages.
  // Returns false if entity is invalid or not in this storage.
  bool remove(id_t id) { return do_remove_erase(id, removal_mode::preserve); }

  // Remove entity by handle. Returns false if handle is invalid or stale.
  bool remove(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), removal_mode::preserve);
  }

  // Remove all entities from this storage. Entities remain alive (staging or
  // other storages).
  void remove_all() { do_remove_erase_all(removal_mode::preserve); }

  // Remove entity from this storage. If this was its last storage, the entity
  // is destroyed in the registry. Returns false if entity is not in this
  // storage. Note: entity may still be alive in other storages on success.
  bool erase(id_t id) { return do_remove_erase(id, removal_mode::remove); }

  // Erase by handle. Returns false if handle is invalid or stale.
  bool erase(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), removal_mode::remove);
  }

  // Remove all entities from this storage. Entities with no remaining
  // storages are destroyed in the registry.
  void clear() { do_remove_erase_all(removal_mode::remove); }

  // Insert component data for an entity. Entity must be valid and not already
  // in this storage. May be in other storages (component model allows this).
  // Returns false if entity is invalid, already in this storage, or at limit.
  // The pre-extension of the reverse index happens before the `add_guard` so
  // that if it throws nothing else has been modified.
  template<typename... Args>
  [[nodiscard]] bool add(id_t id, Args&&... args) {
    if (!registry_->is_valid(id)) return false;
    if (registry_->is_in_location(id, store_id_)) return false;
    const auto ndx = size();
    if (ndx >= limit_) return false;
    // Pre-extend the reverse index. If this throws, no state has changed.
    // If it succeeds but a later step throws, the stale entry is harmless:
    // `lookup` is only called after a bitmap check confirms membership.
    reverse_index_.insert(id, ndx);
    add_guard guard{derived()};
    derived().do_add_components(std::forward<Args>(args)...);
    ids_.push_back(id);
    registry_->add_location(id, store_id_);
    return guard.disarm();
  }

  // Insert by handle. Validates handle first.
  template<typename... Args>
  [[nodiscard]] bool add(handle_t handle, Args&&... args) {
    if (!registry_->is_valid(handle)) return false;
    return add(handle.id(), std::forward<Args>(args)...);
  }

  // Create a new entity in the registry and insert it into this storage.
  // Returns a valid handle on success, or an invalid handle if the registry
  // refused creation or the storage limit would be exceeded.
  template<typename... Args>
  [[nodiscard]] handle_t add_new(const metadata_t& metadata, Args&&... args) {
    auto owner = registry_->create_owner(metadata);
    if (!owner || !add(owner.id(), std::forward<Args>(args)...)) return {};
    return owner.release();
  }

  // RAII guard for `add()`: captures `size()` on construction. If `disarm()`
  // is not called before destruction, rolls `ids_` and the derived storage
  // back to the saved size, providing strong exception safety. The
  // `reverse_index_` is NOT rolled back; any stale entry is harmless because
  // the bitmap bit is never set on the failure path.
  struct add_guard {
    explicit add_guard(derived_t& owner) noexcept
        : owner_{&owner}, saved_size_{owner.size()} {}
    add_guard(const add_guard&) = delete;
    add_guard& operator=(const add_guard&) = delete;

    // Disarm the guard (success path). Returns true for use in
    // `return guard.disarm()`.
    bool disarm() noexcept {
      saved_size_ = *id_t::invalid;
      return true;
    }

    ~add_guard() { // NOLINT(bugprone-exception-escape)
      if (saved_size_ == *id_t::invalid) return;
      owner_->ids_.resize(saved_size_);
      owner_->derived().do_resize_storage(saved_size_);
    }

  private:
    derived_t* owner_{};
    size_type saved_size_{};
  };

  component_storage_base(const component_storage_base&) = delete;
  component_storage_base& operator=(const component_storage_base&) = delete;

private:
  friend CHILD;

  component_storage_base() noexcept = default;

  // Move constructor: explicitly clears `other.ids_` after stealing, so
  // derived destructors calling `clear()` do not iterate stale IDs.
  component_storage_base(component_storage_base&& other) noexcept
      : registry_{other.registry_}, store_id_{other.store_id_},
        limit_{other.limit_}, ids_{std::move(other.ids_)},
        reverse_index_{std::move(other.reverse_index_)} {
    other.ids_.clear();
  }

protected:
  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
  component_storage_base(registry_t& registry, store_id_t store_id,
      size_type limit)
      : registry_{&registry}, store_id_{store_id}, limit_{limit},
        ids_{id_allocator_t{registry.get_allocator()}} {
    if (store_id == store_id_t::invalid || store_id == store_id_t{})
      throw std::invalid_argument("store_id must be a valid non-zero value");
  }

  component_storage_base& operator=(component_storage_base&& other) noexcept {
    if (this != &other) {
      registry_ = other.registry_;
      store_id_ = other.store_id_;
      limit_ = other.limit_;
      ids_ = std::move(other.ids_);
      other.ids_.clear();
      reverse_index_ = std::move(other.reverse_index_);
    }
    return *this;
  }

  ~component_storage_base() = default;

  // CRTP helpers.
  [[nodiscard]] derived_t& derived() noexcept {
    return static_cast<derived_t&>(*this);
  }
  [[nodiscard]] const derived_t& derived() const noexcept {
    return static_cast<const derived_t&>(*this);
  }

  // Swap all base-class members with `other`.
  void do_swap_base(component_storage_base& other) noexcept {
    using std::swap;
    swap(registry_, other.registry_);
    swap(store_id_, other.store_id_);
    swap(limit_, other.limit_);
    ids_.swap(other.ids_);
    swap(reverse_index_, other.reverse_index_);
  }

  // Data members.
  registry_t* registry_{nullptr};
  store_id_t store_id_{store_id_t::invalid};
  size_type limit_{*id_t::invalid};
  id_vector_t ids_{};
  index_t reverse_index_{};

  // Grant `component_scene_base` (and through it, `component_scene<>`) access
  // to `do_drop_all()`.
  friend class ::corvid::ecs::component_scene_base;

private:
  // Fast bulk-drop: clear component and ID vectors without touching the
  // registry. Only safe when the registry will be reset wholesale immediately
  // afterward (e.g. `component_scene::clear()`). Called via
  // `component_scene_base::storage_drop_all`.
  void do_drop_all() {
    ids_.clear();
    reverse_index_.clear();
    derived().do_clear_storage();
  }

  bool do_remove_erase(id_t id, removal_mode mode) {
    if (!contains(id)) return false;
    const auto ndx = reverse_index_.lookup(id);
    const auto last = size() - 1;
    if (ndx != last) {
      // Move the last entry into the vacated slot.
      ids_[ndx] = ids_[last];
      // Update the moved entity's reverse-index entry to reflect its new ndx.
      reverse_index_.update(ids_[ndx], ndx);
    }
    ids_.pop_back();
    reverse_index_.erase(id);
    derived().do_swap_and_pop(ndx);
    registry_->remove_location(id, store_id_, mode);
    return true;
  }

  void do_remove_erase_all(removal_mode mode) {
    for (const auto id : ids_) registry_->remove_location(id, store_id_, mode);
    ids_.clear();
    reverse_index_.clear();
    derived().do_clear_storage();
  }
};

}}} // namespace corvid::ecs::component_storage_bases
