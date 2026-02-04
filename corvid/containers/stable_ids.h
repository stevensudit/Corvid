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
// Component Systems.
//
// It allows linear iteration, fixed-time lookups by ID, as well as fixed-time
// insertion and removal. The IDs remain stable throughout although entities
// may be moved around in memory. A handle can be used to detect ID reuse.
//
// ID values range from 0 to N-1, where N is the number of elements ever added
// to the container. When an element is removed, its ID may be reused for a new
// one. If this is problematic, use a handle to detect the change.
//
// By specializing on an ID type that's unique to your container instance, you
// get type-safe IDs. Note that it must be a sequential enum type and define an
// `invalid` value equal to its max, as shown by `id_t`.
//
// Insertion may throw `std::overflow_error` if the maximum ID value is
// exceeded. Alternately, you can disable throwing by calling
// `throw_on_insert_failure(false)`, in which case `id_t::invalid` is
// returned.
//
// Motivated by https://github.com/johnBuffer/StableIndexVector.
template<typename T, typename ID = id_enums::id_t,
    class Allocator = std::allocator<T>>
class stable_ids {
public:
  using id_t = ID;
  using size_type = std::underlying_type_t<id_t>;
  using allocator_type = Allocator;
  using data_allocator_type = Allocator;
  using index_allocator_type = typename std::allocator_traits<
      Allocator>::template rebind_alloc<size_type>;

  static_assert(*id_t::invalid ==
                    std::numeric_limits<std::underlying_type_t<id_t>>::max(),
      "ID type for stable_ids must define 'invalid' as the maximum value of "
      "its underlying type");

  // TODO: Consider writing a drop-in replacement for vector that contains
  // parallel heterogeneous vectors, such as for position and color and so on.
  // This would let use a common indexing scheme across all of these. We'll
  // want a fake object with a pointer to the container and an index, so that
  // we can have references to the elements. But we'll also want the traits to
  // implement the swap method for this multivector container so that it's
  // reasonably efficient.

  // TODO: Consider refactoring to make generation optional, perhaps through
  // traits.

  // A handle contains the generation in addition to the ID, allowing detection
  // of ID reuse.
  //
  // TODO: Consider adding an even heavier handle that contains a pointer to
  // this instance, preventing IDs from being used outside their scope.
  struct handle_t {
  private:
    handle_t(id_t id, size_type gen = 0) : id_{id}, gen_{gen} {}

  public:
    handle_t() = default;
    handle_t(const handle_t&) = default;
    handle_t& operator=(const handle_t&) = default;

    auto operator<=>(const handle_t& other) const = default;

    [[nodiscard]] id_t get_id() const { return id_; }
    [[nodiscard]] size_type get_gen() const { return gen_; }

  private:
    id_t id_{id_t::invalid};
    size_type gen_{};

    friend class stable_ids<T, ID, Allocator>;
  };

  using handle_allocator_type = typename std::allocator_traits<
      Allocator>::template rebind_alloc<handle_t>;

  static_assert(std::is_trivially_copyable_v<handle_t>);
  static_assert(sizeof(handle_t) <= 16);

  // Construction.
  stable_ids() = default;
  explicit stable_ids(const allocator_type& alloc)
      : data_{alloc}, indexes_{index_allocator_type{alloc}},
        reverse_{handle_allocator_type{alloc}} {}
  stable_ids(stable_ids&&) noexcept = default;

  stable_ids& operator=(stable_ids&&) noexcept = default;

  friend void swap(stable_ids& lhs, stable_ids& rhs) noexcept {
    using std::swap;
    swap(lhs.data_, rhs.data_);
    swap(lhs.indexes_, rhs.indexes_);
    swap(lhs.reverse_, rhs.reverse_);
    swap(lhs.throw_on_insert_failure_, rhs.throw_on_insert_failure_);
  }

  // Control whether insertion throws on ID overflow or returns
  // `id_t::invalid`.
  [[nodiscard]] bool throw_on_insert_failure() const noexcept {
    return throw_on_insert_failure_;
  }
  void throw_on_insert_failure(bool value) noexcept {
    throw_on_insert_failure_ = value;
  }

