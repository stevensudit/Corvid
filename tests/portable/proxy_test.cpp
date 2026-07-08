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

#include <concepts>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "corvid/meta/proxy.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::meta::prox::literals;

// The facade under test: mixes value returns, a const method, a void
// mutator, and a reference return.
struct gunslinger
    : prox::facade<                                      //
          prox::method<"fire", int(int)>,                //
          prox::method<"describe", std::string() const>, //
          prox::method<"reload", void()>,                //
          prox::method<"shots", int&()>> {};             //

// This base class for proxy_impl<gunslinger, T> would not normally be
// necessary, but it's used for `sheriff`. It also wouldn't need `prox::method`
// to be fully specified.
template<typename T>
struct proxy_impl_gunslinger {
  static int on(prox::method_key<"fire">, T& t, int rounds) {
    return t.fire(rounds);
  }
  static std::string on(prox::method_key<"describe">, const T& t) {
    return t.describe();
  }
  static void on(prox::method_key<"reload">, T& t) { t.reload(); }
  static int& on(prox::method_key<"shots">, T& t) { return t.shots(); }
};

// Boilerplate impl for `gunslinger`: written once by the facade author,
// generic over any registered type whose member names line up.
//
// Again, it would normally contain the guts of what's been moved to
// `proxy_impl_gunslinger`.
template<typename T>
requires prox::ProxyRegistered<gunslinger, T>
struct prox::proxy_impl<gunslinger, T>: proxy_impl_gunslinger<T> {};

// A type whose method names line up with the facade; conforms by
// registration through the boilerplate impl.
struct lawman {
  int fire(int rounds) {
    rounds_fired += rounds;
    return rounds_fired;
  }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "lawman";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }

  int rounds_fired{};
};

// Registration for `lawman`: pure opt-in, one hook, no bindings.
consteval auto corvid_proxy_spec(gunslinger*, lawman*) {
  return prox::make_proxy_spec<gunslinger, lawman>();
}

// Exactly like a lawman, but with a different name. Works automatically.
struct deputy: public lawman {
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "deputy";
  }
};

// A type with the right shape but the wrong names; conforms through an
// explicit impl, with no registration.
struct robber {
  int shoot(int rounds) {
    fired += rounds;
    return fired;
  }
  [[nodiscard]] std::string description() const {
    (void)this;
    return "robber";
  }
  void rearm() { fired = 0; }

  int fired{};
};

// Custom impl for `robber`.
//
// A full specialization outranks the boilerplate and needs no registration.
// Note the name adaptation, including binding "shots" to a data member.
template<>
struct prox::proxy_impl<gunslinger, robber> {
  static int on(method_key<"fire">, robber& r, int rounds) {
    return r.shoot(rounds);
  }
  static std::string on(method_key<"describe">, const robber& r) {
    return r.description();
  }
  static void on(method_key<"reload">, robber& r) { r.rearm(); }
  static int& on(method_key<"shots">, robber& r) { return r.fired; }
};

// A type whose method names line up, with one exception: `shoot` instead of
// `fire`. Uses a full specialization that inherits and overrides the
// boilerplate impl.
struct sheriff {
  int shoot(int rounds) {
    rounds_fired += rounds;
    return rounds_fired;
  }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "sheriff";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }

  int rounds_fired{};
};

// Custom impl for `sheriff`: inherits the boilerplate and overrides the
// one method whose name differs.
template<>
struct prox::proxy_impl<gunslinger, sheriff>: proxy_impl_gunslinger<sheriff> {
  using proxy_impl_gunslinger<sheriff>::on;
  static int on(method_key<"fire">, sheriff& s, int rounds) {
    return s.shoot(rounds);
  }
};

// A type whose method names line up but which never opts in. Nominal
// conformance means this must NOT be proxiable.
struct cowboy {
  int fire(int rounds) {
    (void)this;
    return rounds;
  }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "cowboy";
  }
  void reload() {}
  int& shots() { return count; }

  int count{};
};

// A facade with noexcept methods: conformance requires the bindings
// themselves to be noexcept.
struct hair_trigger
    : prox::facade<                                //
          prox::method<"fire", int(int) noexcept>, //
          prox::method<"jams", bool() const noexcept>> {};

// Bindings marked noexcept: conforms. The target's own methods need not be
// noexcept; the binding is the terminate boundary.
template<>
struct prox::proxy_impl<hair_trigger, lawman> {
  static int on(method_key<"fire">, lawman& l, int rounds) noexcept {
    return l.fire(rounds);
  }
  static bool on(method_key<"jams">, const lawman&) noexcept { return false; }
};

