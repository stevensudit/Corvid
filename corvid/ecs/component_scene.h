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

#include <cstddef>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "component_storage_base.h"
#include "ecs_meta.h"
#include "entity_registry.h"

namespace corvid { inline namespace ecs { inline namespace component_scenes {

// Non-templated base for `component_scene<>`. Befriended by
// `component_storage_base` so that the protected `storage_drop_all` thunk can
// reach the otherwise-private `do_drop_all()` on any storage, without making
// that method public.
class component_scene_base {
protected:
  // Invoke `do_drop_all()` on storage `s`. Skips per-entity registry updates;
  // safe only when the registry will be reset wholesale immediately after.
  template<typename S>
  static void storage_drop_all(S& s) {
    s.do_drop_all();
  }
};

// Aggregates a component-mode `entity_registry` with a fixed, heterogeneous
// tuple of `component_storage_base`-derived storages and exposes a unified
// component-model ECS interface.
//
// Unlike `archetype_scene`, a single entity may occupy multiple storages
// simultaneously. Components are added and removed independently per storage;
// the registry tracks membership via a `fixed_bitset` presence bitmap (one bit
// per `store_id_t`).
//
// No staging reservation in the tuple: `store_id_t{1}` is the first storage.
// Entities begin in staging (bitmap bit 0 set) and leave staging when their
// first component is added via `store_entity` or `add_new`. They return to
// staging if all components are removed with `remove_entity`. They are
// destroyed (completely removed from the registry) only by `erase_entity`.
//
// All `STORES` must be `component_storage_base`-derived types sharing the same
// `REG`. `REG` must be a component-mode registry (`is_component_v == true`).
// At most `*store_id_t::invalid - 1` storages are supported (`store_id_t{0}`
// is reserved for staging, so store IDs 1..`*store_id_t::invalid - 1` are
// available for storages).
//
// Construction creates all storages with unlimited capacity. To restrict a
// specific storage, call `storage<store_id_t{N}>().set_limit(n)` after
// construction.
//
// Template parameters:
//   REG    - Shared component-mode `entity_registry` specialization.
//   STORES - Fully-typed `component_storage`-family specializations,
//            all using `REG`.
template<typename REG, typename... STORES>
class component_scene: public component_scene_base {
public:
  using registry_t = REG;
  using storage_ts = std::tuple<STORES...>;
  using storage_tuple_t = std::tuple<std::monostate, STORES...>;
  using id_t = registry_t::id_t;
  using handle_t = registry_t::handle_t;
  using size_type = registry_t::size_type;
  using store_id_t = registry_t::store_id_t;
  using metadata_t = registry_t::metadata_t;
  using allocator_type = registry_t::allocator_type;

  static constexpr size_t storage_count_v = sizeof...(STORES);

  static_assert(registry_t::is_component_v,
      "component_scene requires a component-mode registry "
      "(OWN_COUNT must be >= 2)");
  static_assert(sizeof...(STORES) >= 1,
      "component_scene requires at least one storage");
  static_assert(
      (std::is_same_v<registry_t, typename STORES::registry_t> && ...),
      "all STORES must use the same registry_t");
  // Defensive: the largest store_id assigned is `storage_count_v`. It must not
  // reach the sentinel.
  static_assert(storage_count_v < *store_id_t::invalid,
      "too many STORES: store_id_t would overflow into the invalid sentinel");
  // Defensive: `add_location`/`remove_location` assert `*sid < OWN_COUNT`
  // (i.e., `< bitmap_bits_v`). The largest assigned `store_id` is
  // `storage_count_v`, so it must be strictly less than the bitmap size.
  static_assert(storage_count_v < registry_t::bitmap_bits_v,
      "too many STORES: storage_count_v must be < OWN_COUNT (bitmap_bits_v)");

  // Type of the storage with the given `store_id`. `std::monostate` occupies
  // index 0 so that `*SID` equals the tuple index directly.
  template<store_id_t SID>
  using storage_t = std::tuple_element_t<*SID, storage_tuple_t>;

  // Construct with unlimited, unbound storages. Each storage is bound to this
  // scene's registry and assigned `store_id_t{N}` for 1-based index `N`. An
  // optional allocator propagates to the registry.
  explicit component_scene(const allocator_type& alloc = allocator_type{})
      : registry_{alloc},
        storages_{make_storages(std::index_sequence_for<STORES...>{})} {}

  component_scene(const component_scene&) = delete;
  component_scene(component_scene&&) = delete;
  component_scene& operator=(const component_scene&) = delete;
  component_scene& operator=(component_scene&&) = delete;

  ~component_scene() { clear(); }

  // Registry access.
  [[nodiscard]] decltype(auto) registry(this auto& self) noexcept {
    return (self.registry_);
  }

