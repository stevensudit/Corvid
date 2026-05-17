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
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../enums/bool_enums.h"

namespace corvid { inline namespace container {
using namespace bool_enums;

// Default no-op callback for `object_pool`. Satisfies `void cb(T&)`.
struct no_op_cb {
  constexpr void operator()(auto&) const noexcept {}
};

template<typename FN>
concept IsNoOpCb = std::is_same_v<no_op_cb, std::remove_cvref_t<FN>>;

#pragma region object_pool

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
// so you should free up things like locks, but not buffers. For a pool of
// `std::string`, for example, you could call `clear()` in the `ReturnCb`,
// since it doesn't deallocate.
//
// Optimizations:
// There's an obvious potential for false sharing, particularly among the
// generation counters. The solutions include padding out the generation
// counters while keeping them separate from the slots, or combining the slots
// and generation counters, perhaps with padding.
//
// Whether any of these are necessary, and which one to choose, depends heavily
// on the use case and the specific types, so this has to be a tunable
// parameter, not a one-size-fits-all solution. And it has to be justified and
// confirmed by benchmarks.
template<typename T, size_t N,
    generation_scheme GEN = generation_scheme::versioned,
    typename BorrowCb = no_op_cb, typename ReturnCb = no_op_cb,
    typename TAG = void>
class object_pool {
public:
  static constexpr bool is_versioned_v = (GEN == generation_scheme::versioned);
  using gen_t = maybe_t<uint32_t, is_versioned_v>;
  using atomic_gen_t = maybe_t<std::atomic_uint32_t, is_versioned_v>;
  using gen_array_t = maybe_t<std::array<atomic_gen_t, N>, is_versioned_v>;

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

#pragma region borrowed
public:
  // Moveable RAII handle; returns its slot to the pool on destruction.
  class borrowed {
  public:
    borrowed() noexcept = default;

    borrowed(borrowed&& other) noexcept
        : item_{std::exchange(other.item_, nullptr)},
          pool_{std::exchange(other.pool_, nullptr)} {}

    borrowed& operator=(borrowed&& other) noexcept {
      if (this == &other) return *this;
      reset();
      item_ = std::exchange(other.item_, nullptr);
      pool_ = std::exchange(other.pool_, nullptr);
      return *this;
    }

    // Copyable enough to satisfy `std::function`, but throws if you actually
    // try to copy it. This will no longer be necessary once
    // `std::move_only_function` becomes available.
    borrowed(const borrowed&) {
      throw std::logic_error("object_pool::borrowed is not copyable");
    }
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

    template<typename... Args>
    auto operator()(Args&&... args) const
    requires std::invocable<T&, Args...>
    {
      return std::invoke(*item_, std::forward<Args>(args)...);
    }

  private:
    friend class object_pool;

    borrowed(object_pool* pool, T* item) noexcept : item_{item}, pool_{pool} {}

    T* item_{};
    object_pool* pool_{};
  };

#pragma endregion
#pragma region token

  // A cheaply-copied, non-owning token for a slot. It has `std::weak_ptr`
  // semantics in that it can escalate to ownership. However, it can only do
  // this if there isn't a `borrowed` handle that currently owns the slot.
  //
  // To save space, it does not store a pointer to the pool or to the slot:
  // just the index (and generation, if versioned). You will need the pool in
  // order to dereference it, much less take ownership.
  //
  // It also can't distinguish among pools, although it's typesafe so at least
  // it works only with one type of pool. If you have distinct pools of the
  // same item type, you should use the TAG parameter to distinguish them.
  class token {
  public:
    static constexpr index_t npos = std::numeric_limits<index_t>::max();
    static constexpr bool allows_int_conversion =
        (index_bits_v <= 32) || !is_versioned_v;

    // No actual ownership.
    token() noexcept = default;
    token(const token&) = default;
    token& operator=(const token&) = default;
    token(token&& other) noexcept
        : gen_{std::exchange(other.gen_, {})},
          ndx_{std::exchange(other.ndx_, npos)} {}
    token& operator=(token&& other) noexcept {
      if (this == &other) return *this;
      gen_ = std::exchange(other.gen_, {});
      ndx_ = std::exchange(other.ndx_, npos);
      return *this;
    }

    // Construct from a `borrowed` handle. No ownership semantics; it just
    // refers to the slot. When versioned, it can detect staleness.
    explicit token(const borrowed& h) { (void)copy_from_handle(h); }

