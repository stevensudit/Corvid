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
#include "../../meta/crossplatform.h"
#include "../../meta/maybe.h"

#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../../infra/exception_firewalls.h"
#include "../../meta/bool_enums.h"

namespace corvid { inline namespace container {

// Default no-op callback for `object_pool`. Satisfies `void cb(T&)`.
struct no_op_cb {
  constexpr void operator()(auto&) const noexcept {}
};

template<typename FN>
concept IsNoOpCb = std::is_same_v<no_op_cb, std::remove_cvref_t<FN>>;

// Width of the per-slot generation counter used for stale-token detection, or
// `none` to disable versioning entirely.
//
// Counter storage rounds up to the next integer width, so `bits24` occupies
// the same four bytes per slot as `bits32`; what it narrows is the generation
// field in packed tokens (an 8-bit index plus a 24-bit generation fits in 32
// bits).
enum class generation_size : uint8_t {
  none = 0,
  bits8 = 8,
  bits16 = 16,
  bits24 = 24,
  bits32 = 32,
};

namespace details {

// Bit scheme for a generation counter of the given size.
//
// The counter occupies the low `bits` of `storage_t`. Its top bit is the
// borrow flag; the rest is the generation, which runs over [1, `mask`], with 0
// reserved as invalid.
template<generation_size GS>
struct gen_traits {
  static constexpr auto bits = size_t{std::to_underlying(GS)};
  using storage_t = std::conditional_t<(bits <= 8), uint8_t,
      std::conditional_t<(bits <= 16), uint16_t, uint32_t>>;
  static constexpr auto borrow_bit = storage_t{storage_t{1} << (bits - 1)};
  static constexpr auto mask = storage_t{borrow_bit - 1};

  // Calculate the generation value that follows `old_gen` on release.
  //
  // Clears the borrow bit and increments the generation, wrapping back to 1
  // when the increment would otherwise spill into the borrow bit. The result
  // is always in [1, `mask`]; 0 is reserved as invalid.
  [[nodiscard]] static constexpr storage_t calc_next_gen(
      storage_t old_gen) noexcept {
    auto new_gen = static_cast<storage_t>((old_gen & mask) + 1);
    if (new_gen == borrow_bit) new_gen = 1;
    return new_gen;
  }
};

// The `none` case carries a placeholder storage type; the counter itself is
// elided via `maybe_t`.
template<>
struct gen_traits<generation_size::none> {
  static constexpr auto bits = 0UZ;
  using storage_t = uint32_t;
};

} // namespace details

#pragma region object_pool

// Thread-safe fixed-capacity object pool with LIFO slot reuse.
//
// Callers borrow slots via `borrow`, which returns a moveable RAII `borrowed`
// handle. When the `borrowed` is destroyed (or `reset`), the slot returns to
// the free list.
//
// Optional callbacks, both with signature `void cb(T&)`:
//   `BorrowCb`: called on each borrow.
//   `ReturnCb`: called on each return.
//
// Use `BorrowCb` if the desired initial state is not the same as the
// default-constructed state. Use `ReturnCb` in order to free up resources.
//
// Note, however, that the whole point of an object pool is to reuse objects,
// so you should free up things like locks, but not buffers. For example, for a
// pool of `std::string`, you could call `clear` in the `ReturnCb`, since it
// doesn't deallocate, while calling `reserve` in the `BorrowCb` to allocate
// the buffer up front.
//
// The only data a callback may touch is the object it is passed: `BorrowCb`
// initializes it for use, `ReturnCb` clears it out without deallocating. In
// particular, a callback must not call back into the pool; callbacks run under
// an internal, non-recursive mutex, so reentry deadlocks. Both callbacks are
// serialized through that mutex, and both must be noexcept.
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
template<typename T, size_t N, generation_size GEN = generation_size::bits32,
    typename BorrowCb = no_op_cb, typename ReturnCb = no_op_cb,
    typename TAG = void>
class object_pool {
public:
  static constexpr bool is_versioned_v = (GEN != generation_size::none);
  using gen_traits_t = details::gen_traits<GEN>;
  using gen_storage_t = gen_traits_t::storage_t;
  using gen_t = maybe_t<gen_storage_t, is_versioned_v>;
  using atomic_gen_t = maybe_t<std::atomic<gen_storage_t>, is_versioned_v>;
  using gen_array_t = maybe_t<std::array<atomic_gen_t, N>, is_versioned_v>;