  // Access the storage with the given `store_id` by mutable or const
  // reference.
  template<store_id_t SID>
  [[nodiscard]] decltype(auto) storage(this auto& self) noexcept {
    return (std::get<*SID>(self.storages_));
  }

  // Access a storage with the given type by mutable or const reference.
  template<typename STORAGE>
  [[nodiscard]] decltype(auto) storage(this auto& self) noexcept {
    using storage_type = std::remove_cvref_t<STORAGE>;
    return (std::get<storage_type>(self.storages_));
  }

  // Create a new entity in staging. Returns its handle, or an invalid handle
  // on failure (e.g., registry at ID limit).
  [[nodiscard]] handle_t stage_new_entity(const metadata_t& metadata = {}) {
    return registry_.create_handle({}, metadata);
  }

  // Insert an existing entity into storage `SID`. The entity must be valid
  // and not already present in that storage. Trailing components may be
  // omitted and will be default-constructed. Returns false if the entity is
  // invalid, already in `SID`, or if the storage is at its limit. Unlike
  // `archetype_scene::store_entity`, the entity need not be in staging -- it
  // may already occupy other storages.
  template<store_id_t SID, typename... Args>
  [[nodiscard]] bool store_entity(id_t id, Args&&... args) {
    if (!registry_.is_valid(id)) return false;
    return storage<SID>().add(id, std::forward<Args>(args)...);
  }

  // Insert an existing entity (by handle) into storage `SID`. Validates the
  // handle first.
  template<store_id_t SID, typename... Args>
  [[nodiscard]] bool store_entity(handle_t handle, Args&&... args) {
    if (!registry_.is_valid(handle)) return false;
    return store_entity<SID>(handle.id(), std::forward<Args>(args)...);
  }

  // Remove entity from storage `SID` only. The entity remains alive in the
  // registry and any other storages it occupies. If this is its last storage,
  // the entity returns to staging. Returns false if the entity is invalid or
  // not in storage `SID`.
  template<store_id_t SID>
  bool remove_entity(id_t id) {
    if (!registry_.is_valid(id)) return false;
    return storage<SID>().remove(id);
  }

  // Remove entity from storage `SID` by handle. Validates the handle first.
  template<store_id_t SID>
  bool remove_entity(handle_t handle) {
    if (!registry_.is_valid(handle)) return false;
    return remove_entity<SID>(handle.id());
  }

  // Remove entity from all storages without destroying it in the registry.
  // The entity is returned to staging. Returns false if the entity is invalid.
  [[nodiscard]] bool restage_entity(id_t id) {
    if (!registry_.is_valid(id)) return false;
    const auto location = registry_.get_location(id);
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      ((location[store_id_t{
            static_cast<std::underlying_type_t<store_id_t>>(Is + 1)}]
               ? std::get<Is + 1>(storages_).remove(id)
               : false),
          ...);
    }(std::make_index_sequence<storage_count_v>{});
    return true;
  }

  // Remove entity from all storages by handle. Validates the handle first.
  [[nodiscard]] bool restage_entity(handle_t handle) {
    if (!registry_.is_valid(handle)) return false;
    return restage_entity(handle.id());
  }

  // Erase entity: remove from all storages it currently occupies, then
  // destroy it in the registry. Sets `id` to `id_t::invalid`. Returns false
  // if the entity is already invalid.
  [[nodiscard]] bool erase_entity(id_t& id) {
    const auto old_id = id;
    id = id_t::invalid;
    if (!registry_.is_valid(old_id)) return false;
    // Remove from every storage (preserve mode so each storage's swap-and-pop
    // runs cleanly; the entity stays alive until the registry erase below).
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      (std::get<Is + 1>(storages_).remove(old_id), ...);
    }(std::make_index_sequence<storage_count_v>{});
    // Entity is now in staging. Destroy it.
    registry_.erase(old_id);
    return true;
  }

  // Erase entity by handle. Resets `handle` to an invalid state. Returns false
  // if the handle is invalid or stale.
  [[nodiscard]] bool erase_entity(handle_t& handle) {
    const auto old_handle = handle;
    handle = handle_t{};
    if (!registry_.is_valid(old_handle)) return false;
    auto id = old_handle.id();
    if (!erase_entity(id)) return false;
    return true;
  }

  // Erase all staged entities (those with no components in any storage).
  // Returns the count erased. Useful for cleaning up entities that were
  // removed from all their storages via `remove_entity`.
  size_type erase_staged_entities() {
    return registry_.erase_if([](auto, const auto& rec) {
      return rec.location.contains(store_id_t{});
    });
  }

  // Return the total number of living entities (including those in staging).
  [[nodiscard]] size_type size() const noexcept { return registry_.size(); }

