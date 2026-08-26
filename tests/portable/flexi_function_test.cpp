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
#include "corvid/meta/invoke/fixed_function.h"
#include "corvid/meta/invoke/flexi_function.h"

#include "catch2_main.h"

#include <array>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// Observing moved-from instances is the subject of these tests, so the
// moved-from diagnostics are disabled file-wide.
// NOLINTBEGIN(bugprone-use-after-move)
// NOLINTBEGIN(clang-analyzer-cplusplus.Move)

namespace {

constexpr auto dflt = invocable_policy::basic;
constexpr auto silent = dflt.with(on_empty::silent);
constexpr auto terminating = dflt.with(on_empty::terminate);
constexpr auto heap_only = invocable_policy::heap;
constexpr auto big_inline = dflt.with_storage_size(96);

// The starting points and the size arithmetic, on a 64-bit platform.
static_assert(dflt == invocable_policy{});
static_assert(big_inline.inline_size == 96);
static_assert(invocable_policy::fixed.with_storage_size(48).inline_size == 48);
static_assert(
    invocable_policy::fixed.with_storage_size(17).inline_size ==
    padded_size(17));
static_assert(
    invocable_policy::fixed.with_storage_size(16, 32).inline_size == 32);
static_assert(
    invocable_policy::fixed.with_storage_size(40, 32).inline_size == 64);
static_assert(sizeof(flexi_function<int()>) == 4 * sizeof(void*));
static_assert(sizeof(flexi_function<int(), heap_only>) == 3 * sizeof(void*));
static_assert(sizeof(fixed_function<int(), 64>) == 64);

// An empty inline_only buffer is legal and takes no space: the wrapper is the
// two thunk pointers, and serves only direct targets.
constexpr auto direct_only = invocable_policy::fixed.with_storage_size(0);
static_assert(direct_only.inline_size == 0);
static_assert(sizeof(flexi_function<int(), direct_only>) == 2 * sizeof(void*));
static_assert(
    sizeof(fixed_function<int(), 2 * sizeof(void*)>) == 2 * sizeof(void*));
// Exactly the pair: the empty storage area is `empty_t`, hidden with
// `CORVID_NO_UNIQUE_ADDRESS`.
static_assert(
    sizeof(flexi_function<int(), direct_only>) ==
    sizeof(flexi::details::flexi_thunks<int()>::thunk_pair));

// A callable whose payload exceeds the default buffer.
struct fat_fn {
  std::array<std::byte, 64> pad{};
  int operator()() const noexcept { return 7; }
};
static_assert(sizeof(fat_fn) > dflt.inline_size);

// A callable whose move may throw, so it is never inline.
struct throwing_mover {
  int v;
  explicit throwing_mover(int v) noexcept : v{v} {}
  // The throwing move is the point.
  // NOLINTNEXTLINE(performance-noexcept-move-constructor)
  throwing_mover(throwing_mover&& o) : v{o.v} {}
  int operator()() const noexcept { return v; }
};

// Counts live instances so heap and inline destruction can be checked alike.
struct counted {
  int* count;
  explicit counted(int* c) noexcept : count{c} { ++*count; }
  counted(counted&& o) noexcept : count{o.count} { ++*count; }
  ~counted() { --*count; }
  int operator()() const noexcept { return 1; }
};

// A callable too big for the default buffer that counts its moves, so a test
// can tell a boxing arrival (one move, into the new block) from a hand-over
// (none).
struct fat_counted {
  int* moves;
  std::array<std::byte, 64> pad{};
  explicit fat_counted(int* m) noexcept : moves{m} {}
  fat_counted(fat_counted&& o) noexcept : moves{o.moves} { ++*moves; }
  int operator()() const noexcept { return 7; }
};

struct no_default {
  explicit no_default(int) {}
};

int plain_seven() { return 7; }
int add_one(int x) noexcept { return x + 1; }

// A target for member-pointer constants.
struct gadget {
  int n = 0;
  int factor = 2;
  int tick() { return ++n; }
  [[nodiscard]] int scaled(int x) const noexcept { return x * factor; }
};

struct widget {
  int n;
  explicit widget(int n) : n{n} {}
  [[nodiscard]] int value() const { return n; }
};

} // namespace