// Bindings not marked noexcept: must NOT conform, even though the shapes
// otherwise line up.
template<>
struct prox::proxy_impl<hair_trigger, robber> {
  static int on(method_key<"fire">, robber& r, int rounds) {
    return r.shoot(rounds);
  }
  static bool on(method_key<"jams">, const robber&) { return false; }
};

// Lifetime accounting shared by the owning-proxy targets.
struct life_stats {
  int constructed{};
  int destroyed{};
  int moves{};
};

// Move-only lifetime-counting target. `Pad` scales the footprint so one
// instantiation fits the proxy's inline buffer and another forces the heap
// path.
template<std::size_t Pad>
struct strongbox {
  explicit strongbox(life_stats& stats) noexcept : stats_{&stats} {
    ++stats_->constructed;
  }
  strongbox(strongbox&& other) noexcept
      : stats_{other.stats_}, gold_{other.gold_} {
    ++stats_->constructed;
    ++stats_->moves;
  }
  strongbox(const strongbox&) = delete;
  strongbox& operator=(const strongbox&) = delete;
  strongbox& operator=(strongbox&&) = delete;
  ~strongbox() { ++stats_->destroyed; }

  int add(int nuggets) { return gold_ += nuggets; }
  [[nodiscard]] int gold() const { return gold_; }

  life_stats* stats_{};
  int gold_{};
  std::byte pad_[Pad]{};
};

// The facade for the lifetime tests.
struct lockbox
    : prox::facade<                      //
          prox::method<"add", int(int)>, //
          prox::method<"gold", int() const>> {};

// Boilerplate impl for `lockbox`.
template<typename T>
requires prox::ProxyRegistered<lockbox, T>
struct prox::proxy_impl<lockbox, T> {
  static int on(method_key<"add">, T& t, int nuggets) {
    return t.add(nuggets);
  }
  static int on(method_key<"gold">, const T& t) { return t.gold(); }
};

// Registration for every `strongbox` size, via a template hook.
template<std::size_t Pad>
consteval auto corvid_proxy_spec(lockbox*, strongbox<Pad>*) {
  return prox::make_proxy_spec<lockbox, strongbox<Pad>>();
}

// One size fits the inline buffer exactly; the other forces the heap path.
using small_box = strongbox<4>;
using big_box = strongbox<64>;
static_assert(sizeof(small_box) <= prox::proxy<lockbox>::sbo_size);
static_assert(sizeof(big_box) > prox::proxy<lockbox>::sbo_size);

// Facade detection.
static_assert(prox::Facade<gunslinger>);
static_assert(!prox::Facade<lawman>);
static_assert(!prox::Facade<int>);

// A method is-a key: usable anywhere its key is.
static_assert(std::derived_from<prox::method<"fire", int(int)>,
    prox::method_key<"fire">>);
static_assert(std::derived_from<prox::method<"describe", std::string() const>,
    prox::method_key<"describe">>);
static_assert(!std::derived_from<prox::method<"fire", int(int)>,
    prox::method_key<"reload">>);

// UDL: `"name"_method` is a `prox::method_key<"name">`.
static_assert(std::same_as<decltype("fire"_method), prox::method_key<"fire">>);

// Registration state: lawman is registered, robber conforms without
// registration, cowboy neither registers nor conforms.
static_assert(prox::ProxyRegistered<gunslinger, lawman>);
static_assert(prox::ProxyRegistered<gunslinger, deputy>);
static_assert(!prox::ProxyRegistered<gunslinger, robber>);
static_assert(!prox::ProxyRegistered<gunslinger, sheriff>);
static_assert(!prox::ProxyRegistered<gunslinger, cowboy>);
static_assert(std::same_as<decltype(prox::proxy_spec_v<gunslinger, lawman>),
    const prox::proxy_spec<gunslinger, lawman>>);

