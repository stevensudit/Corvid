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
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "ecs_meta.h"
#include "entity_registry.h"

namespace corvid { inline namespace ecs { inline namespace archetype_scenes {

// Non-templated base for `archetype_scene<>`. Befriended by `storage_base` so
// that the protected `storage_drop_all` thunk can reach the otherwise-private
// `do_drop_all()` on any storage, without making that method public.
class archetype_scene_base {
protected:
  // Invoke `do_drop_all()` on storage `s`. Skips per-entity registry updates;
  // safe only when the registry will be reset wholesale immediately after.
  template<typename S>
  static void storage_drop_all(S& s) {
    s.do_drop_all();
  }
};

// Aggregates an `entity_registry` with a fixed, heterogeneous tuple of entity
// data storages (`archetype_storage` or `chunked_archetype_storage` or
// `mono_archetype_storage`) and exposes a unified ECS interface.
//
// Each of these is identified by a `store_id_t`. The value of `store_id_t{0}`
// is reserved for entities in the staging state: existing in the registry but
// not currently in any storage. In other words, `store_id_t{0}` is
// conceptually the entity registry itself.
//
// Physically, the storages are stored in a tuple, and their `store_id` values
// are assigned sequentially starting from 1 in the order they appear in the
// template parameter pack. At index 0 is a `std::monostate` placeholder, once
// again representing staging in the entity registry.
//
// All `storage_ts` must be fully-typed storage specializations (e.g.,
// `archetype_storage<registry_t, tuple<Pos, Vel>>`) sharing the same
// `registry_t` type. At most `*store_id_t::invalid - 1` storages are
// supported. It is helpful if each type is distinct, so you can use the `TAG`
// specialization to achieve this.
//
// Construction creates all storages with unlimited capacity. To restrict a
// specific storage call `storage<store_id_t{N}>().set_limit(n)` after
// construction, and `storage<store_id_t{N}>().reserve(n)` for up-front
// allocation.
//
// TODO: Add a "pass in pre-constructed storages" constructor once the
//       store_id alignment problem (ensuring sequential IDs match tuple
//       positions) is solved cleanly.
//
// TODO: Add `for_each<C>()` to iterate across all storages that contain
//       component type `C` at compile time (multi-storage view).
//
// Template parameters:
//   REG      - Shared `entity_registry` specialization.
//   STORES   - Fully-typed storage specializations, all using `REG`.
template<typename REG, typename... STORES>
class archetype_scene: public archetype_scene_base {
public:
  using registry_t = REG;
  using storage_ts = std::tuple<STORES...>;
  using storage_tuple_t = std::tuple<std::monostate, STORES...>;
  using id_t = registry_t::id_t;
  using handle_t = registry_t::handle_t;
  using size_type = registry_t::size_type;
  using store_id_t = registry_t::store_id_t;
  using location_t = registry_t::location_t;
  using metadata_t = registry_t::metadata_t;
  using allocator_type = registry_t::allocator_type;

  static constexpr size_t storage_count_v = sizeof...(STORES);

  static_assert(sizeof...(STORES) >= 1,
      "archetype_scene requires at least one storage");
  static_assert(
      (std::is_same_v<registry_t, typename STORES::registry_t> && ...),
      "all storage_ts must use the same Registry type");
  // Defensive: the largest store_id assigned is `storage_count_v`. It must not
  // reach the sentinel. In practice store_id_t is at least a byte wide, so
  // this only fires if someone tries more than 253 (or 65534, etc.)
  // archetypes.
  static_assert(storage_count_v < *store_id_t::invalid,
      "too many storage_ts: store_id_t would overflow into the invalid "
      "sentinel");

  // Type of the storage with the given `store_id`.
  template<store_id_t SID>
  using storage_t = std::tuple_element_t<*SID, storage_tuple_t>;

  // Construct with unlimited, unbound storages. Each storage is bound to
  // this scene's registry and assigned `store_id_t{N}` for 1-based index N.
  // An optional allocator propagates to the registry; all storage allocations
  // derive from it via `registry_.get_allocator()`.
  explicit archetype_scene(const allocator_type& alloc = allocator_type{})
      : registry_{alloc},
        storages_{make_storages(std::index_sequence_for<STORES...>{})} {}