// The alias reproduces `fixed_function` exactly.
static_assert(std::is_same_v<fixed_function<int(), 64>,
    flexi_function<int(),
        invocable_policy{.inline_size = 64 - (2 * sizeof(void*)),
            .storage = invocables::storage_policy::inline_only}>>);
static_assert(is_fixed_function_v<fixed_function<int(), 64>>);
static_assert(!is_fixed_function_v<flexi_function<int(), dflt>>);
static_assert(is_flexi_function_v<fixed_function<int(), 64>>);
static_assert(sizeof(flexi_function<int(), heap_only>) == 3 * sizeof(void*));
static_assert(flexi_function<int(), heap_only>::inline_size == 0);
static_assert(flexi_function<int(), dflt>::inline_size == 2 * sizeof(void*));

// An inline_only buffer may be aligned below the default: with no heap
// pointer to hold, nothing forces the buffer up to the default geometry.
constexpr auto lean = invocable_policy::fixed.with_storage_size(
    2 * sizeof(void*), alignof(void*));
static_assert(lean.inline_size == 2 * sizeof(void*));
static_assert(sizeof(flexi_function<int(), lean>) == 4 * sizeof(void*));
static_assert(alignof(flexi_function<int(), lean>) == alignof(void*));

// Same-policy moves never throw; a move that may box or refuse does.
static_assert(
    std::is_nothrow_move_constructible_v<flexi_function<int(), dflt>>);
static_assert(std::is_nothrow_constructible_v<
    flexi_function<int(), big_inline>, flexi_function<int(), dflt>&&>);
static_assert(!std::is_nothrow_constructible_v<flexi_function<int(), dflt>,
    flexi_function<int(), big_inline>&&>);
static_assert(!std::is_nothrow_constructible_v<fixed_function<int(), 64>,
    flexi_function<int(), dflt>&&>);

// Storing a callable is noexcept exactly when it stays inline.
static_assert(
    std::is_nothrow_constructible_v<flexi_function<int(), dflt>, int (*)()>);
static_assert(
    !std::is_nothrow_constructible_v<flexi_function<int(), dflt>, fat_fn>);
static_assert(!std::is_nothrow_constructible_v<
    flexi_function<int(), heap_only>, int (*)()>);

#pragma region Storage

TEST_CASE("Heap fallback", "[flexi_function]") {
  int live{};
  {
    // Too big for the buffer: heap.
    flexi_function<int(), dflt> fat{fat_fn{}};
    CHECK(fat);
    CHECK(fat() == 7);
    CHECK(fat.size() == sizeof(fat_fn));
    CHECK(fat.capacity() == dflt.inline_size);

    // Throwing move: heap, even though it would fit.
    flexi_function<int(), dflt> tm{throwing_mover{3}};
    CHECK(tm() == 3);

    // Small and nothrow: inline.
    flexi_function<int(), dflt> small{counted{&live}};
    CHECK(live == 1);
    CHECK(small() == 1);

    // heap_only: everything on the heap, destroyed on the way out.
    flexi_function<int(), heap_only> h{counted{&live}};
    CHECK(live == 2);
    CHECK(h() == 1);
    CHECK(h.size() == sizeof(counted));
    CHECK(h.capacity() == 0);

    // Heap-to-heap move hands over the block without touching the callable.
    flexi_function<int(), heap_only> h2{std::move(h)};
    CHECK(live == 2);
    CHECK(!h);
    CHECK(h2() == 1);
  }
  // The heap blocks were freed through the erased lifespan thunk, which
  // reads the pointer back through the storage address; the analyzer
  // cannot connect that read to the `storage_area_.ptr` it saw written.
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
  CHECK(live == 0);
}

