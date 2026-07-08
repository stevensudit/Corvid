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
#include <string>
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

// Boilerplate impl for `gunslinger`: written once by the facade author,
// generic over any registered type whose member names line up.
template<typename T>
requires prox::ProxyRegistered<gunslinger, T>
struct prox::proxy_impl<gunslinger, T> {
  static int on(method_key<"fire">, T& t, int rounds) {
    return t.fire(rounds);
  }
  static std::string on(method_key<"describe">, const T& t) {
    return t.describe();
  }
  static void on(method_key<"reload">, T& t) { t.reload(); }
  static int& on(method_key<"shots">, T& t) { return t.shots(); }
};

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
static_assert(!prox::ProxyRegistered<gunslinger, robber>);
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
static_assert(!prox::Proxiable<cowboy, gunslinger>);
static_assert(!prox::Proxiable<int, gunslinger>);

// A view satisfies its own facade.
static_assert(prox::Proxiable<prox::proxy_view<gunslinger>, gunslinger>);

// Views are value-semantic: copyable and rebindable by assignment. The table
// member is pointer-to-const, not a const member, so assignment stays viable.
static_assert(std::assignable_from<prox::proxy_view<gunslinger>&,
    const prox::proxy_view<gunslinger>&>);

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

TEST_CASE("Heterogeneous dispatch", "[proxy]") {
  lawman l;
  robber r;
  std::vector<prox::proxy_view<gunslinger>> gang{l, r};

  std::string roll_call;
  for (const auto& pv : gang) {
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
}

// NOLINTEND(readability-function-cognitive-complexity)