  // Return true if there are no living entities.
  [[nodiscard]] bool empty() const noexcept { return registry_.size() == 0; }

  // Call `fn(id, std::tuple<Cs&...>)` for each entity simultaneously present
  // in all component storages for `Cs...`. At runtime, the storage with the
  // fewest entities is chosen as the primary to minimize outer-loop
  // iterations; entities lacking any other required component are skipped via
  // a single `is_subset_of` check against the registry bitmap. `fn` must
  // return `bool`: `true` continues, `false` stops early. Deduces `const` from
  // the scene: on a const scene, component references are `const Cs&...`.
  //
  // Every `C` in `Cs...` must be the `component_t` of exactly one storage in
  // `STORES`.
  //
  // Fn shape: `(id_t, std::tuple<Cs&...>) -> bool`.
  template<typename... Cs>
  void for_each(this auto& self, auto&& fn) {
    static_assert(sizeof...(Cs) >= 1,
        "`for_each` requires at least one component type");
    const auto target_mask = self.template make_target_mask<Cs...>();
    const auto primary_ids = self.template find_primary_ids<Cs...>();
    for (const id_t id : primary_ids) {
      if (!target_mask.is_subset_of(self.registry_.get_location(id))) continue;
      if (!fn(id,
              std::forward_as_tuple(self.template get_component<Cs>(id)...)))
        return;
    }
  }

  // Erase all entities in all storages and in staging.
  //
  // Release path (default): drops storage vectors without per-entity registry
  // updates, then resets the registry wholesale. Invalidates all outstanding
  // generation counters. O(S) in the number of storages.
  //
  // Preserve path: erases entities one by one via each storage's `clear()`,
  // preserving generation counter validity. O(N) in entities.
  void clear(deallocation_policy policy = deallocation_policy::release) {
    if (policy == deallocation_policy::release) {
      [&]<size_t... Is>(std::index_sequence<Is...>) {
        (storage_drop_all(std::get<Is + 1>(storages_)), ...);
      }(std::make_index_sequence<storage_count_v>{});
      registry_.clear(deallocation_policy::release);
    } else {
      [&]<size_t... Is>(std::index_sequence<Is...>) {
        (std::get<Is + 1>(storages_).clear(), ...);
      }(std::make_index_sequence<storage_count_v>{});
      erase_staged_entities();
    }
  }

private:
  // Return a mutable or const reference to component `C` for entity `id`,
  // propagating the scene's constness.
  template<typename C>
  [[nodiscard]] decltype(auto)
  get_component(this auto& self, id_t id) noexcept {
    constexpr size_t idx = find_component_storage_index_v<C, STORES...>;
    auto& st = std::get<idx + 1>(self.storages_);
    if constexpr (std::is_const_v<std::remove_reference_t<decltype(st)>>)
      return st[id].value;
    else
      return st[id];
  }

  // Build a store-ID bitmask with one bit set for each component in `Cs...`.
  template<typename... Cs>
  [[nodiscard]] registry_t::store_id_set_t make_target_mask() const noexcept {
    typename registry_t::store_id_set_t mask{};
    auto set_bit = [&]<typename C>() {
      constexpr size_t idx = find_component_storage_index_v<C, STORES...>;
      mask[std::get<idx + 1>(storages_).store_id()] = true;
    };
    (set_bit.template operator()<Cs>(), ...);
    return mask;
  }

  // Return the `entity_ids()` span of the smallest storage among `Cs...`.
  template<typename... Cs>
  [[nodiscard]] std::span<const id_t> find_primary_ids() const noexcept {
    using first_c = std::tuple_element_t<0, std::tuple<Cs...>>;
    constexpr size_t first_idx =
        find_component_storage_index_v<first_c, STORES...>;
    std::span<const id_t> primary =
        std::get<first_idx + 1>(storages_).entity_ids();
    auto check_smaller = [&]<typename C>() {
      constexpr size_t idx = find_component_storage_index_v<C, STORES...>;
      const auto ids = std::get<idx + 1>(storages_).entity_ids();
      if (ids.size() < primary.size()) primary = ids;
    };
    (check_smaller.template operator()<Cs>(), ...);
    return primary;
  }

  // Construct all storages. `std::monostate` occupies index 0 so that each
  // storage's `store_id_t{N}` equals its tuple index N directly.
  template<size_t... Is>
  storage_tuple_t make_storages(std::index_sequence<Is...>) {
    return std::make_tuple(std::monostate{},
        STORES{registry_, store_id_t{Is + 1}}...);
  }

  // `registry_` must be declared before `storages_` because each storage
  // holds a pointer to it (initialized in `make_storages`).
  registry_t registry_;
  storage_tuple_t storages_;
};

}}} // namespace corvid::ecs::component_scenes