TEST_CASE("Cross-policy transplant", "[flexi_function]") {
  int live{};

  // Inline into a bigger buffer stays inline.
  flexi_function<int(), dflt> a{counted{&live}};
  flexi_function<int(), big_inline> b{std::move(a)};
  CHECK(live == 1);
  CHECK(!a);
  CHECK(b() == 1);

  // A heap arrival into a heap-admitting policy stays boxed: the block is
  // handed over as is, even though the bigger buffer could hold it.
  flexi_function<int(), dflt> fat{fat_fn{}};
  flexi_function<int(), big_inline> roomy{std::move(fat)};
  CHECK(roomy() == 7);
  CHECK(roomy.size() == sizeof(fat_fn));

  // The same block is handed over again into a smaller heap-admitting
  // buffer; a heap arrival is never re-boxed.
  flexi_function<int(), dflt> back{std::move(roomy)};
  CHECK(!roomy);
  CHECK(back() == 7);

  // An inline arrival into a buffer too small for it is boxed onto the heap,
  // which costs one move (into the block). Handing that block back to a
  // buffer that could hold it costs none.
  int fat_moves{};
  flexi_function<int(), big_inline> wide{fat_counted{&fat_moves}};
  CHECK(fat_moves == 1);
  flexi_function<int(), dflt> narrow{std::move(wide)};
  CHECK(!wide);
  CHECK(fat_moves == 2);
  CHECK(narrow() == 7);
  flexi_function<int(), big_inline> handed{std::move(narrow)};
  CHECK(!narrow);
  CHECK(fat_moves == 2);
  CHECK(handed() == 7);

  // Into heap_only, an inline arrival is boxed; back out, the block is
  // handed over, un-boxing only when the destination is inline_only (below).
  flexi_function<int(), heap_only> boxed{std::move(b)};
  CHECK(live == 1);
  CHECK(boxed() == 1);
  flexi_function<int(), dflt> unboxed{std::move(boxed)};
  CHECK(live == 1);
  CHECK(!boxed);
  CHECK(unboxed() == 1);

  // An inline_only destination refuses what it cannot hold, leaving the
  // source intact, in both the constructor and the pre-flight assignment.
  CHECK_THROWS_AS((fixed_function<int(), 64>{std::move(back)}),
      std::length_error);
  CHECK(back);
  CHECK(back() == 7);
  fixed_function<int(), 64> target{[] { return 0; }};
  CHECK_THROWS_AS(target = std::move(back), std::length_error);
  CHECK(target() == 0);
  CHECK(back() == 7);

  // An inline_only destination accepts a heap arrival that fits.
  fixed_function<int(), 64> fits{std::move(unboxed)};
  CHECK(live == 1);
  CHECK(fits() == 1);
  fits = nullptr;
  CHECK(live == 0);
}

// A direct-eligible functor: no data, trivially constructible and
// destructible.
struct nine_fn {
  int operator()() const noexcept { return 9; }
};

static_assert(invocables::implementation::is_direct_eligible<nine_fn>());
static_assert(!invocables::implementation::is_direct_eligible<counted>());
static_assert(!invocables::implementation::is_direct_eligible<int (*)()>());

// Storing a direct callable is noexcept under every policy, heap_only
// included, because nothing is stored.
static_assert(std::is_nothrow_constructible_v<flexi_function<int(), heap_only>,
    nine_fn>);

TEST_CASE("Direct-only wrapper", "[flexi_function]") {
  using direct_fn = fixed_function<int(), 2 * sizeof(void*)>;

  // A compile-time target and a captureless lambda store nothing, so they
  // need no buffer at all.
  direct_fn a{constant_fn<plain_seven>{}};
  CHECK(a() == 7);
  CHECK(a.size() == 0);
  CHECK(a.capacity() == 0);
  direct_fn b{[] { return 9; }};
  CHECK(b() == 9);

  // Direct pairs travel in both directions between siblings.
  flexi_function<int(), dflt> roomy{std::move(a)};
  CHECK(!a);
  CHECK(roomy() == 7);
  direct_fn back{std::move(roomy)};
  CHECK(!roomy);
  CHECK(back() == 7);

  // A stored target has nowhere to go: refused, with both sides intact.
  int live{};
  flexi_function<int(), dflt> stored{counted{&live}};
  CHECK(!direct_fn::can_adopt(stored));
  CHECK_THROWS_AS((direct_fn{std::move(stored)}), std::length_error);
  CHECK(stored);
  CHECK(live == 1);
  CHECK_THROWS_AS(back = std::move(stored), std::length_error);
  CHECK(back() == 7);
  CHECK(stored() == 1);
}