// Conformance: boilerplate via registration, explicit impl directly, and
// matching names alone are NOT enough.
//
// Diagnostics on record (clang 22, captured 2026-07-08). Constructing
// `proxy_view<gunslinger>` from an unregistered `cowboy` emits exactly one
// error: "no matching constructor for initialization of
// 'proxy_view<gunslinger>' ... candidate template ignored: constraints not
// satisfied [with T = cowboy] ... because 'Proxiable<cowboy, gunslinger>'
// evaluated to false ... because
// 'details::info_t<gunslinger>::all_bound_v<gunslinger, cowboy>' evaluated
// to false". Calling `pv.call<"missing">()` fires the static_assert
// "facade has no method with this name", followed by `std::get` index-noise
// errors; the follow-on noise is accepted (a guarding `if constexpr` was
// tried and dropped as a simplification), since fixing the obvious message
// also removes the noise.
static_assert(prox::Proxiable<lawman, gunslinger>);
static_assert(prox::Proxiable<robber, gunslinger>);
static_assert(prox::Proxiable<sheriff, gunslinger>);
static_assert(!prox::Proxiable<cowboy, gunslinger>);
static_assert(!prox::Proxiable<int, gunslinger>);

// Noexcept qualification is part of the method flavor.
static_assert(!prox::method<"fire", int(int)>::noexcept_v);
static_assert(prox::method<"fire", int(int) noexcept>::noexcept_v);
static_assert(prox::method<"jams", bool() const noexcept>::const_v);
static_assert(prox::method<"jams", bool() const noexcept>::noexcept_v);

// Noexcept conformance: the binding itself must be noexcept.
static_assert(prox::Proxiable<lawman, hair_trigger>);
static_assert(!prox::Proxiable<robber, hair_trigger>);

// The erased call carries the method's noexcept.
static_assert(
    noexcept(std::declval<prox::proxy_view<hair_trigger>&>().call<"fire">(1)));
static_assert(
    !noexcept(std::declval<prox::proxy_view<gunslinger>&>().call<"fire">(1)));

// Both handles satisfy their own facade, including facades with noexcept
// methods.
static_assert(prox::Proxiable<prox::proxy_view<gunslinger>, gunslinger>);
static_assert(prox::Proxiable<prox::proxy<gunslinger>, gunslinger>);
static_assert(prox::Proxiable<prox::proxy_view<hair_trigger>, hair_trigger>);
static_assert(prox::Proxiable<prox::proxy<hair_trigger>, hair_trigger>);

// Views are value-semantic: copyable and rebindable by assignment. The table
// member is pointer-to-const, not a const member, so assignment stays viable.
static_assert(std::assignable_from<prox::proxy_view<gunslinger>&,
    const prox::proxy_view<gunslinger>&>);

// The owning proxy is move-only.
static_assert(!std::copy_constructible<prox::proxy<gunslinger>>);
static_assert(std::movable<prox::proxy<gunslinger>>);

// Deep const: non-const facade methods are not callable on a const handle,
// and never exist on a `const_proxy_view` at all. The probes go through a
// concept so the negative case is a constraint failure rather than a hard
// error (a requires-expression outside a template does not get SFINAE).
template<typename P>
concept CanFire = requires(P& p) { p.template call<"fire">(1); };
template<typename P>
concept CanDescribe = requires(P& p) { p.template call<"describe">(); };

static_assert(CanFire<prox::proxy<gunslinger>>);
static_assert(!CanFire<const prox::proxy<gunslinger>>);
static_assert(CanDescribe<const prox::proxy<gunslinger>>);
static_assert(CanFire<prox::proxy_view<gunslinger>>);
static_assert(!CanFire<const prox::proxy_view<gunslinger>>);
static_assert(CanDescribe<const prox::proxy_view<gunslinger>>);
static_assert(!CanFire<prox::const_proxy_view<gunslinger>>);
static_assert(!CanFire<const prox::const_proxy_view<gunslinger>>);
static_assert(CanDescribe<prox::const_proxy_view<gunslinger>>);

// Const-view conversion rules: a target loses mutability implicitly, never
// regains it. A const target binds only to the const view.
static_assert(std::convertible_to<prox::proxy_view<gunslinger>,
    prox::const_proxy_view<gunslinger>>);
static_assert(!std::constructible_from<prox::proxy_view<gunslinger>,
    prox::const_proxy_view<gunslinger>&>);
static_assert(std::constructible_from<prox::const_proxy_view<gunslinger>,
    const lawman&>);
static_assert(
    !std::constructible_from<prox::proxy_view<gunslinger>, const lawman&>);

// An all-const facade is the one case where a const view satisfies the
// invariant; a mixed facade correctly fails it (the mutable methods are not
// dispatchable, so conformance is impossible; Rust's `&dyn` analog).
struct census: prox::facade<prox::method<"describe", std::string() const>> {};

