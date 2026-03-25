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
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../containers/fixed_bitset.h"
#include "../meta/maybe.h"
#include "entity_ids.h"
#include "id_container.h"

namespace corvid { inline namespace ecs { inline namespace entity_registries {

// Entity Component System: Entity Registry
//
// The entity registry owns IDs for entities of a particular entity type in the
// ECS system and tracks their location and metadata. Entities of different
// types are stored in instances specialized on different ID types.
//
// IDs are reused after being freed, but unless `GEN` is set to
// `generation_scheme::unversioned`, record handles have generation counters to
// detect this. Reuse is done in FIFO order to maximize the time before an ID
// is recycled, unless LIFO is selected.
//
// Entity records are not guaranteed to remain at fixed memory locations unless
// you set a limit and reserve space for that many entities up front. In this
// case, you may be able to avoid locking a mutex when looking up records that
// nobody else will be changing.
//
// Lookup by ID or handle is O(1), but unless you reserve space in advance,
// insertion is O(1) only when amortized, because it may trigger a resize.
// Erasure is O(1). Iteration is O(n) in the size of the underlying vector,
// which may be larger than the count of living entities, due to free records.
// In other words, records are not packed, though they are reused efficiently.
//
// Operations that take an ID expect it to be valid and generally will not
// check. Even if they do, they can't detect reuse. In contrast, operations
// that take a handle will check for validity and (when generation versioning
// is enabled) reuse. This means that, even when `GEN` is
// `generation_scheme::unversioned`, you can still get some additional safety
// by using handles instead of raw IDs.
//
// Template parameters:
//  T         - Per-entity metadata type stored alongside location and
//              generation data. Must be trivially copyable. Leave `void` for
//              no metadata.
//  EID       - Entity ID enum type, such as `entity_id_t`. Must be unsigned
//              with `invalid` defined as the maximum representable value.
//  SID       - Store ID enum type, such as `store_id_t`. In archetype mode,
//              identifies which storage an entity resides in. In component
//              mode, identifies all of the storages. Same constraints as EID.
//  GEN       - When `generation_scheme::versioned` (default), handles carry a
//              generation counter that detects ID reuse. When
//              `generation_scheme::unversioned`, handles are equivalent to
//              bare IDs but still trigger validity checks, albeit less
//              thorough ones.
//  OWN_COUNT - 1 selects archetype mode, where `record_t` stores a
//              `location_record{store_id, ndx}` for O(1) entity location
//              lookup. Any value >= 2 selects component mode, where
//              `record_t` stores a `location_record{store_ids,ndx}`, where
//              `store_ids` is a `fixed_bitset<OWN_COUNT>` presence bitmap.
//  REUSE     - `sequence_order::fifo` (default) or `sequence_order::lifo`,
//              controlling whether freed IDs are reused in first-in-first-out
//              or last-in-first-out order.
//  A         - Allocator for the metadata type. Rebound internally for
//              record storage.
template<typename T = void,
    sequence::SequentialEnum EID = id_enums::entity_id_t,
    sequence::SequentialEnum SID = id_enums::store_id_t,
    generation_scheme GEN = generation_scheme::versioned, size_t OWN_COUNT = 1,
    sequence_order REUSE = sequence_order::fifo, class A = std::allocator<T>>
class entity_registry {
public:
  static constexpr bool is_versioned_v = (GEN == generation_scheme::versioned);
  static constexpr bool is_archetype_v = (OWN_COUNT == 1);
  static constexpr bool is_component_v = !is_archetype_v;
  static constexpr bool is_fifo_v = (REUSE == sequence_order::fifo);
  static constexpr bool is_lifo_v = !is_fifo_v;
  static constexpr size_t bitmap_bits_v = is_component_v ? OWN_COUNT : 1;
  // `fixed_bitset` requires `N_BITS % 8 == 0`; round up to the nearest
  // multiple of 8. In component mode, bits above `bitmap_bits_v` are padding
  // and are never set; all `OWN_COUNT`- and `bitmap_bits_v`-based validation
  // uses the unpadded value. In archetype mode the value is 8 so that
  // `store_id_set_t` (which uses this as its `N_BITS`) is always well-formed.
  static constexpr size_t padded_bitmap_bits_v =
      is_component_v ? ((bitmap_bits_v + 7) / 8 * 8) : 8;

