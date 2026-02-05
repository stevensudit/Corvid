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
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <tuple>
#include <utility>
#include <vector>

#include "enum_vector.h"

namespace corvid { inline namespace container {
inline namespace archetype_vectors {

// A vector replacement that contains a tuple of vectors, one per archetype
// field, to implement an ECS-style archetype storage system. This does not
// implement the full vector interface, just enough to satisfy `stable_ids`.
//
// Note that, despite specializing on a tuple of values, it does not
// physically store a vector of tuples. Rather, the initial implementation
// uses a tuple of vectors, which is SoA. Future versions may use more
// sophisticated storage techniques (e.g., AoSoA chunks) to improve cache
// performance.
template<typename ID, typename CsTuple,
    class Allocator = std::allocator<std::byte>>
class archetype_vector;

template<typename ID, typename... Cs, typename Allocator>
class archetype_vector<ID, std::tuple<Cs...>, Allocator> {
public:
  using tuple_t = std::tuple<Cs...>;
  using id_t = ID;
  using size_type = std::underlying_type_t<id_t>;
  using allocator_type = Allocator;

  template<typename T>
  using component_allocator_t =
      typename std::allocator_traits<allocator_type>::template rebind_alloc<T>;

  template<typename T>
  using component_vector_t = std::vector<T, component_allocator_t<T>>;

  static_assert(sizeof...(Cs) >= 0);

  // Mutable row lens for accessing all components of a given index.
  template<bool WRITE = true>
  class row_lens {
  public:
    static constexpr bool writeable_v = WRITE;
    using owner_t = std::conditional_t<writeable_v, archetype_vector,
        const archetype_vector>;

    // Get index and ID.
    [[nodiscard]] size_type get_index() const noexcept { return ndx; }
    [[nodiscard]] id_t get_id() const { return owner->index_to_id(ndx); }

    // Access component by type.
    template<typename C>
    requires(writeable_v)
    [[nodiscard]] C& component() noexcept {
      return owner->template get_component_span<C>()[ndx];
    }
    template<typename C>
    [[nodiscard]] const C& component() const noexcept {
      return owner->template get_component_span<C>()[ndx];
    }

    // Access component by index.
    template<std::size_t Index>
    requires(writeable_v)
    [[nodiscard]] auto& component() noexcept {
      return owner->template get_component_span<Index>()[ndx];
    }
    template<std::size_t Index>
    [[nodiscard]] const auto& component() const noexcept {
      return owner->template get_component_span<Index>()[ndx];
    }

    // Access all components as a tuple of mutable values.
    [[nodiscard]] auto components() noexcept
    requires(writeable_v)
    {
      return std::apply(
          [&](auto&&... vecs) {
            return std::tuple<decltype(vecs[ndx])&...>{vecs[ndx]...};
          },
          owner->get_component_spans_tuple());
    }

    // Access all components as a tuple of const values.
    [[nodiscard]] auto components() const noexcept {
      return std::apply(
          [&](auto&&... vecs) {
            return std::tuple<
                const std::remove_reference_t<decltype(vecs[ndx])>&...>{
                vecs[ndx]...};
          },
          owner->get_component_spans_tuple());
    }

    // Swap elements without using `row_lens` or whatever.
    void swap_elements(size_type left_ndx, size_type right_ndx) noexcept
    requires(writeable_v)
    {
      std::apply(
          [&](auto&... vecs) {
            ((std::swap(vecs[left_ndx], vecs[right_ndx])), ...);
          },
          owner->components_);
    }

  private:
    row_lens(owner_t* owner_, size_type ndx_) : owner{owner_}, ndx{ndx_} {}

  private:
    owner_t* owner;
    size_type ndx;

    friend class archetype_vector;
  };

  // Read-only view, as opposed to mutable lens.
  using row_view = row_lens<false>;

  // TODO: Iterators over rows.

public:
  // Constructors.
  archetype_vector() = default;
  explicit archetype_vector(const allocator_type&) {}
  archetype_vector(const archetype_vector&) = delete;
  archetype_vector(archetype_vector&&) noexcept = default;
  archetype_vector& operator=(const archetype_vector&) = delete;
  archetype_vector& operator=(archetype_vector&&) noexcept = default;

  // Set callback to map index to ID.
  using index_to_id_fn = std::function<id_t(size_type)>;
  void set_index_to_id(index_to_id_fn fn) noexcept {
    index_to_id_ = std::move(fn);
  }

  // Look up ID for index.
  [[nodiscard]] id_t index_to_id(size_type ndx) const {
    return index_to_id_(ndx);
  }

  // Vector methods.

  // Return number of elements.
  [[nodiscard]] size_type size() const noexcept {
    return static_cast<size_type>(std::get<0>(components_).size());
  }

  // Return whether vector is empty.
  [[nodiscard]] bool empty() const noexcept {
    return std::get<0>(components_).empty();
  }

  // Reserve capacity for at least `new_cap` elements.
  void reserve(size_type new_cap) {
    const auto cap = static_cast<std::size_t>(new_cap);
    for_each_component([&](auto& vec) { vec.reserve(cap); });
  }

  // Return current capacity (minimum across all component vectors).
  [[nodiscard]] size_type capacity() const noexcept {
    std::size_t min_cap = std::numeric_limits<std::size_t>::max();
    std::apply(
        [&](const auto&... vecs) {
          ((min_cap = std::min(min_cap, vecs.capacity())), ...);
        },
        components_);
    return static_cast<size_type>(min_cap);
  }

  // Resize all component vectors to `count` elements.
  void resize(size_type count) {
    const auto sz = static_cast<std::size_t>(count);
    for_each_component([&](auto& vec) { vec.resize(sz); });
  }

  // Clear all component vectors.
  void clear() noexcept {
    for_each_component([](auto& vec) { vec.clear(); });
  }

  // Shrink all component vectors to fit their size.
  void shrink_to_fit() {
    for_each_component([](auto& vec) { vec.shrink_to_fit(); });
  }

  // Emplace, but without parameters. This is followed up with accessing by ID
  // and setting each component separately.
  //
  // Intentionally fails if called with parameters or if return value is used.
  void emplace_back() {
    for_each_component([](auto& vec) { vec.emplace_back(); });
  }

  [[nodiscard]] row_lens<> operator[](size_type ndx) noexcept {
    return row_lens<>{this, ndx};
  }
  [[nodiscard]] row_view operator[](size_type ndx) const noexcept {
    return row_view{this, ndx};
  }

  // Swap.
  void swap(archetype_vector& other) noexcept(
      std::is_nothrow_swappable_v<decltype(components_)> &&
      std::is_nothrow_swappable_v<index_to_id_fn>) {
    using std::swap;
    swap(components_, other.components_);
    swap(index_to_id_, other.index_to_id_);
  }

  friend void swap(archetype_vector& lhs, archetype_vector& rhs) noexcept(
      noexcept(lhs.swap(rhs))) {
    lhs.swap(rhs);
  }

  // Access.

  // Get span of component specified by type.
  template<typename C>
  [[nodiscard]] auto get_component_span(this auto& self) noexcept {
    auto& vec = std::get<component_vector_t<C>>(self.components_);
    return std::span{vec.data(), vec.size()};
  }

  // Get span of component specified by index.
  template<std::size_t Index>
  [[nodiscard]] auto get_component_span(this auto& self) noexcept {
    auto& vec = std::get<Index>(self.components_);
    return std::span{vec.data(), vec.size()};
  }

  // Get vector of component specified by type.
  template<typename C>
  [[nodiscard]] const component_vector_t<C>&
  get_component_vector() const noexcept {
    return std::get<component_vector_t<C>>(components_);
  }

  // Get vector of component specified by index.
  template<std::size_t Index>
  [[nodiscard]] const auto& get_component_vector() const noexcept {
    return std::get<Index>(components_);
  }

  // Get tuple of spans for all components.
  [[nodiscard]] auto get_component_spans_tuple(this auto& self) noexcept {
    return std::apply(
        [&](auto&... vecs) {
          return std::tuple{std::span{vecs.data(), vecs.size()}...};
        },
        self.components_);
  }

  template<typename... Args>
  void emplace_back(Args&&... args) {
    static_assert(sizeof...(Args) == sizeof...(Cs));
    (std::get<component_vector_t<Cs>>(components_)
            .emplace_back(std::forward<Args>(args)),
        ...);
  }

private:
  template<typename F>
  void for_each_component(F&& f) {
    std::apply([&](auto&... vecs) { (f(vecs), ...); }, components_);
  }

  std::tuple<component_vector_t<Cs>...> components_{};
  index_to_id_fn index_to_id_{};
};
}}} // namespace corvid::container::archetype_vectors

// TODO: Test how well it fits into stable_ids. We'll at least need to offer a
// way to detect swap_elements and make use of it.
