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

// Gated like the header, so that a compiler without P2996 (clang, cl, and
// clangd when it indexes this file) sees none of the reflection operators.
// The one case outside the gate, at the end, reports which mode the run is
// in.
#if __cpp_impl_reflection >= 202506L

using namespace std::literals;
using namespace corvid;
using namespace corvid::meta::prox::literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Fixtures

// The fixtures mirror the `gunslinger` family of "proxy_test.cpp", with the
// facades stripped to their method lists: no `boilerplate`, no `api`. The
// reflected impl is what serves them. `gunslinger` itself stays spelled the
// traditional way, as the reference the interface-first spelling is held to.

struct gunslinger
    : prox::facade<prox::name<"gunslinger">,             //
          prox::method<"fire", int(int)>,                //
          prox::method<"describe", std::string() const>, //
          prox::method<"reload", void()>,                //
          prox::method<"shots", int&()>> {};

// The same facade, interface-first: the interface is written as declarations,
// and the facade derives from it. The `name` entry overrides the identifier so
// that the two facades agree to the letter (see "Agreement" below).
struct gunslinger_api {
  int fire(int);
  std::string describe() const;
  void reload();
  int& shots();
};

struct gunslinger2
    : prox::reflected_facade<gunslinger2, gunslinger_api,
          prox::name<"gunslinger">> {};

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
consteval auto corvid_proxy_spec(gunslinger2*, lawman*) {
  return prox::make_proxy_spec<gunslinger2, lawman>();
}

// The interface need not be written for the purpose: any class serves, its
// public member functions being the spec. `lawman_facade` is a facade over
// `lawman`'s whole public interface, `jams` included.
struct lawman_facade: prox::reflected_facade<lawman_facade, lawman> {};