  using metadata_t = maybe_void_t<T>;
  using id_t = EID;
  using size_type = std::underlying_type_t<id_t>;
  using store_id_t = SID;
  using gen_t = maybe_t<size_type, is_versioned_v>;
  using allocator_type = A;

  using store_id_set_t = fixed_bitset<padded_bitmap_bits_v, store_id_t>;

  static_assert(*id_t::invalid ==
                    std::numeric_limits<std::underlying_type_t<id_t>>::max(),
      "Entity ID type must define 'invalid' as the maximum value of its "
      "underlying type");

  static_assert(std::is_unsigned_v<std::underlying_type_t<id_t>>,
      "Entity ID type must use an unsigned underlying type");

  static_assert(
      *store_id_t::invalid ==
          std::numeric_limits<std::underlying_type_t<store_id_t>>::max(),
      "Location ID type must define 'invalid' as the maximum value of its "
      "underlying type");

  static_assert(std::is_unsigned_v<std::underlying_type_t<store_id_t>>,
      "Store ID type must use an unsigned underlying type");

  static_assert(!std::is_same_v<store_id_t, id_t>,
      "Store ID and Entity ID must not be the same type");

  static_assert(std::is_void_v<T> || std::is_trivially_copyable_v<T>,
      "Metadata type T must be void or trivially copyable");

  static_assert(OWN_COUNT >= 1,
      "OWN_COUNT must be 1 (archetype mode) or >= 2 (component mode)");

  // A handle to an entity. Contains ID and optionally generation data to
  // detect reuse. No ownership: does nothing on destruction.
  //
  // When `GEN` is enabled, it captures a generation snapshot that allows
  // `is_valid(handle_t)` to detect ID reuse. Otherwise, it becomes equivalent
  // to an `id_t`, except that methods taking a handle do check
  // `is_valid(handle_t)`, whereas methods taking an `id_t` typically do not
  // check `is_valid(id_t)`.
  //
  // A handle with an ID of `id_t::invalid` is always invalid.
  struct handle_t {
    handle_t() = default;
    handle_t(const handle_t&) = default;
    handle_t& operator=(const handle_t&) = default;

    [[nodiscard]] bool operator==(const handle_t& other) const noexcept {
      if (id_ != other.id_) return false;
      if constexpr (is_versioned_v) return gen_ == other.gen_;
      return true;
    }

    [[nodiscard]] auto operator<=>(const handle_t& other) const noexcept {
      auto cmp = id_ <=> other.id_;
      if constexpr (is_versioned_v)
        if (cmp == 0) cmp = gen_ <=> other.gen_;
      return cmp;
    }

    [[nodiscard]] id_t id() const { return id_; }

    // True if the handle holds a valid (non-invalid) ID.
    [[nodiscard]] explicit operator bool() const noexcept {
      return id_ != id_t::invalid;
    }

    // Note: While equality/inequality is guaranteed, the precise value is not.
    // As a result, you should not be checking for specific generation values,
    // and should probably not call this method at all.
    [[nodiscard]] size_type gen() const
    requires is_versioned_v
    {
      return gen_;
    }

  private:
    id_t id_{id_t::invalid};
    [[no_unique_address]] gen_t gen_{*id_t::invalid};

    explicit handle_t(id_t id, size_type gen) : id_{id}, gen_{gen} {}
    friend class entity_registry<T, EID, SID, GEN, OWN_COUNT, REUSE, A>;
  };