    // Construct from a `borrowed` handle, detaching it. Although it removes
    // ownership from the `borrowed` (by clearing it out), it does not take
    // ownership. However, it can still access the pointer to the item or even
    // escalate to ownership by calling `borrow`.
    //
    // As with `detach`, once you call this, you become fully responsible for
    // ensuring that the item gets returned to the pool. With great power,
    // yada, yada, yada.
    explicit token(borrowed&& h) {
      if (!copy_from_handle(h)) return;
      (void)h.pool_->detach(std::move(h));
    }

    // Construct from a packed `uint64_t` produced by `as_int`. Only available
    // when it fits.
    explicit token(uint64_t v) noexcept
    requires allows_int_conversion
    {
      ndx_ = static_cast<index_t>(v);
      if constexpr (is_versioned_v) gen_ = static_cast<uint32_t>(v >> 32);
    }

    // Pack `gen_` (upper 32 bits) and `ndx_` (lower bits) into a `uint64_t`.
    // Only available when it fits.
    [[nodiscard]] uint64_t as_int() const noexcept
    requires allows_int_conversion
    {
      auto i = uint64_t{ndx_};
      if constexpr (is_versioned_v) i |= (uint64_t{gen_} << 32);
      return i;
    }

    // Get pointer to item, if still valid. Returns nullptr on failure.
    //
    // When versioning is disabled, it can't detect staleness, so it just
    // returns the pointer so long as the token is valid. Even when versioning
    // is enabled, this method is inherently racy since you don't own the slot
    // and it could therefore be freed immediately. This may be fine or it may
    // be a mistake: you get to decide.
    [[nodiscard]] T* get_ptr(object_pool& pool) const noexcept {
      if (!is_valid()) return nullptr;
      if constexpr (is_versioned_v) {
        auto gen =
            pool.gen_array_[ndx_].load(std::memory_order_relaxed) & 0x7FFFFFFF;
        if (gen != gen_) return nullptr;
      }
      return &pool.slots_[ndx_];
    }

    // Borrow the slot if it's not already borrowed. When versioned, returns
    // empty if the slot is already borrowed or if the generation doesn't match
    // (stale token).
    //
    // This is akin to `std::weak_ptr::lock()`, except that it can only succeed
    // if the slot is not currently borrowed, and it returns a `borrowed`
    // handle that has ownership semantics.
    //
    // Note that, in the absence of versioning, this method not only fails to
    // detect staleness, but it also cannot prevent multiple borrows of the
    // same slot, which would lead to double-deletion. Use with caution.
    [[nodiscard]] borrowed borrow(object_pool& pool) const {
      if (!is_valid()) return {};
      if constexpr (is_versioned_v)
        if (!pool.set_borrowed_if(ndx_, gen_)) return {};

      return borrowed{&pool, &pool.slots_[ndx_]};
    }

    [[nodiscard]] explicit operator bool() const noexcept {
      return is_valid();
    }
    [[nodiscard]] bool operator!() const noexcept { return !is_valid(); }

    // Return whether the token refers to a slot. There is no guarantee that
    // it's not stale; you can only find that out by trying.
    [[nodiscard]] bool is_valid() const noexcept { return ndx_ != npos; }

  private:
    void swap(token& other) noexcept {
      using std::swap;
      if constexpr (is_versioned_v) swap(gen_, other.gen_);
      swap(ndx_, other.ndx_);
    }

    bool copy_from_handle(const borrowed& h) {
      if (!h) return false;
      ndx_ = h.pool_->slot_from_item(h.item_);
      if constexpr (is_versioned_v)
        gen_ = h.pool_->gen_array_[ndx_].load(std::memory_order_relaxed) &
               0x7FFFFFFF;
      return true;
    }

  private:
    // Order is important for packing. The index could be 8 bits while the
    // generation is always 32 when present.
    [[no_unique_address]] gen_t gen_{};
    index_t ndx_{npos};
  };

  friend class token;

#pragma endregion
#pragma region Construction

  // Constructs the pool with optional borrow and return callbacks.
  explicit object_pool(BorrowCb borrow_cb = {}, ReturnCb return_cb = {})
      : borrow_cb_{std::move(borrow_cb)}, return_cb_{std::move(return_cb)} {
    std::iota(free_list_.begin(), free_list_.end(), index_t{0});
    // Generation ranges from 1 to 2^31-1, as 0 is invalid and the high bit
    // is reserved for borrower detection.
    if constexpr (is_versioned_v)
      for (auto& gen : gen_array_) gen.store(1, std::memory_order_relaxed);
  }

  // Factory method to deduce callback types. Use with `object_pool_factory`.
  template<typename U, size_t M, generation_scheme G, typename borrow_t,
      typename return_t>
  [[nodiscard]] static object_pool<U, M, G, std::decay_t<borrow_t>,
      std::decay_t<return_t>>
  create(borrow_t&& borrow_cb = {}, return_t&& return_cb = {}) {
    return object_pool<U, M, G, std::decay_t<borrow_t>,
        std::decay_t<return_t>>{std::forward<borrow_t>(borrow_cb),
        std::forward<return_t>(return_cb)};
  }

