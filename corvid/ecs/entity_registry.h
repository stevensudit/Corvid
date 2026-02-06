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

namespace corvid { inline namespace container {
inline namespace entity_registries {

// The entity registry owns IDs for entities in an ECS system and tracks their
// location and metadata.
//
// IDs may be reused, but unless UseGen is set false, their handles have
// generation counters to detect this. Reuse is done in FIFO order to maximize
// the time before an ID is recycled.
//
// Entity records are not guaranteed to remain at fixed memory locations unless
// you set a limit and reserve space for that many entities up front. In that
// case, you may be able to avoid locking a mutex when looking up records that
// nobody else will be changing.
//
// Operations that take an ID generally expect it to be valid and will not
// check. In contrast, operations that take a handle will check. This means
// that, even when `UseGen` is false, you can still get some safety by using
// handles instead of raw IDs.
template<typename T, typename EID = id_enums::entity_id,
    typename SID = id_enums::store_id_t, bool UseGen = true,
    class Allocator = std::allocator<T>>
class entity_registry {
public:
  using metadata_t = maybe_void_t<T>;
  using id_t = EID;
  using size_type = std::underlying_type_t<id_t>;
  using store_id_t = SID;
  using gen_t = maybe_t<size_type, UseGen>;
  using allocator_type = Allocator;

  // Location of an entity within a storage.
  struct location_t {
    store_id_t store_id{store_id_t::invalid};
    size_type ndx{*store_id_t::invalid};
  };

  static_assert(*id_t::invalid ==
                    std::numeric_limits<std::underlying_type_t<id_t>>::max(),
      "Entity ID type must define 'invalid' as the maximum value of  its "
      "underlying type");

  static_assert(std::is_unsigned_v<std::underlying_type_t<id_t>>,
      "Entity ID type must use an unsigned underlying type");

  static_assert(
      *store_id_t::invalid ==
          std::numeric_limits<std::underlying_type_t<store_id_t>>::max(),
      "Location ID type must define 'invalid' as the maximum value of its "
      "underlying type");

  static_assert(std::is_unsigned_v<std::underlying_type_t<store_id_t>>,
      "Location ID type must use an unsigned underlying type");

  // A handle to an entity. When UseGen is enabled, it captures a generation
  // snapshot that allows `is_valid(handle_t)` to detect ID reuse. Otherwise,
  // it becomes equivalent to an `id_t`, but triggers `is_valid(handle_t)`
  // checks.
  struct handle_t {
  private:
    explicit handle_t(id_t id, size_type gen) : id_{id}, gen_{gen} {}

  public:
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

    [[nodiscard]] id_t get_id() const { return id_; }

    // Note: While equality/inequality is guaranteed, the precise value is not.
    [[nodiscard]] size_type get_gen() const
    requires UseGen
    {
      return gen_;
    }

  private:
    id_t id_{id_t::invalid};
    [[no_unique_address]] gen_t gen_{*id_t::invalid};

    friend class entity_registry<T, EID, SID, UseGen, Allocator>;
  };

private:
  // Entity record. The entity ID is implied by its location in `records_`.
  //
  // The user must do the math to figure out the size and ensure that it aligns
  // well with cache lines.
  struct record_t {
    // Location of the entity. When invalid, entity is considered destroyed and
    // its ID is up for grabs. You may wish to reserve `store_id_t{0}` for
    // living entities that are not yet stored.
    // For valid entities, `ndx` identifies where it is in the storage
    // indicated by `store_id`.  You can use a valid `store_id` with an `ndx`
    // of `*store_id_t::invalid` to indicate entities that are not yet stored.
    // For invalid entities,  `ndx` is reused as the next ID in the intrusive
    // free list.
    location_t location{};

    // Generation counter for this entity ID.
    [[no_unique_address]] gen_t gen{0};

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

  // Maximum allowed ID value. Insertion fails when this limit is reached.
  // Defaults to `id_t::invalid` (the maximum representable value).
  [[nodiscard]] id_t id_limit() const noexcept { return id_limit_; }

  // Set a new ID limit. Returns true on success, false if the limit would
  // invalidate live IDs.
  //
  // If the limit is reduced, triggers `shrink_to_fit()` to reclaim unused
  // records.
  [[nodiscard]] bool set_id_limit(id_t new_limit) {
    const auto id_end = records_.size_as_enum();
    if (new_limit < id_end) {
      // Fail if any live record exists at or past the new limit.
      for (auto id = new_limit; id < id_end; ++id) {
        if (records_[id].location.store_id != store_id_t::invalid)
          return false;
      }
      records_.resize(*new_limit);
      shrink_to_fit();
    }

    id_limit_ = new_limit;
    return true;
  }

  // Create a new entity record, returning its ID, or `id_t::invalid` on
  // failure. Must provide valid `location.store_id` or creation fails.
  //
  // If you know the `store_id` where the entity will go but not where in it,
  // it is safe to pass an `ndx` of `*store_id_t::invalid` initially and then
  // `set_location` later. If you do not know the `store_id` at creation time,
  // you may wish to reserve `store_id_t{}` for that case.
  [[nodiscard]]
  id_t create(location_t location, const metadata_t& metadata = {}) {
    if (location.store_id == store_id_t::invalid) return id_t::invalid;
    const id_t id = alloc_id();
    if (id == id_t::invalid) return id_t::invalid;
    auto& rec = records_[id];
    rec.location = location;
    rec.metadata = metadata;
    return id;
  }

  // Create a new entity record, returning its handle, or an invalid handle on
  // failure. See `create` for details.
  [[nodiscard]]
  handle_t
  create_with_handle(location_t location, const metadata_t& metadata = {}) {
    return get_handle(create(location, metadata));
  }

  // Get handle for ID. When invalid, returns an invalid handle.
  [[nodiscard]] handle_t get_handle(id_t id) const {
    if (!is_valid(id)) return {};
    if constexpr (UseGen)
      return handle_t{id, records_[id].gen};
    else
      return handle_t{id, 0};
  }

  // Check whether ID is valid. Note that many calls taking an ID expect it to
  // be valid and will not check. In contrast, calls taking a handle will
  // check.
  [[nodiscard]] bool is_valid(id_t id) const {
    if (*id >= records_.size()) return false;
    auto loc = records_[id].location;
    return loc.store_id != store_id_t::invalid;
  }

  // Check whether handle is valid.
  [[nodiscard]] bool is_valid(handle_t handle) const {
    if (!is_valid(handle.id_)) return false;
    if constexpr (UseGen) return records_[handle.id_].gen == handle.gen_;
    return true;
  }

  // Get location for ID. Must be valid.
  [[nodiscard]] const location_t& get_location(id_t id) const {
    assert(is_valid(id));
    return records_[id].location;
  }

  // Get location for handle. When invalid, returns invalid location.
  [[nodiscard]] const location_t& get_location(handle_t handle) const {
    static const location_t invalid_loc{};
    if (!is_valid(handle)) return invalid_loc;
    return get_location(handle.id_);
  }

  // Set location for ID. The ID must be valid. Setting location to invalid
  // erases the entity.
  void set_location(id_t id, location_t location) {
    assert(is_valid(id));
    if (location.store_id == store_id_t::invalid)
      do_erase(id);
    else
      records_[id].location = location;
  }

  // Set location for handle. The handle must be valid. Setting to invalid
  // location erases the entity.
  void set_location(handle_t handle, location_t location) {
    if (!is_valid(handle)) return;
    set_location(handle.id_, location);
  }

  // Erase by ID. Fails if ID is invalid. Note that the ID may then be
  // reused.
  bool erase(id_t id) {
    if (!is_valid(id)) return false;
    return do_erase(id);
  }

  // Erase by handle. Fails if handle is invalid. Note that the ID may then be
  // reused, but the handle will be invalidated.
  bool erase(handle_t handle) {
    if (!is_valid(handle)) return false;
    return do_erase(handle.id_);
  }

  // Access metadata by ID. Must be valid.
  [[nodiscard]]
  decltype(auto) operator[](this auto& self, id_t id) noexcept {
    assert(self.is_valid(id));
    return (self.records_[id].metadata);
  }

  // Access metadata by handle. Throws if invalid.
  [[nodiscard]]
  decltype(auto) operator[](this auto& self, handle_t handle) {
    if (!self.is_valid(handle)) throw std::invalid_argument("invalid handle");
    return (self.records_[handle.id_].metadata);
  }

  // Erase all records matching predicate. Returns count erased.
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

  // Clear all records.
  void clear(bool shrink = false) noexcept {
    living_count_ = 0;
    if (shrink) {
      records_.clear();
      records_.shrink_to_fit();
      fifo_head_ = id_t::invalid;
      fifo_tail_ = id_t::invalid;
      return;
    }
    for (auto& rec : records_) {
      rec.location.store_id = store_id_t::invalid;
      if constexpr (UseGen) ++rec.gen;
    }
    // TODO: Consider whether it's inefficient to use rebuild_free_list here,
    // since we know all records are free. Maybe just, given that it checks for
    // invalid.
    rebuild_free_list();
  }

  // Reduce memory usage to fit current size.
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
      // TODO: Consider whether calling `append_to_tail` here is inefficient
      // since we don't need to keep checking whether fifo_tail_ is invalid. We
      // can instead set it to id from the start. Arguably, we could keep
      // `append_to_tail` but template it on a bool for whether to test it.
      append_to_tail(id_t{i});
    }
  }

  // Erase by ID helper. Assumes validity checked by caller.
  bool do_erase(id_t id) {
    auto& rec = records_[id];
    rec.location.store_id = store_id_t::invalid;
    if constexpr (UseGen) ++rec.gen;

    // Push onto tail of free list (FIFO).
    append_to_tail(id);
    --living_count_;
    return true;
  }

private:
  enum_vector<record_t, id_t, record_allocator_type> records_;
  size_type living_count_{0};

  id_t fifo_head_{id_t::invalid};
  id_t fifo_tail_{id_t::invalid};

  // Allowed ID values are below this limit.
  id_t id_limit_{id_t::invalid};
};
}}} // namespace corvid::container::entity_registries
