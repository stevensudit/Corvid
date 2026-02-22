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
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "entity_registry.h"

// Forward declaration for friendship; defined in "scene.h".
namespace corvid { inline namespace ecs { inline namespace scenes {
class scene_base;
}}} // namespace corvid::ecs::scenes

namespace corvid { inline namespace ecs { inline namespace storage_bases {

// CRTP base class shared by `archetype_storage_base` and `component_storage`.
//
// Provides the registry-interaction plumbing common to all packed ECS
// storages: entity ID tracking, `entity_registry` pointer, `store_id`,
// capacity limit, and the core `remove`/`erase`/`clear` operations built on
// swap-and-pop.
//
// Required customization points in `CHILD` (may be private; befriend `base_t`
// and `typename base_t::add_guard`):
//   `void do_swap_and_pop(size_type ndx);`
//   `void do_clear_storage();`
//   `void do_resize_storage(size_type new_size);`
//
// Template parameters:
//   CHILD - The concrete derived class (CRTP).
//   REG   - `entity_registry` instantiation. Provides types.
template<typename CHILD, typename REG>
class storage_base {
public:
  using derived_t = CHILD;
  using registry_t = REG;
  using id_t = typename registry_t::id_t;
  using handle_t = typename registry_t::handle_t;
  using size_type = typename registry_t::size_type;
  using store_id_t = typename registry_t::store_id_t;
  using location_t = typename registry_t::location_t;
  using metadata_t = typename registry_t::metadata_t;
  using allocator_type = typename registry_t::allocator_type;
  using id_allocator_t = typename std::allocator_traits<
      allocator_type>::template rebind_alloc<id_t>;
  using id_vector_t = std::vector<id_t, id_allocator_t>;

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

  // Maximum number of entities allowed in this storage.
  [[nodiscard]] size_type limit() const noexcept { return limit_; }

  // Set a new entity limit. Returns true on success, false if the current size
  // exceeds the new limit.
  [[nodiscard]] bool set_limit(size_type new_limit) {
    if (new_limit < ids_.size()) return false;
    limit_ = new_limit;
    return true;
  }

  // Remove entity by ID, moving it back to staging. Returns success flag.
  bool remove(id_t id) { return do_remove_erase(id, store_id_t{}); }

  // Remove entity by handle, moving it back to staging. Returns success flag.
  bool remove(handle_t handle) {
    if (!registry_->is_valid(handle)) return false;
    return do_remove_erase(handle.id(), store_id_t{});
  }

  // Move all entities back to staging. Entities remain valid after this call.
  void remove_all() { do_remove_erase_all(store_id_t{}); }

  // Swap-and-pop the entity out of storage and destroy it in the registry.
  // Sets `id` to `id_t::invalid` on success. Returns success flag.
  bool erase(id_t& id) {
    if (!do_remove_erase(id, store_id_t::invalid)) return false;
    id = id_t::invalid;
    return true;
  }

  // Erase by handle; validates the handle first. Resets `handle` to an
  // invalid state on success. Returns success flag.
  bool erase(handle_t& handle) {
    if (!registry_->is_valid(handle)) return false;
    if (!do_remove_erase(handle.id(), store_id_t::invalid)) return false;
    handle = handle_t{};
    return true;
  }

  // Destroy all entities in the registry and empty the storage.
  void clear() { do_remove_erase_all(store_id_t::invalid); }

  // RAII guard for `add()`: captures `size()` on construction. If `disarm()`
  // is not called before destruction, rolls `ids_` and derived storage back to
  // the saved size, providing strong exception safety.
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

    ~add_guard() {
      if (saved_size_ == *id_t::invalid) return;
      owner_->ids_.resize(saved_size_);
      owner_->do_resize_storage(saved_size_);
    }

  private:
    derived_t* owner_{};
    size_type saved_size_{};
  };

protected:
  // Constructors are protected; only derived classes may construct.

  storage_base() = default;

  storage_base(registry_t& registry, store_id_t store_id, size_type limit)
      : registry_{&registry}, store_id_{store_id}, limit_{limit},
        ids_{id_allocator_t{registry.get_allocator()}} {
    if (store_id == store_id_t::invalid || store_id == store_id_t{})
      throw std::invalid_argument("store_id must be a valid non-zero value");
  }

  // Derived destructors call `clear()`, which unconditionally iterates `ids_`
  // and writes to the registry. The standard only guarantees a moved-from
  // `std::vector` is "valid but unspecified" — not necessarily empty — so
  // explicitly clearing `ids_` after the steal makes destruction safe.
  storage_base(const storage_base&) = delete;
  storage_base(storage_base&& other) noexcept
      : registry_{other.registry_}, store_id_{other.store_id_},
        limit_{other.limit_}, ids_{std::move(other.ids_)} {
    other.ids_.clear();
  }
  storage_base& operator=(const storage_base&) = delete;
  storage_base& operator=(storage_base&& other) noexcept {
    if (this != &other) {
      registry_ = other.registry_;
      store_id_ = other.store_id_;
      limit_ = other.limit_;
      ids_ = std::move(other.ids_);
      other.ids_.clear();
    }
    return *this;
  }

  ~storage_base() = default;

  // CRTP helpers.
  [[nodiscard]] derived_t& derived() noexcept {
    return static_cast<derived_t&>(*this);
  }
  [[nodiscard]] const derived_t& derived() const noexcept {
    return static_cast<const derived_t&>(*this);
  }

  // Swap all base-class members with `other`. `CHILD::swap()` should call
  // this, then swap its own component storage.
  void do_swap_base(storage_base& other) noexcept {
    using std::swap;
    swap(registry_, other.registry_);
    swap(store_id_, other.store_id_);
    swap(limit_, other.limit_);
    ids_.swap(other.ids_);
  }

  // Data members shared by all derived classes.
  registry_t* registry_{nullptr};
  store_id_t store_id_{store_id_t::invalid};
  size_type limit_{*id_t::invalid};
  id_vector_t ids_{};

  // Grant scene_base (and through it, scene<>) access to do_drop_all().
  friend class ::corvid::ecs::scene_base;

private:
  // Fast bulk-drop: clear component and ID vectors without touching the
  // registry. Only safe when the registry will be reset wholesale immediately
  // afterward (e.g. `scene::clear()`). Called via
  // `scene_base::storage_drop_all`.
  void do_drop_all() {
    ids_.clear();
    derived().do_clear_storage();
  }

  bool do_remove_erase(id_t id, store_id_t new_store_id) {
    if (!contains(id)) return false;
    derived().do_swap_and_pop(registry_->get_location(id).ndx);
    registry_->set_location(id, {new_store_id, *store_id_t::invalid});
    return true;
  }

  void do_remove_erase_all(store_id_t new_store_id) {
    for (const auto id : ids_) registry_->set_location(id, {new_store_id});
    do_drop_all();
  }
};

}}} // namespace corvid::ecs::storage_bases
