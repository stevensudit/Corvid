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
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../../meta/crossplatform.h"

namespace corvid { inline namespace container { namespace arena {

#pragma region extensible_arena

// Arena implemented as a singly linked list of blocks.
//
// To use:
// 1. Create an `extensible_arena`, with the desired capacity, as a member of
// your container class.
// 2. Specialize its members with `arena_allocator` as the allocator, as by
// using aliases such as `arena_string`.
// 3. Ensure that an `extensible_arena::scope` is created in each method that
// needs to allocate.
//
// The reason for this odd scheme is to allow the `arena_allocator` to take up
// no more space than a `std::allocator`, which lets us recast arena-allocating
// containers (such as `arena_string`) as their standard counterparts (such as
// `std::string`).
//
// Allocates new blocks as needed, chaining them together. Block size is
// constant, except when it must be enlarged to satisfy an allocation. Only
// frees when the entire arena is destroyed.
//
// If you make a container that uses an `arena_allocator`, it will still try to
// destruct and free all of its elements. The free is a no-op, but pointless.
// Both the free and the destructs can be avoided allocating the container with
// `arena_new` or `arena_construct` and then "leaking" it. This also has the
// benefit of ensuring proximity.
//
// The expectation is that the arena is much larger than any single value, so
// the waste from the last unfilled bit is minimal.
class extensible_arena final {
#pragma region Implementation

  struct list_node;
  struct list_node_deleter {
    void operator()(list_node* node) const noexcept {
      // Detach `next_` before destructing so that teardown iterates instead
      // of recursing one stack frame per block.
      while (node) {
        auto next = std::move(node->next_);
        node->~list_node();
        delete[] reinterpret_cast<char*>(node);
        node = next.release();
      }
    }
  };
  using pointer = std::unique_ptr<list_node, list_node_deleter>;

  // Points to the head owned by the active container. Use
  // `extensible_arena::scope` to install whenever an allocation is needed.
  thread_local static inline pointer* tls_head_{};

  [[nodiscard]] static auto& get_head() {
    assert(tls_head_);
    return *tls_head_;
  }

  struct list_node {
    size_t capacity_{};
    size_t size_{};
    pointer next_;
    std::byte data_[1]{};

    // Helper function to calculate the total size needed for a list_node with
    // a given capacity. The minus 1 is because the list_node struct already
    // includes storage for one element; clamping holds that credit to what
    // `data_` actually provides, so a zero capacity cannot undersize the
    // buffer relative to `sizeof(list_node)`.
    [[nodiscard]] static constexpr size_t calculate_total_size(
        size_t capacity) {
      return sizeof(list_node) + std::max(capacity, 1UZ) - 1;
    }

    // Make a new node of `capacity`.
    [[nodiscard]] static pointer make(size_t capacity) {
      if (capacity >
          std::numeric_limits<size_t>::max() - sizeof(list_node) + 1)
        throw std::bad_array_new_length{};
      // The new operator is used to allocate raw memory, and then placement
      // new is used to construct a new list_node object in that memory.
      auto buffer_for_placement = new char[calculate_total_size(capacity)];
      pointer node{new (buffer_for_placement) list_node{}};
      node->capacity_ = capacity;
      return node;
    }

    // Allocate a block of size `n` with `align` alignment from the current
    // node. If no room, returns `nullptr`.
    [[nodiscard]] void* allocate(size_t n, size_t align) noexcept {
      // Round the actual address up to the alignment, not the buffer offset:
      // `data_`'s own offset within the node is not a multiple of alignments
      // above `alignof(list_node)`.
      const auto base = reinterpret_cast<uintptr_t>(data_);
      const auto start_index = static_cast<size_t>(
          ((base + size_ + align - 1) & ~(align - 1)) - base);
      // Compare without summing so that a huge `n` cannot wrap past the
      // capacity check.
      if ((n > capacity_) || (start_index > capacity_ - n)) return nullptr;
      size_ = start_index + n;
      return data_ + start_index;
    }
  };

  // Allocate a block of size `n` with `align` alignment. If no room at `head`,
  // replaces with new block, chaining the rest.
  [[nodiscard]] static void* allocate(pointer& head, size_t n, size_t align) {
    if (auto start = head->allocate(n, align)) return start;
    // Pad the fresh block by `align - 1` so that `n` fits wherever `data_`
    // lands relative to the alignment.
    if (n > std::numeric_limits<size_t>::max() - (align - 1))
      throw std::bad_array_new_length{};
    auto new_head = list_node::make(std::max(head->capacity_, n + align - 1));
    new_head->next_ = std::move(head);
    head = std::move(new_head);
    auto start = head->allocate(n, align);
    assert(start);
    return start;
  }

  pointer head_;

#pragma endregion
#pragma region Construction

public:
  explicit extensible_arena(size_t capacity)
      : head_{list_node::make(capacity)} {}

#pragma endregion
#pragma region Operations