TEST_CASE("constant_fn calls a compile-time target directly",
    "[flexi_function]") {
  static_assert(invocables::implementation::is_direct_eligible<
      constant_fn<plain_seven>>());

  // A function, named with or without its address; not stored under any
  // policy.
  flexi_function<int(), dflt> a{constant_fn<plain_seven>{}};
  CHECK(a() == 7);
  CHECK(a.size() == 0);
  flexi_function<int(), heap_only> b{constant_fn<&plain_seven>{}};
  CHECK(b() == 7);
  CHECK(b.size() == 0);

  // A member function, with the object as the first argument.
  gadget w;
  flexi_function<int(gadget&), dflt> m{constant_fn<&gadget::tick>{}};
  CHECK(m(w) == 1);
  CHECK(m(w) == 2);
  flexi_function<int(const gadget&, int) noexcept, terminating> mc{
      constant_fn<&gadget::scaled>{}};
  CHECK(mc(w, 4) == 8);

  // A captureless lambda as the constant.
  flexi_function<int(int), dflt> l{constant_fn<[](int x) { return x * 3; }>{}};
  CHECK(l(2) == 6);

  // noexcept follows the target, so only a nothrow target satisfies a
  // noexcept signature.
  static_assert(noexcept(constant_fn<add_one>{}(1)));
  static_assert(!noexcept(constant_fn<plain_seven>{}()));
  flexi_function<int(int) noexcept, terminating> ne{constant_fn<add_one>{}};
  CHECK(ne(1) == 2);
  static_assert(!std::is_constructible_v<
      flexi_function<int() noexcept, terminating>, constant_fn<plain_seven>>);
}

TEST_CASE("runtime_fn is an explicit pointer target", "[flexi_function]") {
  // Stored as the pointer and called through it, like a bare pointer.
  flexi_function<int()> a{runtime_fn{&plain_seven}};
  CHECK(a() == 7);
  CHECK(a.size() == sizeof(int (*)()));
  gadget g;
  flexi_function<int(gadget&)> m{runtime_fn{&gadget::tick}};
  CHECK(m(g) == 1);

  // A null one is no callable: the wrapper is empty, not a truthy crash.
  int (*null_fn)() = nullptr;
  flexi_function<int()> n{runtime_fn{null_fn}};
  CHECK(!n);
  a = runtime_fn{null_fn};
  CHECK(!a);
  a = runtime_fn{&plain_seven};
  CHECK(a() == 7);
}

// Refusals that cannot live in the suite, recorded from scratch compiles:
//
// - `flexi_function<int()> f = plain_seven;` (a function lvalue):
//   "flexi_function: a function name is a compile-time target. Wrap it as
//   constant_fn<f>{} to call it directly with nothing stored, or, when the
//   reference is a runtime value (it came through a forwarding parameter or a
//   conditional), take its address to store it as a pointer"
//
// - `strict_fn f = &plain_seven;` under `policy_enforcement::strict`:
//   "flexi_function: under strict enforcement a raw function or member
//   pointer must be wrapped: constant_fn<f>{} when the target is known at
//   compile time (a direct call, nothing stored), or runtime_fn{p} to store
//   the pointer and call through it"
//
// - `constant_fn<static_cast<int (*)()>(nullptr)>`:
//   "constant_fn: a null function or member pointer is not a target"
TEST_CASE("Strict enforcement refuses raw pointers at the border",
    "[flexi_function]") {
  constexpr auto strict = dflt.with(policy_enforcement::strict);
  using strict_fn = flexi_function<int(), strict>;

  // Every explicit spelling is accepted; a bare pointer is the static_assert
  // recorded above.
  strict_fn c{constant_fn<plain_seven>{}};
  CHECK(c() == 7);
  strict_fn r{runtime_fn{&plain_seven}};
  CHECK(r() == 7);
  strict_fn l{[] { return 3; }};
  CHECK(l() == 3);

  // The check is at the border only: a sibling that holds a bare pointer
  // transplants it in unchecked.
  flexi_function<int()> lax{&plain_seven};
  strict_fn adopted{std::move(lax)};
  CHECK(adopted() == 7);
  CHECK(adopted.size() == sizeof(int (*)()));
}

