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
#include "corvid/meta/fixed_function.h"
#include "corvid/meta/flexi_function.h"

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

constexpr invocable_policy dflt{};
constexpr invocable_policy silent{.empty = on_empty::silent};
constexpr invocable_policy terminating{.empty = on_empty::terminate};
constexpr invocable_policy heap_only{.alloc = invocable_alloc::heap_only};
constexpr invocable_policy big_inline{.inline_size = padded_size(96)};

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

struct no_default {
  explicit no_default(int) {}
};

int plain_seven() { return 7; }

struct widget {
  int n;
  explicit widget(int n) : n{n} {}
  [[nodiscard]] int value() const { return n; }
};

} // namespace

// The alias reproduces `fixed_function` exactly.
static_assert(std::is_same_v<fixed_function<64, int()>,
    flexi_function<invocable_policy{.inline_size = 64 - (2 * sizeof(void*)),
                       .alloc = invocable_alloc::inline_only},
        int()>>);
static_assert(is_fixed_function_v<fixed_function<64, int()>>);
static_assert(!is_fixed_function_v<flexi_function<dflt, int()>>);
static_assert(is_flexi_function_v<fixed_function<64, int()>>);
static_assert(sizeof(flexi_function<heap_only, int()>) == 3 * sizeof(void*));
static_assert(flexi_function<heap_only, int()>::storage_size == 0);
static_assert(flexi_function<dflt, int()>::storage_size == 2 * sizeof(void*));

// An inline_only buffer may be aligned below the default: with no heap
// pointer to hold, nothing forces the buffer up to the default geometry.
constexpr invocable_policy lean{.inline_size = 2 * sizeof(void*),
    .inline_align = alignof(void*),
    .alloc = invocable_alloc::inline_only};
static_assert(sizeof(flexi_function<lean, int()>) == 4 * sizeof(void*));
static_assert(alignof(flexi_function<lean, int()>) == alignof(void*));

// Same-policy moves never throw; a move that may box or refuse does.
static_assert(
    std::is_nothrow_move_constructible_v<flexi_function<dflt, int()>>);
static_assert(std::is_nothrow_constructible_v<
    flexi_function<big_inline, int()>, flexi_function<dflt, int()>&&>);
static_assert(!std::is_nothrow_constructible_v<flexi_function<dflt, int()>,
    flexi_function<big_inline, int()>&&>);
static_assert(!std::is_nothrow_constructible_v<fixed_function<64, int()>,
    flexi_function<dflt, int()>&&>);

// Storing a callable is noexcept exactly when it stays inline.
static_assert(
    std::is_nothrow_constructible_v<flexi_function<dflt, int()>, int (*)()>);
static_assert(
    !std::is_nothrow_constructible_v<flexi_function<dflt, int()>, fat_fn>);
static_assert(!std::is_nothrow_constructible_v<
    flexi_function<heap_only, int()>, int (*)()>);

#pragma region Storage

TEST_CASE("Heap fallback", "[flexi_function]") {
  int live{};
  {
    // Too big for the buffer: heap.
    flexi_function<dflt, int()> fat{fat_fn{}};
    CHECK(fat);
    CHECK(fat() == 7);
    CHECK(fat.size() == sizeof(fat_fn));
    CHECK(fat.capacity() == dflt.inline_size);

    // Throwing move: heap, even though it would fit.
    flexi_function<dflt, int()> tm{throwing_mover{3}};
    CHECK(tm() == 3);

    // Small and nothrow: inline.
    flexi_function<dflt, int()> small{counted{&live}};
    CHECK(live == 1);
    CHECK(small() == 1);

    // heap_only: everything on the heap, destroyed on the way out.
    flexi_function<heap_only, int()> h{counted{&live}};
    CHECK(live == 2);
    CHECK(h() == 1);
    CHECK(h.size() == sizeof(counted));
    CHECK(h.capacity() == 0);

    // Heap-to-heap move hands over the block without touching the callable.
    flexi_function<heap_only, int()> h2{std::move(h)};
    CHECK(live == 2);
    CHECK(!h);
    CHECK(h2() == 1);
  }
  CHECK(live == 0);
}