  // Single location type.
  //
  // This is the type used for parameters.
  //
  // A `store_id` identifies the storage an entity resides in, and `ndx` is the
  // index within that storage.
  struct location_t {
    store_id_t store_id{store_id_t::invalid};
    size_type ndx{*id_t::invalid};
  };

  // Record of external storage location(s) for entity.
  //
  // This is used in `erase_if`.
  //
  // Active fields depend on `OWN_COUNT`.
  //
  // Archetype mode (OWN_COUNT == 1):
  //   `store_id` and `ndx` identify the entity's single storage slot.
  //   `store_id_t{0}` is in staging: alive but unplaced.
  //   `store_id_t::invalid` is a dead entity.
  //   When dead, `ndx` doubles as the intrusive free-list next pointer.
  //
  // Component mode (OWN_COUNT >= 2):
  //   `store_ids` is a presence bitmap; a bit is set when the entity occupies
  //   that storage.
  //  `store_id_t{0}` is set (and all other bits are cleared) is in staging,
  //  equivalent to `store_id_t{0}`.
  //  `store_ids.none()` is a dead entity, equivalent to `store_id_t::invalid`.
  //   While an entity is alive, `ndx` is not used for anything.
  //   When dead, `ndx` doubles as the intrusive free-list next pointer,
  //   exactly as in archetype mode.
  //
  // TODO: Investigate encoding the free-list next pointer in the
  // `store_ids` bytes when dead, eliminating the wasted `ndx` in component
  // mode. Requires OWN_COUNT >= 8*sizeof(size_type), since the bytes must
  // be large enough to hold a size_type. The end-of-list sentinel would be
  // *id_t::invalid (all ones), which is not a valid entity index.
  class location_record {
  public:
    constexpr location_record(const location_record& other) noexcept = default;

    [[nodiscard]] size_type ndx() const noexcept { return ndx_; }

    [[nodiscard]] location_t get_underlying() const noexcept
    requires is_archetype_v
    {
      return {store_id_, ndx_};
    }

    [[nodiscard]] const store_id_set_t& get_underlying() const noexcept
    requires is_component_v
    {
      return store_ids_;
    }

    [[nodiscard]] store_id_t get_store_id() const noexcept
    requires is_archetype_v
    {
      return store_id_;
    }

    [[nodiscard]] const store_id_set_t& get_store_ids() const noexcept
    requires is_component_v
    {
      return store_ids_;
    }

    constexpr void set(location_t location) noexcept {
      ndx_ = location.ndx;
      if constexpr (is_archetype_v) {
        store_id_ = location.store_id;
      } else {
        const auto store_id = location.store_id;
        assert(store_id == store_id_t::invalid || *store_id < OWN_COUNT);
        if (store_id == store_id_t::invalid)
          store_ids_.reset();
        else if (store_id == store_id_t{}) {
          store_ids_.reset();
          store_ids_[store_id_t{}] = true;
        } else {
          store_ids_[store_id_t{}] = false;
          store_ids_[store_id] = true;
        }
      }
    }

    constexpr void reset(location_t location) noexcept {
      if constexpr (is_component_v) store_ids_.reset();
      set(location);
    }

    // Whether location contains the `store_id_t`.
    [[nodiscard]] constexpr bool contains(store_id_t sid) const noexcept {
      if constexpr (is_archetype_v) {
        return store_id_ == sid;
      } else {
        if (sid == store_id_t::invalid) return store_ids_.none();
        return store_ids_[sid];
      }
    }

  private:
    [[no_unique_address]] maybe_t<store_id_t, is_archetype_v> store_id_{
        *store_id_t::invalid};
    [[no_unique_address]] maybe_t<store_id_set_t, is_component_v> store_ids_;
    size_type ndx_{*id_t::invalid};

    constexpr location_record(location_t location = location_t{}) noexcept {
      set(location);
    }
    friend class entity_registry<T, EID, SID, GEN, OWN_COUNT, REUSE, A>;
  };

  static constexpr location_record invalid_location;

