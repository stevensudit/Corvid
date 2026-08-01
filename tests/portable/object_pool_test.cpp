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

#include "corvid/containers/utils/object_pool.h"
#include "catch2_main.h"

using namespace corvid;

namespace {

static_assert(std::is_same_v<object_pool<int, 4>::index_t, uint8_t>,
    "Small object pools should use uint8_t indices");
static_assert(std::is_same_v<object_pool<int, 256>::index_t, uint16_t>,
    "Pools at 256 entries should widen to uint16_t indices");
static_assert(std::is_same_v<object_pool<int, 65536>::index_t, uint32_t>,
    "Pools at 65536 entries should widen to uint32_t indices");

// The post-release generation always lands in [1, mask] at every supported
// width: the borrow bit is cleared and the increment at the top of the range
// wraps back to 1 (skipping the invalid 0) instead of spilling into the
// borrow bit.
using gen8_t = container::details::gen_traits<generation_size::bits8>;
static_assert(std::is_same_v<gen8_t::storage_t, uint8_t>);
static_assert(gen8_t::borrow_bit == 0x80U && gen8_t::mask == 0x7FU);
static_assert(gen8_t::calc_next_gen(0x81U) == 2U,
    "Release should clear the borrow bit and increment the generation");
static_assert(gen8_t::calc_next_gen(0xFFU) == 1U,
    "The top generation should wrap to 1, not into the borrow bit");

using gen16_t = container::details::gen_traits<generation_size::bits16>;
static_assert(std::is_same_v<gen16_t::storage_t, uint16_t>);
static_assert(gen16_t::borrow_bit == 0x8000U && gen16_t::mask == 0x7FFFU);
static_assert(gen16_t::calc_next_gen(0xFFFFU) == 1U);

using gen24_t = container::details::gen_traits<generation_size::bits24>;
static_assert(std::is_same_v<gen24_t::storage_t, uint32_t>);
static_assert(
    gen24_t::borrow_bit == 0x0080'0000U && gen24_t::mask == 0x007F'FFFFU);
static_assert(gen24_t::calc_next_gen(0x00FF'FFFFU) == 1U);

using gen32_t = container::details::gen_traits<generation_size::bits32>;
static_assert(std::is_same_v<gen32_t::storage_t, uint32_t>);
static_assert(
    gen32_t::borrow_bit == 0x8000'0000U && gen32_t::mask == 0x7FFF'FFFFU);
static_assert(gen32_t::calc_next_gen(0x8000'0001U) == 2U,
    "Release should clear the borrow bit and increment the generation");
static_assert(gen32_t::calc_next_gen(0xFFFF'FFFEU) == 0x7FFF'FFFFU,
    "The generation just below the top should not wrap");
static_assert(gen32_t::calc_next_gen(0xFFFF'FFFFU) == 1U,
    "The top generation should wrap to 1, not into the borrow bit");
static_assert(gen32_t::calc_next_gen(0x7FFF'FFFFU) == 1U,
    "Wrapping should not require the borrow bit to be set");

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region BorrowAndReturn

TEST_CASE("BorrowAndReturn", "[ObjectPool]") {
  // Borrow a slot and verify it's valid.
  if (true) {
    object_pool<int, 4> pool;
    CHECK(pool.capacity() == 4U);

    auto h = pool.borrow();
    CHECK(h);
    *h = 42;
    CHECK(*h == 42);
    CHECK(h.value() == 42);
  }

  // Slot is returned on handle destruction; the freed slot can be re-borrowed.
  if (true) {
    object_pool<int, 1> pool;
    {
      auto h = pool.borrow();
      CHECK(h);
      CHECK_FALSE(pool.borrow()); // pool full
    }
    CHECK(pool.borrow()); // slot was returned on destruction
  }

  // Explicit `reset` returns the slot early.
  if (true) {
    object_pool<int, 1> pool;
    auto h = pool.borrow();
    CHECK(h);
    CHECK_FALSE(pool.borrow()); // pool full
    h.reset();
    CHECK_FALSE(h);
    CHECK(pool.borrow()); // slot returned
  }
}

#pragma endregion
#pragma region FullPool

TEST_CASE("FullPool", "[ObjectPool]") {
  // Borrowing past capacity returns an empty `borrowed`.
  if (true) {
    object_pool<int, 2> pool;
    auto h0 = pool.borrow();
    auto h1 = pool.borrow();
    CHECK(h0);
    CHECK(h1);

    auto h2 = pool.borrow();
    CHECK_FALSE(h2);
    CHECK(!h2);
  }
}

#pragma endregion
#pragma region LIFOOrder

TEST_CASE("LIFOOrder", "[ObjectPool]") {
  // Slots are returned in LIFO order: last-returned is first-borrowed.
  if (true) {
    object_pool<int, 4> pool;
    auto h0 = pool.borrow();
    auto h1 = pool.borrow();
    int* p0 = h0.get();
    int* p1 = h1.get();

    h1.reset();
    h0.reset();

    // `p0` was returned last, so it should be borrowed first.
    auto h2 = pool.borrow();
    CHECK(h2.get() == p0);

    auto h3 = pool.borrow();
    CHECK(h3.get() == p1);
  }
}

#pragma endregion
#pragma region MoveHandle

TEST_CASE("MoveHandle", "[ObjectPool]") {
  // Move construction transfers ownership; original becomes empty.
  if (true) {
    object_pool<int, 4> pool;
    auto h = pool.borrow();
    CHECK(h);

    auto h2 = std::move(h);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(h);
    CHECK(h2);
  }

  // Slot is returned when the moved-to handle is destroyed.
  if (true) {
    object_pool<int, 1> pool;
    {
      auto outer = pool.borrow();
      {
        auto inner = std::move(outer);
        CHECK(inner);
        CHECK_FALSE(pool.borrow()); // slot still held by inner
      }
      CHECK(pool.borrow()); // inner destroyed, slot returned
    }
  }

  // Move assignment transfers ownership.
  if (true) {
    object_pool<int, 4> pool;
    auto h = pool.borrow();
    object_pool<int, 4>::borrowed h2;
    h2 = std::move(h);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(h);
    CHECK(h2);
  }
}

#pragma endregion
#pragma region MultipleSlots

TEST_CASE("MultipleSlots", "[ObjectPool]") {
  // All slots can be borrowed and individually returned.
  if (true) {
    constexpr auto cap = 8UZ;
    object_pool<int, cap> pool;

    std::array<std::optional<object_pool<int, cap>::borrowed>, cap> handles;
    for (auto ndx = 0UZ; ndx < cap; ++ndx) {
      handles[ndx] = pool.borrow();
      CHECK(handles[ndx].has_value());
      // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
      **handles[ndx] = static_cast<int>(ndx);
    }
    CHECK_FALSE(pool.borrow()); // all slots in use

    // Return every other slot.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    for (auto ndx = 0UZ; ndx < cap; ndx += 2) handles[ndx]->reset();

    // Re-borrow the returned slots.
    for (auto ndx = 0UZ; ndx < cap; ndx += 2) {
      handles[ndx] = pool.borrow();
      CHECK(handles[ndx].has_value());
    }
    CHECK_FALSE(pool.borrow()); // all slots in use again
  }
}

#pragma endregion
#pragma region Callbacks

TEST_CASE("Callbacks", "[ObjectPool]") {
  // BorrowCb is called on each borrow; ReturnCb is called on each return.
  if (true) {
    int borrow_count{};
    int return_count{};
    auto on_borrow = [&](int& v) noexcept { v = ++borrow_count; };
    auto on_return = [&](int&) noexcept { ++return_count; };
    object_pool<int, 4, generation_size::bits32, decltype(on_borrow),
        decltype(on_return)>
        store{on_borrow, on_return};

    auto h0 = store.borrow();
    CHECK(*h0 == 1);
    CHECK(borrow_count == 1);
    CHECK(return_count == 0);

    auto h1 = store.borrow();
    CHECK(*h1 == 2);
    CHECK(borrow_count == 2);

    h0.reset();
    CHECK(return_count == 1);
    h1.reset();
    CHECK(return_count == 2);
  }

  // ReturnCb runs before the slot re-enters the free list; re-borrowing
  // that slot gives whatever BorrowCb writes, not the stale return value.
  if (true) {
    auto on_borrow = [](int& v) noexcept { v = 99; };
    auto on_return = [](int& v) noexcept { v = 0; };
    object_pool<int, 1, generation_size::bits32, decltype(on_borrow),
        decltype(on_return)>
        store{on_borrow, on_return};

    auto h = store.borrow();
    CHECK(*h == 99);
    h.reset();

    auto h2 = store.borrow();
    CHECK(*h2 == 99);
  }

  // Default no-op callbacks still compile and work correctly.
  if (true) {
    object_pool<int, 4> pool;
    auto h = pool.borrow();
    CHECK(h);
  }
}

#pragma endregion
#pragma region CreateHelper

TEST_CASE("CreateHelper", "[ObjectPool]") {
  // `create` deduces callback types from lambdas and wires both callbacks in.
  if (true) {
    int borrow_count{};
    int return_count{};

    auto pool = object_pool_factory::create<int, 2, generation_size::bits32>(
        [&](int& v) noexcept { v = ++borrow_count; },
        [&](int&) noexcept { ++return_count; });

    auto h0 = pool.borrow();
    CHECK(h0);
    CHECK(*h0 == 1);
    CHECK(borrow_count == 1);
    CHECK(return_count == 0);

    h0.reset();
    CHECK(return_count == 1);

    auto h1 = pool.borrow();
    CHECK(h1);
    CHECK(*h1 == 2);
    CHECK(borrow_count == 2);
  }
}

#pragma endregion
#pragma region DetachAndReattach

TEST_CASE("DetachAndReattach", "[ObjectPool]") {
  // `detach` releases ownership from the handle without returning the slot.
  if (true) {
    object_pool<int, 1> pool;
    auto h = pool.borrow();
    CHECK(h);
    int* item = h.get();

    auto detached = pool.detach(std::move(h));
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(h);
    CHECK(detached == item);
    CHECK_FALSE(pool.borrow()); // detached slot is still out of the pool

    // NOLINTNEXTLINE(performance-move-const-arg)
    auto h2 = pool.reattach(std::move(detached));
    CHECK(h2);
    CHECK(h2.get() == item);

    h2.reset();
    CHECK(pool.borrow()); // slot returned after reattached handle resets
  }

  // `reattach` fails for pointers that do not belong to the pool.
  if (true) {
    object_pool<int, 1> pool;
    int outside{};
    // NOLINTNEXTLINE(performance-move-const-arg)
    auto h = pool.reattach(std::move(&outside));
    CHECK_FALSE(h);
  }

  // Reattaching a stale pointer is a contract violation, but the damage is
  // contained: the squatted entry is dropped from the free list, so that one
  // borrow fails, and later borrows succeed without anything being freed in
  // between. Once the squatter returns the slot, the pool fully reconverges.
  if (true) {
    object_pool<int, 2> pool;
    auto h = pool.borrow();
    int* p = h.get();
    int* stale = p;
    h.reset(); // slot goes back on the free list; `stale` now dangles

    // NOLINTNEXTLINE(performance-move-const-arg)
    auto squatter = pool.reattach(std::move(stale)); // the misuse "succeeds"
    CHECK(squatter);
    CHECK(squatter.get() == p);

    CHECK_FALSE(pool.borrow()); // pops the squatted entry, drops it, fails
    auto other = pool.borrow(); // next attempt serves the remaining slot
    CHECK(other);
    CHECK(other.get() != p);

    squatter.reset();           // squatter returns its slot
    auto again = pool.borrow(); // ...and it is borrowable again
    CHECK(again);
    CHECK(again.get() == p);
  }
}

#pragma endregion
#pragma region TokenBasics

TEST_CASE("TokenBasics", "[ObjectPool]") {
  // Default-constructed token is invalid on all fronts.
  if (true) {
    object_pool<int, 4> pool;
    object_pool<int, 4>::token h;
    CHECK_FALSE(h);
    CHECK(!h);
    CHECK_FALSE(h.is_valid());
    CHECK(h.get_ptr(pool) == nullptr);
  }

  // Token from `borrowed&` is valid and `get_ptr` returns the slot pointer.
  if (true) {
    object_pool<int, 4> pool;
    auto b = pool.borrow();
    *b = 7;
    object_pool<int, 4>::token h{b};
    CHECK(h);
    CHECK(h.is_valid());
    CHECK(h.get_ptr(pool) == b.get());
  }

  // Token from `borrowed&` does not transfer ownership; slot stays borrowed.
  if (true) {
    object_pool<int, 1> pool;
    auto b = pool.borrow();
    object_pool<int, 1>::token h{b};
    CHECK_FALSE(pool.borrow()); // b still owns the slot
    b.reset();
    CHECK(pool.borrow()); // slot returned when b resets
  }

  // Tokens are copyable; the copy refers to the same slot.
  if (true) {
    object_pool<int, 4> pool;
    auto b = pool.borrow();
    object_pool<int, 4>::token h1{b};
    auto h2 = h1;
    CHECK(h2);
    CHECK(h2.get_ptr(pool) == b.get());
  }
}

#pragma endregion
#pragma region TokenDetachAndBorrow

TEST_CASE("TokenDetachAndBorrow", "[ObjectPool]") {
  // `token(borrowed&&)` detaches the `borrowed`; slot stays out of the pool.
  if (true) {
    object_pool<int, 1> pool;
    auto b = pool.borrow();
    int* p = b.get();
    object_pool<int, 1>::token h{std::move(b)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(b); // b was detached
    CHECK(h);
    CHECK_FALSE(pool.borrow());  // slot not in free list
    CHECK(h.get_ptr(pool) == p); // handle still resolves to the pointer
  }

  // `token::borrow` re-acquires the detached slot.
  if (true) {
    object_pool<int, 1> pool;
    auto b = pool.borrow();
    *b = 42;
    object_pool<int, 1>::token h{std::move(b)};

    auto b2 = h.borrow(pool);
    CHECK(b2);
    CHECK(*b2 == 42);
    CHECK_FALSE(pool.borrow()); // slot still held by b2

    // A second `borrow` on the same handle fails while b2 owns the slot.
    CHECK_FALSE(h.borrow(pool));

    b2.reset();
    CHECK(pool.borrow()); // slot returned to pool
  }
}

#pragma endregion
#pragma region TokenStaleness

TEST_CASE("TokenStaleness", "[ObjectPool]") {
  // `get_ptr` returns nullptr once the slot's generation has advanced.
  if (true) {
    object_pool<int, 4> pool;
    auto b = pool.borrow();
    object_pool<int, 4>::token h{b};
    CHECK(h.get_ptr(pool) == b.get()); // valid while b is live
    b.reset();                         // gen incremented on return
    CHECK(h.get_ptr(pool) == nullptr); // stale
  }

  // `token::borrow` returns empty once the handle is stale.
  if (true) {
    object_pool<int, 4> pool;
    auto b = pool.borrow();
    object_pool<int, 4>::token h{b};
    b.reset(); // gen incremented; handle is now stale
    CHECK_FALSE(h.borrow(pool));
  }

  // `token::borrow` returns empty if the slot is currently borrowed.
  if (true) {
    object_pool<int, 4> pool;
    auto b = pool.borrow();
    object_pool<int, 4>::token h{b}; // h and b refer to the same slot
    CHECK_FALSE(h.borrow(pool));     // b already owns it
  }
}

#pragma endregion
#pragma region TokenAsInt

TEST_CASE("TokenAsInt", "[ObjectPool]") {
  // Round-trip through `as_int` and `token(uint64_t)` resolves to the same
  // slot.
  if (true) {
    object_pool<int, 4> pool;
    auto b = pool.borrow();
    *b = 55;
    object_pool<int, 4>::token h{b};
    auto packed = h.as_int();
    object_pool<int, 4>::token h2{packed};
    CHECK(h2);
    CHECK(h2.get_ptr(pool) == b.get());
  }

  // A token reconstructed from a stale `as_int` value is also stale.
  if (true) {
    object_pool<int, 4> pool;
    auto b = pool.borrow();
    object_pool<int, 4>::token h{b};
    auto packed = h.as_int();
    b.reset(); // gen incremented; packed value is now stale
    object_pool<int, 4>::token h2{packed};
    CHECK(h2.get_ptr(pool) == nullptr);
  }

  // Unversioned pool: round-trip through `as_int` resolves to the same slot.
  if (true) {
    object_pool<int, 4, generation_size::none> pool;
    auto b = pool.borrow();
    object_pool<int, 4, generation_size::none>::token h{b};
    auto packed = h.as_int();
    object_pool<int, 4, generation_size::none>::token h2{packed};
    CHECK(h2);
    CHECK(h2.get_ptr(pool) == b.get());
  }

  // A packed value that `as_int` could never have produced yields an invalid
  // token instead of indexing out of bounds.
  if (true) {
    object_pool<int, 4> pool;

    // Index field out of range for the pool.
    object_pool<int, 4>::token oob{7ULL};
    CHECK_FALSE(oob);
    CHECK(oob.get_ptr(pool) == nullptr);
    CHECK_FALSE(oob.borrow(pool));

    // Bit set above the packed span. This pool packs an 8-bit index plus a
    // 32-bit generation, so bit 40 is the first foreign one.
    object_pool<int, 4>::token overflow{(1ULL << 40) | 2ULL};
    CHECK_FALSE(overflow);

    // Borrow bit set in the generation field (the field's top bit, at bit 39
    // here).
    auto b = pool.borrow();
    object_pool<int, 4>::token h{b};
    object_pool<int, 4>::token forged{h.as_int() | (1ULL << 39)};
    CHECK_FALSE(forged);

    // A zero generation field passes validation (a post-shutdown mint can
    // produce it), but generation 0 is reserved as invalid and never borrows.
    object_pool<int, 4>::token zero_gen{2ULL};
    CHECK(zero_gen.is_valid());
    CHECK_FALSE(zero_gen.borrow(pool));
  }

  // A default token round-trips through `as_int` as invalid.
  if (true) {
    object_pool<int, 4> pool;
    object_pool<int, 4>::token h{object_pool<int, 4>::token{}.as_int()};
    CHECK_FALSE(h);
    CHECK(h.get_ptr(pool) == nullptr);
  }

  // Unversioned pool: an out-of-range value yields an invalid token.
  if (true) {
    object_pool<int, 4, generation_size::none> pool;
    object_pool<int, 4, generation_size::none>::token h{4ULL};
    CHECK_FALSE(h);
    CHECK(h.get_ptr(pool) == nullptr);
  }

  // Narrow generations pack tightly: an 8-bit index plus an 8-bit generation
  // fits `as_int` in 16 bits, and still round-trips.
  if (true) {
    object_pool<int, 4, generation_size::bits8> pool;
    auto b = pool.borrow();
    object_pool<int, 4, generation_size::bits8>::token h{b};
    auto packed = h.as_int();
    CHECK(packed <= 0xFFFFU);
    object_pool<int, 4, generation_size::bits8>::token h2{packed};
    CHECK(h2);
    CHECK(h2.get_ptr(pool) == b.get());
  }
}

#pragma endregion
#pragma region GenerationWrap

TEST_CASE("GenerationWrap", "[ObjectPool]") {
  // An 8-bit generation cycles through 127 values; the pool keeps borrowing
  // cleanly across multiple wraps.
  if (true) {
    object_pool<int, 1, generation_size::bits8> pool;
    for (auto cycle = 0; cycle < 300; ++cycle) {
      auto b = pool.borrow();
      REQUIRE(b);
    }
  }

  // The ABA limitation, pinned: once the slot's generation cycles all the way
  // around, a stale token matches again.
  if (true) {
    object_pool<int, 1, generation_size::bits8> pool;
    auto b = pool.borrow();
    int* p = b.get();
    object_pool<int, 1, generation_size::bits8>::token h{b};
    b.reset();
    CHECK(h.get_ptr(pool) == nullptr); // stale after one release
    for (auto cycle = 0; cycle < 126; ++cycle) {
      auto b2 = pool.borrow();
      REQUIRE(b2);
    }
    CHECK(h.get_ptr(pool) == p); // 127 releases in: wrapped back around
  }
}

#pragma endregion
#pragma region Shutdown

TEST_CASE("Shutdown", "[ObjectPool]") {
  // After `shutdown`, `borrow` fails.
  if (true) {
    object_pool<int, 4> pool;
    auto h = pool.borrow();
    CHECK(h);
    h.reset();

    CHECK(pool.shutdown());
    CHECK_FALSE(pool.borrow());

    // Idempotent: subsequent calls report no-op.
    CHECK_FALSE(pool.shutdown());
  }

  // `shutdown` invokes `return_cb_` on every slot that is not currently
  // borrowed. Borrowed slots are left to their handles: returning a
  // still-live handle afterwards runs `return_cb_` from the normal
  // `return_slot` path.
  if (true) {
    int return_count{};
    auto on_return = [&](int&) noexcept { ++return_count; };
    object_pool<int, 4, generation_size::bits32, no_op_cb, decltype(on_return)>
        pool{{}, on_return};

    auto h0 = pool.borrow();
    auto h1 = pool.borrow();
    h0.reset(); // one return: count == 1
    CHECK(return_count == 1);

    // Shutdown invokes return_cb on the 3 unborrowed slots, skipping h1's.
    CHECK(pool.shutdown());
    CHECK(return_count == 4);

    // Returning the still-live handle runs return_cb once, on its real value.
    h1.reset();
    CHECK(return_count == 5);
  }

  // `shutdown` leaves a borrowed slot's contents alone; the handle keeps
  // working and resets cleanly afterwards, but the slot is not returned to
  // the free list, so subsequent borrows still fail.
  if (true) {
    object_pool<int, 2> pool;
    auto h = pool.borrow();
    *h = 42;
    CHECK(*h == 42);

    CHECK(pool.shutdown());

    // The live handle's contents are preserved.
    CHECK(*h == 42);

    h.reset();
    CHECK_FALSE(pool.borrow());
  }

  // With no outstanding borrows at shutdown time, `borrow` continues to
  // return empty: shutdown emptied the free list and no return has
  // refilled it.
  if (true) {
    object_pool<int, 2> pool;
    CHECK(pool.shutdown());
    CHECK_FALSE(pool.borrow());
    CHECK_FALSE(pool.borrow());
  }

  // Tokens captured before `shutdown` cannot escalate to a `borrowed`
  // afterwards: shutdown seals every slot, setting the borrow bit and wiping
  // the generation, so the token's generation no longer matches.
  if (true) {
    object_pool<int, 2> pool;
    auto h = pool.borrow();
    object_pool<int, 2>::token tok{h};
    CHECK(tok);

    CHECK(pool.shutdown());

    CHECK_FALSE(tok.borrow(pool));
    CHECK(tok.get_ptr(pool) == nullptr);
  }

  // `token(borrowed&&)` after shutdown: the sealed slot refuses the detach,
  // so the ctor returns the slot on the spot. The handle ends up empty either
  // way, and the token cannot escalate: the seal must not be cleared, or a
  // gen-0 token could reacquire a shut-down slot.
  if (true) {
    object_pool<int, 1> pool;
    auto h = pool.borrow();
    CHECK(pool.shutdown());

    object_pool<int, 1>::token tok{std::move(h)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(h); // emptied even though the detach failed
    CHECK(tok.is_valid());
    CHECK_FALSE(tok.borrow(pool));
    // The minted token carries the sealed generation 0, which never matches:
    // pre-fix, this resolved to a live pointer into the sealed slot.
    CHECK(tok.get_ptr(pool) == nullptr);
  }

  // Plain `detach` after shutdown fails, leaving the handle intact; resetting
  // it returns the slot the normal way.
  if (true) {
    object_pool<int, 1> pool;
    auto h = pool.borrow();
    CHECK(pool.shutdown());

    // The move stays outside CHECK: Catch2's macro repeats the expression in
    // a dead retry condition, tripping bugprone-use-after-move.
    auto* p = pool.detach(std::move(h));
    CHECK(p == nullptr);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(h); // detach leaves the handle intact on failure
    CHECK(h.reset());
  }

  // A slot detached before `shutdown` is reclaimed by it: `return_cb_` runs
  // on the detached item, and the later `reattach` fails cleanly.
  if (true) {
    int return_count{};
    auto on_return = [&](int&) noexcept { ++return_count; };
    object_pool<int, 2, generation_size::bits32, no_op_cb, decltype(on_return)>
        pool{{}, on_return};

    auto h = pool.borrow();
    auto* p = pool.detach(std::move(h));
    REQUIRE(p);

    // Both the free slot and the detached slot are reclaimed.
    CHECK(pool.shutdown());
    CHECK(return_count == 2);

    // Move outside CHECK_FALSE, as above.
    // NOLINTNEXTLINE(performance-move-const-arg)
    const auto reattached = pool.reattach(std::move(p));
    CHECK_FALSE(reattached);
  }

  // The pool's destructor calls `shutdown`, so `return_cb_` runs for
  // every slot even when no explicit `shutdown` was invoked.
  if (true) {
    int return_count{};
    auto on_return = [&](int&) noexcept { ++return_count; };
    {
      object_pool<int, 3, generation_size::bits32, no_op_cb,
          decltype(on_return)>
          pool{{}, on_return};
      auto h = pool.borrow();
      h.reset(); // 1 return so far
      CHECK(return_count == 1);
    }
    // Destructor invoked shutdown: 3 more invocations (one per slot).
    CHECK(return_count == 4);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