template<>
struct prox::proxy_impl<census, lawman> {
  static std::string on(method_key<"describe">, const lawman& l) {
    return l.describe();
  }
};

static_assert(prox::Proxiable<prox::const_proxy_view<census>, census>);
static_assert(
    !prox::Proxiable<prox::const_proxy_view<gunslinger>, gunslinger>);

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Boilerplate impl through registration", "[proxy]") {
  lawman l;
  prox::proxy_view<gunslinger> pv{l};

  CHECK(pv.call<"fire">(3) == 3);
  CHECK(pv.call<"fire">(2) == 5);
  CHECK(l.rounds_fired == 5);
  CHECK(pv.call<"describe">() == "lawman"s);

  // Reference return: assign through the erased call.
  pv.call<"shots">() = 42;
  CHECK(l.rounds_fired == 42);

  pv.call<"reload">();
  CHECK(l.rounds_fired == 0);
}

TEST_CASE("Custom impl without registration", "[proxy]") {
  robber r;
  prox::proxy_view<gunslinger> pv{r};

  CHECK(pv.call<"fire">(6) == 6);
  CHECK(r.fired == 6);
  CHECK(pv.call<"describe">() == "robber"s);
  CHECK(pv.call<"shots">() == 6);

  pv.call<"reload">();
  CHECK(r.fired == 0);
}

TEST_CASE("Partially-overridden boilerplate impl", "[proxy]") {
  sheriff s;
  prox::proxy_view<gunslinger> pv{s};

  // The overriding binding: "fire" routes to `shoot`.
  CHECK(pv.call<"fire">(4) == 4);
  CHECK(s.rounds_fired == 4);

  // The inherited boilerplate bindings still serve the rest.
  CHECK(pv.call<"describe">() == "sheriff"s);
  CHECK(pv.call<"shots">() == 4);
  pv.call<"reload">();
  CHECK(s.rounds_fired == 0);
}

TEST_CASE("Heterogeneous dispatch", "[proxy]") {
  lawman l;
  robber r;
  std::vector<prox::proxy_view<gunslinger>> gang{l, r};

  std::string roll_call;
  // Mutable references: "fire" no longer dispatches through a const view.
  for (auto& pv : gang) {
    pv.call<"fire">(2);
    roll_call += pv.call<"describe">();
    roll_call += ' ';
  }
  CHECK(roll_call == "lawman robber "s);
  CHECK(l.rounds_fired == 2);
  CHECK(r.fired == 2);

  // Copies are shallow: both views alias the same target.
  auto pv2 = gang[0];
  pv2.call<"fire">(1);
  CHECK(l.rounds_fired == 3);
}

TEST_CASE("Views rebind by assignment", "[proxy]") {
  lawman l;
  robber r;
  prox::proxy_view<gunslinger> pv{l};
  CHECK(pv.call<"describe">() == "lawman"s);

  // Assignment rebinds both the target and the dispatch table.
  pv = prox::proxy_view<gunslinger>{r};
  CHECK(pv.call<"describe">() == "robber"s);
}

TEST_CASE("Const view", "[proxy]") {
  // A const target binds directly; only const methods exist on this view.
  const lawman cl{};
  prox::const_proxy_view<gunslinger> cv{cl};
  CHECK(cv.call<"describe">() == "lawman"s);

  // A const instance of the mutable view enforces the same restriction.
  lawman l;
  const prox::proxy_view<gunslinger> cpv{l};
  CHECK(cpv.call<"describe">() == "lawman"s);

  // The mutable view converts implicitly, and const views rebind by
  // assignment like any view.
  prox::proxy_view<gunslinger> pv{l};
  prox::const_proxy_view<gunslinger> cv2{pv};
  CHECK(cv2.call<"describe">() == "lawman"s);
  cv = cv2;
  CHECK(cv.call<"describe">() == "lawman"s);

  // All-const facades keep the invariant: generic code constrained on the
  // facade accepts the concrete const target and the const view alike.
  auto describe_it = [](const prox::Proxiable<census> auto& t) {
    prox::const_proxy_view<census> v{t};
    return v.template call<"describe">();
  };
  CHECK(describe_it(cl) == "lawman"s);
  prox::const_proxy_view<census> census_view{cl};
  CHECK(describe_it(census_view) == "lawman"s);
}