  static constexpr auto index_bits_v =
      (N <= std::numeric_limits<uint8_t>::max())    ? 8UZ
      : (N <= std::numeric_limits<uint16_t>::max()) ? 16UZ
      : (N <= std::numeric_limits<uint32_t>::max())
          ? 32UZ
          : 64UZ;
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
      throw std::logic_error{"object_pool::borrowed is not copyable"};
    }
    borrowed& operator=(const borrowed&) = delete;

    ~borrowed() {
      try_or_terminate([&] { reset(); });
    }

    // Returns the slot to the pool immediately; handle becomes empty.
    bool reset() {
      if (!item_) return false;
      pool_->return_slot(item_);
      item_ = nullptr;
      pool_ = nullptr;
      return true;
    }

    [[nodiscard]] explicit operator bool() const noexcept { return item_; }

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

  // A cheaply copied, non-owning token for a slot. It has `std::weak_ptr`
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
        (index_bits_v + gen_traits_t::bits <= 64);

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
    //
    // When versioned, if this races or follows `shutdown`, the detach cannot
    // happen; the slot is instead returned on the spot. The handle ends up
    // empty either way, and in that case the responsibility ends there.
    explicit token(borrowed&& h) {
      if (!copy_from_handle(h)) return;

      // `detach` fails only when `shutdown` got there first, and it leaves the
      // handle untouched when it does, so the fallback `reset` is not a
      // use-after-move.
      //
      // NOLINTNEXTLINE(bugprone-use-after-move)
      if (!h.pool_->detach(std::move(h))) h.reset();
    }

    // Construct from a packed `uint64_t` produced by `as_int`. Only available
    // when it fits.
    //
    // Packed values are meant to travel through untyped channels, so garbage
    // input is anticipated, not a contract violation. A value that `as_int` on
    // this pool type could not have produced (index field out of range for the
    // pool, borrow bit set in the generation field, or any bit set above the
    // packed span) yields a default, invalid token.
    //
    // As this does not throw, it is the caller's responsibility to check
    // `is_valid` before using the token.
    explicit token(uint64_t v) noexcept
    requires allows_int_conversion
    {
      // Validate before narrowing; the cast would discard the very bits that
      // prove a value foreign.
      if constexpr (is_versioned_v) {
        constexpr auto packed_bits = index_bits_v + gen_traits_t::bits;
        constexpr auto index_mask = (uint64_t{1} << index_bits_v) - 1;
        constexpr auto packed_borrow_bit =
            uint64_t{gen_traits_t::borrow_bit} << index_bits_v;

        if ((v & index_mask) >= N || (v & packed_borrow_bit)) return;
        if constexpr (packed_bits < 64)
          if (v >> packed_bits) return;

        gen_ = static_cast<gen_storage_t>(v >> index_bits_v);
      } else {
        if (v >= N) return;
      }
      ndx_ = static_cast<index_t>(v);
    }

    // Pack the index into the low bits, with the generation, if any, directly
    // above it. Only available when the whole thing fits in a `uint64_t`.
    [[nodiscard]] uint64_t as_int() const noexcept
    requires allows_int_conversion
    {
      uint64_t packed{ndx_};
      if constexpr (is_versioned_v) packed |= (uint64_t{gen_} << index_bits_v);
      return packed;
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
        auto gen = pool.gen_array_[ndx_].load(std::memory_order_relaxed) &
                   gen_traits_t::mask;
        // Generation 0 is reserved as invalid and never matches, so a token
        // carrying the sealed generation cannot resolve a shut-down slot.
        if ((gen != gen_) || !gen_) return nullptr;
      }
      return &pool.slots_[ndx_];
    }