  archetype_scene(const archetype_scene&) = delete;
  archetype_scene(archetype_scene&&) = delete;
  archetype_scene& operator=(const archetype_scene&) = delete;
  archetype_scene& operator=(archetype_scene&&) = delete;

  ~archetype_scene() { clear(); }

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

  // Access a storage with the given storage type by mutable or const
  // reference.
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

  // Create a new default-valued entity in the storage selected at runtime by
  // `store_id`. Returns an invalid handle if `store_id` does not match a
  // storage or insertion fails.
  [[nodiscard]] handle_t
  store_new_entity(store_id_t store_id, const metadata_t& metadata = {}) {
    return dispatch_storage<handle_t>(
        store_id, [&](auto& s) -> handle_t { return s.add_new(metadata); },
        handle_t{});
  }

  // Create a new entity and insert it into the storage with the given
  // `store_id` in one step. Returns a valid handle on success, or an invalid
  // handle if the registry refused creation or the storage limit would be
  // exceeded.
  template<store_id_t SID, typename... Args>
  [[nodiscard]] handle_t
  store_new_entity(const metadata_t& metadata, Args&&... args) {
    return storage<SID>().add_new(metadata, std::forward<Args>(args)...);
  }

  // Create a new entity in the storage uniquely identified by the type of
  // `obj`. `T` must be the `tuple_t` of exactly one storage in this scene; if
  // two or more storages share the same `tuple_t`, use the
  // `store_new_entity<SID>()` overload to disambiguate. The component tuple is
  // unpacked into the storage. Returns a valid handle on success, or an
  // invalid handle if the registry refused creation or the storage limit would
  // be exceeded.
  template<typename T>
  [[nodiscard]] handle_t
  store_new_entity(const metadata_t& metadata, T&& obj) {
    using U = std::remove_cvref_t<T>;
    static_assert(count_stores_with_tuple_v<U> == 1,
        "store_new_entity: T must be the `tuple_t` of exactly one storage; "
        "use store_new_entity<SID>() to disambiguate");
    constexpr auto SID =
        find_sid_for_tuple<U>(std::index_sequence_for<STORES...>{});
    return std::apply(
        [&](auto&&... cs) {
          return storage<SID>().add_new(metadata,
              std::forward<decltype(cs)>(cs)...);
        },
        std::forward<T>(obj));
  }

  // Insert an already-staged entity into a storage selected at runtime by
  // `store_id`. Returns false if `store_id` does not match a storage, the
  // entity is not in staging, or insertion fails (e.g., storage at limit).
  // ID must be valid.
  [[nodiscard]] bool store_entity(id_t id, store_id_t store_id) {
    assert(registry_.is_valid(id));
    return dispatch_storage<bool>(
        store_id, [&](auto& s) { return s.add(id); }, false);
  }

  // Insert an already-staged entity into the storage with the given `store_id`
  // at compile time. Trailing components may be omitted and will be default-
  // constructed. Returns false if the entity is not in staging or insertion
  // fails. ID must be valid.
  template<store_id_t SID, typename... Args>
  [[nodiscard]] bool store_entity(id_t id, Args&&... args) {
    assert(registry_.is_valid(id));
    return storage<SID>().add(id, std::forward<Args>(args)...);
  }

  // Insert an already-staged entity into the storage uniquely identified by
  // the type of `obj`. `T` must be the `tuple_t` of exactly one storage in
  // this scene; use `store_entity<SID>()` to disambiguate when two storages
  // share the same `tuple_t`. The component tuple is unpacked into the
  // storage. Returns false if the entity is not in staging or insertion fails.
  // ID must be valid.
  template<typename T>
  [[nodiscard]] bool store_entity(id_t id, T&& obj) {
    using U = std::remove_cvref_t<T>;
    static_assert(count_stores_with_tuple_v<U> == 1,
        "store_entity: T must be the `tuple_t` of exactly one storage; "
        "use store_entity<SID>() to disambiguate");
    assert(registry_.is_valid(id));
    constexpr auto SID =
        find_sid_for_tuple<U>(std::index_sequence_for<STORES...>{});
    return std::apply(
        [&](auto&&... cs) {
          return storage<SID>().add(id, std::forward<decltype(cs)>(cs)...);
        },
        std::forward<T>(obj));
  }

