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
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../containers/enum_vector.h"
#include "../meta/maybe.h"
#include "entity_ids.h"

namespace corvid { inline namespace ecs { inline namespace entity_registries {

// The entity registry owns IDs for entities of a particular entity type in the
// ECS system and tracks their location and metadata. Entities of different
// types would be stored in instances specialized on different ID types.
//
// IDs will be reused after being freed, but unless `UseGen` is set to false,
// record handles have generation counters to detect this. Reuse is done in
// FIFO order to maximize the time before an ID is recycled.
//
// Entity records are not guaranteed to remain at fixed memory locations unless
// you set a limit and reserve space for that many entities up front. In that
// case, you may be able to avoid locking a mutex when looking up records that
// nobody else will be changing.
//
// Lookup by ID or handle is O(1), but unless you reserve space in advance,
// insertion is O(1) only amortized since it may trigger a resize. Erasure is
// O(1). Iteration is O(n) in the size of the underlying vector, which may be
// larger than the count of living entities due to free records. In other
// words, records are not packed, though they are reused efficiently.
//
// Operations that take an ID expect it to be valid and generally will not
// check. Even if they do, they can't detect reuse. In contrast, operations
// that take a handle will check. This means that, even when `UseGen` is false,
// you can still get some additional safety by using handles instead of raw
// IDs.
//
// Template parameters:
//  T         - Per-entity metadata type stored alongside location and
//              generation data. Must be trivially copyable. Leave `void` for
//              no metadata.
//  EID       - Entity ID enum type. Must be unsigned with `invalid` defined as
//              the maximum representable value.
//  SID       - Store ID enum type identifying which storage an entity resides
//              in. Same constraints as EID.
//  UseGen    - When true, handles carry a generation counter that detects ID
//              reuse. When false, handles are equivalent to bare IDs but still
//              trigger validity checks.
//  Allocator - Allocator for the metadata type. Rebound internally for
//              record storage.
template<typename T = void,
    sequence::SequentialEnum EID = id_enums::entity_id_t,
    sequence::SequentialEnum SID = id_enums::store_id_t, bool UseGen = true,
    class Allocator = std::allocator<T>>
class entity_registry {
public:
  using metadata_t = maybe_void_t<T>;
  using id_t = EID;
  using size_type = std::underlying_type_t<id_t>;
  using store_id_t = SID;
  using gen_t = maybe_t<size_type, UseGen>;
  using allocator_type = Allocator;

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

  // A handle to an entity.
  //
  // When UseGen is enabled, it captures a generation snapshot that allows
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
      if constexpr (UseGen) return gen_ == other.gen_;
      return true;
    }

    [[nodiscard]] auto operator<=>(const handle_t& other) const noexcept {
      auto cmp = id_ <=> other.id_;
      if constexpr (UseGen)
        if (cmp == 0) cmp = gen_ <=> other.gen_;
      return cmp;
    }

    [[nodiscard]] id_t id() const { return id_; }

    // Note: While equality/inequality is guaranteed, the precise value is not.
    // As a result, you should not be checking for specific generation values,
    // and should probably not call this method at all.
    [[nodiscard]] size_type gen() const
    requires UseGen
    {
      return gen_;
    }

  private:
    id_t id_{id_t::invalid};
    [[no_unique_address]] gen_t gen_{*id_t::invalid};

    explicit handle_t(id_t id, size_type gen) : id_{id}, gen_{gen} {}
    friend class entity_registry<T, EID, SID, UseGen, Allocator>;
  };

  // Location of an entity within an external storage.
  //
  // When `store_id` is `store_id_t::invalid`, the entity is considered
  // destroyed and its ID is up for grabs. The value of `store_id_t{0}` is
  // suitable for living entities that are not yet assigned storage. These can
  // always be cleaned up with `erase_if` if they get lost.
  //
  // For valid entities, `ndx` identifies where it is in the storage indicated
  // by `store_id`. The index is an externally-managed value with no meaning
  // to this class, so you can use a valid `store_id` with an `ndx` of
  // `*store_id_t::invalid` to indicate entities that are bound for that
  // storage but not yet stored.
  struct location_t {
    store_id_t store_id{store_id_t::invalid};
    size_type ndx{*store_id_t::invalid};
  };

  // Entity record. The entity ID is implied by its location in `records_`.
  //
  // When specializing `entity_registry` class, the user must do the math to
  // figure out the size and ensure that the `record_t` aligns well with cache
  // lines.
  //
  // The other reason this structure is public is because it's passed to
  // predicates in `erase_if`.
  struct record_t {
    // Location of the entity. See class definition for details.
    //
    // Here is an implementation detail that's not visible from outside:
    // While an entity is invalid, `ndx` is reused to hold the next ID (not
    // index) in the intrusive free list.
    location_t location{};

    // Generation counter for this entity.
    [[no_unique_address]] gen_t gen{};