  [[nodiscard]] static void* allocate(size_t n, size_t align) {
    return allocate(get_head(), n, align);
  }

  [[nodiscard]] static bool contains(const void* pv) {
    for (auto next = get_head().get(); next; next = next->next_.get())
      if (next->data_ <= pv && pv < next->data_ + next->size_) return true;

    return false;
  }

#pragma endregion
#pragma region scope

  // Sets thread-local scope for arena.
  //
  // gcc's -Wdangling-pointer misfires here: `arena` is a reference parameter,
  // so `&arena.head_` points into the caller's arena (which outlives the
  // guard), not a local. Silence it on gcc; clang does not warn.
  PRAGMA_GCC_DIAG(push)
  PRAGMA_GCC_IGNORED("-Wdangling-pointer")
  class [[nodiscard]] scope {
  public:
    explicit scope(extensible_arena& arena) noexcept : old_head{tls_head_} {
      tls_head_ = &arena.head_;
    }

    ~scope() noexcept { tls_head_ = old_head; }

  private:
    pointer* old_head{};
  };
  PRAGMA_GCC_DIAG(pop)

#pragma endregion
};

#pragma endregion
#pragma region arena_allocator

// Allocator that uses the `extensible_arena` that is currently in scope.
template<typename T>
class arena_allocator {
#pragma region Types

  static_assert(std::is_same_v<T, std::remove_cv_t<T>>);

public:
  using value_type = T;
  using size_type = size_t;
  using difference_type = std::ptrdiff_t;
  using propagate_on_container_move_assignment = std::true_type;

#pragma endregion
#pragma region Construction

  constexpr arena_allocator() noexcept = default;
  constexpr arena_allocator(const arena_allocator&) noexcept = default;
  template<class U>
  constexpr arena_allocator(const arena_allocator<U>&) noexcept {}

#pragma endregion
#pragma region Operations

  // NOLINTBEGIN(bugprone-sizeof-expression)

  // Allocates a block of memory suitable for an array of `n` objects of type
  // `T`, using the scoped `extensible_arena`. Throws
  // `std::bad_array_new_length` if the total size would overflow `size_t`, as
  // `std::allocator` does.
  [[nodiscard]] constexpr T* allocate(size_t n) {
    if (n > std::numeric_limits<size_t>::max() / sizeof(T))
      throw std::bad_array_new_length{};
    return static_cast<T*>(
        extensible_arena::allocate(n * sizeof(T), alignof(T)));
  }

  // NOLINTEND(bugprone-sizeof-expression)

  constexpr void deallocate(T*, size_t) {}

#pragma endregion
};

#pragma endregion
#pragma region Collections

// Arena-specialized collections. Can be reinterpreted as their standard forms
// because they are designed to be the same size and memory layout.

using arena_string =
    std::basic_string<char, std::char_traits<char>, arena_allocator<char>>;

static_assert(sizeof(arena_string) == sizeof(std::string));

template<typename K, typename V>
using arena_map = std::unordered_map<K, V, std::hash<K>, std::equal_to<K>,
    arena_allocator<std::pair<const K, V>>>;

static_assert(
    sizeof(arena_map<int, int>) == sizeof(std::unordered_map<int, int>));

template<typename T>
using arena_deque = std::deque<T, arena_allocator<T>>;

static_assert(sizeof(arena_deque<int>) == sizeof(std::deque<int>));

template<typename T>
using arena_allocator_traits = std::allocator_traits<arena_allocator<T>>;

static_assert(
    sizeof(arena_allocator_traits<int>) ==
    sizeof(std::allocator_traits<std::allocator<int>>));

#pragma endregion
#pragma region Helpers

// Allocate a new object of type `T` using the scoped `extensible_arena`.
// Designed to allocate an arena-specialized container whose contents use the
// same arena. To avoid unnecessary destructors and frees, the caller should
// "leak" the object.
template<typename T, class... Args>
[[nodiscard]] T* arena_new(Args&&... args) {
  arena_allocator<T> a;
  auto p = arena_allocator_traits<T>::allocate(a, 1);
  arena_allocator_traits<T>::construct(a, p, std::forward<Args>(args)...);
  return p;
}

// Construct a new object of type `T` using the `arena` parameter.
template<typename T, class... Args>
[[nodiscard]] T& arena_construct(extensible_arena& arena, Args&&... args) {
  extensible_arena::scope s(arena);
  return *arena_new<T>(std::forward<Args>(args)...);
}

// Invoke callback within scope of `arena`.
template<typename F>
decltype(auto) arena_scope(extensible_arena& arena, F&& f) {
  extensible_arena::scope s(arena);
  return std::forward<F>(f)();
}

#pragma endregion
}}} // namespace corvid::container::arena
