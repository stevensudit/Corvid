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

#include <cstdint>
#include <limits>
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
template<typename T, typename ID = id_enums::id_t>
class stable_ids {
public:
  using id_t = ID;

  // TODO: Support ranged for by directly exposing iterator from the data
  // array.

  // TODO: Also expose read access to underlying array. Or, rather, allow
  // read/write of values without changing vector.

  // ID with generation count.
  struct handle_t {
    handle_t(id_t id, uint64_t gen = 0) : id_{id}, gen_{gen} {}
    handle_t(const handle_t&) = default;
    handle_t& operator=(const handle_t&) = default;

    auto operator<=>(const handle_t& other) const = default;

    id_t get_id() const { return id_; }
    uint64_t get_gen() const { return gen_; }

  private:
    id_t id_{id_t::invalid};
    uint64_t gen_{};

    friend class stable_ids<T, ID>;
  };

  stable_ids() = default;

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

  template<typename Self>
  decltype(auto) operator[](this Self&& self, id_t id) {
    const auto ndx = self.indexes_[*id];
    return std::forward<Self>(self).data_[ndx];
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
    // Because we use swap and pop, we can always check the end.

    // If there's a free ID, use it.
    if (reverse_.size() > data_.size()) {
      auto& h = reverse_[data_.size()];
      h.gen_++;
      return h.id_;
    }
    // Expand vectors and return new ID.
    const auto new_ndx = data_.size();
    const auto new_id = id_t{new_ndx};
    reverse_.push_back({new_id});
    indexes_.push_back(new_ndx);
    return new_id;
  }

private:
  // Actual data.
  std::vector<T> data_;

  // Lookup from ID to data index. May be larger than `data_`.
  enum_vector<size_t, id_t> indexes_;

  // Reverse lookup from data index to handle. May be larger than `data_`.
  std::vector<handle_t> reverse_;
};

// each container.

}}} // namespace corvid::container::stable_id_vector
