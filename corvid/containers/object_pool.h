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

#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <type_traits>
#include <utility>

namespace corvid { inline namespace container {

// Default no-op callback for `object_pool`. Satisfies `void cb(T&)`.
struct no_op_cb {
  constexpr void operator()(auto&) const noexcept {}
};

// Thread-safe fixed-capacity object pool with LIFO slot reuse.
//
// Callers borrow slots via `borrow()`, which returns a moveable RAII
// `borrowed` handle. When the `borrowed` is destroyed (or `reset()`), the slot
// returns to the free list.
//
// Optional callbacks, both with signature `void cb(T&)`:
//   `BorrowCb` -- called on each borrow.
//   `ReturnCb` -- called on each return.
//
// Use `BorrowCb` if the desired initial state is not the same as the
// default-constructed state. Use `ReturnCb` in order to free up resources.
//
// Note, however, that the whole point of an object pool is to reuse objects,
// so you should free up things like locks but not buffers. For a pool of
// `std::string`, for example, you could call `clear()` in the `ReturnCb`,
// since it doesn't deallocate.
template<typename T, size_t N, typename BorrowCb = no_op_cb,
    typename ReturnCb = no_op_cb>
class object_pool {
public:
  static constexpr size_t index_bits_v =
      N <= std::numeric_limits<uint8_t>::max()    ? 8U
      : N <= std::numeric_limits<uint16_t>::max() ? 16U
      : N <= std::numeric_limits<uint32_t>::max()
          ? 32U
          : 64U;

  using index_t = std::conditional_t<(index_bits_v == 8), uint8_t,
      std::conditional_t<(index_bits_v == 16), uint16_t,
          std::conditional_t<(index_bits_v == 32), uint32_t, uint64_t>>>;

private:
  static_assert(N > 0, "object_pool N must be positive");
  static_assert(N <= std::numeric_limits<index_t>::max(),
      "object_pool N exceeds the largest supported index type");
  static_assert(std::is_nothrow_invocable_v<BorrowCb, T&>,
      "BorrowCb must be noexcept");
  static_assert(std::is_nothrow_invocable_v<ReturnCb, T&>,
      "ReturnCb must be noexcept");

public:
  // Moveable RAII handle; returns its slot to the pool on destruction.
  class borrowed {
  public:
    borrowed() noexcept = default;

    borrowed(borrowed&& other) noexcept
        : item_{std::exchange(other.item_, nullptr)},
          pool_{std::exchange(other.pool_, nullptr)} {}

    borrowed& operator=(borrowed&& other) noexcept {
      if (this != &other) {
        reset();
        item_ = std::exchange(other.item_, nullptr);
        pool_ = std::exchange(other.pool_, nullptr);
      }
      return *this;
    }

    borrowed(const borrowed&) = delete;
    borrowed& operator=(const borrowed&) = delete;

    ~borrowed() { reset(); }

    // Returns the slot to the pool immediately; handle becomes empty.
    void reset() noexcept {
      if (!item_) return;
      pool_->return_slot(item_);
      item_ = nullptr;
      pool_ = nullptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return item_; }
    [[nodiscard]] bool operator!() const noexcept { return !item_; }

    [[nodiscard]] T* get() const noexcept { return item_; }
    [[nodiscard]] T& value() const noexcept { return *item_; }

    [[nodiscard]] T& operator*() const noexcept { return *item_; }
    [[nodiscard]] T* operator->() const noexcept { return item_; }

  private:
    friend class object_pool;

    borrowed(object_pool* pool, T* item) noexcept : item_{item}, pool_{pool} {}

    T* item_{};
    object_pool* pool_{};
  };

  // Constructs the pool with optional borrow and return callbacks.
  explicit object_pool(BorrowCb borrow_cb = {}, ReturnCb return_cb = {})
      : borrow_cb_{std::move(borrow_cb)}, return_cb_{std::move(return_cb)} {
    std::iota(free_list_.begin(), free_list_.end(), index_t{0});
  }