  // Insert an already-staged entity (by handle) into a storage selected at
  // runtime by `store_id`. Returns false if the handle is invalid or stale,
  // the entity is not in staging, or insertion fails.
  [[nodiscard]] bool store_entity(handle_t handle, store_id_t store_id) {
    if (!registry_.is_valid(handle)) return false;
    return store_entity(handle.id(), store_id);
  }

  // Insert an already-staged entity (by handle) into the storage with the
  // given `store_id` at compile time. Returns false if the handle is invalid
  // or stale, the entity is not in staging, or insertion fails.
  template<store_id_t SID, typename... Args>
  [[nodiscard]] bool store_entity(handle_t handle, Args&&... args) {
    if (!registry_.is_valid(handle)) return false;
    return store_entity<SID>(handle.id(), std::forward<Args>(args)...);
  }

  // Insert an already-staged entity (by handle) into the storage uniquely
  // identified by the type of `obj`. Returns false if the handle is invalid
  // or stale, the entity is not in staging, or insertion fails.
  template<typename T>
  [[nodiscard]] bool store_entity(handle_t handle, T&& obj) {
    if (!registry_.is_valid(handle)) return false;
    return store_entity(handle.id(), std::forward<T>(obj));
  }

  // Erase entity from whatever storage it occupies (or from staging if it
  // has not been placed in any storage). Invalidates `id`. ID must be valid.
  [[nodiscard]] bool erase_entity(id_t& id) {
    assert(registry_.is_valid(id));
    const auto old_id = id;
    id = id_t::invalid;
    const auto store_id = registry_.get_location(old_id).store_id;
    if (store_id == store_id_t{}) {
      if (!registry_.erase(old_id)) return false;
      return true;
    }
    return dispatch_storage<bool>(
        store_id,
        [&](auto& s) {
          auto temp_id = old_id;
          return s.erase(temp_id);
        },
        false);
  }

  // Erase entity by handle. Invalidates `handle`. Returns false if the handle
  // is invalid or stale.
  [[nodiscard]] bool erase_entity(handle_t& handle) {
    const auto old_handle = handle;
    auto old_id = handle.id();
    handle = handle_t{};
    if (!registry_.is_valid(old_handle)) return false;
    if (!erase_entity(old_id)) return false;
    return true;
  }

  // Destroy all entities in staging (`store_id == store_id_t{0}`). Returns
  // the count erased. Useful for cleaning up entities stranded in staging
  // after failed migrations or other error paths. Runs in O(N) in the total
  // count of living entities.
  size_type erase_staged_entities() {
    if (registry_.size() == size()) return 0;
    return registry_.erase_if([](auto, const auto& rec) {
      return rec.location.contains(store_id_t{});
    });
  }

  // Move entity back to staging from whatever storage it occupies. Returns
  // success. If already staged, succeeds. ID must be valid.
  [[nodiscard]] bool remove_entity(id_t id) {
    assert(registry_.is_valid(id));
    const auto store_id = registry_.get_location(id).store_id;
    if (store_id == store_id_t{}) return true;
    return dispatch_storage<bool>(
        store_id, [&](auto& s) { return s.remove(id); }, false);
  }

  // Move entity back to staging by handle. Returns success.
  [[nodiscard]] bool remove_entity(handle_t handle) {
    if (!registry_.is_valid(handle)) return false;
    return remove_entity(handle.id());
  }

