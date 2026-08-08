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
#include <cstddef>
#include <iterator>
#include <type_traits>

#include "entity_ids.h"

namespace corvid { inline namespace ecs { inline namespace storage_iterators {

// Shared iteration machinery for the single-component entity storages.
//
// `mono_archetype_storage` and `component_storage` both pack one component
// type into a contiguous vector parallel to an entity-ID vector. This header
// provides the contiguous iterator and read-only row view they expose,
// parameterized on the storage type.

#pragma region single_component_row_view

// Read-only view of a single entity's row in a single-component storage.
//
// Provides a `component<T>` accessor uniform with archetype storages (only
// valid for `T == C`), plus an implicit conversion to `const C&`.
template<typename C, typename ID>
struct single_component_row_view {
  const C& value;
  ID entity_id{};

  [[nodiscard]] operator const C&() const noexcept { return value; }

  // Uniform accessor (`C` only).
  template<typename T>
  [[nodiscard]] const T& component() const noexcept {
    static_assert(std::is_same_v<T, C>,
        "this storage only has one component type");
    return value;
  }

  [[nodiscard]] ID id() const noexcept { return entity_id; }
};

#pragma endregion
#pragma region contiguous_storage_iterator

// Contiguous iterator over a single-component storage's packed components.
//
// Dereferencing yields a `component_t` reference; `id` returns the entity ID
// at the current position. `STORAGE` provides the `component_t`, `id_t`, and
// `size_type` aliases and grants this class access to its `components_` and
// `ids_` vectors via friendship; the storage itself is befriended so its
// `begin`/`end` can call the private constructor.
template<typename STORAGE, access ACCESS>
class contiguous_storage_iterator {
public:
  static constexpr bool mutable_v = static_cast<bool>(ACCESS);
  using iterator_category = std::contiguous_iterator_tag;
  using iterator_concept = std::contiguous_iterator_tag;
  using value_type = STORAGE::component_t;
  using id_t = STORAGE::id_t;
  using size_type = STORAGE::size_type;
  using difference_type = std::ptrdiff_t;
  using reference =
      std::conditional_t<mutable_v, value_type&, const value_type&>;
  using pointer =
      std::conditional_t<mutable_v, value_type*, const value_type*>;
  using storage_ptr = std::conditional_t<mutable_v, STORAGE*, const STORAGE*>;

  contiguous_storage_iterator() = default;
  contiguous_storage_iterator(const contiguous_storage_iterator&) = default;
  contiguous_storage_iterator(contiguous_storage_iterator&&) = default;
  contiguous_storage_iterator& operator=(
      const contiguous_storage_iterator&) = default;
  contiguous_storage_iterator& operator=(
      contiguous_storage_iterator&&) = default;

  // Converting constructor: `iterator` to `const_iterator`.
  template<access OTHER>
  contiguous_storage_iterator(
      const contiguous_storage_iterator<STORAGE, OTHER>& other)
  requires(!mutable_v && static_cast<bool>(OTHER))
      : storage_{other.storage_}, ndx_{other.ndx_} {}

  [[nodiscard]] reference operator*() const {
    return storage_->components_[ndx_];
  }
  [[nodiscard]] pointer operator->() const {
    return &storage_->components_[ndx_];
  }

  [[nodiscard]] id_t id() const { return storage_->ids_[ndx_]; }

  contiguous_storage_iterator& operator++() {
    ++ndx_;
    return *this;
  }
  contiguous_storage_iterator operator++(int) {
    auto tmp = *this;
    ++ndx_;
    return tmp;
  }
  contiguous_storage_iterator& operator--() {
    --ndx_;
    return *this;
  }
  contiguous_storage_iterator operator--(int) {
    auto tmp = *this;
    --ndx_;
    return tmp;
  }

  contiguous_storage_iterator& operator+=(difference_type n) {
    ndx_ += n;
    return *this;
  }
  contiguous_storage_iterator& operator-=(difference_type n) {
    ndx_ -= n;
    return *this;
  }
  [[nodiscard]] contiguous_storage_iterator operator+(
      difference_type n) const {
    auto tmp = *this;
    return tmp += n;
  }
  [[nodiscard]] contiguous_storage_iterator operator-(
      difference_type n) const {
    auto tmp = *this;
    return tmp -= n;
  }
  [[nodiscard]] difference_type operator-(
      const contiguous_storage_iterator& o) const {
    return static_cast<difference_type>(ndx_) -
           static_cast<difference_type>(o.ndx_);
  }

  [[nodiscard]] reference operator[](difference_type n) const {
    return storage_->components_[ndx_ + n];
  }

  [[nodiscard]] friend contiguous_storage_iterator
  operator+(difference_type n, const contiguous_storage_iterator& it) {
    return it + n;
  }

  [[nodiscard]] bool operator==(const contiguous_storage_iterator& o) const {
    assert(storage_ == o.storage_);
    return ndx_ == o.ndx_;
  }
  [[nodiscard]] auto operator<=>(const contiguous_storage_iterator& o) const {
    assert(storage_ == o.storage_);
    return ndx_ <=> o.ndx_;
  }

private:
  storage_ptr storage_{};
  size_type ndx_{};

  contiguous_storage_iterator(storage_ptr s, size_type ndx)
      : storage_{s}, ndx_{ndx} {}
  friend STORAGE;
  template<typename, access>
  friend class contiguous_storage_iterator;
};

#pragma endregion
}}} // namespace corvid::ecs::storage_iterators
