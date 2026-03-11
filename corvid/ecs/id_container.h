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
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>

#include "../containers/enum_vector.h"
#include "entity_ids.h"

namespace corvid { inline namespace ecs { inline namespace id_containers {

// ECS ID-keyed flat vector.
//
// Wraps `enum_vector<T, ID, A>` plus an `ID limit_`, using composition instead
// of inheritance for control. There is no free list, no FIFO/LIFO, and no
// defined sentinel. Callers are responsible for interpreting slot content.
//
// The ID limit is the exclusive upper bound on valid IDs. It defaults to
// `id_t::invalid` (the maximum representable value), i.e., effectively
// unlimited. Callers may lower it to enforce a stricter cap. Combining this
// with `reserve` or `resize` allows preallocating a fixed-size pool of IDs.
//
// ID requirements:
//   - Must be a `SequentialEnum`.
//   - The underlying type must be unsigned.
//   - `id_t::invalid` must equal the maximum value of the underlying type.
//
// Template parameters:
//   T   - Value type stored at each slot.
//   ID  - ECS ID enum type (e.g., `entity_id_t`, `store_id_t`).
//   A   - Allocator for `T`. Defaults to `std::allocator<T>`.
template<typename T, sequence::SequentialEnum ID, class A = std::allocator<T>>
class id_container {
public:
  using id_t = ID;
  using size_type = std::underlying_type_t<id_t>;
  using value_type = T;
  using allocator_type = A;

  static_assert(*id_t::invalid == std::numeric_limits<size_type>::max(),
      "id_container: ID type must define 'invalid' as the maximum value of "
      "its underlying type");

  static_assert(std::is_unsigned_v<size_type>,
      "id_container: ID underlying type must be unsigned");

  // Construction.

  id_container() = default;

  explicit id_container(const allocator_type& alloc) : data_{alloc} {}

  explicit id_container(id_t limit,
      allocation_policy prefill = allocation_policy::lazy,
      const allocator_type& alloc = allocator_type{})
      : data_{alloc} {
    (void)set_id_limit(limit, prefill);
  }

  [[nodiscard]] allocator_type get_allocator() const noexcept {
    return data_.get_allocator();
  }

  // ID limit.

  // Exclusive upper bound on valid IDs. Defaults to `id_t::invalid`.
  [[nodiscard]] id_t id_limit() const noexcept { return limit_; }

  // Change the ID limit. Fails when the new limit would invalidate live IDs.
  // When expanding and `prefill` is `allocation_policy::eager`, reserves but
  // does not resize.
  [[nodiscard]] bool set_id_limit(id_t new_limit,
      allocation_policy prefill = allocation_policy::lazy) {
    if (new_limit < size_as_enum()) return false;
    limit_ = new_limit;
    if (prefill == allocation_policy::eager) reserve(*limit_);
    return true;
  }

  // Capacity and size.

  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(data_.size());
  }

  [[nodiscard]] id_t size_as_enum() const noexcept {
    return data_.size_as_enum();
  }

  [[nodiscard]] size_type capacity() const noexcept {
    return static_cast<size_type>(data_.capacity());
  }

  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

  // Memory management.

  void reserve(size_type new_cap) { data_.reserve(new_cap); }

  void resize(size_type count) { data_.resize(count); }
  void resize(size_type count, const value_type& value) {
    const auto new_size = static_cast<id_t>(count);
    if (limit_ < new_size) limit_ = new_size;
    data_.resize(count, value);
  }

  void clear() noexcept { data_.clear(); }

  void shrink_to_fit() { data_.shrink_to_fit(); }

  // Element access.

  // Access slot by ID. No bounds check.
  [[nodiscard]] decltype(auto) operator[](this auto& self, id_t id) noexcept {
    assert(id < self.size_as_enum());
    return self.data_[id];
  }

  // Access slot by ID with bounds checking; throws std::out_of_range.
  [[nodiscard]] decltype(auto) at(this auto& self, id_t id) {
    return self.data_.at(id);
  }

  [[nodiscard]] decltype(auto) front(this auto& self) {
    return self.data_.front();
  }

  [[nodiscard]] decltype(auto) back(this auto& self) {
    return self.data_.back();
  }

  [[nodiscard]] decltype(auto) data(this auto& self) noexcept {
    return self.data_.data();
  }

  // Modifiers.

  [[nodiscard]] bool push_back(const value_type& value) {
    if (size_as_enum() >= limit_) return false;
    data_.push_back(value);
    return true;
  }

  [[nodiscard]] bool push_back(value_type&& value) {
    if (size_as_enum() >= limit_) return false;
    data_.push_back(std::move(value));
    return true;
  }

  template<class... Args>
  [[nodiscard]] value_type* emplace_back(Args&&... args) {
    if (size_as_enum() >= limit_) return nullptr;
    return &data_.emplace_back(std::forward<Args>(args)...);
  }

  void pop_back() { data_.pop_back(); }

  // Iteration over all allocated slots (slot 0 through size()-1).

  [[nodiscard]] decltype(auto) begin(this auto& self) noexcept {
    return self.data_.begin();
  }

  [[nodiscard]] decltype(auto) end(this auto& self) noexcept {
    return self.data_.end();
  }

  [[nodiscard]] auto cbegin() const noexcept { return data_.cbegin(); }
  [[nodiscard]] auto cend() const noexcept { return data_.cend(); }

  // Direct access to the underlying enum_vector.
  [[nodiscard]] decltype(auto) underlying(this auto& self) noexcept {
    return self.data_.underlying();
  }

private:
  enum_vector<value_type, id_t, allocator_type> data_;
  id_t limit_{id_t::invalid};
};

}}} // namespace corvid::ecs::id_containers