TEST_CASE("Direct callables are not stored", "[flexi_function]") {
  // A captureless lambda and a data-free functor occupy no storage under any
  // policy, so heap_only allocates nothing for them.
  flexi_function<int(), dflt> lam{[] { return 5; }};
  CHECK(lam);
  CHECK(lam() == 5);
  CHECK(lam.size() == 0);

  flexi_function<int(), heap_only> h{nine_fn{}};
  CHECK(h);
  CHECK(h() == 9);
  CHECK(h.size() == 0);

  // A stateful callable is stored and reports its size as before.
  int live{};
  flexi_function<int(), dflt> stateful{counted{&live}};
  CHECK(stateful.size() == sizeof(counted));

  // Transplants go anywhere: into the leanest inline_only buffer, into
  // heap_only, and back, without touching either side's storage.
  CHECK(flexi_function<int(), lean>::can_adopt(lam));
  flexi_function<int(), lean> tiny{std::move(lam)};
  CHECK(!lam);
  CHECK(tiny() == 5);
  CHECK(tiny.size() == 0);
  flexi_function<int(), heap_only> boxed{std::move(tiny)};
  CHECK(!tiny);
  CHECK(boxed() == 5);
  CHECK(boxed.size() == 0);
  fixed_function<int(), 64> back{std::move(boxed)};
  CHECK(!boxed);
  CHECK(back() == 5);

  // Reset and reassignment behave as for any callable.
  back = nullptr;
  CHECK(!back);
  back = nine_fn{};
  CHECK(back() == 9);
  CHECK(back.size() == 0);
}

#pragma endregion
#pragma region Empty-call policy

TEST_CASE("Can adopt", "[flexi_function]") {
  using small_ff = fixed_function<int(), 64>;

  // A callable too large for an inline_only buffer is refused up front.
  flexi_function<int(), big_inline> fat{fat_fn{}};
  CHECK(!small_ff::can_adopt(fat));
  small_ff target{[] { return 0; }};
  CHECK(!target.can_adopt(fat));
  CHECK_THROWS_AS(target = std::move(fat), std::length_error);
  CHECK(target() == 0);
  CHECK(fat() == 7);

  // A callable that fits, and an empty source, are adoptable.
  flexi_function<int(), big_inline> thin{[] { return 1; }};
  CHECK(small_ff::can_adopt(thin));
  target = std::move(thin);
  CHECK(target() == 1);
  flexi_function<int(), big_inline> hollow;
  CHECK(small_ff::can_adopt(hollow));

  // A destination with heap fallback always accommodates.
  CHECK((flexi_function<int(), dflt>::can_adopt(fat)));
}

TEST_CASE("Null callables are empty", "[flexi_function]") {
  using fn_t = flexi_function<int(), dflt>;

  // A null function pointer yields an empty wrapper, as with
  // `std::function`, rather than one that calls through null.
  using fn_ptr = int (*)();
  fn_t f{fn_ptr{nullptr}};
  CHECK(!f);
  CHECK_THROWS_AS(f(), std::bad_function_call);

  // A non-null one is stored as usual.
  f = &plain_seven;
  CHECK(f);
  CHECK(f() == 7);

  // Assigning a null pointer empties it again.
  f = fn_ptr{nullptr};
  CHECK(!f);

  // Null member pointers likewise, for both member functions and members.
  using mf_ptr = int (widget::*)() const;
  flexi_function<int(const widget&), dflt> g{mf_ptr{nullptr}};
  CHECK(!g);
  g = &widget::value;
  CHECK(g(widget{3}) == 3);
  using mp_ptr = int widget::*;
  flexi_function<int(widget&), dflt> h{mp_ptr{nullptr}};
  CHECK(!h);
  h = &widget::n;
  widget w{5};
  CHECK(h(w) == 5);

  // An empty std wrapper yields an empty wrapper too, by the same rule; a
  // live one is stored.
  fn_t std_empty{std::function<int()>{}};
  CHECK(!std_empty);
  fn_t std_live{std::function<int()>{[] { return 3; }}};
  CHECK(std_live() == 3);
}

