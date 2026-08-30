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
#include <string>
#include <utility>

#include "corvid/meta/invoke/proxy_reflect.h"
#include "catch2_main.h"

// Gated like the header, so that a compiler without P2996 (clangd's clang,
// when it indexes this file) sees an empty translation unit rather than the
// reflection operators.
#if __cpp_impl_reflection >= 202506L

using namespace std::literals;
using namespace corvid;
using namespace corvid::meta::prox::literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Fixtures

// The fixtures mirror the `gunslinger` family of "proxy_test.cpp", with the
// facades stripped to their method lists: no `boilerplate`, no `api`. The
// reflected impl is what serves them.

struct gunslinger
    : prox::facade<prox::name<"gunslinger">,             //
          prox::method<"fire", int(int)>,                //
          prox::method<"describe", std::string() const>, //
          prox::method<"reload", void()>,                //
          prox::method<"shots", int&()>> {};

// A type whose method names line up with the facade. Conforms by
// registration alone: the reflected impl is the facade's default impl.
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
  // For `hair_trigger`, which needs `noexcept` and gets none here.
  [[nodiscard]] bool jams() const {
    (void)this;
    return false;
  }

  int rounds_fired{};
};

consteval auto corvid_proxy_spec(gunslinger*, lawman*) {
  return prox::make_proxy_spec<gunslinger, lawman>();
}

// A `deputy` is a `lawman` with its own `describe`. The reflected impl for a
// `deputy` finds `describe` on the derived class and the rest on the base,
// exactly as `t.fire(...)` would.
struct deputy: public lawman {
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "deputy";
  }
};

// `sheriff` lines up except that `fire` is spelled `shoot`. Its registration
// carries an impl that inherits the reflected impl, re-exposes its `on`, and
// overrides the one divergent binding.
//
// The impl sits at namespace scope rather than local to the hook: gcc 16
// cannot define a class deriving from `reflected_impl` inside a hook body
// (see "gcc 16 notes" in proxy.md).
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

struct sheriff_as_gunslinger: prox::reflected_impl<sheriff> {
  using prox::reflected_impl<sheriff>::on;
  static int on(method_key<"fire">, sheriff& s, int rounds) {
    return s.shoot(rounds);
  }
};

consteval auto corvid_proxy_spec(gunslinger*, sheriff*) {
  return prox::make_proxy_spec<gunslinger, sheriff, sheriff_as_gunslinger>();
}

// `wrangler` is the same divergence, registered with one `member` binding:
// every unlisted key routes to the reflected impl.
struct wrangler {
  int shoot(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "wrangler";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }

  int rounds_fired{};
};

consteval auto corvid_proxy_spec(gunslinger*, wrangler*) {
  return prox::make_proxy_spec<gunslinger, wrangler,
      prox::members<prox::member<"fire", &wrangler::shoot>>>();
}

// `turncoat` lines up, but nests its impl in the class, inheriting the
// reflected impl and replacing `describe`. Nested in the class, the impl
// enumerates as the class itself would, private members included (see
// `crack`).
struct turncoat {
  int fire(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "turncoat";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }

  int rounds_fired{};

  struct as_gunslinger: prox::reflected_impl<turncoat> {
    using prox::reflected_impl<turncoat>::on;
    static std::string on(method_key<"describe">, const turncoat&) {
      return "undercover";
    }
  };
};

consteval auto corvid_proxy_spec(gunslinger*, turncoat*) {
  return prox::make_proxy_spec<gunslinger, turncoat,
      turncoat::as_gunslinger>();
}

// `safecracker` keeps `fire` private and binds it anyway: its hook is a
// hidden friend, and it names `reflected_impl<safecracker>` there, so the
// enumeration runs with the friend's access.
struct safecracker {
  friend consteval auto corvid_proxy_spec(gunslinger*, safecracker*) {
    return prox::make_proxy_spec<gunslinger, safecracker,
        prox::reflected_impl<safecracker>>();
  }

  [[nodiscard]] std::string describe() const {
    (void)this;
    return "safecracker";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }

private:
  int fire(int rounds) { return rounds_fired += rounds; }

