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
#include <type_traits>
#include <utility>
#include <vector>

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

// `repeater` is a class template, so a specialization has no identifier to
// reflect, and the `name` entry is required rather than optional.
template<typename Round>
struct repeater
    : prox::reflected_facade<repeater<Round>, gunslinger_api,
          prox::name<"repeater">> {};

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

// `bushwhacker` takes `this` explicitly throughout: a deducing-this template
// per object-parameter shape, and two explicit-object non-templates. Each
// binds as the plain member it stands for.
struct bushwhacker {
  int fire(this auto&& self, int rounds) {
    self.rounds_fired += rounds;
    return self.rounds_fired;
  }
  [[nodiscard]] std::string describe(this const bushwhacker& self) {
    (void)self;
    return "bushwhacker";
  }
  void reload(this auto& self) { self.rounds_fired = 0; }
  int& shots(this bushwhacker& self) { return self.rounds_fired; }

  int rounds_fired{};
};

consteval auto corvid_proxy_spec(gunslinger*, bushwhacker*) {
  return prox::make_proxy_spec<gunslinger, bushwhacker>();
}

// A `road_agent` is a `bushwhacker` with its own `describe`. The inherited
// templates deduce the derived type, and the inherited explicit-object
// `shots` binds through a pointer taking the base.
struct road_agent: public bushwhacker {
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  [[nodiscard]] std::string describe(this const road_agent& self) {
    (void)self;
    return "road agent";
  }
};

// A by-value object parameter (`this auto self`) binds as a const method
// that works on a copy, as the language has it.
struct tally_api {
  int bump() const;
};

struct tally: prox::reflected_facade<tally, tally_api> {};

struct abacus {
  int bump(this auto self) { return ++self.beads; }

  int beads{};
};

consteval auto corvid_proxy_spec(tally*, abacus*) {
  return prox::make_proxy_spec<tally, abacus>();
}

// `nickel` answers `bump()` through its conversion to `int`: a non-class
// object parameter takes its lvalue through the conversion, and the member
// binds as the const method it folds to. `slug` declines, as the language
// does, since no conversion result binds an lvalue reference.
struct nickel {
  operator int() const { return 5; }
  int bump(this int self) { return self + 1; }
};

consteval auto corvid_proxy_spec(tally*, nickel*) {
  return prox::make_proxy_spec<tally, nickel>();
}

struct slug {
  operator int() const { return 5; }
  int bump(this int& self) { return self; }
};

consteval auto corvid_proxy_spec(tally*, slug*) {
  return prox::make_proxy_spec<tally, slug>();
}

// `any_doubler` offers `double_it` only as a member function template.
//
// The candidate is the template itself, and the call splice deduces `T`
// from the argument, as `t.double_it(1)` would. Its body compiles only for
// arithmetic `T`, which guards against speculative instantiation; the
// declared return type is what lets a probe type the call without the
// body.
struct doubler_api {
  int double_it(int i);
};

struct doubler: prox::reflected_facade<doubler, doubler_api> {};

class any_doubler {
public:
  template<typename T>
  T double_it(T i) {
    return i + i;
  }
};

consteval auto corvid_proxy_spec(doubler*, any_doubler*) {
  return prox::make_proxy_spec<doubler, any_doubler>();
}

// `halver` asks for `double_it` with an arity the template cannot deduce.
//
// The pair is registered but not conformant, and since a failed deduction
// instantiates nothing, the body that would not compile for a class-type
// `T` stays uncompiled.
struct halver_api {
  int double_it(int i, int j);
};

struct halver: prox::reflected_facade<halver, halver_api> {};

consteval auto corvid_proxy_spec(halver*, any_doubler*) {
  return prox::make_proxy_spec<halver, any_doubler>();
}

// `smelter` offers `refine` as a template with a deduced return type.
//
// Typing the call takes the body, so probing instantiates it, the one case
// where a probe does. This body is valid for the facade's argument, so the
// pair conforms; one invalid for a probed argument list would be a hard
// error rather than a lost ranking, which is why that case cannot be
// pinned here.
struct refinery_api {
  int refine(int ore);
};

struct refinery: prox::reflected_facade<refinery, refinery_api> {};

struct smelter {
  auto refine(auto ore) { return ore * 2; }
};