    // Borrow the slot if it's not already borrowed. When versioned, returns
    // empty if the slot is already borrowed or if the generation doesn't match
    // (stale token).
    //
    // This is akin to `std::weak_ptr::lock`, except that it can only succeed
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
        gen_ = static_cast<gen_storage_t>(
            h.pool_->gen_array_[ndx_].load(std::memory_order_relaxed) &
            gen_traits_t::mask);
      return true;
    }

  private:
    // Order is important for packing; both field widths vary with the pool's
    // parameters.
    CORVID_NO_UNIQUE_ADDRESS gen_t gen_{};
    index_t ndx_ = npos;
  };

  friend class token;

#pragma endregion
#pragma region Construction

  // Constructs the pool with optional borrow and return callbacks.
  explicit object_pool(BorrowCb borrow_cb = {}, ReturnCb return_cb = {})
      : borrow_cb_{std::move(borrow_cb)}, return_cb_{std::move(return_cb)} {
    std::iota(free_list_.begin(), free_list_.end(), index_t{0});
    // Generations range over [1, `gen_traits_t::mask`], as 0 is invalid and
    // the counter's top bit is reserved for borrower detection.
    if constexpr (is_versioned_v)
      for (auto& gen : gen_array_) gen.store(1, std::memory_order_relaxed);
  }

  // Factory method to deduce callback types. Use with `object_pool_factory`.
  template<typename U, size_t M, generation_size G, typename borrow_t,
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

  ~object_pool() {
    try_or_terminate([&] { (void)shutdown(); });
  }