consteval auto corvid_proxy_spec(lawman_facade*, lawman*) {
  return prox::make_proxy_spec<lawman_facade, lawman>();
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

// The same override serves the interface-first spelling of the facade: the
// impl binds by name, so it is agnostic to how the facade was written.
consteval auto corvid_proxy_spec(gunslinger2*, sheriff*) {
  return prox::make_proxy_spec<gunslinger2, sheriff, sheriff_as_gunslinger>();
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
consteval auto corvid_proxy_spec(gunslinger2*, wrangler*) {
  return prox::make_proxy_spec<gunslinger2, wrangler,
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

// `battery` carries an overload set, a const pair, and a `noexcept` method,
// all spelled as declarations, and takes its name from its identifier.
struct battery_api {
  int fire(int);
  int fire(int, int);
  int& count();
  int count() const;
  bool reload() noexcept;
};

struct battery: prox::reflected_facade<battery, battery_api> {};

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
struct hair_trigger_api {
  int fire(int) noexcept;
  bool jams() const noexcept;
};

struct hair_trigger: prox::reflected_facade<hair_trigger, hair_trigger_api> {};

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

// `marshal` composes: an interface-first facade extending the hand-written
// `gunslinger`, with `constable` registered for the whole chain by one hook.
struct marshal_api {
  bool arrest(int);
};

struct marshal
    : prox::reflected_facade<marshal, marshal_api, prox::extends<gunslinger>> {
};

struct constable: public lawman {
  bool arrest(int outlaws) {
    arrested += outlaws;
    return true;
  }

  int arrested{};
};

template<prox::InChainOf<marshal> F>
consteval auto corvid_proxy_spec(F*, constable*) {
  return prox::make_proxy_spec<F, constable>();
}

// `ranger` is the second level of the chain, interface-first over
// interface-first: it extends `marshal`, itself derived from `marshal_api`, so
// a `ranger` handle reaches methods declared in three interfaces, two of them
// written as declarations. `texas_ranger` conforms to the whole chain by one
// hook.
struct ranger_api {
  int track() const;
};

struct ranger
    : prox::reflected_facade<ranger, ranger_api, prox::extends<marshal>> {};

struct texas_ranger {
  int fire(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "texas_ranger";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }
  bool arrest(int outlaws) {
    arrested += outlaws;
    return true;
  }
  [[nodiscard]] int track() const { return arrested; }

  int rounds_fired{};
  int arrested{};
};

template<prox::InChainOf<ranger> F>
consteval auto corvid_proxy_spec(F*, texas_ranger*) {
  return prox::make_proxy_spec<F, texas_ranger>();
}

// A deducing-this forwarder is a function template, which reflection does
// not see as a function, so an interface written that way declares nothing.
struct forwarder_api {
  void speak(this const auto& self);
};

struct mute: prox::reflected_facade<mute, forwarder_api> {};

// `census` defines its own `api`, which the reflected one yields to. A
// hand-written `api` over a reflected boilerplate cannot be validated (the
// reflected impl cannot drive the probe's forwarder templates), so the
// registration opts out, as its diagnostic asks.
struct census
    : prox::facade<prox::name<"census">, //
          prox::method<"describe", std::string() const>> {
  struct api {
    std::string describe(this const auto& self) {
      return "counted " + self.template call<"describe">();
    }
  };
};

consteval auto corvid_proxy_spec(census*, lawman*) {
  return prox::make_proxy_spec<census, lawman, prox::api_check::off>();
}

// `till` over a non-class target: there are no members to reflect, so a
// plain registration binds nothing.
struct till: prox::facade<prox::name<"till">, //
                 prox::method<"amount", int() const>> {};

consteval auto corvid_proxy_spec(till*, int*) {
  return prox::make_proxy_spec<till, int>();
}

// Whether handle `H` dispatches `Key` with `Args`, in the core spelling.
//
// A concept rather than an inline requires-expression, which gcc 16 treats
// as ill-formed outside a template instead of as false.
template<typename H, fixed_string Key, typename... Args>
concept CanCall = requires(H h, Args... args) {
  h.template call<Key>(args...);
};

// Whether a const handle `H` fires, in the sugar spelling.
template<typename H>
concept ConstCanFire = requires(const H& h) { h.fire(1); };

#pragma endregion
#pragma region Agreement

// The interface-first spelling yields the hand-written facade to the letter:
// the same name and the same flattened slot list, so the two are
// interchangeable everywhere the library reads a facade.
using gunslinger_build = prox::details::vtbuild_t<gunslinger>;
using gunslinger2_build = prox::details::vtbuild_t<gunslinger2>;
static_assert(
    gunslinger2_build::name_v.view() == gunslinger_build::name_v.view());
static_assert(std::is_same_v<gunslinger2_build::flat_slots_t,
    gunslinger_build::flat_slots_t>);

// The identifier serves as the name, and the declaration grammar carries
// constness and `noexcept` into the slots.
using battery_build = prox::details::vtbuild_t<battery>;
static_assert(battery_build::name_v.view() == "battery");
static_assert(battery_build::count_v == 5);
static_assert(std::is_same_v<battery_build::slot_t<3>::method_t,
    prox::method<"count", int() const>>);
static_assert(std::is_same_v<battery_build::slot_t<4>::method_t,
    prox::method<"reload", bool() noexcept>>);

// A concrete class as the interface: every public member function is a
// method, and nothing else is.
using lawman_facade_build = prox::details::vtbuild_t<lawman_facade>;
static_assert(lawman_facade_build::name_v.view() == "lawman_facade");
static_assert(lawman_facade_build::count_v == 5);
static_assert(std::is_same_v<lawman_facade_build::slot_t<4>::method_t,
    prox::method<"jams", bool() const>>);

// Composition flattens the base's slots behind the own ones, as for any
// facade.
using marshal_build = prox::details::vtbuild_t<marshal>;
static_assert(marshal_build::name_v.view() == "marshal");
static_assert(marshal_build::count_v == 5);
using ranger_build = prox::details::vtbuild_t<ranger>;
static_assert(ranger_build::name_v.view() == "ranger");
static_assert(ranger_build::count_v == 6);

// The documented limit: a deducing-this forwarder declares no method.
static_assert(prox::details::vtbuild_t<mute>::count_v == 0);

#pragma endregion
#pragma region Sugar layout

// The reflected sugar API is an empty base of empty members, so a handle keeps
// its size, and every dispatching flavor carries it.
using gunslinger_view_api =
    prox::details::api_base_t<gunslinger, proxy_view<gunslinger>>;
static_assert(std::is_empty_v<gunslinger_view_api>);
static_assert(std::is_base_of_v<gunslinger_view_api, proxy_view<gunslinger>>);
static_assert(sizeof(proxy_view<gunslinger>) == 2 * sizeof(void*));
static_assert(
    sizeof(proxy_view<gunslinger2>) == sizeof(proxy_view<gunslinger>));
static_assert(std::is_base_of_v<
    prox::details::api_base_t<battery, const_proxy_view<battery>>,
    const_proxy_view<battery>>);
static_assert(std::is_base_of_v<
    prox::details::api_base_t<battery, shared_proxy<battery>>,
    shared_proxy<battery>>);
static_assert(std::is_base_of_v<
    prox::details::api_base_t<battery, proxy<battery>>, proxy<battery>>);

// A facade's own `api` outranks the reflected one.
static_assert(std::is_base_of_v<census::api, proxy_view<census>>);

// Deep const: a const handle has no mutable `fire` at all.
static_assert(!ConstCanFire<proxy_view<gunslinger>>);
static_assert(!ConstCanFire<const_proxy_view<gunslinger>>);
static_assert(!ConstCanFire<proxy<gunslinger>>);

#pragma endregion
#pragma region Conformance

// Conformance is pinned at file scope, so a regression names the pair.
static_assert(prox::Proxiable<lawman, gunslinger>);
static_assert(prox::Proxiable<lawman, gunslinger2>);
static_assert(prox::Proxiable<lawman, lawman_facade>);
static_assert(prox::Proxiable<deputy, lawman_facade>);
static_assert(prox::Proxiable<constable, marshal>);
static_assert(prox::Proxiable<constable, gunslinger>);
static_assert(prox::Proxiable<texas_ranger, ranger>);
static_assert(prox::Proxiable<texas_ranger, marshal>);
static_assert(prox::Proxiable<texas_ranger, gunslinger>);
static_assert(prox::Proxiable<sheriff, gunslinger2>);
static_assert(prox::Proxiable<wrangler, gunslinger2>);
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

TEST_CASE("Interface-first facade dispatches as the hand-written one",
    "[proxy_reflect]") {
  lawman l;
  proxy_view<gunslinger2> pv{l};
  CHECK(pv.fire(3) == 3);
  CHECK(pv.describe() == "lawman"s);
  pv.shots() = 9;
  CHECK(l.rounds_fired == 9);
  pv.reload();
  CHECK(l.rounds_fired == 0);
}

TEST_CASE("Interface-first facade composes", "[proxy_reflect]") {
  constable c;
  proxy_view<marshal> pv{c};
  CHECK(pv.arrest(2));
  CHECK(c.arrested == 2);
  CHECK(pv.fire(3) == 3);
  CHECK(pv.describe() == "lawman"s);

  // An upcast view dispatches the inherited methods through the base.
  proxy_view<gunslinger> base{pv};
  CHECK(base.fire(1) == 4);
}

TEST_CASE("A concrete class serves as the interface", "[proxy_reflect]") {
  lawman l;
  proxy_view<lawman_facade> pv{l};
  CHECK(pv.fire(2) == 2);
  CHECK(pv.describe() == "lawman"s);
  CHECK(!pv.jams());
  const_proxy_view<lawman_facade> cv{l};
  CHECK(!cv.jams());

  // A type with the same public methods conforms too.
  deputy d;
  proxy_view<lawman_facade> pd{d};
  CHECK(pd.describe() == "deputy"s);
}

TEST_CASE("Interface-first facades compose over interface-first facades",
    "[proxy_reflect]") {
  texas_ranger t;
  proxy_view<ranger> pv{t};

  // Methods from all three interfaces, through one handle.
  CHECK(pv.track() == 0);
  CHECK(pv.arrest(2));
  CHECK(pv.track() == 2);
  CHECK(pv.fire(3) == 3);
  CHECK(pv.describe() == "texas_ranger"s);
  pv.shots() = 1;
  CHECK(t.rounds_fired == 1);
  pv.reload();
  CHECK(t.rounds_fired == 0);

  // Each level upcasts to the one below, and dispatches its own methods.
  proxy_view<marshal> pm{pv};
  CHECK(pm.arrest(1));
  CHECK(pm.fire(2) == 2);
  proxy_view<gunslinger> pg{pm};
  CHECK(pg.fire(1) == 3);

  // A const view reaches the const method of the top level and the const
  // one of the bottom, and nothing mutable in between.
  const_proxy_view<ranger> cv{t};
  CHECK(cv.track() == 3);
  CHECK(cv.describe() == "texas_ranger"s);
  static_assert(!ConstCanFire<proxy_view<ranger>>);

  // Owning and shared handles over the chain.
  auto owned = make_proxy<ranger, texas_ranger>();
  CHECK(owned.arrest(4));
  CHECK(owned.track() == 4);
  auto shared = make_shared_proxy<ranger, texas_ranger>();
  CHECK(shared.fire(5) == 5);
  CHECK(shared.track() == 0);
}

TEST_CASE("Per-method overrides over an interface-first facade",
    "[proxy_reflect]") {
  // A binding class inheriting the reflected impl, with one override.
  sheriff s;
  proxy_view<gunslinger2> ps{s};
  CHECK(ps.fire(4) == 4);
  CHECK(s.rounds_fired == 4);
  CHECK(ps.describe() == "sheriff"s);

  // One `member` binding, the rest falling through to the reflected impl.
  wrangler w;
  proxy_view<gunslinger2> pw{w};
  CHECK(pw.fire(2) == 2);
  CHECK(pw.describe() == "wrangler"s);
  CHECK(pw.shots() == 2);
}

TEST_CASE("Reflected sugar on every handle flavor", "[proxy_reflect]") {
  lawman l;

  // The sugar and the core spelling dispatch through the same table.
  proxy_view<gunslinger> pv{l};
  CHECK(pv.fire(3) == 3);
  CHECK(pv.call<"fire">(2) == 5);
  pv.shots() = 1;
  CHECK(l.rounds_fired == 1);
  pv.reload();
  CHECK(l.rounds_fired == 0);

  const_proxy_view<gunslinger> cv{l};
  CHECK(cv.describe() == "lawman"s);

  // The owner is recovered at whatever depth the handle inherits the sugar
  // API.
  auto owned = make_proxy<gunslinger, lawman>();
  CHECK(owned.fire(4) == 4);
  const auto& cowned = owned;
  CHECK(cowned.describe() == "lawman"s);
  auto shared = make_shared_proxy<gunslinger, lawman>();
  CHECK(shared.fire(5) == 5);
  const_shared_proxy<gunslinger> cshared{shared};
  CHECK(cshared.describe() == "lawman"s);

  // Overloads, the const pair, `int&`, and `noexcept` resolve inside
  // `call<>`, so the sugar agrees with it.
  cannon c;
  proxy_view<battery> pb{c};
  CHECK(pb.fire(3) == 3);
  CHECK(pb.fire(2, 4) == 11);
  pb.count() = 5;
  CHECK(c.shots == 5);
  const_proxy_view<battery> cb{c};
  CHECK(cb.count() == 5);
  static_assert(std::is_same_v<decltype(pb.count()), int&>);
  static_assert(std::is_same_v<decltype(cb.count()), int>);
  static_assert(noexcept(pb.reload()));
  static_assert(!noexcept(pb.fire(1)));
  CHECK(pb.reload());

  // A facade's own `api` serves as before.
  proxy_view<census> pc{l};
  CHECK(pc.describe() == "counted lawman"s);
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

TEST_CASE("Reflection layer gate", "[proxy_reflect]") {
#if __cpp_impl_reflection >= 202506L
  SUCCEED("C++26 reflection is enabled; the reflected cases ran");
#else
  SUCCEED(
      "C++26 reflection is unavailable here; the reflected cases are "
      "compiled out");
#endif
}