  // Entity record. The entity ID is implied by its location in `records_`.
  //
  // When specializing `entity_registry` class, the user must do the math to
  // figure out the size and ensure that the `record_t` aligns well with cache
  // lines.
  //
  // The other reason this structure is public is because it's passed to
  // predicates in `erase_if`.
  struct record_t {
    // Entity location; active fields within `location_record` depend on
    // `OWN_COUNT`.
    location_record location{};

    // Generation counter for this entity.
    [[no_unique_address]] gen_t gen{};

    // Optional user metadata. This is the only field that can be freely
    // modified by the user.
    [[no_unique_address]] metadata_t metadata{};
  };

  using record_allocator_type =
      std::allocator_traits<A>::template rebind_alloc<record_t>;

public:
  entity_registry() = default;
  explicit entity_registry(const allocator_type& alloc)
      : records_{record_allocator_type{alloc}} {}

  explicit entity_registry(id_t id_limit,
      allocation_policy prefill = allocation_policy::lazy,
      const allocator_type& alloc = allocator_type{})
      : records_{id_limit, prefill, record_allocator_type{alloc}} {}

  [[nodiscard]] allocator_type get_allocator() const noexcept {
    return allocator_type{records_.get_allocator()};
  }

  // Maximum allowed ID value.
  //
  // Insertion fails when this limit is reached. Defaults to `id_t::invalid`
  // (the maximum representable value). Note that the limit is exclusive, so
  // the maximum valid ID is `id_t{id_limit() - 1}`.
  [[nodiscard]] id_t id_limit() const noexcept { return records_.id_limit(); }

  // Set a new ID limit. Returns true on success, false if the limit would
  // invalidate live IDs.
  //
  // If the limit is reduced, triggers `shrink_to_fit` to reclaim unused
  // records.
  //
  // WARNING: Reducing the limit invalidates generation detection for trimmed
  // IDs if they are later reused.
  [[nodiscard]] bool set_id_limit(id_t new_limit) {
    const auto id_end = records_.size_as_enum();
    // Fail if any live record exists at or past the new limit.
    if (new_limit < id_end) {
      for (auto id = new_limit; id < id_end; ++id)
        if (is_alive(id)) return false;

      records_.resize(*new_limit);
      shrink_to_fit();
    }

    return records_.set_id_limit(new_limit);
  }

  // Create a new entity record, returning its ID, or `id_t::invalid` on
  // failure.
  //
  // See `create_handle` for details.
  [[nodiscard]]
  id_t create_id(location_t location = location_t{},
      const metadata_t& metadata = {}) {
    if (location.store_id == store_id_t::invalid)
      location.store_id = store_id_t{};
    const id_t id = alloc_id();
    if (id == id_t::invalid) return id_t::invalid;
    auto& rec = records_[id];
    rec.location.reset(location);
    rec.metadata = metadata;
    return id;
  }

  // Create a new entity record, returning its handle, or an invalid handle on
  // failure. One cause of failure is reaching the ID limit.
  //
  // When `location` is defaulted to `{store_id_t::invalid, *id_t::invalid}`,
  // the `store_id` is interpreted as `store_id_t{0}`. The entity is then
  // created in staging, and its location may be updated later with
  // `set_location` or `add_location`/`remove_location`.
  [[nodiscard]]
  handle_t create_handle(location_t location = location_t{},
      const metadata_t& metadata = {}) {
    return get_handle(create_id(location, metadata));
  }

  // Get handle for ID. When invalid, returns an invalid handle.
  [[nodiscard]] handle_t get_handle(id_t id) const {
    if (!is_valid(id)) return {};
    if constexpr (is_versioned_v)
      return handle_t{id, records_[id].gen};
    else
      return handle_t{id, 0};
  }

  // Check whether the ID is valid. Cannot detect ID reuse, since it's not a
  // handle.
  //
  // Note that many methods taking an ID expect it to be valid and will not
  // check. In contrast, methods taking a handle will check.
  [[nodiscard]] bool is_valid(id_t id) const {
    if (*id >= records_.size()) return false;
    return is_alive(id);
  }