  object_pool(const object_pool&) = delete;
  object_pool(object_pool&&) = delete;
  object_pool& operator=(const object_pool&) = delete;
  object_pool& operator=(object_pool&&) = delete;

  ~object_pool() noexcept(false) { (void)shutdown(); }

#pragma endregion
#pragma region Borrowing

  // Borrows a slot; returns empty if the pool is full or has been shut
  // down.
  [[nodiscard]] borrowed borrow() {
    // We do not increment the generation until return, but we do set the
    // high bit to indicate that it's borrowed.
    T* item{};
    if (std::scoped_lock pool_lock{pool_mutex_}; true) {
      if (free_top_ == 0) return {};
      auto ndx = free_list_[--free_top_];
      item = &slots_[ndx];
      [[maybe_unused]] const bool impossible = !set_borrowed(ndx);
      assert(!impossible);
    }
    if constexpr (!IsNoOpCb<BorrowCb>) {
      std::scoped_lock func_lock{func_mutex_};
      borrow_cb_(*item);
    }
    return {this, item};
  }

  [[nodiscard]] constexpr size_t capacity() const noexcept { return N; }

  // Detach item from handle without returning it to the pool. Sometimes
  // necessary, as when it must be `void*`. Once you call this, you become
  // fully responsible for using `reattach` to return the item to the pool.
  // Otherwise, it will leak, eventually leading to a lack of available
  // slots.
  [[nodiscard]] T* detach(borrowed&& h) noexcept {
    assert(h.pool_ == this);
    if constexpr (is_versioned_v) {
      auto ndx = slot_from_item(h.item_);
      if (!unset_borrowed(ndx)) return nullptr;
    }
    h.pool_ = nullptr;
    return std::exchange(h.item_, nullptr);
  }

  // Reattach item to a new handle. Useful after `detach()`. Returns empty if
  // the item is not from this pool, which "should never happen", so check
  // the results. Nulls out the input.
  // NOLINTBEGIN(performance-move-const-arg)
  [[nodiscard]] borrowed reattach(T*&& item) noexcept {
    if (!is_in_pool(item)) return {};
    if constexpr (is_versioned_v) {
      auto ndx = slot_from_item(item);
      if (!set_borrowed(ndx)) return {};
    }
    return {this, std::exchange(item, nullptr)};
  }
  // NOLINTEND(performance-move-const-arg)

  // Permanently shut down the pool. After the first call, `borrow` (and
  // `token::borrow`) returns empty. Already-borrowed handles can still be
  // reset/destroyed safely; `return_cb_` runs as usual on each return, but
  // the freed slots are not offered for reuse. Each currently-free slot
  // also has `return_cb_` invoked, for callers that use it to release
  // resources. Idempotent; returns true on the first call, false on any
  // subsequent call.
  //
  // The mechanism that rejects post-shutdown borrows differs by path. `borrow`
  // is blocked because `free_top_` is cleared. `token::borrow`, in versioned
  // mode, is blocked because `release_slot_gen` below increments the gen of
  // every currently-borrowed slot, so any outstanding token's gen no longer
  // matches; tokens for already-returned slots were stale anyway. In
  // unversioned mode `token::borrow` has no staleness check at all (see the
  // caveat on `token::borrow`), so shutdown adds no protection beyond what
  // versioning itself does not provide.
  //
  // `return_cb` contract: `return_cb` is invoked outside the pool mutex by
  // `return_slot` (to minimize lock hold time), and is also invoked under
  // the lock by `shutdown` on every slot. A late `return_slot` racing with
  // `shutdown` can therefore see `return_cb` fired twice on the same slot:
  // once with the original value, once with the post-shutdown `T{}`. So
  // `return_cb` must be idempotent and safe to invoke on a default-
  // constructed `T`.
  [[nodiscard]] bool shutdown() noexcept {
    std::scoped_lock both_lock{pool_mutex_, func_mutex_};
    if (shut_down_) return false;
    shut_down_ = true;
    for (size_t ndx = 0; ndx < N; ++ndx) {
      (void)release_slot_gen(ndx);
      if constexpr (!IsNoOpCb<ReturnCb>) return_cb_(slots_[ndx]);
      slots_[ndx] = T{};
    }
    free_top_ = 0;
    return true;
  }

#pragma endregion
#pragma region Helpers
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
    return return_slot(ndx);
  }

