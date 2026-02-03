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
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
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

// A vector to store elements by stable ID, suitable for Entity Component
// Systems.
//
// It allows linear iteration, fixed-time lookups by ID, as well as fixed-time
// insertion and removal. The IDs remain stable throughout although entities
// may be moved around in memory.
//
// By specializing on an `ID` that's unique to your container instance, you get
// type-safe IDs. Note that `ID` must be a sequential enum type and define an
// `invalid` value, as shown by `id_t`.
//
// Based loosely on https://github.com/johnBuffer/StableIndexVector.
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

  // TODO: Consider refactoring to make generation optional, perhaps through
  // traits.

  // ID with generation count.
  struct handle_t {
    handle_t(id_t id, size_type gen = 0) : id_{id}, gen_{gen} {}
    handle_t(const handle_t&) = default;
    handle_t& operator=(const handle_t&) = default;

    auto operator<=>(const handle_t& other) const = default;

    id_t get_id() const { return id_; }
    size_type get_gen() const { return gen_; }

  private:
    id_t id_{id_t::invalid};
    size_type gen_{};

    friend class stable_ids<T, ID, Allocator>;
  };

  using handle_allocator_type = typename std::allocator_traits<
      Allocator>::template rebind_alloc<handle_t>;

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
  }

  id_t push_back(const T& value) {
    const auto id = alloc_id();
    data_.push_back(value);
    return id;
  }

  template<typename... Args>
  id_t emplace_back(Args&&... args) {
    const auto id = alloc_id();
    data_.emplace_back(std::forward<Args>(args)...);
    return id;
  }

  void erase(id_t id) {
    assert(id != id_t::invalid);
    assert(*id < indexes_.size());
    assert(indexes_[*id] < data_.size());
    const auto ndx = indexes_[*id];
    const auto last_ndx = data_.size() - 1;
    const auto last_id = reverse_[last_ndx].id_;

    // Invalidate handle by bumping generation.
    ++reverse_[ndx].gen_;

    // Swap and pop.
    std::swap(data_[ndx], data_[last_ndx]);
    std::swap(indexes_[*id], indexes_[*last_id]);
    std::swap(reverse_[ndx], reverse_[last_ndx]);
    data_.pop_back();
  }

  void erase_if(auto pred) {
    for (size_type ndx{}; ndx < data_.size();) {
      if (pred(data_[ndx]))
        erase(ndx);
      else
        ++ndx;
    }
  }

  void clear() noexcept { data_.clear(); }

  void shrink_to_fit() {
    indexes_.resize(data_.size());
    reverse_.resize(data_.size());
    data_.shrink_to_fit();
    indexes_.shrink_to_fit();
    reverse_.shrink_to_fit();
  }

  void reserve(size_type new_cap) {
    data_.reserve(new_cap);
    indexes_.reserve(new_cap);
    reverse_.reserve(new_cap);
  }

  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(data_.size());
  }

  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

  [[nodiscard]] decltype(auto) operator[](this auto& self, id_t id) noexcept {
    assert(id != id_t::invalid);
    assert(*id < self.indexes_.size());
    assert(self.indexes_[*id] < self.data_.size());
    const auto ndx = self.indexes_[*id];
    return std::forward<decltype(self)>(self).data_[ndx];
  }

  [[nodiscard]] decltype(auto) at(this auto& self, id_t id) {
    if (id == id_t::invalid) throw std::out_of_range("id out of range");
    if (*id >= self.indexes_.size())
      throw std::out_of_range("id out of range");
    const auto ndx = self.indexes_[*id];
    if (ndx >= self.data_.size()) throw std::out_of_range("id out of range");
    return std::forward<decltype(self)>(self).data_[ndx];
  }

  [[nodiscard]] auto span(this auto& self) noexcept {
    return std::span{self.data_};
  }

  auto begin(this auto& self) noexcept {
    return std::forward<decltype(self)>(self).data_.begin();
  }
  auto end(this auto& self) noexcept {
    return std::forward<decltype(self)>(self).data_.end();
  }
  auto cbegin(this auto& self) noexcept {
    return std::forward<decltype(self)>(self).data_.cbegin();
  }
  auto cend(this auto& self) noexcept {
    return std::forward<decltype(self)>(self).data_.cend();
  }

  // TODO: get handle for id

private:
  // Allocate a new slot, returning its ID.
  id_t alloc_slot() {
    const auto id = alloc_id();
    indexes_[*id] = id_t{data_.size()};
    return id;
  }

  // Allocate a new ID, either by reusing a freed one or creating a new one.
  id_t alloc_id() {
    // If there's a free ID, use it.
    if (reverse_.size() > data_.size()) {
      auto& h = reverse_[data_.size()];
      h.gen_++;
      return h.id_;
    }
    // Expand vectors and return new ID.
    const auto new_ndx = data_.size();
    const auto new_id = id_t{new_ndx};
    if (new_ndx == *id_t::invalid)
      throw std::overflow_error("stable_ids: exceeded maximum id");
    reverse_.push_back({new_id});
    indexes_.push_back(new_ndx);
    return new_id;
  }

private:
  void erase(size_type ndx) {
    assert(ndx < data_.size());
    const auto last_ndx = data_.size() - 1;
    const auto last_id = reverse_[last_ndx].id_;

    // Invalidate handle by bumping generation.
    ++reverse_[ndx].gen_;

    // Swap and pop.
    std::swap(data_[ndx], data_[last_ndx]);
    std::swap(indexes_[*reverse_[ndx].id_], indexes_[*last_id]);
    std::swap(reverse_[ndx], reverse_[last_ndx]);
    data_.pop_back();
  }

private:
  // Actual data.
  std::vector<T, data_allocator_type> data_;

  // Lookup from ID to data index. May be larger than `data_`.
  enum_vector<size_type, id_t, index_allocator_type> indexes_;

  // Reverse lookup from data index to handle. May be larger than `data_`.
  std::vector<handle_t, handle_allocator_type> reverse_;
};

// each container.

}}} // namespace corvid::container::stable_id_vector