  // Check whether handle is valid, including generation.
  [[nodiscard]] bool is_valid(handle_t handle) const {
    if (!is_valid(handle.id_)) return false;
    if constexpr (is_versioned_v)
      return records_[handle.id_].gen == handle.gen_;
    return true;
  }

  // Get location for ID. Must be valid.
  //
  // Returns `location_t` in archetype mode, and `store_id_set_t` in component
  // mode.
  [[nodiscard]] decltype(auto) get_location(id_t id) const {
    assert(is_valid(id));
    return records_[id].location.get_underlying();
  }

  // Get location for handle. When invalid, returns invalid location.
  //
  // Returns `location_t` in archetype mode, and `store_id_set_t` in component
  // mode.
  [[nodiscard]] decltype(auto) get_location(handle_t handle) const {
    if (!is_valid(handle)) return invalid_location.get_underlying();
    return get_location(handle.id_);
  }

  // Set location for ID. The ID must be valid.
  //
  // Setting `location.store_id` to `store_id_t::invalid` erases the entity,
  // while `store_id_t{0}` indicates that the entity is alive but not assigned
  // to a storage. (Archetype mode only.)
  void set_location(id_t id, location_t location)
  requires is_archetype_v
  {
    assert(is_valid(id));
    if (location.store_id == store_id_t::invalid)
      do_erase(id);
    else
      records_[id].location.set(location);
  }

  // Set location for handle. The handle must be valid.
  //
  // Setting `location.store_id` to `store_id_t::invalid` erases the entity.
  // Returns success. (Archetype mode only.)
  bool set_location(handle_t handle, location_t location)
  requires is_archetype_v
  {
    if (!is_valid(handle)) return false;
    set_location(handle.id_, location);
    return true;
  }

  // Add entity to a storage. If the entity is currently in staging (bit 0
  // set), the staging bit is cleared first. `*sid` must be in [1, OWN_COUNT).
  // (Component mode only.)
  void add_location(id_t id, store_id_t sid)
  requires is_component_v
  {
    assert(is_valid(id));
    assert(*sid >= 1 && *sid < OWN_COUNT);
    auto& bm = records_[id].location.store_ids_;
    bm[store_id_t{0}] = false; // leave staging (no-op if already out)
    bm[sid] = true;
  }

  // Remove entity from a storage. If the bitmap becomes empty:
  //   `removal_mode::preserve` -> return entity to staging (bit 0 set).
  //   `removal_mode::remove`   -> erase the entity.
  // `*sid` must be in [1, OWN_COUNT). (Component mode only.)
  // TODO: Consider adding `erase_location` which calls here with
  // removal_mode::remove.
  void remove_location(id_t id, store_id_t sid,
      removal_mode mode = removal_mode::preserve)
  requires is_component_v
  {
    assert(is_valid(id));
    assert(*sid >= 1 && *sid < OWN_COUNT);
    auto& bm = records_[id].location.store_ids_;
    bm[sid] = false;
    if (bm.none()) {
      if (mode == removal_mode::preserve)
        bm[store_id_t{0}] = true; // back to staging
      else
        do_erase(id);
    }
  }

  // Test whether entity is in a given storage. `*sid` must be < `OWN_COUNT`.
  // (Component mode only.)
  [[nodiscard]] bool is_in_location(id_t id, store_id_t sid) const noexcept
  requires is_component_v
  {
    assert(is_valid(id));
    assert(*sid < OWN_COUNT);
    return records_[id].location.store_ids_[sid];
  }

  // Erase by ID. Fails if ID is invalid (but cannot detect ID reuse). Returns
  // success.
  //
  // Deleted IDs will be reused.
  bool erase(id_t id) {
    if (!is_valid(id)) return false;
    return do_erase(id);
  }