TEST_CASE("Cross-policy transplant", "[flexi_function]") {
  int live{};

  // Inline into a bigger buffer stays inline.
  flexi_function<dflt, int()> a{counted{&live}};
  flexi_function<big_inline, int()> b{std::move(a)};
  CHECK(live == 1);
  CHECK(!a);
  CHECK(b() == 1);

  // A heap arrival into a heap-admitting policy stays boxed: the block is
  // handed over as is, even though the bigger buffer could hold it.
  flexi_function<dflt, int()> fat{fat_fn{}};
  flexi_function<big_inline, int()> roomy{std::move(fat)};
  CHECK(roomy() == 7);
  CHECK(roomy.size() == sizeof(fat_fn));

  // Inline arrival into a buffer too small for it is boxed onto the heap.
  flexi_function<dflt, int()> back{std::move(roomy)};
  CHECK(!roomy);
  CHECK(back() == 7);

  // Into heap_only, an inline arrival is boxed; back out, the block is
  // handed over, un-boxing only when the destination is inline_only (below).
  flexi_function<heap_only, int()> boxed{std::move(b)};
  CHECK(live == 1);
  CHECK(boxed() == 1);
  flexi_function<dflt, int()> unboxed{std::move(boxed)};
  CHECK(live == 1);
  CHECK(!boxed);
  CHECK(unboxed() == 1);

  // An inline_only destination refuses what it cannot hold, leaving the source
  // intact, in both the constructor and the pre-flight assignment.
  CHECK_THROWS_AS((fixed_function<64, int()>{std::move(back)}),
      std::length_error);
  CHECK(back);
  CHECK(back() == 7);
  fixed_function<64, int()> target{[] { return 0; }};
  CHECK_THROWS_AS(target = std::move(back), std::length_error);
  CHECK(target() == 0);
  CHECK(back() == 7);

  // An inline_only destination accepts a heap arrival that fits.
  fixed_function<64, int()> fits{std::move(unboxed)};
  CHECK(live == 1);
  CHECK(fits() == 1);
  fits = nullptr;
  CHECK(live == 0);
}

#pragma endregion
#pragma region Empty-call policy

TEST_CASE("Can adopt", "[flexi_function]") {
  using small_ff = fixed_function<64, int()>;

  // A callable too large for an inline_only buffer is refused up front.
  flexi_function<big_inline, int()> fat{fat_fn{}};
  CHECK(!small_ff::can_adopt(fat));
  small_ff target{[] { return 0; }};
  CHECK(!target.can_adopt(fat));
  CHECK_THROWS_AS(target = std::move(fat), std::length_error);
  CHECK(target() == 0);
  CHECK(fat() == 7);

  // A callable that fits, and an empty source, are adoptable.
  flexi_function<big_inline, int()> thin{[] { return 1; }};
  CHECK(small_ff::can_adopt(thin));
  target = std::move(thin);
  CHECK(target() == 1);
  flexi_function<big_inline, int()> hollow;
  CHECK(small_ff::can_adopt(hollow));

  // A destination with heap fallback always accommodates.
  CHECK((flexi_function<dflt, int()>::can_adopt(fat)));
}

TEST_CASE("Null callables are empty", "[flexi_function]") {
  using fn_t = flexi_function<dflt, int()>;

  // A null function pointer yields an empty wrapper, as with `std::function`,
  // rather than one that calls through null.
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
  flexi_function<dflt, int(const widget&)> g{mf_ptr{nullptr}};
  CHECK(!g);
  g = &widget::value;
  CHECK(g(widget{3}) == 3);
  using mp_ptr = int widget::*;
  flexi_function<dflt, int(widget&)> h{mp_ptr{nullptr}};
  CHECK(!h);
  h = &widget::n;
  widget w{5};
  CHECK(h(w) == 5);
}

TEST_CASE("Trivially copyable lvalues are copied in", "[flexi_function]") {
  using fn_t = flexi_function<dflt, int()>;

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
}

TEST_CASE("Silent returns a default", "[flexi_function]") {
  flexi_function<silent, int()> i;
  CHECK(!i);
  CHECK(i() == 0);

  flexi_function<silent, std::string(int)> s;
  CHECK(s(5).empty());

  flexi_function<silent, void()> v;
  CHECK_NOTHROW(v());

  // A result that cannot be value-initialized (`int&()`, `no_default()`) is a
  // static_assert under `silent`, so it cannot be exercised here; the
  // alternatives are `raise` and `terminate`. Both accept any result.
  flexi_function<terminating, int&()> tr;
  CHECK(!tr);
  flexi_function<terminating, no_default()> tnd;
  CHECK(!tnd);

  // The compile-time default is raise.
  flexi_function<dflt, int()> d;
  CHECK_THROWS_AS(d(), std::bad_function_call);
}

TEST_CASE("Terminate policy invokes normally when not empty",
    "[flexi_function]") {
  // Invoking an empty `terminating` wrapper calls `std::terminate`, which a
  // unit test cannot observe; only the non-empty path is exercised.
  flexi_function<terminating, int(int)> t{[](int x) { return x * 2; }};
  CHECK(t);
  CHECK(t(21) == 42);
  t.reset();
  CHECK(!t);
}