  void return_slot(index_t ndx) noexcept {
    if constexpr (!IsNoOpCb<ReturnCb>) {
      std::scoped_lock func_lock{func_mutex_};
      return_cb_(slots_[ndx]);
    }
    std::scoped_lock pool_lock{pool_mutex_};
    if (shut_down_) return;
    (void)release_slot_gen(ndx);
    free_list_[free_top_++] = ndx;
  }

  // Set the high bit to indicate that it's now borrowed. The
  // `std::atomic` is used to ensure that a `token` can't observe a torn
  // generation value or mistakenly borrow a slot being returned.
  [[nodiscard]] bool set_borrowed(index_t ndx) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto old_gen = gen.load(std::memory_order::relaxed);
      if (old_gen & 0x80000000) return false;
      auto new_gen = old_gen | 0x80000000;
      return gen.compare_exchange_strong(old_gen, new_gen,
          std::memory_order::release, std::memory_order::relaxed);
    }
    return true;
  }

  // Set the high bit to indicate that it's now borrowed, if the generation
  // matches. The `std::atomic` is used to ensure that a `token` can't
  // observe a torn generation value or mistakenly borrow a slot being
  // returned.
  [[nodiscard]] bool set_borrowed_if(index_t ndx, gen_t expected_gen) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto old_gen = gen.load(std::memory_order::relaxed);
      if (old_gen & 0x80000000) return false;
      if (old_gen != expected_gen) return false;
      auto new_gen = old_gen | 0x80000000;
      return gen.compare_exchange_strong(old_gen, new_gen,
          std::memory_order::release, std::memory_order::relaxed);
    }
    return true;
  }

  // Clear the high bit to indicate that it's not borrowed anymore. The
  // `std::atomic` is used to ensure that a `token` can't observe a torn
  // generation value.
  [[nodiscard]] bool unset_borrowed(index_t ndx) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto old_gen = gen.load(std::memory_order::relaxed);
      if (!(old_gen & 0x80000000)) return false;
      auto new_gen = old_gen & 0x7FFFFFFF;
      return gen.compare_exchange_strong(old_gen, new_gen,
          std::memory_order::release, std::memory_order::relaxed);
    }
    return true;
  }

  // Increment atomically, and wrapping past 0 (which is invalid). Also clear
  // the high bit to indicate that it's not borrowed anymore. The
  // `std::atomic` is used to ensure that a `token` can't observe a torn
  // generation value or mistakenly borrow a slot being returned.
  [[nodiscard]] bool release_slot_gen(index_t ndx) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto old_gen = gen.load(std::memory_order::relaxed);
      if (!(old_gen & 0x80000000)) return false;
      auto new_gen = (old_gen & 0x7FFFFFFF) + 1;
      if (new_gen == 0) new_gen = 1;
      return gen.compare_exchange_strong(old_gen, new_gen,
          std::memory_order::release, std::memory_order::relaxed);
    }
    return true;
  }

#pragma endregion
#pragma region Data members

  // Order of members is important for alignment.
  alignas(T) std::array<T, N> slots_{};
  std::array<index_t, N> free_list_{};
  size_t free_top_{N};
  bool shut_down_{};
  mutable std::mutex pool_mutex_;
  mutable std::mutex func_mutex_;
  [[no_unique_address]] gen_array_t gen_array_;
  [[no_unique_address]] BorrowCb borrow_cb_;
  [[no_unique_address]] ReturnCb return_cb_;
};

#pragma endregion

// Use with `object_pool::create`, since the specialization of the scoping
// class doesn't matter.
using object_pool_factory = object_pool<int, 1, generation_scheme::versioned>;

#pragma endregion
#pragma region slot_retention

// `slot_retention` controls when an object is released to the pool.
enum class slot_retention : uint8_t {
  // Release the slot when it should no longer be needed.
  automatic,
  // Always release the slot, even if would otherwise have been retained.
  release,
  // Always retain the slot, even if it would otherwise have been released.
  retain,
};

#pragma endregion

// Implementation note: It is possible in principle to replace the `index_t`
// values with `std::atomic_index_t` and do lock-free stack push and pop on
// the free list. However, contention is extremely unlikely, and the lock is
// held for a short, fixed period. Moreover, lock-free doesn't guarantee
// speed. Therefore, we would need benchmarks to justify the added
// complexity.
//
// The other possible optimization would be to use a
// `std::vector<std::pair<index_t, T>>` for an intrusive free list, instead
// of the separate `slots_` array. The potential benefit is that it would let
// us resize and might provide better cache locality. On the other hand, we
// would lose the ability to use a packed 8-bit array for small pools, and
// would generally run into cache-unfriendly packing issues and possible
// false sharing. Once again, this would need to be thoroughly benchmarked to
// justify the added complexity.
}} // namespace corvid::container
