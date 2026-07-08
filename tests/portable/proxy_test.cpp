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
using namespace corvid::meta::prox;
using namespace corvid::meta::prox::literals;

// The facade under test: mixes value returns, a const method, a void
// mutator, and a reference return.
struct gunslinger
    : facade<method<"fire", int(int)>, method<"describe", std::string() const>,
          method<"reload", void()>, method<"shots", int&()>> {};

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

// Registration for `lawman`: pure opt-in, one hook, no bindings.
consteval auto corvid_proxy_spec(gunslinger*, lawman*) {
  return make_proxy_spec<gunslinger, lawman>();
}

namespace corvid {

// Boilerplate impl for `gunslinger`: written once by the facade author,
// generic over any registered type whose member names line up.
template<typename T>
requires ProxyRegistered<gunslinger, T>
struct proxy_impl<gunslinger, T> {
  static int on(method_key<"fire">, T& t, int rounds) {
    return t.fire(rounds);
  }
  static std::string on(method_key<"describe">, const T& t) {
    return t.describe();
  }
  static void on(method_key<"reload">, T& t) { t.reload(); }
  static int& on(method_key<"shots">, T& t) { return t.shots(); }
};

// Custom impl for `robber`: a full specialization outranks the boilerplate
// and needs no registration. Note the name adaptation, including binding
// "shots" to a data member.
template<>
struct proxy_impl<gunslinger, robber> {
  static int on(method_key<"fire">, robber& r, int rounds) {
    return r.shoot(rounds);
  }
  static std::string on(method_key<"describe">, const robber& r) {
    return r.description();
  }
  static void on(method_key<"reload">, robber& r) { r.rearm(); }
  static int& on(method_key<"shots">, robber& r) { return r.fired; }
};

} // namespace corvid

// Facade detection.
static_assert(Facade<gunslinger>);
static_assert(!Facade<lawman>);
static_assert(!Facade<int>);

// A method is-a key: usable anywhere its key is.
static_assert(std::derived_from<method<"fire", int(int)>, method_key<"fire">>);
static_assert(std::derived_from<method<"describe", std::string() const>,
    method_key<"describe">>);
static_assert(
    !std::derived_from<method<"fire", int(int)>, method_key<"reload">>);

// UDL: `"name"_method` is a `method_key<"name">`.
static_assert(std::same_as<decltype("fire"_method), method_key<"fire">>);

// Registration state: lawman is registered, robber conforms without
// registration, cowboy neither registers nor conforms.
static_assert(ProxyRegistered<gunslinger, lawman>);
static_assert(!ProxyRegistered<gunslinger, robber>);
static_assert(!ProxyRegistered<gunslinger, cowboy>);
static_assert(std::same_as<decltype(proxy_spec_v<gunslinger, lawman>),
    const proxy_spec<gunslinger, lawman>>);

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
// to false". Calling `pv.call<"missing">()` emits exactly one error:
// "static assertion failed ... facade has no method with this name" (the
// `if constexpr` in `call` discards the dispatch on that path, so the
// assert is not followed by `std::get` index noise).
static_assert(Proxiable<lawman, gunslinger>);
static_assert(Proxiable<robber, gunslinger>);
static_assert(!Proxiable<cowboy, gunslinger>);
static_assert(!Proxiable<int, gunslinger>);

// A view satisfies its own facade.
static_assert(Proxiable<proxy_view<gunslinger>, gunslinger>);

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("Boilerplate impl through registration", "[proxy]") {
  lawman l;
  proxy_view<gunslinger> pv{l};

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
  proxy_view<gunslinger> pv{r};

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
  std::vector<proxy_view<gunslinger>> gang{l, r};

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

TEST_CASE("Generic code accepts concrete and erased alike", "[proxy]") {
  // Facade-constrained generic code: erase, then call. `Proxiable` is the
  // trait bound for the static-dispatch half.
  auto fire_twice = [](Proxiable<gunslinger> auto& g) {
    auto pv = make_proxy_view<gunslinger>(g);
    // In a template context the view's type is dependent, hence `template`.
    pv.template call<"fire">(1);
    return pv.template call<"fire">(1);
  };

  lawman l;
  CHECK(fire_twice(l) == 2);

  // Passing a view works too, and flattens rather than stacking a second
  // indirection.
  proxy_view<gunslinger> pv{l};
  CHECK(fire_twice(pv) == 4);

  // The library-provided view impl is also usable directly.
  CHECK(prox::proxy_impl<gunslinger, proxy_view<gunslinger>>::on(
            "describe"_method, pv) == "lawman"s);
}

// NOLINTEND(readability-function-cognitive-complexity)