  // Migrate entity to storage `to` using a caller-supplied `build` function
  // that maps the source row to the target components. Both the source storage
  // (looked up from the registry) and the destination storage are dispatched
  // at runtime. This is linear for a small N.
  //
  // If `to` is already the entity's current storage, returns true immediately
  // without calling `build` or modifying any state.
  //
  // `build` receives a const `row_view` of the source entity (whose concrete
  // type depends on the source storage) and must return the target storage's
  // component tuple. It is called before the entity is removed from the
  // source, so the row is valid during the call.
  //
  // Returns false if the entity is staged, `to` is not a valid storage ID, or
  // the target storage rejects the insertion (e.g., at limit). On failure the
  // entity remains in its current storage or is stranded in staging.  ID must
  // be valid.
  //
  // Build shape: `(const auto& row) -> <destination component tuple>`.
  [[nodiscard]] bool migrate_entity(id_t id, store_id_t to, auto&& build) {
    assert(registry_.is_valid(id));
    const auto store_id = registry_.get_location(id).store_id;
    if (store_id == to) return true; // already there -- no-op
    return dispatch_storage<bool>(
        store_id,
        [&](auto& src) {
          return dispatch_storage<bool>(
              to,
              [&](auto& dst) {
                // The build result type must match dst's tuple_t. With
                // runtime dispatch every (src, dst) pair is instantiated,
                // so we guard mismatched combinations with `if constexpr`
                // (they become a runtime false return, never reached).
                using build_result_t = std::invoke_result_t<decltype(build),
                    decltype(std::as_const(src)[id])>;
                using dst_tuple_t =
                    std::remove_reference_t<decltype(dst)>::tuple_t;
                if constexpr (std::is_same_v<
                                  std::remove_cvref_t<build_result_t>,
                                  dst_tuple_t>)
                {
                  // Capture target components while the source row is still
                  // valid. Use `std::as_const` so all storage types return
                  // their const `row_view`.
                  auto components = build(std::as_const(src)[id]);
                  if (!src.remove(id)) return false;
                  return std::apply(
                      [&](auto&&... cs) {
                        return dst.add(id, std::forward<decltype(cs)>(cs)...);
                      },
                      std::move(components));
                } else {
                  return false; // build return type doesn't match dst
                }
              },
              false);
        },
        false);
  }

  // Overload taking a handle.
  [[nodiscard]] bool
  migrate_entity(handle_t handle, store_id_t to, auto&& build) {
    if (!registry_.is_valid(handle)) return false;
    return migrate_entity(handle.id(), to,
        std::forward<decltype(build)>(build));
  }

  // Migrate entity to storage `to`, automatically mapping components by type:
  // components present in both archetypes are copied; components present only
  // in the target are default-constructed.
  //
  // If `to` is already the entity's current storage, returns true immediately.
  // ID must be valid.
  //
  // This handles both promotion (target has more components than source) and
  // demotion (target has fewer). Components are matched by type; if a type
  // appears in both archetypes it is copied, otherwise it is default-
  // constructed in the target.
  [[nodiscard]] bool migrate_entity(id_t id, store_id_t to) {
    assert(registry_.is_valid(id));
    const auto store_id = registry_.get_location(id).store_id;
    if (store_id == to) return true; // already there -- no-op
    return dispatch_storage<bool>(
        store_id,
        [&](auto& src) {
          return dispatch_storage<bool>(
              to,
              [&](auto& dst) {
                using src_tuple_t =
                    std::remove_reference_t<decltype(src)>::tuple_t;
                using dst_tuple_t =
                    std::remove_reference_t<decltype(dst)>::tuple_t;
                auto components = [&]<size_t... Is>(
                                      std::index_sequence<Is...>) {
                  return dst_tuple_t{
                      get_or_default<std::tuple_element_t<Is, dst_tuple_t>,
                          src_tuple_t>(std::as_const(src)[id])...};
                }(std::make_index_sequence<std::tuple_size_v<dst_tuple_t>>{});
                if (!src.remove(id)) return false;
                return std::apply(
                    [&](auto&&... cs) {
                      return dst.add(id, std::forward<decltype(cs)>(cs)...);
                    },
                    std::move(components));
              },
              false);
        },
        false);
  }

  // Overload taking a handle.
  [[nodiscard]] bool migrate_entity(handle_t handle, store_id_t to) {
    if (!registry_.is_valid(handle)) return false;
    return migrate_entity(handle.id(), to);
  }

  // Return the total number of entities across all storages. Does not include
  // staged entities.
  [[nodiscard]] size_type size() const noexcept {
    return [&]<size_t... Is>(std::index_sequence<Is...>) {
      return (std::get<Is>(storages_).size() + ...);
    }(storage_indices());
  }