TEST_CASE("Reset", "[flexi_function]") {
  using raiser = flexi_function<dflt, int()>;
  using silencer = flexi_function<silent, int()>;

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
  using raiser = flexi_function<dflt, int()>;
  using silencer = flexi_function<silent, int()>;

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

  // Across policies, through the converting constructor: the `fixed_function`
  // raises on empty because that is its type's behavior, while the drained
  // `silent` source stays silent.
  silencer h{[] { return 4; }};
  fixed_function<64, int()> k{std::move(h)};
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
      std::is_constructible_v<flexi_function<dflt, int()>, lvalue_only>);
  static_assert(
      std::is_constructible_v<flexi_function<dflt, int() &>, lvalue_only>);
  static_assert(
      !std::is_constructible_v<flexi_function<dflt, int()>, rvalue_only>);
  static_assert(
      !std::is_constructible_v<flexi_function<dflt, int() &>, rvalue_only>);

  // An `&&`-qualified signature invokes the target as an rvalue.
  static_assert(
      std::is_constructible_v<flexi_function<dflt, int() &&>, rvalue_only>);
  static_assert(
      !std::is_constructible_v<flexi_function<dflt, int() &&>, lvalue_only>);

  // A `const` signature invokes the target as const, so a callable whose
  // `operator()` is not const is refused, and a const-only one is admitted
  // under both.
  static_assert(!std::is_constructible_v<flexi_function<dflt, int() const>,
      lvalue_only>);
  static_assert(
      std::is_constructible_v<flexi_function<dflt, int() const>, const_only>);
  static_assert(
      std::is_constructible_v<flexi_function<dflt, int()>, const_only>);

  // The lvalue invocation reaches the same stored object every call.
  flexi_function<dflt, int() &> counting{lvalue_only{}};
  CHECK(counting() == 1);
  CHECK(counting() == 2);

  // An rvalue call invokes the target as an rvalue but does not consume the
  // wrapper.
  flexi_function<dflt, int() &&> moving{rvalue_only{}};
  CHECK(std::move(moving)() == 7);
  CHECK(moving);

  // A const call reaches the target as const.
  const flexi_function<dflt, int() const> constant{const_only{}};
  CHECK(constant() == 9);

  // A `noexcept` signature admits only nothrow-invocable callables, and needs
  // an empty-call policy that cannot throw.
  static_assert(
      std::is_constructible_v<flexi_function<terminating, int() noexcept>,
          decltype([]() noexcept { return 1; })>);
  static_assert(
      !std::is_constructible_v<flexi_function<terminating, int() noexcept>,
          decltype([] { return 1; })>);
}

// Whether a wrapper over `Sig` under policy `P` can be called through an
// lvalue, a const lvalue, and an rvalue, respectively.
template<invocable_policy P, class Sig>
constexpr bool
calls_through(bool lvalue, bool const_lvalue, bool rvalue) noexcept {
  using wrapper = flexi_function<P, Sig>;
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
  flexi_function<terminating, noex_sig> n{[]() noexcept { return 1; }};
  static_assert(noexcept(n()));
  CHECK(n() == 1);
  flexi_function<dflt, int()> p{[] { return 2; }};
  static_assert(!noexcept(p()));
  CHECK(p() == 2);
}

TEST_CASE("Qualified signatures wrap and adopt like plain ones",
    "[flexi_function]") {
#ifdef __cpp_lib_move_only_function
  // A wrapped std wrapper is invoked as the signature invokes any target, so
  // an `&&` signature takes an `&&`-qualified `std::move_only_function` and
  // refuses an `&`-qualified one, the reverse of the unqualified case.
  using rref = flexi_function<dflt, int() &&>;
  static_assert(
      std::is_constructible_v<rref, std::move_only_function<int() &&>&&>);
  static_assert(
      !std::is_constructible_v<rref, std::move_only_function<int() &>&&>);
  rref r{std::move_only_function<int() &&>{[] { return 5; }}};
  CHECK(std::move(r)() == 5);
#endif

  // Thunks are keyed by the whole signature, so siblings that share one
  // transplant the target across policies as before.
  flexi_function<dflt, int() const> a{const_only{}};
  CHECK(fixed_function<64, int() const>::can_adopt(a));
  fixed_function<64, int() const> b{std::move(a)};
  CHECK(!a);
  const auto& cb = b;
  CHECK(cb() == 9);
}

#pragma endregion

// NOLINTEND(clang-analyzer-cplusplus.Move)
// NOLINTEND(bugprone-use-after-move)
// NOLINTEND(readability-function-cognitive-complexity)