  // Factory method to deduce callback types. Use with `object_pool_factory`.
  template<typename U, size_t M, typename borrow_t, typename return_t>
  [[nodiscard]] static object_pool<U, M, std::decay_t<borrow_t>,
      std::decay_t<return_t>>
  create(borrow_t&& borrow_cb = {}, return_t&& return_cb = {}) {
    return object_pool<U, M, std::decay_t<borrow_t>, std::decay_t<return_t>>{
        std::forward<borrow_t>(borrow_cb), std::forward<return_t>(return_cb)};
  }

  object_pool(const object_pool&) = delete;
  object_pool(object_pool&&) = delete;
  object_pool& operator=(const object_pool&) = delete;
  object_pool& operator=(object_pool&&) = delete;

  // Borrows a slot; returns empty if the pool is full.
  [[nodiscard]] borrowed borrow() {
    T* item{};
    if (std::lock_guard lock{mutex_}; true) {
      if (free_top_ == 0) return {};
      item = &slots_[free_list_[--free_top_]];
    }
    borrow_cb_(*item);
    return {this, item};
  }

  [[nodiscard]] constexpr size_t capacity() const noexcept { return N; }

  // Detach item from handle without returning it to the pool. Sometimes
  // necessary, as when it must be `void*`. Once you call this, you become
  // fully responsible for using `reattach` to return the item to the pool.
  // Otherwise, it will leak, eventually leading to a lack of available slots.
  [[nodiscard]] T* detach(borrowed&& h) noexcept {
    assert(h.pool_ == this);
    h.pool_ = nullptr;
    return std::exchange(h.item_, nullptr);
  }

  // Reattach item to a new handle. Useful after `detach()`. Returns empty if
  // the item is not from this pool, which "should never happen", so check the
  // results. Nulls out the input.
  // NOLINTBEGIN(performance-move-const-arg)
  [[nodiscard]] borrowed reattach(T*&& item) noexcept {
    if (!is_in_pool(item)) return {};
    return {this, std::exchange(item, nullptr)};
  }
  // NOLINTEND(performance-move-const-arg)

private:
  [[nodiscard]] index_t slot_from_item(const T* item) const noexcept {
    const auto ndx = static_cast<index_t>(item - slots_.data());
    return ndx;
  }

  [[nodiscard]] bool is_in_pool(const void* p) const noexcept {
    if (!p) return false;
    const auto addr = reinterpret_cast<std::uintptr_t>(p);
    const auto begin = reinterpret_cast<std::uintptr_t>(slots_.data());
    const auto end = begin + sizeof(slots_);
    return addr >= begin && addr < end && ((addr - begin) % sizeof(T) == 0);
  }

  void return_slot(T* item) noexcept {
    assert(is_in_pool(item));
    const auto ndx = slot_from_item(item);
    return_cb_(*item);
    std::lock_guard lock{mutex_};
    free_list_[free_top_++] = ndx;
  }

  mutable std::mutex mutex_;
  std::array<T, N> slots_{};
  std::array<index_t, N> free_list_{};
  size_t free_top_{N};
  [[no_unique_address]] BorrowCb borrow_cb_;
  [[no_unique_address]] ReturnCb return_cb_;
};

// Use with `object_pool::create`, since the specialization of the scoping
// class doesn't matter.
using object_pool_factory = object_pool<int, 1>;

// Implementation note: It is possible in principle to replace the `index_t`
// values with `std::atomic_index_t` and do lock-free stack push and pop on
// the free list. However, contention is extremely unlikely, and the lock is
// held for a short, fixed period. Moreover, lock-free doesn't guarantee speed.
// Therefore, we would need benchmarks to justify the added complexity.
//
// The other possible optimization would be to use a
// `std::vector<std::pair<index_t, T>>` for an intrusive free list, instead of
// the separate `slots_` array. The potential benefit is that it would let us
// resize and might provide better cache locality. On the other hand, we would
// lose the ability to use a packed 8-bit array for small pools, and would
// generally run into cache-unfriendly packing issues and possible false
// sharing. Once again, this would need to be thoroughly benchmarked to justify
// the added complexity.

}} // namespace corvid::container