consteval auto corvid_proxy_spec(refinery*, smelter*) {
  return prox::make_proxy_spec<refinery, smelter>();
}

// `blunderbuss` carries C-variadic overloads, implicit- and explicit-object,
// beside a plain `fire`. A variadic member is never a candidate, so the
// plain one binds as if it stood alone.
struct volley_api {
  int fire(int shots);
};

struct volley: prox::reflected_facade<volley, volley_api> {};

struct blunderbuss {
  int fire(int shots) { return shots * 2; }
  int fire(const char*, ...) { return -1; }
  int fire(this blunderbuss&, double, ...) { return -1; }
};

consteval auto corvid_proxy_spec(volley*, blunderbuss*) {
  return prox::make_proxy_spec<volley, blunderbuss>();
}

// `scattergun` offers `fire` only as a C-variadic member, so the key is
// unbound, where the language would call through the ellipsis.
struct scattergun {
  int fire(const char*, ...) { return -1; }
};

consteval auto corvid_proxy_spec(volley*, scattergun*) {
  return prox::make_proxy_spec<volley, scattergun>();
}

// `mitrailleuse` overloads `fire`: a plain member takes `long`, a template
// deduces the rest.
//
// Ranking is the language's. An `int` argument deduces the template
// exactly, beating the conversion to `long`, and a `long` argument takes
// the plain member over the template on the tie-break.
struct gatling_api {
  int fire(int n);
  int fire(long n);
};

struct gatling: prox::reflected_facade<gatling, gatling_api> {};

struct mitrailleuse {
  int fire(long n) { return static_cast<int>(n) + 100; }
  template<typename T>
  T fire(T n) {
    return n + n;
  }
};

consteval auto corvid_proxy_spec(gatling*, mitrailleuse*) {
  return prox::make_proxy_spec<gatling, mitrailleuse>();
}

// `assayer` reports the value category deduction saw.
//
// A forwarding reference deduces `T&` from an lvalue and `T` from an
// rvalue, so each slot pins what the splice call handed the template.
struct assay_api {
  bool from_lvalue(int& ore);
  bool from_rvalue(int ore);
};

struct assay: prox::reflected_facade<assay, assay_api> {};

struct assayer {
  template<typename T>
  bool from_lvalue(T&& ore) {
    (void)ore;
    return std::is_lvalue_reference_v<T>;
  }
  template<typename T>
  bool from_rvalue(T&& ore) {
    (void)ore;
    return std::is_lvalue_reference_v<T>;
  }
};

consteval auto corvid_proxy_spec(assay*, assayer*) {
  return prox::make_proxy_spec<assay, assayer>();
}

// `mule_train` deduces `U` inside a compound parameter type.
//
// `brand`'s constraint takes part in deduction, so only an integral mark
// binds.
struct caravan_api {
  int tote(std::vector<int> load);
  int brand(int mark);
};

struct caravan: prox::reflected_facade<caravan, caravan_api> {};

struct mule_train {
  template<typename U>
  int tote(std::vector<U> load) {
    return static_cast<int>(load.size());
  }
  template<typename U>
  requires std::is_integral_v<U>
  int brand(U mark) {
    return static_cast<int>(mark) + 1;
  }
};

consteval auto corvid_proxy_spec(caravan*, mule_train*) {
  return prox::make_proxy_spec<caravan, mule_train>();
}

// `no_brand` asks for a mark the constraint rejects, so the pair is
// registered but not conformant.
struct no_brand_api {
  int brand(long double mark);
};

struct no_brand: prox::reflected_facade<no_brand, no_brand_api> {};

consteval auto corvid_proxy_spec(no_brand*, mule_train*) {
  return prox::make_proxy_spec<no_brand, mule_train>();
}

// `squatter` mixes a concrete object parameter with a deduced argument,
// which deduce independently, as in a plain call.
struct claim_api {
  int stake(int spot);
};

struct claim: prox::reflected_facade<claim, claim_api> {};

struct squatter {
  template<typename U>
  int stake(this squatter& self, U spot) {
    return self.stakes += static_cast<int>(spot);
  }

  int stakes{};
};

consteval auto corvid_proxy_spec(claim*, squatter*) {
  return prox::make_proxy_spec<claim, squatter>();
}

// `armory` hides the base's `fire` behind a static member template, which
// hides but never binds.
struct armory: public lawman {
  template<typename T>
  static int fire(T rounds);
};