  // Return true if all storages are empty (staged entities are not counted).
  [[nodiscard]] bool empty() const noexcept { return size() == 0; }

  // Erase all entities in all storages and in staging. After this call the
  // registry and all storages are empty. When `policy` is release, uses the
  // fast path that drops storage vectors and resets the registry wholesale,
  // invalidating all generation counters. When `policy` is preserve, erases
  // entities one by one, which updates the registry and allows generation
  // counters to survive, but is slower. O(S) vs O(N).
  //
  // Release path: drops storage vectors without per-entity registry updates,
  // then resets the registry wholesale. This invalidates all outstanding
  // generation counters, but that is acceptable since every entity is
  // destroyed. O(S) in the number of storages, not O(N) in entities.
  void clear(deallocation_policy policy = deallocation_policy::release) {
    if (policy == deallocation_policy::release) {
      [&]<size_t... Is>(std::index_sequence<Is...>) {
        (storage_drop_all(std::get<Is>(storages_)), ...);
      }(storage_indices());
      registry_.clear(deallocation_policy::release);
    } else {
      // Slow path: erase entities one by one, which updates the registry and
      // allows generation counters to survive. O(N) in entities.
      [&]<size_t... Is>(std::index_sequence<Is...>) {
        (std::get<Is>(storages_).clear(), ...);
      }(storage_indices());
      erase_staged_entities();
    }
  }

private:
  // Count how many storages expose a `tuple_t` equal to `T`.
  template<typename T>
  static constexpr size_t count_stores_with_tuple_v =
      (std::is_same_v<T, typename STORES::tuple_t> + ...);

  // Find the `store_id_t` of the first storage whose `tuple_t` equals `T`.
  // Caller must ensure exactly one match (guarded by
  // `count_stores_with_tuple_v`).
  template<typename T, size_t... Is>
  static constexpr store_id_t
  find_sid_for_tuple(std::index_sequence<Is...>) noexcept {
    store_id_t result = store_id_t::invalid;
    (void)((std::is_same_v<T, typename std::tuple_element_t<Is,
                                  std::tuple<STORES...>>::tuple_t>
                   ? (result = store_id_t{Is + 1}, true)
                   : false) ||
           ...);
    return result;
  }

  // Produces `std::index_sequence<Offset, Offset+1, ..., Offset+N-1>`.
  template<size_t Offset, size_t... Is>
  static constexpr auto make_offset_sequence(std::index_sequence<Is...>)
      -> std::index_sequence<(Is + Offset)...> {
    return {};
  }

  // Index sequence spanning all real storages: 1, 2, ..., `storage_count_v`.
  // Matches `store_id_t` values and tuple indices directly (monostate at 0).
  static constexpr auto storage_indices() {
    return make_offset_sequence<1>(
        std::make_index_sequence<storage_count_v>{});
  }

  // Construct all storages. `std::monostate` occupies index 0 so that each
  // storage's `store_id_t{N}` equals its tuple index N directly.
  template<size_t... Is>
  storage_tuple_t make_storages(std::index_sequence<Is...>) {
    return std::make_tuple(std::monostate{},
        STORES{registry_, store_id_t{Is + 1}}...);
  }

  // Invoke `f(storage)` on the storage whose assigned `store_id` matches
  // `store_id`. Returns `fallback` if no storage matches.
  //
  // The `||...` fold short-circuits after the first match, so `f` is called
  // exactly once.
  template<typename R, typename F>
  R dispatch_storage(store_id_t store_id, F&& f, R fallback) {
    R result = fallback;
    [&]<size_t... Is>(std::index_sequence<Is...>) {
      (void)((store_id == store_id_t{Is}
                     ? (result = f(std::get<Is>(storages_)), true)
                     : false) ||
             ...);
    }(storage_indices());
    return result;
  }

private:
  // `registry_` must be declared before `storages_` because each storage
  // holds a pointer to it (initialized in `make_storages`).
  registry_t registry_;
  storage_tuple_t storages_;
};

}}} // namespace corvid::ecs::archetype_scenes
