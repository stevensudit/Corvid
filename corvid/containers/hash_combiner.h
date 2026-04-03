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
#include "containers_shared.h"

#include <cstddef>
#include <functional>

namespace corvid { inline namespace hash_combiners {

// Incrementally combine hashes for compound keys.
//
// The mixer matches the common boost-style combine formula and is suitable for
// composing the results of `std::hash` across multiple fields.
//
// Example:
//
//   size_t operator()(const my_key& key) const {
//     hash_combiner hash;
//     hash.combine(key.name);
//     hash.combine(key.index);
//     return hash.value();
//   }
class hash_combiner {
public:
  constexpr hash_combiner() noexcept = default;
  constexpr explicit hash_combiner(size_t seed) noexcept : seed_(seed) {}

  // Return the current accumulated hash value.
  [[nodiscard]] constexpr size_t value() const noexcept { return seed_; }
  [[nodiscard]] constexpr explicit operator size_t() const noexcept {
    return seed_;
  }

  // Mix an already-computed hash value into the accumulator.
  constexpr void combine_hash(size_t hash) noexcept {
    seed_ ^= hash + 0x9e3779b97f4a7c15ULL + (seed_ << 6) + (seed_ >> 2);
  }

  // Hash a value with `std::hash` and fold it into the accumulator.
  template<typename T>
  constexpr void
  combine(const T& value) noexcept(noexcept(std::hash<T>{}(value))) {
    combine_hash(std::hash<T>{}(value));
  }

  // Convenience overload for combining several values in sequence.
  template<typename... Ts>
  constexpr void combine_all(const Ts&... values) noexcept(
      (noexcept(std::hash<Ts>{}(values)) && ...)) {
    (combine(values), ...);
  }

private:
  size_t seed_ = 0;
};

// Return `seed` after mixing in a precomputed hash value.
[[nodiscard]] constexpr size_t
combine_hash(size_t seed, size_t hash) noexcept {
  auto combiner = hash_combiner{seed};
  combiner.combine_hash(hash);
  return combiner.value();
}

// Return `seed` after hashing and combining one value.
template<typename T>
[[nodiscard]] constexpr size_t combine_hash(size_t seed,
    const T& value) noexcept(noexcept(std::hash<T>{}(value))) {
  auto combiner = hash_combiner{seed};
  combiner.combine(value);
  return combiner.value();
}

// Hash several values as an ordered tuple-like sequence.
//
// This is a compact helper for simple compound keys:
//
//   return combined_hash(key.name, key.index, key.kind);
template<typename... Ts>
[[nodiscard]] constexpr size_t combined_hash(const Ts&... values) noexcept(
    (noexcept(std::hash<Ts>{}(values)) && ...)) {
  auto combiner = hash_combiner{};
  combiner.combine_all(values...);
  return combiner.value();
}

}} // namespace corvid::hash_combiners
