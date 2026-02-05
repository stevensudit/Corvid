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

#include "enum_vector.h"
#include "../meta/maybe.h"

namespace corvid { inline namespace container {
inline namespace stable_id_vector {

// This has to be declared up front so that we can place `enum_spec_v` in the
// global namespace. Note that "sys/types.h" also defines a type named `id_t`
// and rudely injects it into the global namespace.
namespace id_enums {
enum class id_t : size_t { invalid = std::numeric_limits<size_t>::max() };
}
}}} // namespace corvid::container::stable_id_vector

template<>
constexpr auto corvid::enums::registry::enum_spec_v<
    corvid::container::stable_id_vector::id_enums::id_t> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::container::stable_id_vector::id_enums::id_t, "">();

namespace corvid { inline namespace container {
inline namespace stable_id_vector {

// An indexed vector to store elements by stable ID, suitable for Entity
// Component Systems, where it functions as the basis for a sparse set.
//
// It allows linear iteration, fixed-time lookups by ID, as well as fixed-time
// insertion and removal. The IDs remain stable throughout, although entities
// may be moved around in memory. A handle can be used to detect ID reuse.
//
// ID values range from 0 to N-1, where N is the number of elements ever added
// to the container. When an element is removed, its ID may be reused for a new
// one.
//
// By specializing on an ID type that's unique to your container instance, you
// get type-safe IDs. Note that it must be a sequential enum type and define an
// `invalid` value equal to its max, as shown by `id_t`.
//
// Template options:
//   T         — element type stored in the container.
//   ID        — enum type used for IDs. Must be a sequential enum with an
//               `invalid` value equal to its underlying type's max (see
//               `id_t` for an example). Defaults to `id_enums::id_t`.
//   UseGen    — when true (default), handles carry a generation counter that
//               detects ID reuse via is_valid(handle_t). When false, the gen
//               field is elided entirely via [[no_unique_address]].
//   UseFifo   — when true, freed IDs are reused in FIFO order (oldest first).
//               When false (default), reuse is LIFO. FIFO increases the delay
//               before an ID is recycled, which can make the no-gen option
//               safer in domains with bounded handle lifetimes (e.g. per-frame
//               ECS). The free-list next-pointer is colocated in an internal
//               slot type and elided when FIFO is off.
//   Allocator — allocator type for element storage. Also rebound internally
//               for index and slot vectors. Defaults to `std::allocator<T>`.
//
// Insertion may throw `std::out_of_range` if the maximum ID value is
// exceeded. Alternately, you can disable throwing by calling
// `throw_on_insert_failure(false)`, in which case `id_t::invalid` is
// returned.
//
// Exception safety note: In FIFO mode, `alloc_id` modifies the free list
// before returning. If the subsequent `data_.push_back` throws (e.g., due to
// allocation failure), the container is left in an inconsistent state. To
// avoid this, call `reserve` with `prefill=true` (or use the prefilling
// constructor) before inserting, ensuring that insertions do not allocate.
//
// Motivated by https://github.com/johnBuffer/StableIndexVector.
template<typename T, typename ID = id_enums::id_t, bool UseGen = true,
    bool UseFifo = false, class Allocator = std::allocator<T>>
class stable_ids {
public:
  using id_t = ID;
  using size_type = std::underlying_type_t<id_t>;
  using allocator_type = Allocator;

private:
  using data_allocator_type = Allocator;
  using index_allocator_type = typename std::allocator_traits<
      Allocator>::template rebind_alloc<size_type>;

public:
  static_assert(*id_t::invalid ==
                    std::numeric_limits<std::underlying_type_t<id_t>>::max(),
      "ID type for stable_ids must define 'invalid' as the maximum value of "
      "its underlying type");
  static_assert(std::is_unsigned_v<size_type>,
      "ID type for stable_ids must use an unsigned underlying type");

  // An opaque handle that refers to an element. When UseGen is enabled, it
  // captures a generation snapshot that allows `is_valid(handle_t)` to detect
  // ID reuse. Otherwise, it becomes equivalent to an `id_t`.
  struct handle_t {
  private:
    explicit handle_t(id_t id) : id_{id} {}

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

    // Note: While equality/inequality is guaranteed, the precise value is not.
    [[nodiscard]] id_t get_id() const { return id_; }
    [[nodiscard]] size_type get_gen() const
    requires UseGen
    {
      return gen_;
    }

  private:
    id_t id_{id_t::invalid};
    [[no_unique_address]] maybe_t<size_type, UseGen> gen_{};

    friend class stable_ids<T, ID, UseGen, UseFifo, Allocator>;
  };

private:
  // Internal slot stored in `reverse_`. Contains the handle and, when FIFO is
  // enabled, a next-pointer for the intrusive free list.
  struct slot_t {
    handle_t h_;
    [[no_unique_address]] maybe_t<id_t, UseFifo> fifo_next_{id_t::invalid};
  };

  using slot_allocator_type =
      typename std::allocator_traits<Allocator>::template rebind_alloc<slot_t>;

  static_assert(std::is_trivially_copyable_v<handle_t>);
  static_assert(sizeof(handle_t) <= 16);
  static_assert(std::is_trivially_copyable_v<slot_t>);

public:
  // Construction.
  stable_ids() = default;
  explicit stable_ids(const allocator_type& alloc)
      : data_{alloc}, indexes_{index_allocator_type{alloc}},
        reverse_{slot_allocator_type{alloc}} {}

  // Construct with a maximum ID limit. If `prefill` is true, pre-allocates and
  // initializes `indexes_` and `reverse_` so that subsequent insertions do not
  // allocate (useful for exception safety in FIFO mode).
  explicit stable_ids(id_t id_limit, bool prefill = false,
      const allocator_type& alloc = allocator_type{})
      : data_{alloc}, indexes_{index_allocator_type{alloc}},
        reverse_{slot_allocator_type{alloc}}, id_limit_{id_limit} {
    if (prefill) do_prefill(*id_limit_);
  }

  stable_ids(stable_ids&&) noexcept = default;

  stable_ids& operator=(stable_ids&&) noexcept = default;

  friend void swap(stable_ids& lhs, stable_ids& rhs) noexcept {
    using std::swap;
    swap(lhs.data_, rhs.data_);
    swap(lhs.indexes_, rhs.indexes_);
    swap(lhs.reverse_, rhs.reverse_);
    swap(lhs.id_limit_, rhs.id_limit_);
    swap(lhs.throw_on_insert_failure_, rhs.throw_on_insert_failure_);
    if constexpr (UseFifo) {
      swap(lhs.fifo_head_, rhs.fifo_head_);
      swap(lhs.fifo_tail_, rhs.fifo_tail_);
    }
  }

  // Maximum allowed ID value. Insertion fails when this limit is reached.
  // Defaults to `id_t::invalid` (the maximum representable value).
  [[nodiscard]] id_t id_limit() const noexcept { return id_limit_; }

  // Set a new ID limit. Returns true on success, false if the limit would
  // invalidate live IDs (i.e., if `new_limit <= find_max_extant_id()`).
  //
  // If the new limit is lower than `max_id()` but higher than
  // `find_max_extant_id()`, this calls `shrink_to_fit()` to reclaim freed
  // slots that would exceed the new limit. This prevents reusing IDs that
  // were previously allocated but are now beyond the new limit.
  //
  // After setting a new id limit, consider calling `reserve` with
  // `prefill=true` to pre-allocate slots up to the new limit.
  //
  // To lower the limit when live IDs exceed it, first erase those entities,
  // then retry.
  [[nodiscard]] bool set_id_limit(id_t new_limit) noexcept {
    // Empty container: any limit is valid.
    if (data_.empty()) {
      // If there are freed slots beyond the new limit, clear them.
      if (!indexes_.empty() && new_limit <= max_id()) clear(true);
      id_limit_ = new_limit;
      return true;
    }

    // If raising above the high-water mark, no further checks needed.
    if (new_limit > max_id()) {
      id_limit_ = new_limit;
      return true;
    }

    // Lowering the limit: check if it would invalidate live IDs.
    const auto max_extant = find_max_extant_id();
    if (new_limit <= max_extant) return false;

    // new_limit > max_extant but <= max_id(): shrink to remove freed slots.
    shrink_to_fit();
    id_limit_ = new_limit;
    return true;
  }

  // Control whether insertion throws on ID overflow (by default) or returns
  // `id_t::invalid`, requiring the caller to check.
  [[nodiscard]] bool throw_on_insert_failure() const noexcept {
    return throw_on_insert_failure_;
  }
  void throw_on_insert_failure(bool value) noexcept {
    throw_on_insert_failure_ = value;
  }

  // Push a new element, returning its assigned ID.
  [[nodiscard]] id_t push_back(const T& value) {
    const auto id = alloc_id();
    if (id != id_t::invalid) data_.push_back(value);
    return id;
  }

  // Emplace a new element, returning its assigned ID.
  template<typename... Args>
  [[nodiscard]] id_t emplace_back(Args&&... args) {
    const auto id = alloc_id();
    if (id != id_t::invalid) data_.emplace_back(std::forward<Args>(args)...);
    return id;
  }

  // Push a new element, returning its handle.
  [[nodiscard]] handle_t push_back_handle(const T& value) {
    return get_handle(push_back(value));
  }

  // Emplace a new element, returning its handle.
  template<typename... Args>
  [[nodiscard]] handle_t emplace_back_handle(Args&&... args) {
    return get_handle(emplace_back(std::forward<Args>(args)...));
  }

  // Get handle for ID.
  [[nodiscard]] handle_t get_handle(id_t id) const {
    if (!is_valid(id)) return handle_t{id_t::invalid};
    const auto ndx = indexes_[id];
    return reverse_[ndx].h_;
  }

  // Check whether ID is valid.
  [[nodiscard]] bool is_valid(id_t id) const {
    if (*id >= indexes_.size()) return false;
    const auto ndx = indexes_[id];
    if (ndx >= data_.size()) return false;
    assert(reverse_[ndx].h_.id_ == id);
    return true;
  }

  // Check whether handle is valid.
  [[nodiscard]] bool is_valid(handle_t handle) const {
    const auto id = handle.id_;
    if (*id >= indexes_.size()) return false;
    const auto ndx = indexes_[id];
    if (ndx >= data_.size()) return false;
    const auto& live = reverse_[ndx].h_;
    assert(live.id_ == id);
    if constexpr (UseGen) return live.gen_ == handle.gen_;
    return true;
  }

  // Erase element by ID. Returns true if erased, false if ID was invalid.
  //
  // Note that the ID may be reused.
  bool erase(id_t id) {
    if (!is_valid(id)) return false;
    return do_erase(id);
  }

  // Erase element by handle. Returns true if erased, false if handle was
  // invalid.
  //
  // Note that the ID may be reused, but the handle be invalidated.
  bool erase(handle_t handle) {
    if (!is_valid(handle)) return false;
    return do_erase(handle.id_);
  }

  // Erase all elements matching predicate. Returns count erased.
  size_type erase_if(auto pred) {
    size_type cnt{};
    for (size_type ndx{}; ndx < data_.size();) {
      if (pred(data_[ndx])) {
        ++cnt;
        do_erase(ndx);
      } else
        ++ndx;
    }
    return cnt;
  }

  // Clear all elements. If `shrink` is true, also free all memory.
  // It's faster to clear with shrink than to clear and then `shrink_to_fit`.
  // See warning in `shrink_to_fit`.
  void clear(bool shrink = false) noexcept {
    const auto live_size = data_.size();
    data_.clear();
    if (shrink) {
      indexes_.clear();
      reverse_.clear();
      if constexpr (UseFifo) {
        fifo_head_ = id_t::invalid;
        fifo_tail_ = id_t::invalid;
      }
    } else {
      // Bump generation only for entries that were live, to invalidate
      // outstanding handles. Free entries already had their gen bumped on
      // erase.
      if constexpr (UseGen) {
        for (size_type i{}; i < live_size; ++i) ++reverse_[i].h_.gen_;
      }
      // Maintain FIFO free list.
      if constexpr (UseFifo) rebuild_fifo_list();
    }
  }

  // Reduce memory usage to fit current size. Note that this does not preserve
  // generations, hence it cannot guarantee preserving the validity of handles.
  // Do not call if you might have dangling handles.
  void shrink_to_fit() {
    // If already empty, just clear with shrink.
    const auto live_size = data_.size();
    if (live_size == 0) {
      clear(true);
      return;
    }

    // IDs can be sparse; find the highest live ID to size the mappings.
    const auto new_size = *find_max_extant_id() + 1U;
    if (new_size != reverse_.size()) {
      indexes_.resize(new_size);
      reverse_.resize(new_size);

      // Rebuild the free-list tail in-place. An ID is live iff its index
      // points into the live range and reverse_ confirms the match. Freed IDs
      // are placed into the tail; live entries are left untouched.
      size_type free_pos = live_size;
      for (id_t id{}; *id < new_size; ++id) {
        const auto ndx = indexes_[id];
        if (ndx < live_size && reverse_[ndx].h_.id_ == id) continue;
        indexes_[id] = free_pos;
        reverse_[free_pos] = slot_t{handle_t{id}};
        ++free_pos;
      }

      if constexpr (UseFifo) rebuild_fifo_list();
    }

    data_.shrink_to_fit();
    indexes_.shrink_to_fit();
    reverse_.shrink_to_fit();
  }

  // Reserve space for at least `new_cap` elements. If `prefill` is true,
  // pre-allocates and initializes `indexes_` and `reverse_` so that subsequent
  // insertions do not allocate (useful for exception safety in FIFO mode).
  void reserve(size_type new_cap, bool prefill = false) {
    data_.reserve(new_cap);
    indexes_.reserve(new_cap);
    reverse_.reserve(new_cap);
    if (prefill) do_prefill(new_cap);
  }

  // Return current size. This is the count of elements actually present,
  // regardless of their IDs.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(data_.size());
  }

  // Return maximum valid ID, or `id_t::invalid` if empty. This is effectively
  // the high-water mark, not the highest extant ID. See `find_max_extant_id`.
  //
  // Note that the list of valid IDs is sparse, so you may need to call
  // `is_valid` on the specific one you care about.
  [[nodiscard]] id_t max_id() const noexcept {
    return static_cast<id_t>(indexes_.size() - 1);
  }

  // Return maximum extant ID, or `id_t::invalid` if empty.
  //
  // Note that the list of valid IDs is sparse, so you may need to call
  // `is_valid` on the specific one you care about.
  [[nodiscard]] id_t find_max_extant_id() const noexcept {
    if (data_.empty()) return id_t::invalid;

    id_t max_id{};
    for (size_type ndx{}; ndx < data_.size(); ++ndx) {
      const auto id = reverse_[ndx].h_.id_;
      if (id > max_id) max_id = id;
    }

    return max_id;
  }

  // Return next ID to be allocated: the head of the free list if one exists,
  // otherwise the next sequential value past the high-water mark.
  [[nodiscard]] id_t next_id() const noexcept {
    if constexpr (UseFifo) {
      if (fifo_head_ != id_t::invalid) return fifo_head_;
    } else {
      if (reverse_.size() > data_.size()) return reverse_[data_.size()].h_.id_;
    }
    return max_id() + 1U;
  }

  // Return whether container is empty.
  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

  // Access element by ID. Must be valid.
  [[nodiscard]] decltype(auto) operator[](this auto& self, id_t id) noexcept {
    assert(self.is_valid(id));
    const auto ndx = self.indexes_[id];
    return std::forward<decltype(self)>(self).data_[ndx];
  }

  // Access element by ID, with bounds checking.
  [[nodiscard]] decltype(auto) at(this auto& self, id_t id) {
    if (!self.is_valid(id)) throw std::out_of_range("id out of range");
    const auto ndx = self.indexes_[id];
    return std::forward<decltype(self)>(self).data_[ndx];
  }

  // Access element by handle, with bounds and generation checking.
  [[nodiscard]] decltype(auto) at(this auto& self, handle_t handle) {
    if (!self.is_valid(handle)) throw std::invalid_argument("invalid handle");
    return std::forward<decltype(self)>(self).at(handle.id_);
  }

  // Const-only access to vector.
  [[nodiscard]] auto& vector() const noexcept { return data_; }

  // Access to data as a span.
  //
  // Note that, unlike `vector`, this can allow modifying values.
  [[nodiscard]] auto span(this auto& self) noexcept {
    return std::span{self.data_};
  }

  // Iterators.
  [[nodiscard]] auto begin(this auto& self) noexcept {
    return std::forward<decltype(self)>(self).data_.begin();
  }
  [[nodiscard]] auto end(this auto& self) noexcept {
    return std::forward<decltype(self)>(self).data_.end();
  }
  [[nodiscard]] auto cbegin() const noexcept { return data_.cbegin(); }
  [[nodiscard]] auto cend() const noexcept { return data_.cend(); }

private:
  // Allocate a new ID, either by reusing a freed one or creating a new one.
  // When UseFifo is true, pops the oldest freed ID (FIFO) and swaps it into
  // the tail-front position so the caller's push_back absorbs it into the
  // live range. When UseFifo is false, returns the tail-front directly
  // (LIFO).
  id_t alloc_id() {
    const auto new_ndx = data_.size();

    if constexpr (UseFifo) {
      if (fifo_head_ != id_t::invalid) {
        const auto id = fifo_head_;
        const auto id_ndx = indexes_[id];

        // Advance the free-list head.
        fifo_head_ = reverse_[id_ndx].fifo_next_;
        if (fifo_head_ == id_t::invalid) fifo_tail_ = id_t::invalid;

        // Swap the popped slot into new_ndx so that the caller's push_back
        // makes it live. The linked list stays consistent: it is threaded
        // by ID (via indexes_), not by position, so swapping two slots and
        // updating both indexes_ entries is sufficient.
        if (id_ndx != new_ndx) {
          const auto front_id = reverse_[new_ndx].h_.id_;
          std::swap(reverse_[id_ndx], reverse_[new_ndx]);
          indexes_[id] = new_ndx;
          indexes_[front_id] = id_ndx;
        }

        return id;
      }
    } else {
      // LIFO: reuse the ID at the tail front (most recently freed).
      if (reverse_.size() > new_ndx) return reverse_[new_ndx].h_.id_;
    }

    // No free ID available; expand with a new one.
    const auto new_id = static_cast<id_t>(new_ndx);
    if (new_id < id_limit_) {
      reverse_.push_back(slot_t{handle_t{new_id}});
      indexes_.push_back(new_ndx);
      return new_id;
    }

    if (throw_on_insert_failure_)
      throw std::out_of_range("stable_ids: exceeded id limit");
    return id_t::invalid;
  }

  // Swap-and-pop erase helper.
  bool swap_and_pop(size_type ndx, size_type last_ndx, id_t id, id_t last_id) {
    // Assumes validity checked by caller.

    // Invalidate handle by bumping generation.
    if constexpr (UseGen) ++reverse_[ndx].h_.gen_;

    // Swap and pop.
    std::swap(data_[ndx], data_[last_ndx]);
    std::swap(indexes_[id], indexes_[last_id]);
    std::swap(reverse_[ndx], reverse_[last_ndx]);
    data_.pop_back();

    // id is now free; its slot is at indexes_[id] (== last_ndx, which is now
    // == data_.size()). Append it to the FIFO free-list tail.
    if constexpr (UseFifo) {
      const auto free_ndx = indexes_[id];
      reverse_[free_ndx].fifo_next_ = id_t::invalid;
      if (fifo_tail_ != id_t::invalid)
        reverse_[indexes_[fifo_tail_]].fifo_next_ = id;
      else
        fifo_head_ = id;
      fifo_tail_ = id;
    }

    return true;
  }

  // Erase by ID helper.
  bool do_erase(id_t id) {
    // Assumes validity checked by caller.
    assert(is_valid(id));
    const auto ndx = indexes_[id];
    const auto last_ndx = data_.size() - 1;
    const auto last_id = reverse_[last_ndx].h_.id_;
    return swap_and_pop(ndx, last_ndx, id, last_id);
  }

  // Erase by data index helper.
  void do_erase(size_type ndx) {
    // Assumes validity checked by caller.
    assert(ndx < data_.size());
    const auto last_ndx = data_.size() - 1;
    const auto last_id = reverse_[last_ndx].h_.id_;
    const auto id = reverse_[ndx].h_.id_;
    assert(is_valid(id));
    swap_and_pop(ndx, last_ndx, id, last_id);
  }

  // Rebuild the FIFO linked list to cover all current free entries. Called
  // after clear or shrink_to_fit, which may rearrange or reset the tail.
  // Links free entries in position order; prior free-order is not preserved
  // (nor is it meaningful after a bulk operation).
  void rebuild_fifo_list() noexcept
  requires UseFifo
  {
    const auto live = data_.size();
    const auto total = reverse_.size();
    if (live >= total) {
      fifo_head_ = fifo_tail_ = id_t::invalid;
      return;
    }

    fifo_head_ = reverse_[live].h_.id_;
    for (auto ndx = live; ndx + 1 < total; ++ndx)
      reverse_[ndx].fifo_next_ = reverse_[ndx + 1].h_.id_;

    reverse_[total - 1].fifo_next_ = id_t::invalid;
    fifo_tail_ = reverse_[total - 1].h_.id_;
  }

  // Pre-allocate and initialize `indexes_` and `reverse_` up to `count` slots.
  // All slots beyond `data_.size()` become free entries. In FIFO mode, also
  // rebuilds the free list.
  void do_prefill(size_type count) {
    const auto old_size = reverse_.size();
    if (count <= old_size) return;

    indexes_.resize(count);
    reverse_.resize(count);

    // Initialize new slots as free entries.
    // TODO: Change to iterate using ID.
    for (size_type ndx = old_size; ndx < count; ++ndx) {
      const auto id = static_cast<id_t>(ndx);
      indexes_[id] = ndx;
      reverse_[ndx] = slot_t{handle_t{id}};
    }

    if constexpr (UseFifo) rebuild_fifo_list();
  }

private:
  // Actual data.
  std::vector<T, data_allocator_type> data_;

  // Lookup from ID to data index. May be larger than `data_`.
  enum_vector<size_type, id_t, index_allocator_type> indexes_;

  // Reverse lookup from data index to slot. May be larger than `data_`.
  // Each slot contains the handle (id + optional gen) and, when FIFO is
  // enabled, a next-pointer for the intrusive free list.
  std::vector<slot_t, slot_allocator_type> reverse_;

  // FIFO free-list head and tail.
  [[no_unique_address]] maybe_t<id_t, UseFifo> fifo_head_{id_t::invalid};
  [[no_unique_address]] maybe_t<id_t, UseFifo> fifo_tail_{id_t::invalid};

  // Allowed ID values are below this limit.
  id_t id_limit_{id_t::invalid};

  // Whether to throw on insert failure as opposed to returning
  // `id_t::invalid`.
  bool throw_on_insert_failure_{true};

  // Data structure:
  // `data_` is always sized to the number of elements currently stored.
  //
  // `indexes_` and `reverse_` are always the same size as each other, which is
  // the high water mark of IDs ever allocated (max(max_id()) + 1). Their size
  // is always >= `data_.size()`.
  //
  // The free list lives in the tail of `reverse_` — the slots past
  // `data_.size()`. When `UseFifo` is false, `alloc_id` simply takes the first
  // free slot (LIFO). When `UseFifo` is true, the free slots are threaded into
  // a singly-linked list via their `fifo_next_` fields, ordered by free time;
  // `alloc_id` pops from the head (oldest) and swaps the slot into the
  // tail-front position before the caller's `push_back` absorbs it.
  //
  // To erase an ID, we optionally bump the generation for its slot in
  // `reverse_` to invalidate any handles. We swap its element in `data_`
  // with the last element in `data_`, modify the corresponding `indexes_`
  // and `reverse_` so that nothing has changed from the outside, and
  // truncate `data_`. This leaves the erased slot consistent but referring
  // to an index past the end of `data_`, hence invalid. When `UseFifo` is
  // true, the slot is appended to the free-list tail at this point.
  //
  // When we `alloc_id`, we first check for a free slot. If found, we
  // reactivate it (LIFO or FIFO as above). Otherwise we expand and prefill
  // `indexes_` and `reverse_` with a new ID. Either way, the caller pushes
  // the new value to the back of `data_`.
};
}}} // namespace corvid::container::stable_id_vector