consteval auto corvid_proxy_spec(gunslinger*, armory*) {
  return prox::make_proxy_spec<gunslinger, armory>();
}

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

// `arsonist` is a `lawman` whose own `fire` is a data member.
//
// A member by that name hides the base's `fire`, function or not, and a data
// member is never a method, so the pair is registered but not conformant.
struct arsonist: public lawman {
  int fire{};
};

consteval auto corvid_proxy_spec(gunslinger*, arsonist*) {
  return prox::make_proxy_spec<gunslinger, arsonist>();
}

// `saboteur` reaches `fire` under both bases, a data member in one and the
// `lawman` functions in the other. The language rejects that merge at name
// lookup, so the key is unbound and the pair is not conformant.
struct powder_keg {
  int fire{};
};

struct saboteur: public powder_keg, public lawman {};

consteval auto corvid_proxy_spec(gunslinger*, saboteur*) {
  return prox::make_proxy_spec<gunslinger, saboteur>();
}

// `vigilante` reaches a `fire` function under each base. Only the `lawman`
// ones are viable for the facade's arguments, but the merge is ambiguous
// before viability is ever weighed, as the language rules it.
struct signalman {
  int fire(const char* pattern) { return pattern ? 1 : 0; }
};

struct vigilante: public signalman, public lawman {};

consteval auto corvid_proxy_spec(gunslinger*, vigilante*) {
  return prox::make_proxy_spec<gunslinger, vigilante>();
}

// `pyromaniac` injects `fire` as an enumerator of an unscoped member enum,
// which hides the `lawman` functions as a data member would, so the pair is
// not conformant.
struct pyromaniac: public lawman {
  enum { fire = 1 };
};

consteval auto corvid_proxy_spec(gunslinger*, pyromaniac*) {
  return prox::make_proxy_spec<gunslinger, pyromaniac>();
}

// `quartermaster` injects `fire` as an anonymous-union member, which hides
// the same way.
struct quartermaster: public lawman {
  union {
    int fire{};
    long powder;
  };
};

consteval auto corvid_proxy_spec(gunslinger*, quartermaster*) {
  return prox::make_proxy_spec<gunslinger, quartermaster>();
}

// `drill_sergeant` pins the negative controls: a scoped enumerator injects
// nothing, and a named union object injects only its own name, so `fire`
// still reaches `lawman`'s and the pair conforms.
struct drill_sergeant: public lawman {
  enum class drill { fire };
  union {
    int fire;
  } load{};
};

consteval auto corvid_proxy_spec(gunslinger*, drill_sergeant*) {
  return prox::make_proxy_spec<gunslinger, drill_sergeant>();
}

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

// A deducing-this member declares the method its object parameter admits:
// const when it takes a const interface object, mutable when it takes only a
// mutable one, and nothing when it takes no lvalue.
//
// `peek` is mutable in form but const by constraint, and declares the const
// method a call would deduce. `murmur`, a forwarding reference gated to
// const, and `gossip`, a template deducing from its argument, pin the
// documented limits by declaring nothing.
struct crier_api {
  void speak(this const auto& self);
  void shout(this auto&& self);
  int volume(this auto self);
  void whisper(this auto& self)
  requires(!std::is_const_v<std::remove_reference_t<decltype(self)>>);
  void peek(this auto& self)
  requires(std::is_const_v<std::remove_reference_t<decltype(self)>>);
  void murmur(this auto&& self)
  requires(std::is_const_v<std::remove_reference_t<decltype(self)>>);
  template<typename U>
  void gossip(this const crier_api& self, U rumor);
  void growl(this const crier_api& self) noexcept;
  void bark(this crier_api&& self);
};

struct crier: prox::reflected_facade<crier, crier_api> {};

// `mirage` pins two misdeclares the derivation cannot detect, as a tripwire
// we want tripped: if these members stop declaring, the derivation got
// smarter, and the asserts should flip to pin the new declines.
//
// No call deduction would select either member. `shimmer` wraps its object
// parameter in a non-deduced context, and `haze` never uses its template
// parameter. Each declares the method its substituted shape suggests.
struct mirage_api {
  template<typename T>
  void shimmer(this std::type_identity_t<T> self);
  template<typename U>
  void haze(this const mirage_api& self);
};

struct mirage: prox::reflected_facade<mirage, mirage_api> {};

