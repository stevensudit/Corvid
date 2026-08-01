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
#include <format>
#include <unordered_map>
#include <functional>
#include <cstddef>

#include "../../meta/formatting.h"

namespace corvid { inline namespace container { inline namespace indirect_key {

// Indirect keys are similar to `std::reference_wrapper`, but are designed to
// be used as keys in associative containers.
//
// This is useful when the key is already stored in another container, which
// guarantees that the value will not be moved, but we want an associative
// container to act as an additional index.

#pragma region indirect_hash_key

// Indirect key for use in hash containers. Contains a reference to the key and
// acts more or less like the key, but is lightweight.
template<typename T, typename H = std::hash<T>, typename E = std::equal_to<T>>
struct indirect_hash_key {
#pragma region Key

  const T& key;

  constexpr indirect_hash_key(const T& key) : key{key} {}

  // We don't want to bind to a temporary.
  indirect_hash_key(const T&&) = delete;

  [[nodiscard]] constexpr operator const T&() const noexcept { return key; }

#pragma endregion
#pragma region hash_equal_to

  struct hash_equal_to {
    using is_transparent = void;

    [[nodiscard]] constexpr size_t operator()(
        const indirect_hash_key& ik) const noexcept(noexcept(H{}(ik.key))) {
      return H{}(ik.key);
    }
    template<typename U>
    [[nodiscard]] constexpr size_t operator()(const U& u) const
        noexcept(noexcept(H{}(u))) {
      return H{}(u);
    }

    [[nodiscard]] constexpr bool
    operator()(const indirect_hash_key& l, const indirect_hash_key& r) const
        noexcept(noexcept(E{}(l.key, r.key))) {
      return E{}(l.key, r.key);
    }
    template<typename U>
    [[nodiscard]] constexpr bool operator()(const indirect_hash_key& l,
        const U& r) const noexcept(noexcept(E{}(l.key, r))) {
      return E{}(l.key, r);
    }
    template<typename U>
    [[nodiscard]] constexpr bool operator()(const U& l,
        const indirect_hash_key& r) const noexcept(noexcept(E{}(l, r.key))) {
      return E{}(l, r.key);
    }
  };

#pragma endregion
};

#pragma endregion
#pragma region indirect_map_key

// Indirect key for use in map containers. Contains a reference to the key and
// acts more or less like the key, but is lightweight.
template<typename T, class C = std::less<T>>
struct indirect_map_key {
#pragma region Key

  const T& key;

  constexpr indirect_map_key(const T& key) : key{key} {}

  // We don't want to bind to a temporary.
  indirect_map_key(const T&&) = delete;

  [[nodiscard]] operator const T&() const noexcept { return key; }

  [[nodiscard]] constexpr bool operator<(const indirect_map_key& r) const
      noexcept(noexcept(C{}(key, r.key))) {
    return C{}(key, r.key);
  }

#pragma endregion
#pragma region compare

  struct compare {
    using is_transparent = void;

    [[nodiscard]] constexpr bool
    operator()(const indirect_map_key& l, const indirect_map_key& r) const
        noexcept(noexcept(C{}(l.key, r.key))) {
      return C{}(l.key, r.key);
    }
    template<typename U>
    [[nodiscard]] constexpr bool operator()(const indirect_map_key& l,
        const U& r) const noexcept(noexcept(C{}(l.key, r))) {
      return C{}(l.key, r);
    }
    template<typename U>
    [[nodiscard]] constexpr bool operator()(const U& l,
        const indirect_map_key& r) const noexcept(noexcept(C{}(l, r.key))) {
      return C{}(l, r.key);
    }
  };

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::container::indirect_key

// NOLINTBEGIN(bugprone-std-namespace-modification).
template<typename T, typename H, typename E>
struct std::hash<corvid::indirect_hash_key<T, H, E>>
    : corvid::indirect_hash_key<T, H, E>::hash_equal_to {};

template<typename T, typename H, typename E>
struct std::equal_to<corvid::indirect_hash_key<T, H, E>>
    : corvid::indirect_hash_key<T, H, E>::hash_equal_to {};

template<typename T, typename C>
struct std::less<corvid::indirect_map_key<T, C>>
    : corvid::indirect_map_key<T, C>::compare {};

template<typename T, typename H, typename E, typename CharT>
requires std::formattable<T, CharT>
struct std::formatter<corvid::indirect_hash_key<T, H, E>, CharT>
    : corvid::forwarding_formatter<T, CharT> {};

template<typename T, typename C, typename CharT>
requires std::formattable<T, CharT>
struct std::formatter<corvid::indirect_map_key<T, C>, CharT>
    : corvid::forwarding_formatter<T, CharT> {};
// NOLINTEND(bugprone-std-namespace-modification)