TEST_CASE("Trivially copyable lvalues are copied in", "[flexi_function]") {
  using fn_t = flexi_function<int(), dflt>;

  // A named function pointer, a captureless lambda, and a lambda with only
  // trivially copyable captures all store from an lvalue by copy.
  int (*fp)() = &plain_seven;
  fn_t a{fp};
  CHECK(a() == 7);
  auto captureless = [] { return 1; };
  fn_t b{captureless};
  CHECK(b() == 1);
  int n = 2;
  auto small = [n] { return n; };
  fn_t c{small};
  CHECK(c() == 2);
  c = fp;
  CHECK(c() == 7);

  // A callable that is not trivially copyable is still rvalue-only, so a
  // heavy capture is never copied by accident.
  auto heavy = [s = std::string{"x"}] { return static_cast<int>(s.size()); };
  static_assert(!std::is_constructible_v<fn_t, decltype(heavy)&>);
  static_assert(std::is_constructible_v<fn_t, decltype(heavy)&&>);
  fn_t d{std::move(heavy)};
  CHECK(d() == 1);
  // The analyzer loses the heap block here as in "Heap fallback".
  // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
}

TEST_CASE("Silent returns a default", "[flexi_function]") {
  flexi_function<int(), silent> i;
  CHECK(!i);
  CHECK(i() == 0);

  flexi_function<std::string(int), silent> s;
  CHECK(s(5).empty());

  flexi_function<void(), silent> v;
  CHECK_NOTHROW(v());

  // A result that cannot be value-initialized (`int&()`, `no_default()`) is
  // a static_assert under `silent`, so it cannot be exercised here; the
  // alternatives are `raise` and `terminate`. Both accept any result.
  flexi_function<int&(), terminating> tr;
  CHECK(!tr);
  flexi_function<no_default(), terminating> tnd;
  CHECK(!tnd);

  // The compile-time default is raise.
  flexi_function<int(), dflt> d;
  CHECK_THROWS_AS(d(), std::bad_function_call);
}

TEST_CASE("Terminate policy invokes normally when not empty",
    "[flexi_function]") {
  // Invoking an empty `terminating` wrapper calls `std::terminate`, which a
  // unit test cannot observe; only the non-empty path is exercised.
  flexi_function<int(int), terminating> t{[](int x) { return x * 2; }};
  CHECK(t);
  CHECK(t(21) == 42);
  t.reset();
  CHECK(!t);
}

TEST_CASE("Reset", "[flexi_function]") {
  using raiser = flexi_function<int(), dflt>;
  using silencer = flexi_function<int(), silent>;

  // `reset()` empties; the empty-call behavior is the type's own.
  raiser f{[] { return 1; }};
  f.reset();
  CHECK(!f);
  CHECK_THROWS_AS(f(), std::bad_function_call);

  // So do assigning nullptr and direct callable assignment.
  f = [] { return 2; };
  CHECK(f() == 2);
  f = nullptr;
  CHECK_THROWS_AS(f(), std::bad_function_call);

  // A `silent` type empties to its own behavior likewise.
  silencer i{[] { return 3; }};
  CHECK(i() == 3);
  i.reset();
  CHECK(!i);
  CHECK(i() == 0);

  // Resetting an already-empty instance is a no-op.
  i.reset();
  CHECK(i() == 0);
}