  // Push a new element, returning its assigned ID.
  [[nodiscard]] id_t push_back(const T& value) {
    const auto id = alloc_id();
    if (id != id_t::invalid)
      data_.push_back(value);
    else if (throw_on_insert_failure_)
      throw std::overflow_error("stable_ids: exceeded maximum id");

    return id;
  }

  // Emplace a new element, returning its assigned ID.
  template<typename... Args>
  [[nodiscard]] id_t emplace_back(Args&&... args) {
    const auto id = alloc_id();
    if (id != id_t::invalid)
      data_.emplace_back(std::forward<Args>(args)...);
    else if (throw_on_insert_failure_)
      throw std::overflow_error("stable_ids: exceeded maximum id");

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
    if (!is_valid(id)) return {id_t::invalid};

    const auto ndx = indexes_[id];
    return reverse_[ndx];
  }

  // Checks whether ID is valid.
  [[nodiscard]] bool is_valid(id_t id) const {
    if (*id >= indexes_.size()) return false;
    const auto ndx = indexes_[id];
    if (ndx >= data_.size()) return false;
    assert(reverse_[ndx].id_ == id);
    return true;
  }

  // Checks whether handle is valid.
  [[nodiscard]] bool is_valid(handle_t handle) const {
    const auto id = handle.id_;
    if (*id >= indexes_.size()) return false;
    const auto ndx = indexes_[id];
    if (ndx >= data_.size()) return false;
    auto& real_handle = reverse_[ndx];
    assert(real_handle.id_ == id);
    return real_handle.gen_ == handle.gen_;
  }

  // Erase element by ID. Returns true if erased, false if ID was invalid.
  bool erase(id_t id) {
    if (!is_valid(id)) return false;
    return do_erase(id);
  }