  // Erase by handle. Fails if handle is invalid. Returns success.
  //
  // Deleted IDs will be reused, but the handle will be invalidated.
  bool erase(handle_t handle) {
    if (!is_valid(handle)) return false;
    return do_erase(handle.id_);
  }

  // Access metadata by ID. Must be valid.
  [[nodiscard]]
  auto& operator[](this auto& self, id_t id) noexcept {
    assert(self.is_valid(id));
    return self.records_[id].metadata;
  }

  // Access metadata by ID, with bounds checking (but no generation checking).
  [[nodiscard]]
  auto& at(this auto& self, id_t id) {
    if (!self.is_valid(id)) throw std::out_of_range("id out of range");
    return self.records_[id].metadata;
  }

  // Access metadata by handle, with bounds and generation checking.
  [[nodiscard]]
  auto& at(this auto& self, handle_t handle) {
    if (!self.is_valid(handle)) throw std::invalid_argument("invalid handle");
    return std::forward<decltype(self)>(self).at(handle.id_);
  }

  // Erase all records matching predicate called as `pred(id, rec)`, where
  // `id` is the entity ID and `rec` is the `record_t&`. Returns count erased.
  //
  // One particularly obvious use case is to erase all entries that aren't in
  // any valid store. The ID is passed so you can cascade deletion to the
  // appropriate storage(s).
  size_type erase_if(auto pred) {
    size_type cnt{};
    const auto id_end = records_.size_as_enum();
    for (id_t id{}; id < id_end; ++id) {
      auto& rec = records_[id];
      if (!is_alive(id)) continue;
      if (pred(id, rec)) {
        do_erase(id);
        ++cnt;
      }
    }
    return cnt;
  }

  // Return current size, which is the count of living entities.
  //
  // This is not necessarily the size of the underlying vector, which may be
  // larger due to free records. This is also not directly linked to the
  // highest ID issued or in use.
  [[nodiscard]] size_type size() const noexcept { return living_count_; }

  // Return maximum valid ID, or `id_t::invalid` if empty. This is the
  // high-water mark, not the highest extant ID. (Note that underflow of the
  // unsigned underlying type is well-defined, so we know that
  // `id_t{size_type{0} - 1}` is `id_t::invalid`.)
  [[nodiscard]] id_t max_id() const noexcept {
    return id_t{records_.size() - 1};
  }

  // Clear all records.
  //
  // WARNING: When `policy=release`, all generation counters are reset,
  // completely invalidating generation detection for reused IDs.
  void clear(
      deallocation_policy policy = deallocation_policy::preserve) noexcept {
    living_count_ = 0;
    free_head_ = id_t::invalid;
    if constexpr (is_fifo_v) free_tail_ = id_t::invalid;
    if (policy == deallocation_policy::release) {
      records_.clear();
      records_.shrink_to_fit();
      return;
    }
    // Erase all records, relinking them into the free list.
    const auto id_end = records_.size_as_enum();
    for (id_t id{}; id < id_end; ++id) {
      auto& rec = records_[id];
      if constexpr (is_archetype_v) {
        rec.location.store_id_ = store_id_t::invalid;
      } else {
        rec.location.store_ids_.reset();
      }
      if constexpr (is_versioned_v) ++rec.gen;
      push_free(id);
    }
  }

  // Reduce memory usage to fit current size.
  //
  // WARNING: Trimming records invalidates generation detection for those IDs
  // if they are later reused.
  void shrink_to_fit() {
    trim_dead_tail();
    records_.shrink_to_fit();
  }

  // Reserve space for at least `new_cap` records.
  void reserve(size_type new_cap,
      allocation_policy prefill = allocation_policy::lazy) {
    records_.reserve(new_cap);
    if (prefill == allocation_policy::eager && new_cap > records_.size()) {
      const auto old_size = records_.size();
      records_.resize(new_cap);
      for (size_type i = old_size; i < new_cap; ++i) push_free(id_t{i});
    }
  }