TEST_CASE("Empty behavior is fixed per type", "[flexi_function]") {
  using raiser = flexi_function<int(), dflt>;
  using silencer = flexi_function<int(), silent>;

  // `a = std::move(b)`: each side keeps the behavior baked into its type,
  // regardless of what arrived or left.
  raiser a;
  silencer b{[] { return 1; }};
  a = std::move(b);
  CHECK(a() == 1);
  CHECK(!b);
  CHECK(b() == 0);
  a.reset();
  CHECK_THROWS_AS(a(), std::bad_function_call);

  // The same in the other direction.
  silencer c;
  raiser d{[] { return 2; }};
  c = std::move(d);
  CHECK(c() == 2);
  CHECK_THROWS_AS(d(), std::bad_function_call);
  c.reset();
  CHECK(c() == 0);

  // A moved-from source reverts to its own behavior, not the destination's.
  raiser e{[] { return 3; }};
  raiser g{std::move(e)};
  CHECK(g() == 3);
  CHECK_THROWS_AS(e(), std::bad_function_call);

  // Across policies, through the converting constructor: the
  // `fixed_function` raises on empty because that is its type's behavior,
  // while the drained `silent` source stays silent.
  silencer h{[] { return 4; }};
  fixed_function<int(), 64> k{std::move(h)};
  CHECK(k() == 4);
  k.reset();
  CHECK_THROWS_AS(k(), std::bad_function_call);
  CHECK(h() == 0);
}

#pragma endregion
#pragma region Qualified signatures

// A callable that can only be invoked as a non-const lvalue, and counts.
struct lvalue_only {
  int n = 0;
  int operator()() & { return ++n; }
};

// A callable that can only be invoked as an rvalue, and reports it.
struct rvalue_only {
  int operator()() && { return 7; }
};

// A callable that can only be invoked as a const object.
struct const_only {
  int operator()() const { return 9; }
};

TEST_CASE("Signature qualifiers select how the target is invoked",
    "[flexi_function]") {
  // An unqualified and an `&`-qualified signature both invoke the target as
  // a non-const lvalue, so an `&`-only callable is admitted and an `&&`-only
  // one is not.
  static_assert(
      std::is_constructible_v<flexi_function<int(), dflt>, lvalue_only>);
  static_assert(
      std::is_constructible_v<flexi_function<int() &, dflt>, lvalue_only>);
  static_assert(
      !std::is_constructible_v<flexi_function<int(), dflt>, rvalue_only>);
  static_assert(
      !std::is_constructible_v<flexi_function<int() &, dflt>, rvalue_only>);

  // An `&&`-qualified signature invokes the target as an rvalue.
  static_assert(
      std::is_constructible_v<flexi_function<int() &&, dflt>, rvalue_only>);
  static_assert(
      !std::is_constructible_v<flexi_function<int() &&, dflt>, lvalue_only>);

  // A `const` signature invokes the target as const, so a callable whose
  // `operator()` is not const is refused, and a const-only one is admitted
  // under both.
  static_assert(!std::is_constructible_v<flexi_function<int() const, dflt>,
      lvalue_only>);
  static_assert(
      std::is_constructible_v<flexi_function<int() const, dflt>, const_only>);
  static_assert(
      std::is_constructible_v<flexi_function<int(), dflt>, const_only>);

  // The lvalue invocation reaches the same stored object every call.
  flexi_function<int() &, dflt> counting{lvalue_only{}};
  CHECK(counting() == 1);
  CHECK(counting() == 2);

  // An rvalue call invokes the target as an rvalue but does not consume the
  // wrapper.
  flexi_function<int() &&, dflt> moving{rvalue_only{}};
  CHECK(std::move(moving)() == 7);
  CHECK(moving);

  // A const call reaches the target as const.
  const flexi_function<int() const, dflt> constant{const_only{}};
  CHECK(constant() == 9);

  // A `noexcept` signature admits only nothrow-invocable callables, and
  // needs an empty-call policy that cannot throw.
  static_assert(
      std::is_constructible_v<flexi_function<int() noexcept, terminating>,
          decltype([]() noexcept { return 1; })>);
  static_assert(
      !std::is_constructible_v<flexi_function<int() noexcept, terminating>,
          decltype([] { return 1; })>);
}