// `mime` derives from an interface whose members all decline (an
// rvalue-only member and a static one), pinning the zero-method pack
// through the whole derivation.
struct mime_api {
  void gesture(this mime_api&& self);
  static void breathe();
};

struct mime: prox::reflected_facade<mime, mime_api> {};

// `census` defines its own `api`, which the reflected one yields to. The
// registration validates it over the reflected boilerplate as it would over
// a hand-written one, and this `api` deliberately deviates (its forwarder
// adds to the result), so the registration opts out.
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

// `roster` is a hand-written `api` over a reflected boilerplate. Its
// forwarders are deducing-this members, which bind on the validation probe
// as on any target, so the registration validates it as it would over a
// hand-written boilerplate.
struct roster
    : prox::facade<prox::name<"roster">, //
          prox::method<"describe", std::string() const>> {
  struct api {
    std::string describe(this const auto& self) {
      return self.template call<"describe">();
    }
  };
};

consteval auto corvid_proxy_spec(roster*, lawman*) {
  return prox::make_proxy_spec<roster, lawman>();
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

// A class template specialization has no identifier; its `name` entry serves,
// and the slots are the interface's as for any other facade.
using repeater_build = prox::details::vtbuild_t<repeater<int>>;
static_assert(repeater_build::name_v.view() == "repeater");
static_assert(std::is_same_v<repeater_build::flat_slots_t,
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

// A deducing-this interface: each member declares the method its object
// parameter admits, and the one taking no lvalue declares nothing. The count
// also pins that `murmur` and `gossip` declare nothing.
using crier_build = prox::details::vtbuild_t<crier>;
static_assert(crier_build::name_v.view() == "crier");
static_assert(crier_build::count_v == 6);
static_assert(std::is_same_v<crier_build::slot_t<0>::method_t,
    prox::method<"speak", void() const>>);
static_assert(std::is_same_v<crier_build::slot_t<1>::method_t,
    prox::method<"shout", void() const>>);
static_assert(std::is_same_v<crier_build::slot_t<2>::method_t,
    prox::method<"volume", int() const>>);
static_assert(std::is_same_v<crier_build::slot_t<3>::method_t,
    prox::method<"whisper", void()>>);
static_assert(std::is_same_v<crier_build::slot_t<4>::method_t,
    prox::method<"peek", void() const>>);
static_assert(std::is_same_v<crier_build::slot_t<5>::method_t,
    prox::method<"growl", void() const noexcept>>);

// The `mirage` tripwire: these pin behavior we WANT to lose. If they fail
// because `shimmer` and `haze` now declare nothing, that is the derivation
// improving; flip the asserts to pin the declines.
using mirage_build = prox::details::vtbuild_t<mirage>;
static_assert(mirage_build::count_v == 2);
static_assert(std::is_same_v<mirage_build::slot_t<0>::method_t,
    prox::method<"shimmer", void() const>>);
static_assert(std::is_same_v<mirage_build::slot_t<1>::method_t,
    prox::method<"haze", void() const>>);

// The zero-method facade: every member declines, and the empty pack still
// derives a facade with its name.
using mime_build = prox::details::vtbuild_t<mime>;
static_assert(mime_build::name_v.view() == "mime");
static_assert(mime_build::count_v == 0);

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
static_assert(prox::Proxiable<bushwhacker, gunslinger>);
static_assert(prox::Proxiable<road_agent, gunslinger>);
static_assert(prox::Proxiable<abacus, tally>);
static_assert(prox::Proxiable<nickel, tally>);
static_assert(!prox::Proxiable<slug, tally>);
static_assert(prox::Proxiable<lawman, roster>);
static_assert(prox::Proxiable<any_doubler, doubler>);
static_assert(prox::Proxiable<smelter, refinery>);
static_assert(prox::Proxiable<blunderbuss, volley>);
static_assert(!prox::Proxiable<scattergun, volley>);
static_assert(prox::Proxiable<mitrailleuse, gatling>);
static_assert(prox::Proxiable<assayer, assay>);
static_assert(prox::Proxiable<mule_train, caravan>);
static_assert(prox::Proxiable<squatter, claim>);

static_assert(!prox::Proxiable<recluse, gunslinger>);
static_assert(!prox::Proxiable<cowboy, gunslinger>);
static_assert(!prox::Proxiable<arsonist, gunslinger>);
static_assert(!prox::Proxiable<saboteur, gunslinger>);
static_assert(!prox::Proxiable<vigilante, gunslinger>);
static_assert(!prox::Proxiable<pyromaniac, gunslinger>);
static_assert(!prox::Proxiable<quartermaster, gunslinger>);
static_assert(prox::Proxiable<drill_sergeant, gunslinger>);
static_assert(!prox::Proxiable<lawman, hair_trigger>);
static_assert(!prox::Proxiable<int, till>);
static_assert(!prox::Proxiable<any_doubler, halver>);
static_assert(!prox::Proxiable<mule_train, no_brand>);
static_assert(!prox::Proxiable<armory, gunslinger>);

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

  // A facade's own `api` serves as before, validated or opted out.
  proxy_view<roster> pr{l};
  CHECK(pr.describe() == "lawman"s);
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

TEST_CASE("Deducing-this members bind as the members they stand for",
    "[proxy_reflect]") {
  bushwhacker b;
  proxy_view<gunslinger> pv{b};
  CHECK(pv.call<"fire">(3) == 3);
  CHECK(b.rounds_fired == 3);
  CHECK(pv.call<"describe">() == "bushwhacker"s);
  CHECK(pv.call<"shots">() == 3);
  pv.call<"reload">();
  CHECK(b.rounds_fired == 0);

  // The sugar spelling and the const view take the same route.
  CHECK(pv.fire(2) == 2);
  const_proxy_view<gunslinger> cv{b};
  CHECK(cv.call<"describe">() == "bushwhacker"s);
  CHECK(cv.describe() == "bushwhacker"s);
}

TEST_CASE("Inherited deducing-this members deduce the derived type",
    "[proxy_reflect]") {
  road_agent r;
  proxy_view<gunslinger> pv{r};
  CHECK(pv.call<"describe">() == "road agent"s);
  CHECK(pv.call<"fire">(4) == 4);
  CHECK(pv.call<"shots">() == 4);
  pv.call<"reload">();
  CHECK(r.rounds_fired == 0);
}

TEST_CASE("A by-value object parameter works on a copy", "[proxy_reflect]") {
  abacus a;
  const_proxy_view<tally> cv{a};
  CHECK(cv.call<"bump">() == 1);
  CHECK(cv.bump() == 1);
  CHECK(a.beads == 0);

  // A non-class object parameter dispatches through the conversion.
  nickel n;
  proxy_view<tally> pn{n};
  CHECK(pn.call<"bump">() == 6);
}

TEST_CASE("Member function templates bind through the call splice",
    "[proxy_reflect]") {
  any_doubler d;
  proxy_view<doubler> pv{d};
  CHECK(pv.call<"double_it">(21) == 42);
  CHECK(pv.double_it(21) == 42);
}

TEST_CASE("Templates and functions rank together", "[proxy_reflect]") {
  mitrailleuse m;
  proxy_view<gatling> pg{m};

  // An `int` deduces the template exactly, beating the conversion to
  // `long`.
  CHECK(pg.call<"fire">(3) == 6);
  // A `long` is exact both ways; the non-template wins the tie.
  CHECK(pg.call<"fire">(3L) == 103);
}

TEST_CASE("Template argument deduction is the language's", "[proxy_reflect]") {
  assayer a;
  proxy_view<assay> pa{a};
  auto ore = 7;
  CHECK(pa.call<"from_lvalue">(ore));
  CHECK_FALSE(pa.call<"from_rvalue">(7));

  mule_train t;
  proxy_view<caravan> pc{t};
  CHECK(pc.call<"tote">(std::vector<int>{1, 2, 3}) == 3);
  CHECK(pc.call<"brand">(41) == 42);

  squatter s;
  proxy_view<claim> ps{s};
  CHECK(ps.call<"stake">(4) == 4);
  CHECK(ps.call<"stake">(3) == 7);

  // A deduced return type dispatches like any other template; its body was
  // compiled at the probe.
  smelter sm;
  proxy_view<refinery> pr{sm};
  CHECK(pr.call<"refine">(21) == 42);
}

TEST_CASE("A C-variadic overload never binds", "[proxy_reflect]") {
  blunderbuss b;
  proxy_view<volley> pv{b};
  CHECK(pv.call<"fire">(3) == 6);
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