  // RAII owner for an entity ID handle. Erases the entity on destruction
  // unless ownership is released first.
  class [[nodiscard]] handle_owner {
  public:
    handle_owner() noexcept = default;

    // Take ownership of an existing handle.
    handle_owner(entity_registry& reg, handle_t handle) noexcept
        : registry_{&reg}, handle_{handle} {
      if (!reg.is_valid(handle_)) handle_ = handle_t{};
    }

    // Create a new entity and take ownership of it (archetype mode). Check
    // `operator bool` or `id()` afterward to detect allocation failure.
    //
    // Prefer calling `create_owner` instead.
    handle_owner(entity_registry& reg, location_t location,
        const metadata_t& metadata = {})
    requires is_archetype_v
        : registry_{&reg}, handle_{reg.create_handle(location, metadata)} {}

    // Create a new entity and take ownership of it (component mode). Check
    // `operator bool` or `id()` afterward to detect allocation failure.
    //
    // Prefer calling `create_owner` instead.
    handle_owner(entity_registry& reg, const metadata_t& metadata = {})
    requires is_component_v
        : registry_{&reg},
          handle_{reg.create_handle(location_t{store_id_t{}, *id_t::invalid},
              metadata)} {}

    handle_owner(const handle_owner&) = delete;
    handle_owner& operator=(const handle_owner&) = delete;

    handle_owner(handle_owner&& other) noexcept
        : registry_{std::exchange(other.registry_, nullptr)},
          handle_{std::exchange(other.handle_, handle_t{})} {}

    handle_owner& operator=(handle_owner&& other) noexcept {
      if (this != &other) {
        reset();
        registry_ = std::exchange(other.registry_, nullptr);
        handle_ = std::exchange(other.handle_, handle_t{});
      }
      return *this;
    }

    ~handle_owner() { reset(); }

    // Get the owned ID.
    [[nodiscard]] id_t id() const noexcept { return handle_.id(); }

    // Get the owned handle.
    [[nodiscard]] const handle_t& handle() const noexcept { return handle_; }

    // True if holding a valid ID.
    [[nodiscard]] explicit operator bool() const noexcept {
      return handle_.id() != id_t::invalid;
    }

    // Release ownership without erasing. Returns the handle.
    [[nodiscard]] handle_t release() noexcept {
      return std::exchange(handle_, handle_t{});
    }

    // Erase the owned entity (if any) and reset to empty.
    void reset() noexcept {
      if (handle_.id() != id_t::invalid) registry_->erase(handle_);
      handle_ = handle_t{};
    }

    // Get the registry.
    [[nodiscard]] decltype(auto) registry(this auto& self) noexcept {
      return *self.registry_;
    }

  private:
    entity_registry* registry_{};
    handle_t handle_{};
  };

  // Create a new owner for a newly created entity. (Archetype mode only.)
  [[nodiscard]] handle_owner
  create_owner(location_t location, const metadata_t& metadata = {})
  requires is_archetype_v
  {
    return handle_owner{*this, location, metadata};
  }

  // Create a new owner for a newly created entity. (Component mode only.)
  [[nodiscard]] handle_owner create_owner(const metadata_t& metadata = {})
  requires is_component_v
  {
    return handle_owner{*this, metadata};
  }

private:
  // True if the record at `id` represents a living entity.
  //
  // In archetype mode: `store_id_ != store_id_t::invalid`.
  // In component mode: `store_ids_.any()` is true.
  //
  // Assumes `id` is within bounds (caller must check).
  [[nodiscard]] bool is_alive(id_t id) const noexcept {
    if constexpr (is_archetype_v)
      return records_[id].location.store_id_ != store_id_t::invalid;
    else
      return records_[id].location.store_ids_.any();
  }

  // Get the intrusive free-list next pointer for a dead record.
  [[nodiscard]] size_type get_next_free(id_t id) const noexcept {
    return records_[id].location.ndx_;
  }