// Whether a wrapper over `Sig` under policy `P` can be called through an
// lvalue, a const lvalue, and an rvalue, respectively.
template<invocable_policy P, class Sig>
constexpr bool
calls_through(bool lvalue, bool const_lvalue, bool rvalue) noexcept {
  using wrapper = flexi_function<Sig, P>;
  return (std::is_invocable_v<wrapper&> == lvalue) &&
         (std::is_invocable_v<const wrapper&> == const_lvalue) &&
         (std::is_invocable_v<wrapper&&> == rvalue);
}

TEST_CASE("Signature qualifiers select which wrappers may call",
    "[flexi_function]") {
  // The six cv/ref combinations, under the default policy.
  static_assert(calls_through<dflt, int()>(true, false, true));
  static_assert(calls_through<dflt, int() const>(true, true, true));
  static_assert(calls_through<dflt, int() &>(true, false, false));
  static_assert(calls_through<dflt, int() const&>(true, true, false));
  static_assert(calls_through<dflt, int() &&>(false, false, true));
  static_assert(calls_through<dflt, int() const&&>(false, false, true));

  // The same six with `noexcept`, which needs a non-throwing empty policy.
  using noex_sig = int() noexcept;
  using const_noex_sig = int() const noexcept;
  using lref_noex_sig = int() & noexcept;
  using const_lref_noex_sig = int() const& noexcept;
  using rref_noex_sig = int() && noexcept;
  using const_rref_noex_sig = int() const&& noexcept;
  static_assert(calls_through<terminating, noex_sig>(true, false, true));
  static_assert(calls_through<terminating, const_noex_sig>(true, true, true));
  static_assert(calls_through<terminating, lref_noex_sig>(true, false, false));
  static_assert(
      calls_through<terminating, const_lref_noex_sig>(true, true, false));
  static_assert(calls_through<terminating, rref_noex_sig>(false, false, true));
  static_assert(
      calls_through<terminating, const_rref_noex_sig>(false, false, true));

  // The call is `noexcept` exactly when the signature is.
  flexi_function<noex_sig, terminating> n{[]() noexcept { return 1; }};
  static_assert(noexcept(n()));
  CHECK(n() == 1);
  flexi_function<int(), dflt> p{[] { return 2; }};
  static_assert(!noexcept(p()));
  CHECK(p() == 2);
}

TEST_CASE("Qualified signatures wrap and adopt like plain ones",
    "[flexi_function]") {
#ifdef __cpp_lib_move_only_function
  // A wrapped std wrapper is invoked as the signature invokes any target, so
  // an `&&` signature takes an `&&`-qualified `std::move_only_function` and
  // refuses an `&`-qualified one, the reverse of the unqualified case.
  using rref = flexi_function<int() &&, dflt>;
  static_assert(
      std::is_constructible_v<rref, std::move_only_function<int() &&>&&>);
  static_assert(
      !std::is_constructible_v<rref, std::move_only_function<int() &>&&>);
  rref r{std::move_only_function<int() &&>{[] { return 5; }}};
  CHECK(std::move(r)() == 5);
#endif

  // Thunks are keyed by the whole signature, so siblings that share one
  // transplant the target across policies as before.
  flexi_function<int() const, dflt> a{const_only{}};
  CHECK(fixed_function<int() const, 64>::can_adopt(a));
  fixed_function<int() const, 64> b{std::move(a)};
  CHECK(!a);
  const auto& cb = b;
  CHECK(cb() == 9);

  // `const_only` and `rvalue_only` are direct, so they are not stored, and
  // the signature's qualifiers apply to the instance the thunk names per
  // call.
  CHECK(b.size() == 0);
  flexi_function<int() &&, dflt> rv{rvalue_only{}};
  CHECK(rv.size() == 0);
  CHECK(std::move(rv)() == 7);
}

#pragma endregion

// NOLINTEND(clang-analyzer-cplusplus.Move)
// NOLINTEND(bugprone-use-after-move)
// NOLINTEND(readability-function-cognitive-complexity)