    // Optional user metadata. This is the only field that can be freely
    // modified by the user.
    [[no_unique_address]] metadata_t metadata{};
  };

  using record_allocator_type = typename std::allocator_traits<
      Allocator>::template rebind_alloc<record_t>;

public:
  entity_registry() = default;
  explicit entity_registry(const allocator_type& alloc)
      : records_{record_allocator_type{alloc}} {}

  explicit entity_registry(id_t id_limit, bool prefill = false,
      const allocator_type& alloc = allocator_type{})
      : records_{record_allocator_type{alloc}}, id_limit_{id_limit} {
    if (!prefill || !id_limit_) return;
    reserve(*id_limit_, true);
  }

  [[nodiscard]] allocator_type get_allocator() const noexcept {
    return allocator_type{records_.get_allocator()};
  }

  // Maximum allowed ID value.
  //
  // Insertion fails when this limit is reached. Defaults to `id_t::invalid`
  // (the maximum representable value).
  [[nodiscard]] id_t id_limit() const noexcept { return id_limit_; }

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
        if (records_[id].location.store_id != store_id_t::invalid)
          return false;

      records_.resize(*new_limit);
      shrink_to_fit();
    }

    id_limit_ = new_limit;
    return true;
  }

  // Create a new entity record, returning its ID, or `id_t::invalid` on
  // failure.
  //
  // See `create_handle` for details.
  [[nodiscard]]
  id_t create_id(location_t location = {store_id_t{}},
      const metadata_t& metadata = {}) {
    if (location.store_id == store_id_t::invalid) return id_t::invalid;
    const id_t id = alloc_id();
    if (id == id_t::invalid) return id_t::invalid;
    auto& rec = records_[id];
    rec.location = location;
    rec.metadata = metadata;
    return id;
  }

  // Create a new entity record, returning its handle, or an invalid handle on
  // failure. One cause of failure is reaching the ID limit.
  //
  // If you do not provide a location, a value of `{store_id_t{},
  // *store_id_t::invalid}` will be provided for you, indicating that the
  // entity is alive but not yet assigned to a storage.
  //
  // If you know where it will be stored but don't yet have an index, you can
  // pass a location of `{store_id, *store_id_t::invalid}`. This is still
  // considered alive and valid, and you can update its location with
  // `set_location` later.
  //
  // A location with `store_id` set to `store_id_t::invalid` is invalid and
  // will fail.
  [[nodiscard]]
  handle_t create_handle(location_t location = {store_id_t{}},
      const metadata_t& metadata = {}) {
    return get_handle(create_id(location, metadata));
  }

  // Get handle for ID. When invalid, returns an invalid handle.
  [[nodiscard]] handle_t get_handle(id_t id) const {
    if (!is_valid(id)) return {};
    if constexpr (UseGen)
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
    return records_[id].location.store_id != store_id_t::invalid;
  }

  // Check whether handle is valid, including generation.
  [[nodiscard]] bool is_valid(handle_t handle) const {
    if (!is_valid(handle.id_)) return false;
    if constexpr (UseGen) return records_[handle.id_].gen == handle.gen_;
    return true;
  }

  // Get location for ID. Must be valid.
  [[nodiscard]] location_t get_location(id_t id) const {
    assert(is_valid(id));
    return records_[id].location;
  }

  // Get location for handle. When invalid, returns invalid location.
  [[nodiscard]] location_t get_location(handle_t handle) const {
    if (!is_valid(handle)) return location_t{};
    return get_location(handle.id_);
  }

  // Set location for ID. The ID must be valid. Setting `location.store_id` to
  // `store_id_t::invalid` erases the entity, while `store_id_t{0}` indicates
  // that the entity is alive but not assigned to a storage.
  void set_location(id_t id, location_t location) {
    assert(is_valid(id));
    if (location.store_id == store_id_t::invalid)
      do_erase(id);
    else
      records_[id].location = location;
  }

  // Set location for handle. The handle must be valid. Setting
  // `location.store_id` to `store_id_t::invalid` erases the entity.
  // Returns success.
  bool set_location(handle_t handle, location_t location) {
    if (!is_valid(handle)) return false;
    set_location(handle.id_, location);
    return true;
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

  // Erase all records matching predicate. Returns count erased.
  //
  // One particularly obvious use case is to erase all entries that aren't in
  // any valid store.
  size_type erase_if(auto pred) {
    size_type cnt{};
    const auto id_end = records_.size_as_enum();
    for (id_t id{}; id < id_end; ++id) {
      auto& rec = records_[id];
      if (rec.location.store_id == store_id_t::invalid) continue;
      if (pred(rec)) {
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
  // high-water mark, not the highest extant ID.
  [[nodiscard]] id_t max_id() const noexcept {
    return id_t{records_.size() - 1};
  }

  // Clear all records.
  //
  // WARNING: When `shrink=true`, all generation counters are reset, completely
  // invalidating generation detection for reused IDs.
  void clear(bool shrink = false) noexcept {
    living_count_ = 0;
    if (shrink) {
      records_.clear();
      records_.shrink_to_fit();
      fifo_head_ = id_t::invalid;
      fifo_tail_ = id_t::invalid;
      return;
    }
    // Erase all records, relinking them into the free list. This is faster
    // than clearing and rebuilding the free list.
    //
    // Note: We could optimize this by special-casing the first element, but
    // this is not going to be measurably faster. Not only is this function
    // outside of the hot path, but we'd only get one branch misprediction out
    // of it.
    const auto id_end = records_.size_as_enum();
    auto prev = id_t::invalid;
    for (id_t id{}; id < id_end; ++id) {
      auto& rec = records_[id];
      rec.location.store_id = store_id_t::invalid;
      rec.location.ndx = *id_t::invalid;
      if constexpr (UseGen) ++rec.gen;
      if (prev != id_t::invalid)
        records_[prev].location.ndx = *id;
      else
        fifo_head_ = id;
      prev = id;
    }
    fifo_tail_ = prev;
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
  void reserve(size_type new_cap, bool prefill = false) {
    records_.reserve(new_cap);
    if (prefill && new_cap > records_.size()) {
      const auto old_size = records_.size();
      records_.resize(new_cap);
      for (size_type i = old_size; i < new_cap; ++i) append_to_tail(id_t{i});
    }
  }

private:
  // Allocate a new ID.
  // Caller is obligated to make the record valid.
  id_t alloc_id() {
    assert(living_count_ <= records_.size());
    // If we're at the limit, can't allocate more.
    if (living_count_ >= *id_limit_) return id_t::invalid;

    // If no free IDs, and since we're allowed to expand, do so.
    if (living_count_ >= records_.size()) {
      // TODO: Consider resizing to the capacity and adding the extras to the
      // free list. But don't do this initially.
      const auto new_id = records_.size_as_enum();
      records_.emplace_back();
      ++living_count_;
      return new_id;
    }

    // Pick the next free ID from the head of the FIFO free list.
    const auto new_id = fifo_head_;
    assert(new_id != id_t::invalid);
    const auto next_ndx = records_[new_id].location.ndx;
    fifo_head_ = id_t{next_ndx};
    if (fifo_head_ == id_t::invalid) fifo_tail_ = id_t::invalid;
    ++living_count_;
    return new_id;
  }

  // Trim trailing dead records from records_ and rebuild the free list.
  void trim_dead_tail() {
    size_type new_size = records_.size();
    while (new_size > 0 &&
           (records_[id_t{new_size - 1}].location.store_id ==
               store_id_t::invalid))
      --new_size;
    if (new_size < records_.size()) {
      records_.resize(new_size);
      rebuild_free_list();
    }
  }

  // Push an already-invalid record onto the tail of the FIFO free list.
  void append_to_tail(id_t id) {
    records_[id].location.ndx = *id_t::invalid;
    if (fifo_tail_ != id_t::invalid)
      records_[fifo_tail_].location.ndx = *id;
    else
      fifo_head_ = id;
    fifo_tail_ = id;
  }

  // Walk `records_` and rebuild the free list from scratch.
  void rebuild_free_list() {
    fifo_head_ = id_t::invalid;
    fifo_tail_ = id_t::invalid;
    const size_type n = records_.size();
    for (size_type i = 0; i < n; ++i) {
      if (records_[id_t{i}].location.store_id != store_id_t::invalid) continue;
      append_to_tail(id_t{i});
    }
  }

  // Erase by ID helper. Assumes validity checked by caller.
  bool do_erase(id_t id) {
    auto& rec = records_[id];
    rec.location.store_id = store_id_t::invalid;
    if constexpr (UseGen) ++rec.gen;
    // Note that we do not clear metadata on erase, since we always wipe it on
    // allocation. If there is any security concern, the user should wipe the
    // metadata prior to erasing the record.

    // Push onto tail of free list (FIFO).
    append_to_tail(id);
    --living_count_;
    return true;
  }

private:
  // Entity records, with the ID implied by the index.
  enum_vector<record_t, id_t, record_allocator_type> records_;
  size_type living_count_{};

  // Free list for recycling IDs. When empty, both are `id_t::invalid`.
  // Otherwise, `fifo_head_` is the next free ID to allocate, and `fifo_tail_`
  // is the most recently freed ID. The free IDs are linked together through
  // the `ndx` field of their location, which is repurposed to hold the next
  // free ID when the record is invalid.
  id_t fifo_head_{id_t::invalid};
  id_t fifo_tail_{id_t::invalid};

  // Allowed ID values are below this limit.
  id_t id_limit_{id_t::invalid};
};
}}} // namespace corvid::ecs::entity_registries