  // Set the intrusive free-list next pointer for a dead record.
  void set_next_free(id_t id, size_type next) noexcept {
    records_[id].location.ndx_ = next;
  }

  // Allocate a new ID.
  // Caller is obligated to make the record valid.
  id_t alloc_id() {
    assert(living_count_ <= records_.size());
    assert(records_.size() <= *records_.id_limit());
    // If we're at the limit, can't allocate more.
    if (living_count_ >= *records_.id_limit()) return id_t::invalid;

    // If no free IDs, and since we're allowed to expand, do so.
    if (living_count_ >= records_.size()) {
      // TODO: Consider resizing to the capacity and adding the extras to the
      // free list. But don't do this initially.
      const auto new_id = records_.size_as_enum();
      (void)records_.emplace_back();
      ++living_count_;
      return new_id;
    }

    // Pop the next free ID from the head of the free list.
    const auto new_id = free_head_;
    assert(new_id != id_t::invalid);
    const auto next_ndx = get_next_free(new_id);
    free_head_ = id_t{next_ndx};
    if constexpr (is_fifo_v)
      if (free_head_ == id_t::invalid) free_tail_ = id_t::invalid;
    ++living_count_;
    return new_id;
  }

  // Trim trailing dead records from records_ and rebuild the free list.
  void trim_dead_tail() {
    size_type new_size = records_.size();
    while (new_size > 0 && !is_alive(id_t{new_size - 1})) --new_size;
    if (new_size < records_.size()) {
      records_.resize(new_size);
      rebuild_free_list();
    }
  }

  // Push a dead record onto the free list.
  // FIFO: appends to tail, maximizing time before ID reuse.
  // LIFO: pushes to head, minimizing time before ID reuse.
  void push_free(id_t id) {
    if constexpr (is_fifo_v) {
      set_next_free(id, *id_t::invalid);
      if (free_tail_ != id_t::invalid)
        set_next_free(free_tail_, *id);
      else
        free_head_ = id;
      free_tail_ = id;
    } else {
      set_next_free(id, *free_head_);
      free_head_ = id;
    }
  }

  // Walk `records_` and rebuild the free list from scratch.
  void rebuild_free_list() {
    free_head_ = id_t::invalid;
    if constexpr (is_fifo_v) free_tail_ = id_t::invalid;
    const size_type n = records_.size();
    for (size_type i = 0; i < n; ++i) {
      const id_t id = id_t{i};
      if (is_alive(id)) continue;
      push_free(id);
    }
  }

  // Erase by ID helper. Assumes validity checked by caller.
  bool do_erase(id_t id) {
    auto& rec = records_[id];
    if constexpr (is_archetype_v)
      rec.location.store_id_ = store_id_t::invalid;
    else
      rec.location.store_ids_.reset();
    if constexpr (is_versioned_v) ++rec.gen;
    // Note that we do not clear metadata on erase, since we always wipe it on
    // allocation. If there is any security concern, the user should wipe the
    // metadata prior to erasing the record.

    push_free(id);
    --living_count_;
    return true;
  }

private:
  using id_container_t = id_container<record_t, id_t, record_allocator_type>;

  // Entity records, with the ID implied by the index.
  id_container_t records_;
  size_type living_count_{};

  // Free list for recycling IDs. `free_head_` is the next free ID to
  // allocate; `id_t::invalid` means empty. Free IDs are linked via
  // `record_t::location.ndx_` in both archetype and component modes.
  //
  // FIFO: `free_tail_` is the last freed ID (queue tail); new IDs are
  // appended there, maximizing the interval before reuse.
  // LIFO: `free_tail_` is absent; new IDs are pushed onto `free_head_`,
  // giving stack (most-recently-freed-first) reuse order.
  id_t free_head_{id_t::invalid};
  [[no_unique_address]] maybe_t<id_t, is_fifo_v> free_tail_{id_t::invalid};
};
}}} // namespace corvid::ecs::entity_registries
