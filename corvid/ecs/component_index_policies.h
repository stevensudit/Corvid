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
#include <type_traits>
#include <utility>
#include <vector>

#include "../enums/sequence_enum.h"

namespace corvid { inline namespace ecs { inline namespace component_indexes {

// Reverse-index policies for `component_storage_base`.
//
// Each policy maps entity IDs to packed-array indices (`ndx`), supporting O(1)
// add/update and either O(1) or O(log K) lookup, depending on the strategy.
//
// clang-format off
//
// All three policies satisfy the same interface concept:
//
//   void insert(id_t id, size_type ndx) -- register or overwrite entity at
//                                          `ndx`
//   void update(id_t id, size_type ndx) -- update in-place; slot must
//                                          exist
//   size_type lookup(id_t id) const     -- return `ndx`; slot must
//                                          exist 
//   void erase(id_t id)                 -- remove entity; no-op where not
//                                          needed
//   void clear()                        -- reset all state
//
// clang-format on
//
// Callers confirm membership via the registry bitmap before calling `lookup`,
// `update`, or `erase`, so those methods may assert slot existence.
//
// Template parameters:
//   ID_T   - Entity ID enum type (e.g., `entity_id_t`). Must be a
//             `SequentialEnum` with unsigned underlying type.
//   SIZE_T - Type used to store packed-array indices. Defaults to the
//             underlying type of `ID_T`.

// Flat sparse reverse index.
//
// A plain `std::vector<size_type>`, indexed by `*id`. The vector grows to
// accommodate the highest entity ID ever inserted; all slots consume
// `sizeof(size_type)` bytes regardless of occupancy. O(1) lookup, insert, and
// update. Memory: O(max_entity_id x sizeof(size_type)).
//
// Best for: components held by a large fraction of entities, or scenes whose
// entity population is bounded and known in advance.
template<sequence::SequentialEnum ID_T,
    typename SIZE_T = std::underlying_type_t<ID_T>>
class flat_sparse_index {
public:
  using id_t = ID_T;
  using size_type = SIZE_T;

  // Insert or overwrite the ndx for `id`. Grows the vector if needed.
  void insert(id_t id, size_type ndx) {
    const auto raw = *id;
    if (raw >= data_.size()) data_.resize(raw + 1, size_type{});
    data_[raw] = ndx;
  }

  // Update the ndx for `id` in-place. Slot must already exist.
  void update(id_t id, size_type ndx) noexcept {
    assert(*id < data_.size());
    data_[*id] = ndx;
  }

  // Return the ndx for `id`. Slot must exist.
  [[nodiscard]] size_type lookup(id_t id) const noexcept {
    assert(*id < data_.size());
    return data_[*id];
  }

  // No-op: the bitmap is the source of truth; stale slots are harmless.
  void erase(id_t id) noexcept { (void)id; }

  // Clear all state.
  void clear() noexcept { data_.clear(); }

private:
  std::vector<size_type> data_;
};

// Sorted-pair reverse index.
//
// A `std::vector<std::pair<id_t, size_type>>` kept sorted by `id_t`. O(log K)
// lookup via binary search. O(K) insert and erase due to shifting. Memory:
// O(K x (sizeof(id_t) + sizeof(size_type))), with no wasted slots.
//
// Best for: components held by a small, slowly-changing set of entities.
template<sequence::SequentialEnum ID_T,
    typename SIZE_T = std::underlying_type_t<ID_T>>
class sorted_pair_index {
  using id_t = ID_T;
  using size_type = SIZE_T;
  using pair_t = std::pair<id_t, size_type>;
  using iter_t = std::vector<pair_t>::iterator;
  using citer_t = std::vector<pair_t>::const_iterator;

  [[nodiscard]] iter_t find_it(id_t id) {
    return std::lower_bound(data_.begin(), data_.end(), id,
        [](const pair_t& p, id_t v) { return p.first < v; });
  }

  [[nodiscard]] citer_t find_it(id_t id) const {
    return std::lower_bound(data_.cbegin(), data_.cend(), id,
        [](const pair_t& p, id_t v) { return p.first < v; });
  }

public:
  // Insert ndx for `id`. If a stale entry already exists (from a prior failed
  // `add` that succeeded here but was rolled back higher up), overwrite it in
  // place (upsert semantics) to prevent duplicate entries.
  void insert(id_t id, size_type ndx) {
    auto it = find_it(id);
    if (it != data_.end() && it->first == id)
      it->second = ndx; // overwrite stale phantom entry
    else
      data_.insert(it, {id, ndx});
  }