  int rounds_fired{};
};

// `recluse` keeps `fire` private too, but registers through a plain hook at
// namespace scope, where the reflected impl sees only public members. The
// pair is registered but not conformant.
struct recluse {
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "recluse";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }

private:
  int fire(int rounds) { return rounds_fired += rounds; }

  int rounds_fired{};
};

consteval auto corvid_proxy_spec(gunslinger*, recluse*) {
  return prox::make_proxy_spec<gunslinger, recluse>();
}

// `cowboy` lines up and never opts in, so it is not proxiable.
struct cowboy {
  int fire(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "cowboy";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }

  int rounds_fired{};
};

// `battery` carries an overload set, a const pair, and a `noexcept` method.
struct battery
    : prox::facade<prox::name<"battery">,      //
          prox::method<"fire", int(int)>,      //
          prox::method<"fire", int(int, int)>, //
          prox::method<"count", int&()>,       //
          prox::method<"count", int() const>,  //
          prox::method<"reload", bool() noexcept>> {};

struct cannon {
  int fire(int rounds) { return shots += rounds; }
  int fire(int rounds, int barrels) { return shots += rounds * barrels; }
  int& count() { return shots; }
  [[nodiscard]] int count() const { return shots; }
  bool reload() noexcept {
    shots = 0;
    return true;
  }

  int shots{};
};

consteval auto corvid_proxy_spec(battery*, cannon*) {
  return prox::make_proxy_spec<battery, cannon>();
}

// `hair_trigger` requires `noexcept` bindings. A reflected binding is
// `noexcept` exactly when the member is, so `lawman` (whose members are not)
// is registered but not conformant, and `marksman` conforms.
struct hair_trigger
    : prox::facade<prox::name<"hair_trigger">,     //
          prox::method<"fire", int(int) noexcept>, //
          prox::method<"jams", bool() const noexcept>> {};

consteval auto corvid_proxy_spec(hair_trigger*, lawman*) {
  return prox::make_proxy_spec<hair_trigger, lawman>();
}

struct marksman {
  int fire(int rounds) noexcept { return rounds_fired += rounds; }
  [[nodiscard]] bool jams() const noexcept {
    (void)this;
    return false;
  }

  int rounds_fired{};
};

consteval auto corvid_proxy_spec(hair_trigger*, marksman*) {
  return prox::make_proxy_spec<hair_trigger, marksman>();
}

// `till` over a non-class target: there are no members to reflect, so a
// plain registration binds nothing.
struct till: prox::facade<prox::name<"till">, //
                 prox::method<"amount", int() const>> {};

consteval auto corvid_proxy_spec(till*, int*) {
  return prox::make_proxy_spec<till, int>();
}

// Whether handle `H` dispatches `Key` with `Args`.
//
// A concept rather than an inline requires-expression, which gcc 16 treats
// as ill-formed outside a template instead of as false.
template<typename H, fixed_string Key, typename... Args>
concept CanCall = requires(H h, Args... args) {
  h.template call<Key>(args...);
};

#pragma endregion
#pragma region Conformance

// Conformance is pinned at file scope, so a regression names the pair.
static_assert(prox::Proxiable<lawman, gunslinger>);
static_assert(prox::Proxiable<deputy, gunslinger>);
static_assert(prox::Proxiable<sheriff, gunslinger>);
static_assert(prox::Proxiable<wrangler, gunslinger>);
static_assert(prox::Proxiable<turncoat, gunslinger>);
static_assert(prox::Proxiable<safecracker, gunslinger>);
static_assert(prox::Proxiable<cannon, battery>);
static_assert(prox::Proxiable<marksman, hair_trigger>);

static_assert(!prox::Proxiable<recluse, gunslinger>);
static_assert(!prox::Proxiable<cowboy, gunslinger>);
static_assert(!prox::Proxiable<lawman, hair_trigger>);
static_assert(!prox::Proxiable<int, till>);

#pragma endregion
#pragma region Tests