TEST_CASE("Generic code accepts concrete and erased alike", "[proxy]") {
  // Facade-constrained generic code: erase, then call. `Proxiable` is the
  // trait bound for the static-dispatch half.
  auto fire_twice = [](prox::Proxiable<gunslinger> auto& g) {
    auto pv = prox::make_proxy_view<gunslinger>(g);
    // In a template context the view's type is dependent, hence `template`.
    pv.template call<"fire">(1);
    return pv.template call<"fire">(1);
  };

  lawman l;
  CHECK(fire_twice(l) == 2);

  // Passing a view works too, and flattens rather than stacking a second
  // indirection.
  prox::proxy_view<gunslinger> pv{l};
  CHECK(fire_twice(pv) == 4);

  // The library-provided view impl is also usable directly.
  CHECK(prox::proxy_impl<gunslinger, prox::proxy_view<gunslinger>>::on(
            "describe"_method, pv) == "lawman"s);

  // An owning proxy satisfies the facade too, so it erases the same way.
  auto p = prox::make_proxy<gunslinger, lawman>();
  CHECK(fire_twice(p) == 2);
}

TEST_CASE("Owning proxy, inline target", "[proxy]") {
  life_stats stats;
  if (true) {
    auto p = prox::make_proxy<lockbox, small_box>(stats);
    CHECK(stats.constructed == 1);
    CHECK(stats.destroyed == 0);
    CHECK(p);
    CHECK(p.call<"add">(3) == 3);
    CHECK(p.call<"gold">() == 3);

    // Inline targets move by relocation: one move-construct plus one destroy.
    auto q = std::move(p);
    CHECK(stats.moves == 1);
    CHECK(stats.constructed == 2);
    CHECK(stats.destroyed == 1);
    CHECK(q.call<"gold">() == 3);

    // The moved-from proxy is empty; assigning into it is the sanctioned
    // reuse.
    CHECK(!p); // NOLINT(bugprone-use-after-move): probing moved-from state.
    p = std::move(q);
    CHECK(p.call<"gold">() == 3);
  }
  // Everything constructed was destroyed exactly once.
  CHECK(stats.constructed == 3);
  CHECK(stats.destroyed == stats.constructed);
}

TEST_CASE("Owning proxy, heap target", "[proxy]") {
  life_stats stats;
  if (true) {
    auto p = prox::make_proxy<lockbox, big_box>(stats);
    CHECK(stats.constructed == 1);
    CHECK(p.call<"add">(7) == 7);

    // Heap targets move by pointer steal: no target activity at all.
    auto q = std::move(p);
    CHECK(stats.moves == 0);
    CHECK(stats.constructed == 1);
    CHECK(stats.destroyed == 0);
    CHECK(q.call<"gold">() == 7);

    // Move assignment over a live target destroys the old target first.
    q = prox::make_proxy<lockbox, big_box>(stats);
    CHECK(stats.constructed == 2);
    CHECK(stats.destroyed == 1);
    CHECK(q.call<"gold">() == 0);
  }
  CHECK(stats.destroyed == stats.constructed);
}

TEST_CASE("Owning proxy is deep-const", "[proxy]") {
  // Only const-qualified facade methods dispatch through a const proxy; the
  // negative half is covered by the `CanFire` static_asserts above.
  const auto p = prox::make_proxy<gunslinger, lawman>();
  CHECK(p.call<"describe">() == "lawman"s);
}

TEST_CASE("Noexcept facade methods", "[proxy]") {
  lawman l;
  prox::proxy_view<hair_trigger> pv{l};
  CHECK(pv.call<"fire">(2) == 2);
  CHECK(!pv.call<"jams">());

  auto p = prox::make_proxy<hair_trigger, lawman>();
  CHECK(p.call<"fire">(4) == 4);
  static_assert(noexcept(p.call<"fire">(4)));
  const auto& cp = p;
  CHECK(!cp.call<"jams">());
  static_assert(noexcept(cp.call<"jams">()));
}

TEST_CASE("Heterogeneous ownership", "[proxy]") {
  std::vector<prox::proxy<gunslinger>> gang;
  gang.push_back(prox::make_proxy<gunslinger, lawman>());
  gang.push_back(prox::make_proxy<gunslinger, robber>());

  std::string roll_call;
  for (auto& p : gang) {
    p.call<"fire">(2);
    roll_call += p.call<"describe">();
    roll_call += ' ';
  }
  CHECK(roll_call == "lawman robber "s);
}

// NOLINTEND(readability-function-cognitive-complexity)