#pragma endregion
#pragma region Borrowing

  // Borrows a slot; returns empty if the pool is full or has been shut
  // down.
  [[nodiscard]] borrowed borrow() {
    // We do not increment the generation until the slot is released, but we do
    // set the borrow bit (when versioned).
    T* item{};
    if (std::scoped_lock pool_lock(pool_mutex_); true) {
      if (free_top_ == 0) return {};
      auto ndx = free_list_[--free_top_];
      // A failed claim means the entry was stale: the slot is already owned
      // through some other path, which "can't happen". This attempt fails, but
      // at least the slot is removed from the free list, so the next borrow
      // will try a different slot.
      if (!set_borrowed(ndx)) return {};
      item = &slots_[ndx];
    }
    if constexpr (!IsNoOpCb<BorrowCb>) {
      std::scoped_lock func_lock(func_mutex_);
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
  //
  // The returned pointer must be the slot's only outstanding reference: it
  // takes the role that a `token` would otherwise play, not a supplement to
  // one. If a `token` for the slot also exists, whichever reference reacquires
  // ownership first wins, and the other must then be discarded without use.
  //
  // When versioned, `detach` fails after `shutdown`, returning `nullptr` and
  // leaving the handle intact; just reset the handle (or let it destruct) to
  // return the slot normally.
  [[nodiscard]] T* detach(borrowed&& h) noexcept {
    assert(h.pool_ == this);
    if constexpr (is_versioned_v) {
      auto ndx = slot_from_item(h.item_);
      if (!unset_borrowed(ndx)) return nullptr;
    }
    h.pool_ = nullptr;
    return std::exchange(h.item_, nullptr);
  }

  // Reattach item to a new handle. Useful after `detach`. Returns empty if the
  // item is not from this pool, which "should never happen", so check the
  // results. Nulls out the input.
  //
  // The pointer must be one previously returned by `detach` on this pool, with
  // each `detach` matched by at most one `reattach`. A raw pointer carries no
  // generation, so unlike `token::borrow` this cannot detect a stale pointer
  // whose slot has since been freed; reattaching one is a contract violation.
  // `borrow` refuses to double-issue the squatted slot, so the damage is
  // contained to a spuriously failed borrow, but the violation remains a bug
  // in the caller. When you need a detached reference that fails politely
  // instead, keep a `token`.
  //
  // When versioned, `reattach` fails after `shutdown`: the detached item was
  // already reclaimed.
  //
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

  // Permanently shut down the pool.
  //
  // Idempotent; returns true on the first call, false on any subsequent call.
  //
  // After the first call, `borrow` (and `token::borrow`) returns empty.
  // Already-borrowed handles keep their contents and can still be
  // reset/destroyed safely; `return_cb_` runs as usual on each return, but the
  // freed slots are not offered for reuse.
  //
  // Every slot that is not currently borrowed, which includes detached ones,
  // has `return_cb_` invoked and its value reset. Since detached items are
  // reclaimed on the spot, a later `reattach` (or `token::borrow`) fails
  // cleanly.
  //
  // The mechanism: `shutdown_slot_gen` below seals every slot by setting its
  // borrow bit and wiping its generation to the reserved invalid 0. `borrow`
  // is blocked because `free_top_` is cleared; every other acquisition path
  // (`token::borrow`, `reattach`, and `detach`, whose success would otherwise
  // unseal the slot) fails against the sealed generation word.
  //
  // In unversioned mode there is no generation word, so none of this
  // protection exists (see the caveat on `token::borrow`), and we instead
  // assume every slot is borrowed.
  //
  // A `borrow` already in flight when `shutdown` runs may still complete and
  // return a live handle; it simply becomes a handle held across shutdown, and
  // is returned as normal.
  //
  // Shutdown procedure: Call `shutdown` first so that new work fails, give
  // in-flight work a chance to drain (typically by failing cleanly, since
  // detached items have been reclaimed), and only then destroy the pool. A
  // handle must never outlive the pool unless reset: its destructor calls back
  // into it. A token is just a number, so merely outliving the pool is
  // harmless; what must not happen is passing a destroyed pool to its methods.
  [[nodiscard]] bool shutdown() noexcept {
    std::scoped_lock both_lock(pool_mutex_, func_mutex_);
    if (shut_down_) return false;
    shut_down_ = true;
    for (auto ndx = 0UZ; ndx < N; ++ndx) {
      // If we know for a fact that it's not borrowed, we wipe it. Otherwise,
      // the wipe has to wait until the handle is returned or the pool is
      // destroyed.
      if (shutdown_slot_gen(ndx)) continue;
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
    const auto addr = reinterpret_cast<uintptr_t>(p);
    const auto begin = reinterpret_cast<uintptr_t>(slots_.data());
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
      std::scoped_lock func_lock(func_mutex_);
      return_cb_(slots_[ndx]);
    }
    std::scoped_lock pool_lock(pool_mutex_);
    if (shut_down_) return;
    (void)release_slot_gen(ndx);
    free_list_[free_top_++] = ndx;
  }

  // Set the borrow bit to indicate that it's now borrowed. The `std::atomic`
  // is used to ensure that a `token` can't observe a torn generation value or
  // mistakenly borrow a slot being returned.
  [[nodiscard]] bool set_borrowed(index_t ndx) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto old_gen = gen.load(std::memory_order::relaxed);
      if (old_gen & gen_traits_t::borrow_bit) return false;
      auto new_gen =
          static_cast<gen_storage_t>(old_gen | gen_traits_t::borrow_bit);
      // Acquire on success pairs with the release in `release_slot_gen` and
      // `unset_borrowed`, making the previous owner's writes to the object
      // visible to the new owner.
      return gen.compare_exchange_strong(old_gen, new_gen,
          std::memory_order::acquire, std::memory_order::relaxed);
    }
    return true;
  }

  // Set the borrow bit to indicate that it's now borrowed, if the generation
  // matches. The `std::atomic` is used to ensure that a `token` can't
  // observe a torn generation value or mistakenly borrow a slot being
  // returned.
  [[nodiscard]] bool set_borrowed_if(index_t ndx, gen_t expected_gen) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto old_gen = gen.load(std::memory_order::relaxed);
      if (old_gen & gen_traits_t::borrow_bit) return false;
      // Generation 0 is reserved as invalid: it appears in tokens minted after
      // `shutdown` sealed the slot, and can arrive through `token(uint64_t)`.
      if ((old_gen != expected_gen) || !expected_gen) return false;
      auto new_gen =
          static_cast<gen_storage_t>(old_gen | gen_traits_t::borrow_bit);
      // Acquire on success pairs with the release in `release_slot_gen`,
      // making the previous owner's writes to the object visible to the new
      // owner.
      return gen.compare_exchange_strong(old_gen, new_gen,
          std::memory_order::acquire, std::memory_order::relaxed);
    }
    return true;
  }

  // Clear the borrow bit to indicate that it's not borrowed anymore. The
  // `std::atomic` is used to ensure that a `token` can't observe a torn
  // generation value.
  [[nodiscard]] bool unset_borrowed(index_t ndx) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto old_gen = gen.load(std::memory_order::relaxed);
      if (!(old_gen & gen_traits_t::borrow_bit)) return false;
      // A sealed slot (borrow bit on, generation wiped to the invalid 0) must
      // stay sealed: clearing it would let `reattach` or a gen-0 token
      // reacquire a shut-down slot.
      if (!(old_gen & gen_traits_t::mask)) return false;
      auto new_gen = static_cast<gen_storage_t>(old_gen & gen_traits_t::mask);
      // Release on success publishes the owner's writes to whoever later
      // reacquires the slot.
      return gen.compare_exchange_strong(old_gen, new_gen,
          std::memory_order::release, std::memory_order::relaxed);
    }
    return true;
  }

  // Increment atomically, wrapping per `gen_traits_t::calc_next_gen`. Also
  // clear the borrow bit to indicate that it's not borrowed anymore. The
  // `std::atomic` is used to ensure that a `token` can't observe a torn
  // generation value or mistakenly borrow a slot being returned.
  [[nodiscard]] bool release_slot_gen(index_t ndx) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto old_gen = gen.load(std::memory_order::relaxed);
      if (!(old_gen & gen_traits_t::borrow_bit)) return false;
      // A sealed word never reaches this point: `return_slot` bails on
      // `shut_down_` before calling. Unsealing here would reopen the slot.
      assert(old_gen & gen_traits_t::mask);
      const auto new_gen = gen_traits_t::calc_next_gen(old_gen);
      // Release on success publishes the owner's writes to whoever later
      // reacquires the slot.
      return gen.compare_exchange_strong(old_gen, new_gen,
          std::memory_order::release, std::memory_order::relaxed);
    }
    return true;
  }

  // Rudely wipe the generation while setting the borrow bit, sealing the slot
  // against every acquisition path. Returns whether the slot was borrowed at
  // the time.
  [[nodiscard]] bool shutdown_slot_gen(index_t ndx) {
    if constexpr (is_versioned_v) {
      auto& gen = gen_array_[ndx];
      auto new_gen = gen_traits_t::borrow_bit;
      // Runs under both mutexes, and nothing consumes data published through
      // the sealed value, so relaxed suffices.
      const auto old_gen = gen.exchange(new_gen, std::memory_order::relaxed);
      return (old_gen & gen_traits_t::borrow_bit);
    }
    // When unversioned, we pretend the slot was borrowed.
    return true;
  }

#pragma endregion
#pragma region Data members

  // Order of members is important for alignment.
  alignas(T) std::array<T, N> slots_{};
  std::array<index_t, N> free_list_{};
  size_t free_top_ = N;
  bool shut_down_{};
  mutable std::mutex pool_mutex_;
  mutable std::mutex func_mutex_;
  CORVID_NO_UNIQUE_ADDRESS gen_array_t gen_array_;
  CORVID_NO_UNIQUE_ADDRESS BorrowCb borrow_cb_;
  CORVID_NO_UNIQUE_ADDRESS ReturnCb return_cb_;
};

#pragma endregion

// Use with `object_pool::create`, since the specialization of the scoping
// class doesn't matter.
using object_pool_factory = object_pool<int, 1, generation_size::bits32>;

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
