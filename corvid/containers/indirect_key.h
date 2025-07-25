#pragma once
#include <unordered_map>
#include <functional>
#include <cstddef>

namespace corvid { inline namespace container { inline namespace indirect_key {

// Indirect keys are similar to `std::reference_wrapper`, but are designed to
// be used as keys in associative containers.
//
// This is useful when the key is already stored in another container, which
// guarantees that the value will not be moved, but we want an associative
// container to act as an additional index.

// Indirect key for use in hash containers. Contains a reference to the key and
// acts more or less like the key, but is lightweight.
template<typename T, typename H = std::hash<T>, typename E = std::equal_to<T>>
struct indirect_hash_key {
  const T& key;

  constexpr indirect_hash_key(const T& key) : key{key} {}

  // We don't want to bind to a temporary.
  indirect_hash_key(const T&&) = delete;

  [[nodiscard]] constexpr operator const T&() const noexcept { return key; }

  struct hash_equal_to {
    using is_transparent = void;

    [[nodiscard]] constexpr std::size_t operator()(
        const indirect_hash_key& ik) const noexcept {
      return H{}(ik.key);
    }
    template<typename U>
    [[nodiscard]] constexpr size_t operator()(const U& u) const noexcept {
      return H{}(u);
    }

    [[nodiscard]] constexpr bool operator()(const indirect_hash_key& l,
        const indirect_hash_key& r) const noexcept {
      return E{}(l.key, r.key);
    }
    template<typename U>
    [[nodiscard]] constexpr bool
    operator()(const indirect_hash_key& l, const U& r) const noexcept {
      return E{}(l.key, r);
    }
    template<typename U>
    [[nodiscard]] constexpr bool
    operator()(const U& l, const indirect_hash_key& r) const noexcept {
      return E{}(l, r.key);
    }
  };
};

// Indirect key for use in map containers. Contains a reference to the key and
// acts more or less like the key, but is lightweight.
template<typename T, class C = std::less<T>>
struct indirect_map_key {
  const T& key;

  constexpr indirect_map_key(const T& key) : key{key} {}

  // We don't want to bind to a temporary.
  indirect_map_key(const T&&) = delete;

  [[nodiscard]] operator const T&() const noexcept { return key; }

  struct compare {
    using is_transparent = void;

    [[nodiscard]] constexpr bool operator()(const indirect_map_key& l,
        const indirect_map_key& r) const noexcept {
      return C{}(l.key, r.key);
    }
    template<typename U>
    [[nodiscard]] constexpr bool
    operator()(const indirect_map_key& l, const U& r) const noexcept {
      return C{}(l.key, r);
    }
    template<typename U>
    [[nodiscard]] constexpr bool
    operator()(const U& l, const indirect_map_key& r) const noexcept {
      return C{}(l, r.key);
    }
  };
};

}}} // namespace corvid::container::indirect_key

template<typename T, typename H, typename E>
struct std::hash<corvid::indirect_hash_key<T, H, E>>
    : corvid::indirect_hash_key<T, H, E>::hash_equal_to {};

template<typename T, typename H, typename E>
struct std::equal_to<corvid::indirect_hash_key<T, H, E>>
    : corvid::indirect_hash_key<T, H, E>::hash_equal_to {};

template<typename T, typename C>
struct std::less<corvid::indirect_map_key<T, C>>
    : corvid::indirect_map_key<T, C>::compare {};