TEST_CASE("Reflected impl through registration", "[proxy_reflect]") {
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

  // A const view dispatches the const member; the mutable ones are not
  // reachable through it.
  const_proxy_view<gunslinger> cv{l};
  CHECK(cv.call<"describe">() == "lawman"s);
  static_assert(!CanCall<const_proxy_view<gunslinger>, "fire", int>);

  // Owning and shared handles go through the same impl.
  auto owned = make_proxy<gunslinger, lawman>();
  CHECK(owned.call<"fire">(4) == 4);
  auto shared = make_shared_proxy<gunslinger, lawman>();
  CHECK(shared.call<"fire">(5) == 5);
}

TEST_CASE("Reflected impl reaches base-class members", "[proxy_reflect]") {
  deputy d;
  proxy_view<gunslinger> pv{d};

  // The derived class's own `describe` hides the base's.
  CHECK(pv.call<"describe">() == "deputy"s);
  // The rest come from `lawman`.
  CHECK(pv.call<"fire">(3) == 3);
  CHECK(d.rounds_fired == 3);
  pv.call<"reload">();
  CHECK(d.rounds_fired == 0);
}

TEST_CASE("Partially-overridden reflected impl", "[proxy_reflect]") {
  sheriff s;
  proxy_view<gunslinger> pv{s};

  // The overriding binding: "fire" routes to `shoot`.
  CHECK(pv.call<"fire">(4) == 4);
  CHECK(s.rounds_fired == 4);

  // The inherited reflected bindings still serve the rest.
  CHECK(pv.call<"describe">() == "sheriff"s);
  CHECK(pv.call<"shots">() == 4);
  pv.call<"reload">();
  CHECK(s.rounds_fired == 0);
}

TEST_CASE("Member bindings fall through to the reflected impl",
    "[proxy_reflect]") {
  wrangler w;
  proxy_view<gunslinger> pv{w};

  CHECK(pv.call<"fire">(4) == 4);
  CHECK(w.rounds_fired == 4);
  CHECK(pv.call<"describe">() == "wrangler"s);
  CHECK(pv.call<"shots">() == 4);
  pv.call<"reload">();
  CHECK(w.rounds_fired == 0);
}

TEST_CASE("Nested impl over the reflected impl", "[proxy_reflect]") {
  turncoat t;
  proxy_view<gunslinger> tv{t};
  CHECK(tv.call<"fire">(2) == 2);
  CHECK(t.rounds_fired == 2);
  CHECK(tv.call<"describe">() == "undercover"s);
}

TEST_CASE("Private members bind with the hook's access", "[proxy_reflect]") {
  safecracker s;
  proxy_view<gunslinger> ps{s};
  CHECK(ps.call<"fire">(7) == 7);
  CHECK(ps.call<"shots">() == 7);
}

TEST_CASE("Overloads and const pairs resolve per call", "[proxy_reflect]") {
  cannon c;
  proxy_view<battery> pv{c};

  CHECK(pv.call<"fire">(3) == 3);
  CHECK(pv.call<"fire">(2, 4) == 11);

  // The mutable view reaches the `int&` half of the pair, the const view the
  // `int` half.
  pv.call<"count">() = 5;
  CHECK(c.shots == 5);
  const_proxy_view<battery> cv{c};
  CHECK(cv.call<"count">() == 5);
  static_assert(std::is_same_v<decltype(pv.call<"count">()), int&>);
  static_assert(std::is_same_v<decltype(cv.call<"count">()), int>);

  // `noexcept` propagates from the member through the erased call.
  static_assert(noexcept(pv.call<"reload">()));
  static_assert(!noexcept(pv.call<"fire">(1)));
  CHECK(pv.call<"reload">());
  CHECK(c.shots == 0);
}

TEST_CASE("Noexcept members satisfy a noexcept facade", "[proxy_reflect]") {
  marksman m;
  proxy_view<hair_trigger> pv{m};
  CHECK(pv.call<"fire">(2) == 2);
  CHECK(!pv.call<"jams">());
  static_assert(noexcept(pv.call<"fire">(2)));
  static_assert(noexcept(pv.call<"jams">()));
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)

#endif