  // Erase element by handle. Returns true if erased, false if handle was
  // invalid.
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
    data_.clear();
    if (shrink) {
      indexes_.clear();
      reverse_.clear();
      data_.shrink_to_fit();
      indexes_.shrink_to_fit();
      reverse_.shrink_to_fit();
    } else {
      for (auto& h : reverse_) ++h.gen_;
    }
  }

  // Reduce memory usage to fit current size. Note that this does not preserve
  // generations, hence it cannot guarantee invalidating handles. Do not call
  // if you might have dangling handles.
  void shrink_to_fit() {
    // If already empty, just clear with shrink.
    const auto live_size = data_.size();
    if (live_size == 0) {
      clear(true);
      return;
    }

    // IDs can be sparse; find the highest live ID to size the mappings.
    const auto new_size = *find_max_extant_id() + 1u;
    if (new_size != reverse_.size()) {
      indexes_.resize(new_size);
      reverse_.resize(new_size);

      // Rebuild the free-list tail in-place. An ID is live iff its index
      // points into the live range and reverse_ confirms the match. Freed IDs
      // are placed into the tail; live entries are left untouched.
      size_type free_pos = live_size;
      for (id_t id{}; *id < new_size; ++id) {
        const auto ndx = indexes_[id];
        if (ndx < live_size && reverse_[ndx].id_ == id) continue;
        indexes_[id] = free_pos;
        reverse_[free_pos] = {id};
        ++free_pos;
      }
    }

    data_.shrink_to_fit();
    indexes_.shrink_to_fit();
    reverse_.shrink_to_fit();
  }

  // Reserve space for at least `new_cap` elements.
  void reserve(size_type new_cap) {
    // TODO: Consider prefilling `indexes_` and `reverse_` instead of just
    // reserving.
    data_.reserve(new_cap);
    indexes_.reserve(new_cap);
    reverse_.reserve(new_cap);
  }

  // Return current size. This is the count of elements actually present,
  // unrelated to their IDs.
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
      const auto id = reverse_[ndx].id_;
      if (id > max_id) max_id = id;
    }

    return max_id;
  }

  // Return next ID to be allocated.
  [[nodiscard]] id_t next_id() const noexcept {
    if (reverse_.size() > data_.size()) return reverse_[data_.size()].id_;
    return max_id() + 1u;
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
  id_t alloc_id() {
    const auto new_ndx = data_.size();

    // If there's a free ID, use it.
    if (reverse_.size() > data_.size()) return reverse_[new_ndx].id_;

    // Otherwise, expand indexes with new ID.
    const auto new_id = static_cast<id_t>(new_ndx);
    if (new_id != id_t::invalid) {
      reverse_.push_back({new_id});
      indexes_.push_back(new_ndx);
    } else if (throw_on_insert_failure_)
      throw std::overflow_error("stable_ids: exceeded maximum id");

    return new_id;
  }

  // Swap-and-pop erase helper.
  bool swap_and_pop(size_type ndx, size_type last_ndx, id_t id, id_t last_id) {
    // Assumes validity checked by caller.

    // Invalidate handle by bumping generation.
    ++reverse_[ndx].gen_;

    // Swap and pop.
    std::swap(data_[ndx], data_[last_ndx]);
    std::swap(indexes_[id], indexes_[last_id]);
    std::swap(reverse_[ndx], reverse_[last_ndx]);
    data_.pop_back();
    return true;
  }

  // Erase by ID helper.
  bool do_erase(id_t id) {
    // Assumes validity checked by caller.
    assert(is_valid(id));
    const auto ndx = indexes_[id];
    const auto last_ndx = data_.size() - 1;
    const auto last_id = reverse_[last_ndx].id_;
    return swap_and_pop(ndx, last_ndx, id, last_id);
  }

  // Erase by data index helper.
  void do_erase(size_type ndx) {
    // Assumes validity checked by caller.
    assert(ndx < data_.size());
    const auto last_ndx = data_.size() - 1;
    const auto last_id = reverse_[last_ndx].id_;
    const auto id = reverse_[ndx].id_;
    assert(is_valid(id));
    swap_and_pop(ndx, last_ndx, id, last_id);
  }

private:
  // Actual data.
  std::vector<T, data_allocator_type> data_;

  // Lookup from ID to data index. May be larger than `data_`.
  enum_vector<size_type, id_t, index_allocator_type> indexes_;

  // Reverse lookup from data index to handle. May be larger than `data_`.
  std::vector<handle_t, handle_allocator_type> reverse_;

  // Whether to throw on insert failure as opposed to returning
  // `id_t::invalid`.
  bool throw_on_insert_failure_{true};

  // Data structure:
  // `data_` is always sized to the number of elements currently stored.
  //
  // `indexes_` and `reverse_` are always the same size as each other, which is
  // the high water mark of IDs ever allocated (max_id_ever + 1). Their size is
  // always >= `data_.size()`.
  //
  // The free list lives in the tail of `reverse_`. It happens to be a LIFO,
  // although order doesn't matter for correctnes. This tail is defined as all
  // the elements of `reverse_` past the size of `data_`.
  //
  // To erase an ID, we first bump up the generation for its element in
  // `reverse_`, to invalidate any handles. We swap its element in `data_` with
  // the last element in `data_`.  We then modify the corresponding `indexes_`
  // and `reverse_` so that nothing has changed from the outside.
  //
  // In other words, the ID still maps to the correct element: it just so
  // happens to have moved. However, now that it's at the end, we can just
  // truncate `data_`. This leaves the corresponding element in `indexes_` and
  // `reverse_` consistent with each other, but referring to an index past the
  // end of `data_`, hence invalid.
  //
  // When we alloc_id(), we first check for whether there's a tail. If
  // `reverse_` is larger than `data_`, then there's a tail. If so, we
  // reactivate the element that's just past the end of what's in `data_`.
  // Otherwise, we have to expand and prefill `indexes_` and `reverse_` with a
  // new ID. Either way, we push the new value to the back of `data_`.
};
}}} // namespace corvid::container::stable_id_vector