  // Update the ndx for `id` in-place. Slot must exist.
  void update(id_t id, size_type ndx) noexcept {
    auto it = find_it(id);
    assert(it != data_.end() && it->first == id);
    it->second = ndx;
  }

  // Return the ndx for `id`. Slot must exist.
  [[nodiscard]] size_type lookup(id_t id) const noexcept {
    auto it = find_it(id);
    assert(it != data_.cend() && it->first == id);
    return it->second;
  }

  // Erase the entry for `id`. Required to prevent stale duplicates on
  // reinsert.
  void erase(id_t id) {
    auto it = find_it(id);
    if (it != data_.end() && it->first == id) data_.erase(it);
  }

  // Clear all state.
  void clear() { data_.clear(); }

private:
  std::vector<pair_t> data_;
};

// Paged sparse reverse index.
//
// A two-level page table: an outer `std::vector<size_type*>` of page pointers
// (null = page not yet allocated), backed by `std::unique_ptr<size_type[]>`
// page objects. Pages are allocated on demand and freed all at once by
// `clear()`. O(1) lookup (two array accesses). Memory: only populated pages
// consume space (O(K_pages x PAGE_SIZE x sizeof(size_type))).
//
// Best for: components with moderate or variable entity counts, where neither
// flat (too wasteful) nor sorted (too slow at higher K) is appropriate.
//
// Template parameters:
//   PAGE_SIZE - Number of slots per page. Must be a power of two.
template<sequence::SequentialEnum ID_T,
    typename SIZE_T = std::underlying_type_t<ID_T>, size_t PAGE_SIZE = 256>
class paged_sparse_index {
  using id_t = ID_T;
  using size_type = SIZE_T;
  static constexpr size_t page_size_v = PAGE_SIZE;
  static_assert((page_size_v & (page_size_v - 1)) == 0,
      "PAGE_SIZE must be a power of two");

  static constexpr size_t page_of(size_t raw) noexcept {
    return raw / page_size_v;
  }
  static constexpr size_t slot_of(size_t raw) noexcept {
    return raw & (page_size_v - 1);
  }

  size_type* get_or_alloc_page(size_t pg) {
    if (pg >= page_dir_.size()) page_dir_.resize(pg + 1, nullptr);
    if (!page_dir_[pg]) {
      alloc_.push_back(std::make_unique<size_type[]>(page_size_v));
      page_dir_[pg] = alloc_.back().get();
    }
    return page_dir_[pg];
  }

public:
  // Insert or overwrite the ndx for `id`.
  void insert(id_t id, size_type ndx) {
    const auto raw = *id;
    auto* page = get_or_alloc_page(page_of(raw));
    page[slot_of(raw)] = ndx;
  }

  // Update the ndx for `id` in-place. Slot must exist.
  void update(id_t id, size_type ndx) noexcept {
    const auto raw = *id;
    assert(page_of(raw) < page_dir_.size());
    assert(page_dir_[page_of(raw)] != nullptr);
    page_dir_[page_of(raw)][slot_of(raw)] = ndx;
  }

  // Return the ndx for `id`. Slot must exist.
  [[nodiscard]] size_type lookup(id_t id) const noexcept {
    const auto raw = *id;
    assert(page_of(raw) < page_dir_.size());
    assert(page_dir_[page_of(raw)] != nullptr);
    return page_dir_[page_of(raw)][slot_of(raw)];
  }

  // No-op: the bitmap is the source of truth; stale slots are harmless.
  void erase(id_t id) noexcept { (void)id; }

  // Free all pages and reset the directory.
  void clear() noexcept {
    page_dir_.clear();
    alloc_.clear();
  }

private:
  // Outer directory: page index -> raw pointer into the page's array.
  // `nullptr` means the page has not been allocated yet.
  std::vector<size_type*> page_dir_;

  // Owning storage for each allocated page. Freed all at once by `clear()`.
  std::vector<std::unique_ptr<size_type[]>> alloc_;
};

}}} // namespace corvid::ecs::component_indexes
