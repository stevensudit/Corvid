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
#include <functional>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "corvid/meta/crossplatform.h"
#include "corvid/meta/proxy.h"
#include "corvid/meta/proxy_codegen.h"
#include "catch2_main.h"

// cl C4702: instantiation-specific unreachable-code noise in facade dispatch.
PRAGMA_MSVC_IGNORED(4702)

using namespace std::literals;
using namespace corvid;
using namespace corvid::meta::prox::literals;

// The fixtures below form one western-themed world, reused across the feature
// tiers.
//
// The "Test fixture map" section of corvid/meta/proxy.md diagrams the whole
// hierarchy: every facade with its `extends` edges, plus the facade each
// conforming type registers under and the registration route it takes.

// The facade under test: mixes value returns, a const method, a void
// mutator, and a reference return.
//
// The nested `api` is the (technically optional) member-call sugar, mapping
// regular C++ methods over the facade's methods. It has one deducing-this
// forwarder per method, and a const method's forwarder takes `self` by const
// reference, mirroring the erased signature. The API is inherited by every
// handle of this facade: the `proxy`, `proxy_view`, and `const_proxy_view`.
//
// The nested `boilerplate` is the facade author's impl, mapping the regular
// methods of a class to the facade's method list. In essence, it's the reverse
// of the API. It's written once and generic over any registered type whose
// member names line up. Being an ordinary inheritable class, it is also the
// base for partial overrides (see `sheriff`). It is used when a class is
// registered for a facade, but can be partially or fully overridden.
// Inheriting `proxy_impl_base` is optional sugar: it carries the `method_key`
// alias, so the bindings spell it unqualified.
//
// With reflection, both the API and boilerplate will be generated
// automatically from the facade method list. For now, let your AI do it for
// you.
struct gunslinger
    : prox::facade<prox::name<"gunslinger">,             //
          prox::method<"fire", int(int)>,                //
          prox::method<"describe", std::string() const>, //
          prox::method<"reload", void()>,                //
          prox::method<"shots", int&()>> {               //
  struct api {
    int fire(this auto&& self, int rounds) {
      return self.template call<"fire">(rounds);
    }
    std::string describe(this const auto& self) {
      return self.template call<"describe">();
    }
    void reload(this auto&& self) { self.template call<"reload">(); }
    int& shots(this auto&& self) { return self.template call<"shots">(); }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"fire">, T& t, int rounds) {
      return t.fire(rounds);
    }
    static std::string on(method_key<"describe">, const T& t) {
      return t.describe();
    }
    static void on(method_key<"reload">, T& t) { t.reload(); }
    static int& on(method_key<"shots">, T& t) { return t.shots(); }
  };
};

// A type whose method names line up with the facade. Conforms by
// registration through the boilerplate impl. This is the simple case of a type
// designed to be a `gunslinger`, and probably the motivating case.
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
  // For `hair_trigger`.
  [[nodiscard]] bool jams() const {
    (void)this;
    return false;
  }

  int rounds_fired{};
};

// Registration for `lawman`. Pure opt-in, one hook, no bindings.
consteval auto corvid_proxy_spec(gunslinger*, lawman*) {
  return prox::make_proxy_spec<gunslinger, lawman>();
}

// A `deputy` is exactly like a lawman, but with a different name. Works
// automatically, without explicit registration, because a `deputy` is-a
// `lawman`.
//
// Note that, even though `describe` is not virtual, calling it through a
// `lawman` proxy invokes the `deputy`'s override. In contrast, calling it
// through a `lawman` reference would invoke the `lawman`'s own method. This
// illustrates how proxies function as a sort of replacement for virtual.
//
// Having said that, if this class owned resources, then you would need to make
// the parent's destructor virtual, for correctness. This is true even though
// combining virtual and proxy is otherwise a code smell.
struct deputy: public lawman {
  // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "deputy";
  }
};

// `robber` is a type with the right shape but the wrong names.
//
// Conforms by carrying its impl in the registration, via the three-type
// `make_proxy_spec`. The binding class is local to the hook, making the whole
// conformance one self-contained declaration, which also works for a type you
// do not own. Nesting the impl in the type instead (see `turncoat`)
// additionally grants access to its private members. You could also
// forward-declare and friend the impl.
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

// Note the name adaptation, including binding "shots" to a data member.
consteval auto corvid_proxy_spec(gunslinger*, robber*) {
  struct as_gunslinger: proxy_impl_base {
    static int on(method_key<"fire">, robber& r, int rounds) {
      return r.shoot(rounds);
    }
    static std::string on(method_key<"describe">, const robber& r) {
      return r.description();
    }
    static void on(method_key<"reload">, robber& r) { r.rearm(); }
    static int& on(method_key<"shots">, robber& r) { return r.fired; }
  };
  return prox::make_proxy_spec<gunslinger, robber, as_gunslinger>();
}

// `sheriff` is a type whose method names line up, with one exception: `shoot`
// instead of `fire`.
//
// Its registration carries an impl that inherits the facade's boilerplate,
// re-exposes its `on` overloads, and overrides only the divergent binding.
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

// Note how `using on` brings in the `sheriff`'s boilerplate.
consteval auto corvid_proxy_spec(gunslinger*, sheriff*) {
  struct as_gunslinger: gunslinger::boilerplate<sheriff> {
    using gunslinger::boilerplate<sheriff>::on;
    static int on(method_key<"fire">, sheriff& s, int rounds) {
      return s.shoot(rounds);
    }
  };
  return prox::make_proxy_spec<gunslinger, sheriff, as_gunslinger>();
}

// `turncoat` could be registered as a `gunslinger` without any extra work,
// but is used to demonstrate two features. First, the `as_gunslinger` is
// inside the class instead of local to the `corvid_proxy_spec` call, which
// grants it access to the type's private members. Second, we replace the call
// to `describe` with a custom value.
struct turncoat {
  int fire(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "turncoat";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }

  int rounds_fired{};

  struct as_gunslinger: gunslinger::boilerplate<turncoat> {
    using gunslinger::boilerplate<turncoat>::on;
    static std::string on(method_key<"describe">, const turncoat&) {
      return "undercover";
    }
  };
};

consteval auto corvid_proxy_spec(gunslinger*, turncoat*) {
  return prox::make_proxy_spec<gunslinger, turncoat,
      turncoat::as_gunslinger>();
}

// `cowboy` is a type whose method names line up but which never opts in.
// Nominal conformance means this must NOT be proxiable.
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

// A facade with noexcept methods. Conformance requires the bindings
// themselves to be noexcept.
//
// The `api` forwarders are marked `noexcept` so the sugar carries the
// qualifier the way `call` does. This is on the facade author; nothing checks
// the forwarders against the method flavors.
//
// The boilerplate's bindings are marked noexcept, as conformance requires. The
// targets' own methods need not be noexcept; the binding is the terminate
// boundary.
struct hair_trigger
    : prox::facade<prox::name<"hair_trigger">,     //
          prox::method<"fire", int(int) noexcept>, //
          prox::method<"jams", bool() const noexcept>> {
  struct api {
    int fire(this auto&& self, int rounds) noexcept {
      return self.template call<"fire">(rounds);
    }
    bool jams(this const auto& self) noexcept {
      return self.template call<"jams">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"fire">, T& t, int rounds) noexcept {
      return t.fire(rounds);
    }
    static bool on(method_key<"jams">, const T& t) noexcept {
      return t.jams();
    }
  };
};

// Registration for `lawman`, which conforms through the boilerplate.
consteval auto corvid_proxy_spec(hair_trigger*, lawman*) {
  return prox::make_proxy_spec<hair_trigger, lawman>();
}

// Carried bindings not marked noexcept: must NOT conform, even though the
// shapes otherwise line up. Registration is the act of opting in, not proof
// of conformance.
consteval auto corvid_proxy_spec(hair_trigger*, robber*) {
  struct as_hair_trigger: proxy_impl_base {
    static int on(method_key<"fire">, robber& r, int rounds) {
      return r.shoot(rounds);
    }
    static bool on(method_key<"jams">, const robber&) { return false; }
  };
  return prox::make_proxy_spec<hair_trigger, robber, as_hair_trigger>();
}

// `town_crier` pins the conversion half of the erased call's `noexcept`: the
// method is noexcept, but a call-site argument needing a throwing conversion
// (a literal into `std::string`) makes the call expression non-noexcept, in
// both spellings alike.
//
// The by-value parameters below are moved, but only in a dependent call that
// the check cannot see.
// NOLINTBEGIN(performance-unnecessary-value-param)
struct town_crier
    : prox::facade<prox::name<"town_crier">, //
          prox::method<"cry", void(std::string) noexcept>> {
  struct api {
    void cry(this auto&& self, std::string words) noexcept {
      self.template call<"cry">(std::move(words));
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static void on(method_key<"cry">, T& t, std::string words) noexcept {
      t.cry(std::move(words));
    }
  };
};
// NOLINTEND(performance-unnecessary-value-param)

struct crier {
  void cry(std::string words) noexcept { last = std::move(words); }
  std::string last;
};

consteval auto corvid_proxy_spec(town_crier*, crier*) {
  return prox::make_proxy_spec<town_crier, crier>();
}

// `drifter` conforms to `gunslinger` by name, but its hooks return specs
// naming the wrong pair, one the wrong target and one the wrong facade.
// Neither counts as registration: the spec must name the pair the hook is
// keyed on, so a copy-paste slip blocks conformance instead of silently
// registering.
struct drifter {
  int fire(int rounds) {
    (void)this;
    return rounds;
  }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "drifter";
  }
  void reload() {}
  int& shots() { return count; }

  int count{};
};

consteval auto corvid_proxy_spec(gunslinger*, drifter*) {
  // Wrong target: the spec names cowboy.
  return prox::make_proxy_spec<gunslinger, cowboy>();
}

consteval auto corvid_proxy_spec(hair_trigger*, drifter*) {
  // Wrong facade: the spec names gunslinger.
  return prox::make_proxy_spec<gunslinger, drifter>();
}

// `mortar` is a facade whose `api` deliberately deviates from the method list:
// the forwarder widens the parameter as a convenience, which registration-time
// validation would reject. Its registrations opt out with `api_check::off`.
struct mortar
    : prox::facade<prox::name<"mortar">, //
          prox::method<"lob", int(int)>> {
  struct api {
    int lob(this auto&& self, long long shells) {
      return self.template call<"lob">(shells);
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"lob">, T& t, int shells) {
      return t.lob(shells);
    }
  };
};

// A conforming type. The registration passes `api_check::off`, so the
// deviating `api` is accepted as written.
struct howitzer {
  int lob(int shells) { return fired += shells; }

  int fired{};
};

consteval auto corvid_proxy_spec(mortar*, howitzer*) {
  return prox::make_proxy_spec<mortar, howitzer, prox::api_check::off>();
}

// `marshal` is a composed facade, effectively inheriting from `gunslinger` by
// extending it with an arrest method (Rust supertrait).
//
// Its `api` inherits the base facade's, adding only the new forwarder;
// deducing `this` sees the complete handle either way, so the inherited
// forwarders dispatch through the derived handle's flattened table.
//
// The boilerplate covers only the facade's own methods. The inherited
// `gunslinger` methods bind through `proxy_impl<gunslinger, T>`, which is
// what keeps an upcast view and a directly-built one identical.
struct marshal
    : prox::facade<prox::name<"marshal">,      //
          prox::extends<gunslinger>,           //
          prox::method<"arrest", bool(int)>> { //
  struct api: gunslinger::api {
    bool arrest(this auto&& self, int outlaws) {
      return self.template call<"arrest">(outlaws);
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static bool on(method_key<"arrest">, T& t, int outlaws) {
      return t.arrest(outlaws);
    }
  };
};

// `ranger` is a second composition level: `gunslinger` -> `marshal` ->
// `ranger`.
struct ranger
    : prox::facade<prox::name<"ranger">,        //
          prox::extends<marshal>,               //
          prox::method<"track", int() const>> { //
  struct api: marshal::api {
    int track(this const auto& self) { return self.template call<"track">(); }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"track">, const T& t) { return t.track(); }
  };
};

// `texas_ranger` is a type conforming to the whole `ranger` chain. Conformance
// is per facade, but one chain hook below registers every level.
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

// One chain hook registers `texas_ranger` for `ranger` and every facade it
// extends.
//
// The bindings stay per facade (each inherited method still binds through its
// declaring facade's impl); the hook only collapses the opt-in ceremony.
template<prox::InChainOf<ranger> F>
consteval auto corvid_proxy_spec(F*, texas_ranger*) {
  return prox::make_proxy_spec<F, texas_ranger>();
}

// `constable` conforms only partway up the chain: it can `arrest` but not
// `track`. Anchoring its chain hook at `marshal` registers it for that level
// and everything below, and nothing above; the anchor names the outermost
// facade the type actually conforms to.
struct constable {
  int fire(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "constable";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }
  bool arrest(int outlaws) {
    arrested += outlaws;
    return true;
  }

  int rounds_fired{};
  int arrested{};
};

template<prox::InChainOf<marshal> F>
consteval auto corvid_proxy_spec(F*, constable*) {
  return prox::make_proxy_spec<F, constable>();
}

// A `vigilante` is `gunslinger`-shaped, but deliberately registered for
// `marshal` alone (a plain per-facade hook rather than a chain one).
//
// This is an incomplete registration, so it must NOT be proxiable as a marshal
// at all, and the failure is loud at first use. Pins the per-facade
// conformance rule. That's what you get for breaking the law: error messages.
struct vigilante {
  int fire(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "vigilante";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }
  bool arrest(int outlaws) {
    (void)this;
    return outlaws > 0;
  }

  int rounds_fired{};
};

// This is bad, actually. For it to work, it would need to use the `InChainOf`
// technique, as `constable` does.
consteval auto corvid_proxy_spec(marshal*, vigilante*) {
  return prox::make_proxy_spec<marshal, vigilante>();
}

// `bounty_hunter` is a sibling of `marshal`: a second facade extending
// `gunslinger`, setting up the diamond below.
struct bounty_hunter
    : prox::facade<prox::name<"bounty_hunter">, //
          prox::extends<gunslinger>,            //
          prox::method<"claim", int(int)>> {    //
  struct api: gunslinger::api {
    int claim(this auto&& self, int bounties) {
      return self.template call<"claim">(bounties);
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"claim">, T& t, int bounties) {
      return t.claim(bounties);
    }
  };
};

// `posse_leader` is a diamond: it extends `marshal` and `bounty_hunter`,
// which both extend `gunslinger`.
//
// Flattening dedups the shared ancestor by facade identity, so `gunslinger`'s
// methods occupy one slot each and both upcast paths reach the same base
// table; there is no ambiguity to resolve, because conformance is per facade
// and only one `proxy_impl<gunslinger, T>` exists no matter the path (unlike a
// C++ non-virtual diamond, which has duplicated subobjects).
//
// The `api` diamond does need the facade author's help, and the recommended
// shape is to build it along ONE path: inherit the heavier chain and
// redeclare the lighter siblings' own forwarders. Inheriting every base
// `api` also works, but costs a using-declaration per shared-ancestor
// method (two subobjects of the same empty `api` type make plain member
// lookup ambiguous until a using-declaration pulls in one path) plus, under
// the MS ABI, one padding word in every handle (same-type subobjects need
// distinct addresses; Itanium hides the second inside the data members, MS
// never overlaps empty bases with members). There is no automatic merge to
// reach for: using-declarations cannot be pack-expanded over arbitrary
// names before reflection, and virtual inheritance would put a vbptr in
// every handle.
struct posse_leader
    : prox::facade<prox::name<"posse_leader">, //
          prox::extends<marshal>,              //
          prox::extends<bounty_hunter>,        //
          prox::method<"rally", void()>> {     //
  struct api: marshal::api {
    int claim(this auto&& self, int bounties) {
      return self.template call<"claim">(bounties);
    }
    void rally(this auto&& self) { self.template call<"rally">(); }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static void on(method_key<"rally">, T& t) { t.rally(); }
  };
};

// `trail_boss` conforms to the whole diamond, registered by one chain hook.
struct trail_boss {
  int fire(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "trail_boss";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }
  bool arrest(int outlaws) {
    arrested += outlaws;
    return true;
  }
  int claim(int bounties) { return claimed += bounties; }
  void rally() { ++rallies; }

  int rounds_fired{};
  int arrested{};
  int claimed{};
  int rallies{};
};

template<prox::InChainOf<posse_leader> F>
consteval auto corvid_proxy_spec(F*, trail_boss*) {
  return prox::make_proxy_spec<F, trail_boss>();
}

// `camera` is an unrelated facade that deliberately collides with
// `gunslinger` on two method names: `fire`, with a different signature (an
// overload set once composed), and `reload`, with the same signature
// (reachable only through the qualified key or an upcast, once composed).
struct camera
    : prox::facade<prox::name<"camera">,             //
          prox::method<"fire", std::string() const>, //
          prox::method<"reload", void()>> {          //
  struct api {
    std::string fire(this const auto& self) {
      return self.template call<"fire">();
    }
    void reload(this auto&& self) { self.template call<"reload">(); }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static std::string on(method_key<"fire">, const T& t) { return t.fire(); }
    static void on(method_key<"reload">, T& t) { t.reload(); }
  };
};

// `war_correspondent` composes the colliding siblings.
//
// The `api` convention for a collision is one using-declaration per base,
// because C++ member lookup finds sibling-base names ambiguous before overload
// resolution ever runs.
//
// For `fire`, whose signatures differ, the using-declarations merge the
// forwarders into a working overload set. For `reload`, whose signatures
// match, the merged set is ambiguous at any unqualified call, lazily, which is
// the pinned model: the qualified key or an upcast disambiguates. There's no
// point injecting either `reload` with using declarations.
//
// The same-signature collision also makes the natural-name spelling
// ambiguous inside the validation probe, so this facade's registrations
// pass `api_check::off`; the base levels still validate normally.
struct war_correspondent
    : prox::facade<prox::name<"correspondent">, //
          prox::extends<gunslinger>,            //
          prox::extends<camera>,                //
          prox::method<"byline", std::string() const>> {
  struct api: gunslinger::api, camera::api {
    using gunslinger::api::fire, camera::api::fire;
    std::string byline(this const auto& self) {
      return self.template call<"byline">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static std::string on(method_key<"byline">, const T& t) {
      return t.byline();
    }
  };
};

// `photographer` conforms to the whole composition.
//
//  One class serves both `fire` methods with an ordinary member overload set,
//  while the same-name same-signature `reload` collision demonstrates
//  per-facade bindings: the gunslinger level reloads the gun through the
//  boilerplate, and the camera level carries an impl that winds the film
//  instead.
struct photographer {
  int fire(int rounds) { return rounds_fired += rounds; }
  [[nodiscard]] std::string fire() const {
    (void)this;
    return "click";
  }
  [[nodiscard]] std::string describe() const {
    (void)this;
    return "photographer";
  }
  void reload() { rounds_fired = 0; }
  int& shots() { return rounds_fired; }
  void wind() { ++film_wound; }
  [[nodiscard]] std::string byline() const {
    (void)this;
    return "photo credit";
  }

  int rounds_fired{};
  int film_wound{};
};

// One chain hook covers the composition, varying by level: the camera level
// carries the film-winding impl, the composed level opts out of api
// validation (see the facade comment), and the rest ride the boilerplates.
template<prox::InChainOf<war_correspondent> F>
consteval auto corvid_proxy_spec(F*, photographer*) {
  struct as_camera: camera::boilerplate<photographer> {
    using camera::boilerplate<photographer>::on;
    static void on(method_key<"reload">, photographer& p) { p.wind(); }
  };
  constexpr auto check =
      std::same_as<F, war_correspondent>
          ? prox::api_check::off
          : prox::api_check::on;
  if constexpr (std::same_as<F, camera>)
    return prox::make_proxy_spec<F, photographer, as_camera, check>();
  else
    return prox::make_proxy_spec<F, photographer, check>();
}

// `arsenal` exercises per-name overload sets within one facade: `issue`
// overloads on arity, `aim` on argument type, and `count` is the const pair
// (a mutable accessor and a read-only query sharing the name, like a
// container's `front`).
//
// The bindings overload naturally: the boilerplate's `on`s share one
// `method_key` and differ in the trailing parameters, or in the constness
// of the target for the const pair. The `api` follows the standard member
// idiom, with the const pair split across `this auto&&` and
// `this const auto&` forwarders, so overload resolution picks the const
// flavor for const handles exactly as it would for a real member pair. The
// registration validates the `api` by default, pinning that `validate_api`
// drives each overload independently.
struct arsenal
    : prox::facade<prox::name<"arsenal">,       //
          prox::method<"issue", int(int)>,      //
          prox::method<"issue", int()>,         //
          prox::method<"aim", int(int)>,        //
          prox::method<"aim", int(double)>,     //
          prox::method<"count", int&()>,        //
          prox::method<"count", int() const>> { //
  struct api {
    int issue(this auto&& self, int rifles) {
      return self.template call<"issue">(rifles);
    }
    int issue(this auto&& self) { return self.template call<"issue">(); }
    int aim(this auto&& self, int paces) {
      return self.template call<"aim">(paces);
    }
    int aim(this auto&& self, double radians) {
      return self.template call<"aim">(radians);
    }
    // The mutable accessor's forwarder repeats its call in a trailing
    // requires-clause (the documented api caveat, load-bearing here): a
    // `const_proxy_view` is a mutable object whose deep const lives in the
    // type, so object constness alone would send it to this overload, and
    // the constraint routes it to the const forwarder instead.
    int& count(this auto&& self)
    requires(requires {
      { self.template call<"count">() } -> std::same_as<int&>;
    })
    {
      return self.template call<"count">();
    }
    int count(this const auto& self) { return self.template call<"count">(); }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"issue">, T& t, int rifles) {
      return t.issue(rifles);
    }
    static int on(method_key<"issue">, T& t) { return t.issue(); }
    static int on(method_key<"aim">, T& t, int paces) { return t.aim(paces); }
    static int on(method_key<"aim">, T& t, double radians) {
      return t.aim(radians);
    }
    static int& on(method_key<"count">, T& t) { return t.count(); }
    static int on(method_key<"count">, const T& t) { return t.count(); }
  };
};

// `armory` extends `arsenal`, inheriting the overload sets whole, and
// overloads the inherited `issue` across the level boundary with a
// two-argument flavor: different functions sharing a spelling, exactly as
// within one facade.
struct armory
    : prox::facade<prox::name<"armory">,        //
          prox::extends<arsenal>,               //
          prox::method<"issue", int(int, int)>, //
          prox::method<"lock", void()>> {       //
  struct api: arsenal::api {
    // The new forwarder hides the inherited `issue` forwarders until the
    // using-declaration merges them, the same convention as sibling
    // collisions; forgetting it is caught by `validate_api` at
    // registration (diagnostic on record at the conformance asserts).
    using arsenal::api::issue;
    int issue(this auto&& self, int rifles, int crates) {
      return self.template call<"issue">(rifles, crates);
    }
    void lock(this auto&& self) { self.template call<"lock">(); }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"issue">, T& t, int rifles, int crates) {
      return t.issue(rifles, crates);
    }
    static void on(method_key<"lock">, T& t) { t.lock(); }
  };
};

// `quartermaster` conforms with naturally overloaded members, including the
// const pair.
struct quartermaster {
  int issue(int rifles) { return stock_ -= rifles; }
  int issue() { return issue(1); }
  int issue(int rifles, int crates) { return stock_ -= rifles * crates; }
  int aim(int paces) {
    (void)this;
    return paces;
  }
  int aim(double radians) {
    (void)this;
    return static_cast<int>(radians * 100);
  }
  int& count() { return stock_; }
  [[nodiscard]] int count() const { return stock_; }
  void lock() { ++locked_; }

  int stock_{20};
  int locked_{};
};

template<prox::InChainOf<armory> F>
consteval auto corvid_proxy_spec(F*, quartermaster*) {
  return prox::make_proxy_spec<F, quartermaster>();
}

// `assayer` pins the blind spot in `validate_api` over overload sets, the
// overload analog of the wrong-key-same-signature hole: its hand-written
// `api` omits the `weigh(int)` forwarder, so an int-argument sugar call is
// absorbed by the `weigh(long)` forwarder through a conversion and
// dispatches the sibling slot. The probe cannot see the omission, because
// the absorbed call lands exactly on the sibling slot in arguments and
// result, so the validating registration compiles anyway.
struct assayer
    : prox::facade<prox::name<"assayer">,     //
          prox::method<"weigh", int(int)>,    //
          prox::method<"weigh", int(long)>> { //
  struct api {
    int weigh(this auto&& self, long grains) {
      return self.template call<"weigh">(grains);
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"weigh">, T& t, int nuggets) {
      return t.weigh(nuggets);
    }
    static int on(method_key<"weigh">, T& t, long grains) {
      return t.weigh(grains);
    }
  };
};

// `prospector` tells the two `weigh` overloads apart by sign.
struct prospector {
  int weigh(int nuggets) {
    (void)this;
    return nuggets;
  }
  int weigh(long grains) {
    (void)this;
    return -static_cast<int>(grains);
  }
};

consteval auto corvid_proxy_spec(assayer*, prospector*) {
  return prox::make_proxy_spec<assayer, prospector>();
}

// Lifetime accounting shared by the owning-proxy targets.
struct life_stats {
  int constructed{};
  int destroyed{};
  int moves{};
  int copies{};
};

// `strongbox` is a move-only lifetime-counting target. `Pad` scales the
// footprint so one instantiation fits the proxy's inline buffer and another
// forces the heap path.
template<size_t Pad>
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

// The facade for the lifetime tests. For testing purposes, we're not providing
// an `api`, or including the `boilerplate` inside the facade.
struct lockbox
    : prox::facade<prox::name<"lockbox">, //
          prox::method<"add", int(int)>,  //
          prox::method<"gold", int() const>> {};

// Boilerplate impl for `lockbox`, in the namespace-scope spelling: a
// `proxy_impl` partial specialization gated on `ProxyRegistered`.
//
// Still supported and equivalent; the other facades in this file host theirs
// nested (see `gunslinger`). The `SpecCarriesImpl` exclusion keeps a
// registration-carried impl winning over this boilerplate, which the nested
// form gets from the library automatically; without it, partial ordering
// would prefer this specialization.
template<typename T>
requires(
    prox::ProxyRegistered<lockbox, T> && !prox::SpecCarriesImpl<lockbox, T>)
struct prox::proxy_impl<lockbox, T> {
  static int on(method_key<"add">, T& t, int nuggets) {
    return t.add(nuggets);
  }
  static int on(method_key<"gold">, const T& t) { return t.gold(); }
};

// `coffer` is `strongbox`'s copyable sibling, for the clone tests.
template<size_t Pad>
struct coffer {
  explicit coffer(life_stats& stats) noexcept : stats_{&stats} {
    ++stats_->constructed;
  }
  coffer(const coffer& other) noexcept
      : stats_{other.stats_}, gold_{other.gold_} {
    ++stats_->constructed;
    ++stats_->copies;
  }
  coffer(coffer&& other) noexcept : stats_{other.stats_}, gold_{other.gold_} {
    ++stats_->constructed;
    ++stats_->moves;
  }
  coffer& operator=(const coffer&) = delete;
  coffer& operator=(coffer&&) = delete;
  ~coffer() { ++stats_->destroyed; }

  int add(int nuggets) { return gold_ += nuggets; }
  [[nodiscard]] int gold() const { return gold_; }

  life_stats* stats_{};
  int gold_{};
  std::byte pad_[Pad]{};
};

// `vault` composes `lockbox` without adding methods of its own: a pure
// aggregation level. The owning-upcast lifetime tests move targets through
// it.
struct vault
    : prox::facade<prox::name<"vault">, //
          prox::extends<lockbox>> {};

// Registration for every `strongbox` and `coffer` size, via chain hooks
// covering `vault` and `lockbox` alike.
template<prox::InChainOf<vault> F, size_t Pad>
consteval auto corvid_proxy_spec(F*, strongbox<Pad>*) {
  return prox::make_proxy_spec<F, strongbox<Pad>>();
}

template<prox::InChainOf<vault> F, size_t Pad>
consteval auto corvid_proxy_spec(F*, coffer<Pad>*) {
  return prox::make_proxy_spec<F, coffer<Pad>>();
}

// `tin` conforms to `lockbox` and registers for it alone, pinning that
// conformance to a base does not leak upward into `vault`, the aggregation
// level above it, whose method list adds nothing to check.
struct tin {
  int add(int nuggets) { return gold_ += nuggets; }
  [[nodiscard]] int gold() const { return gold_; }

  int gold_{};
};

consteval auto corvid_proxy_spec(lockbox*, tin*) {
  return prox::make_proxy_spec<lockbox, tin>();
}

// `keepsake` is a name-only marker facade: no methods, so binding-existence
// is vacuously true for every type, and conformance rides on registration
// alone.
struct keepsake: prox::facade<prox::name<"keepsake">> {};

consteval auto corvid_proxy_spec(keepsake*, tin*) {
  return prox::make_proxy_spec<keepsake, tin>();
}

// One size fits the inline buffer exactly, the other forces the heap path.
using small_box = strongbox<4>;
using big_box = strongbox<64>;
static_assert(sizeof(small_box) <= proxy<lockbox>::inline_size);
static_assert(sizeof(big_box) > proxy<lockbox>::inline_size);
using small_coffer = coffer<4>;
using big_coffer = coffer<64>;

// Storage policies the tests exercise, against the default's two-pointer
// buffer with heap fallback. Buffer sizes go through `padded_size`, as the
// policy requires, so they conform on any platform alignment.
namespace policies {
constexpr invocable_policy big_inline{.inline_size = padded_size(96)};
constexpr auto inline_only = invocable_policy::fixed;
constexpr auto heap_only = invocable_policy::heap;
constexpr invocable_policy big_align{
    .inline_size = padded_size(96, 2 * alignof(std::max_align_t)),
    .inline_align = 2 * alignof(std::max_align_t)};
constexpr auto silent = invocable_policy::basic.with(on_empty::silent);
constexpr auto strict =
    invocable_policy::basic.with(policy_enforcement::strict);
constexpr auto strict_silent = silent.with(policy_enforcement::strict);
} // namespace policies

// `ingot` is `strongbox`'s over-aligned sibling: its alignment exceeds the
// default `inline_align`, so only a policy that raises the alignment knob may
// store it inline, no matter how roomy the buffer is.
struct alignas(2 * alignof(std::max_align_t)) ingot {
  explicit ingot(life_stats& stats) noexcept : stats_{&stats} {
    ++stats_->constructed;
  }
  ingot(ingot&& other) noexcept : stats_{other.stats_}, gold_{other.gold_} {
    ++stats_->constructed;
    ++stats_->moves;
  }
  ingot(const ingot&) = delete;
  ingot& operator=(const ingot&) = delete;
  ingot& operator=(ingot&&) = delete;
  ~ingot() { ++stats_->destroyed; }

  int add(int nuggets) { return gold_ += nuggets; }
  [[nodiscard]] int gold() const { return gold_; }

  life_stats* stats_{};
  int gold_{};
};

consteval auto corvid_proxy_spec(lockbox*, ingot*) {
  return prox::make_proxy_spec<lockbox, ingot>();
}

// Alignment, not size, is what keeps `ingot` out of the roomy default-aligned
// buffer.
static_assert(alignof(ingot) > invocable_policy{}.inline_align);
static_assert(sizeof(ingot) <= policies::big_inline.inline_size);
static_assert(alignof(ingot) <= policies::big_align.inline_align);

// `cursed_coffer` copies like `coffer` until poisoned, after which its copy
// constructor throws. It pins clone's exception-safety contract.
struct cursed_coffer {
  explicit cursed_coffer(life_stats& stats, const bool& poison) noexcept
      : stats_{&stats}, poison_{&poison} {
    ++stats_->constructed;
  }
  cursed_coffer(const cursed_coffer& other)
      : stats_{other.stats_}, poison_{other.poison_}, gold_{other.gold_} {
    if (*poison_) throw std::runtime_error{"cursed"};
    ++stats_->constructed;
    ++stats_->copies;
  }
  cursed_coffer(cursed_coffer&& other) noexcept
      : stats_{other.stats_}, poison_{other.poison_}, gold_{other.gold_} {
    ++stats_->constructed;
    ++stats_->moves;
  }
  cursed_coffer& operator=(const cursed_coffer&) = delete;
  cursed_coffer& operator=(cursed_coffer&&) = delete;
  ~cursed_coffer() { ++stats_->destroyed; }

  int add(int nuggets) { return gold_ += nuggets; }
  [[nodiscard]] int gold() const { return gold_; }

  life_stats* stats_{};
  const bool* poison_{};
  int gold_{};
};

consteval auto corvid_proxy_spec(lockbox*, cursed_coffer*) {
  return prox::make_proxy_spec<lockbox, cursed_coffer>();
}

// `till` erases a non-class target: the registration's spec-agreement term
// must accept the same-type case for a scalar, which `derived_from` alone
// cannot see. The carried impl serves the pair, since an `int` has no members
// for a boilerplate to bind.
struct till: prox::facade<prox::name<"till">, //
                 prox::method<"amount", int() const>> {};

consteval auto corvid_proxy_spec(till*, int*) {
  struct as_till: proxy_impl_base {
    static int on(method_key<"amount">, const int& t) { return t; }
  };
  return prox::make_proxy_spec<till, int, as_till>();
}

static_assert(prox::ProxyRegistered<till, int>);
static_assert(Proxiable<int, till>);
static_assert(!prox::ProxyRegistered<till, long>);
static_assert(!Proxiable<long, till>);

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

// Registration state. Every conforming type is registered; cowboy neither
// registers nor conforms.
static_assert(prox::ProxyRegistered<gunslinger, lawman>);
static_assert(prox::ProxyRegistered<gunslinger, deputy>);
static_assert(prox::ProxyRegistered<gunslinger, robber>);
static_assert(prox::ProxyRegistered<gunslinger, sheriff>);
static_assert(!prox::ProxyRegistered<gunslinger, cowboy>);
static_assert(std::same_as<decltype(prox::proxy_spec_v<gunslinger, lawman>),
    const prox::proxy_spec<gunslinger, lawman>>);

// A hook whose spec names a different pair is not a registration for the
// pair the hook is keyed on, despite drifter's names lining up.
static_assert(!prox::ProxyRegistered<gunslinger, drifter>);
static_assert(!prox::ProxyRegistered<hair_trigger, drifter>);
static_assert(!Proxiable<drifter, gunslinger>);

// Conformance requires the pair's own opt-in even when the facade adds no
// methods for bindings to prove it. `tin` conforms to `lockbox` but never
// registered for `vault`, so the aggregation level does not come along for
// free; `keepsake` has no methods at all and conforms exactly where
// registered. Handles stay self-conformant with no registration, including
// at the aggregation level.
static_assert(Proxiable<tin, lockbox>);
static_assert(!prox::ProxyRegistered<vault, tin>);
static_assert(!Proxiable<tin, vault>);
static_assert(Proxiable<small_box, vault>); // the chain hook covers vault
static_assert(Proxiable<tin, keepsake>);
static_assert(!Proxiable<small_box, keepsake>);
static_assert(!Proxiable<int, keepsake>);
static_assert(Proxiable<proxy_view<vault>, lockbox>);
static_assert(Proxiable<proxy_view<vault>, vault>);

// Conformance. Boilerplate or carried impl, always via registration;
// matching names alone are NOT enough.
//
// Diagnostics on record (clang 22, re-captured 2026-07-08 after the
// composition refactor). Constructing `proxy_view<gunslinger>` from an
// unregistered `cowboy` emits exactly one error: "no matching constructor
// for initialization of 'proxy_view<gunslinger>' ... candidate template
// ignored: constraints not satisfied [with T = cowboy] ... because
// 'Proxiable<cowboy, gunslinger>' evaluated to false ... because
// 'details::vtbuild_t<gunslinger>::all_bound_v<gunslinger, cowboy>'
// evaluated to false", plus two "could not match" notes for the dedicated
// upcasting and proxy-viewing constructors. Calling `pv.call<"missing">()`
// fires the static_assert "facade has no method with this name", followed by
// `std::get` index-noise errors; the follow-on noise is accepted (a guarding
// `if constexpr` was tried and dropped as a simplification), since fixing
// the obvious message also removes the noise.
static_assert(Proxiable<lawman, gunslinger>);
static_assert(Proxiable<robber, gunslinger>);
static_assert(Proxiable<sheriff, gunslinger>);
static_assert(!Proxiable<cowboy, gunslinger>);
static_assert(!Proxiable<int, gunslinger>);

// The concept reports exactly the carried registrations: boilerplate-only
// registrations like lawman's carry nothing.
static_assert(Proxiable<turncoat, gunslinger>);
static_assert(prox::SpecCarriesImpl<gunslinger, robber>);
static_assert(prox::SpecCarriesImpl<gunslinger, sheriff>);
static_assert(prox::SpecCarriesImpl<gunslinger, turncoat>);
static_assert(!prox::SpecCarriesImpl<gunslinger, lawman>);

// Noexcept qualification is part of the method flavor.
static_assert(!prox::method<"fire", int(int)>::is_noexcept);
static_assert(prox::method<"fire", int(int) noexcept>::is_noexcept);
static_assert(prox::method<"jams", bool() const noexcept>::is_const);
static_assert(prox::method<"jams", bool() const noexcept>::is_noexcept);

// A direct-eligible target is stored like any other: proxy has no direct
// mode, so its policy's direct eligibility is not consulted.
namespace {
struct stateless_target {};
} // namespace
static_assert(invocables::details::direct_eligible<stateless_target>());
static_assert(
    prox::details::storage_mode_of<stateless_target>(
        invocable_policy::basic) == invocables::storage_mode::inlined);
static_assert(
    prox::details::storage_mode_of<stateless_target>(invocable_policy::heap) ==
    invocables::storage_mode::dynamic);

// Noexcept conformance: the binding itself must be noexcept.
static_assert(Proxiable<lawman, hair_trigger>);
static_assert(!Proxiable<robber, hair_trigger>);

// The erased call carries the method's noexcept.
static_assert(
    noexcept(std::declval<proxy_view<hair_trigger>&>().call<"fire">(1)));
static_assert(
    !noexcept(std::declval<proxy_view<gunslinger>&>().call<"fire">(1)));

// Both handles satisfy their own facade, including facades with noexcept
// methods.
static_assert(Proxiable<proxy_view<gunslinger>, gunslinger>);
static_assert(Proxiable<proxy<gunslinger>, gunslinger>);
static_assert(Proxiable<proxy_view<hair_trigger>, hair_trigger>);
static_assert(Proxiable<proxy<hair_trigger>, hair_trigger>);

// Views are value-semantic: copyable and rebindable by assignment. The table
// member is pointer-to-const, not a const member, so assignment stays viable.
static_assert(std::assignable_from<proxy_view<gunslinger>&,
    const proxy_view<gunslinger>&>);

// Views are also default-constructible as empty, like the owning proxy, and
// testable via `operator bool`.
static_assert(std::default_initializable<proxy_view<gunslinger>>);
static_assert(std::default_initializable<const_proxy_view<gunslinger>>);

// The owning proxy is move-only.
static_assert(!std::copy_constructible<proxy<gunslinger>>);
static_assert(std::movable<proxy<gunslinger>>);

// Deep const. The non-const facade methods are not callable on a const handle,
// and never exist on a `const_proxy_view` at all. The probes go through a
// concept so the negative case is a constraint failure rather than a hard
// error (a requires-expression outside a template does not get SFINAE).
template<typename P>
concept CanFire = requires(P& p) { p.template call<"fire">(1); };
template<typename P>
concept CanDescribe = requires(P& p) { p.template call<"describe">(); };

// Qualified keys resolve through the same machinery, so they obey the same
// deep-const gating, including through a derived facade's handle.
template<typename P>
concept CanFireQualified = requires(P& p) {
  p.template call<"gunslinger::fire">(1);
};

static_assert(CanFireQualified<proxy_view<gunslinger>>);
static_assert(!CanFireQualified<const proxy_view<gunslinger>>);
static_assert(CanFireQualified<proxy_view<ranger>>);
static_assert(CanFireQualified<proxy<gunslinger>>);

static_assert(CanFire<proxy<gunslinger>>);
static_assert(!CanFire<const proxy<gunslinger>>);
static_assert(CanDescribe<const proxy<gunslinger>>);
static_assert(CanFire<proxy_view<gunslinger>>);
static_assert(!CanFire<const proxy_view<gunslinger>>);
static_assert(CanDescribe<const proxy_view<gunslinger>>);
static_assert(!CanFire<const_proxy_view<gunslinger>>);
static_assert(!CanFire<const const_proxy_view<gunslinger>>);
static_assert(CanDescribe<const_proxy_view<gunslinger>>);

// Const-view conversion rules. A target loses mutability implicitly, never
// regains it. A const target binds only to the const view.
static_assert(
    std::convertible_to<proxy_view<gunslinger>, const_proxy_view<gunslinger>>);
static_assert(!std::constructible_from<proxy_view<gunslinger>,
    const_proxy_view<gunslinger>&>);
static_assert(
    std::constructible_from<const_proxy_view<gunslinger>, const lawman&>);
static_assert(!std::constructible_from<proxy_view<gunslinger>, const lawman&>);

// `census` is an all-const facade.
//
// This is the only case where a const view satisfies the invariant for the
// facade as a whole. A mixed facade correctly fails it (the mutable methods
// are not dispatchable, so conformance is impossible; Rust's `&dyn` analog).
struct census
    : prox::facade<prox::name<"census">,
          prox::method<"describe", std::string() const>> {};

// Conformance here is a one-off carried impl: `census` exists only to serve
// the const-view assertions, so it defines no boilerplate at all.
consteval auto corvid_proxy_spec(census*, lawman*) {
  struct as_census: proxy_impl_base {
    static std::string on(method_key<"describe">, const lawman& l) {
      return l.describe();
    }
  };
  return prox::make_proxy_spec<census, lawman, as_census>();
}

static_assert(Proxiable<const_proxy_view<census>, census>);
static_assert(!Proxiable<const_proxy_view<gunslinger>, gunslinger>);

// Composition. A composed facade flattens its bases' methods ahead of its
// own; `Extends` is transitive and strict (false for the facade itself, per
// the last assert; the walk covers only the bases).
static_assert(prox::Facade<marshal>);
static_assert(prox::Extends<marshal, gunslinger>);
static_assert(prox::Extends<ranger, marshal>);
static_assert(prox::Extends<ranger, gunslinger>);
static_assert(!prox::Extends<gunslinger, marshal>);
static_assert(!prox::Extends<marshal, marshal>);

// Conformance is per facade in the chain. Registering the derived facade
// alone is not enough (Rust's separate `impl Base for T`), and conforming to
// the base says nothing about the derived facade.
//
// Diagnostics on record (clang 22, captured 2026-07-08, detonator halves
// re-captured 2026-07-09 after the collision-rules rework). Constructing
// `proxy_view<marshal>` from `vigilante` emits one error with the same
// constraint walk as the `cowboy` case, ending at
// 'details::vtbuild_t<marshal>::all_bound_v<marshal, vigilante>' evaluated
// to false"; the walk does not name the missing base facade. Redeclaring an
// inherited name with the same signature
// (`facade<extends<gunslinger>, method<"fire", int(int)>>`)
// detonates at first use of the facade's machinery: "static assertion
// failed ... 'no_chain_collision_against<...>()': a method name may recur
// within one extends chain only as overloads differing in arguments or
// constness", with the flattened slot list, duplicate and declaring facades
// included, spelled out in the requirement, followed by one follow-on "no
// type named 'vtable_t'" noise error. (A different signature is a legal
// cross-level overload since the chain relaxation; before per-name
// overloads the message read "a facade cannot declare a method name twice
// or redeclare an inherited one"; before the collision-rules rework, the
// same shape failed 'unique_method_names' with "method names must be
// unique across the facade and its extends bases".)
//
// Listing the identical entry twice, whether a `method` or an `extends`,
// detonates at first use of the facade's machinery (diagnostic on record,
// clang 22, captured 2026-07-11): "static assertion failed ...
// 'entry_listed_once<...>()': a facade may not list the identical method or
// extends entry twice", with the duplicated entry spelled out in the
// requirement and no follow-on noise. Dedup would otherwise collapse the
// duplicate silently; a diamond's two DISTINCT `extends` entries sharing an
// ancestor stay legal, as does an explicit re-extension alongside a path that
// already covers it.
//
// A facade is never a value: the deleted default constructor on `facade`
// propagates, so declaring a `gunslinger` where a handle over one was meant
// fails at the declaration. Handles stay constructible as always.
//
// Diagnostic on record (clang 22, captured 2026-07-10): `gunslinger g;`
// fails with a single error, "call to implicitly-deleted default
// constructor of 'gunslinger'", with notes walking to the cause ("default
// constructor of 'gunslinger' is implicitly deleted because base class
// 'prox::facade<...>' has a deleted default constructor" and "'facade' has
// been explicitly marked deleted here").
static_assert(!std::is_default_constructible_v<gunslinger>);
static_assert(!std::is_default_constructible_v<posse_leader>);
static_assert(std::is_default_constructible_v<proxy<gunslinger>>);
static_assert(std::is_default_constructible_v<proxy_view<gunslinger>>);

static_assert(Proxiable<texas_ranger, gunslinger>);
static_assert(Proxiable<texas_ranger, marshal>);
static_assert(Proxiable<texas_ranger, ranger>);
static_assert(!Proxiable<vigilante, marshal>);
static_assert(!Proxiable<vigilante, gunslinger>);
static_assert(!Proxiable<lawman, marshal>);

// The chain hook registers every level of the chain, and nothing else.
static_assert(prox::ProxyRegistered<ranger, texas_ranger>);
static_assert(prox::ProxyRegistered<marshal, texas_ranger>);
static_assert(prox::ProxyRegistered<gunslinger, texas_ranger>);
static_assert(!prox::ProxyRegistered<hair_trigger, texas_ranger>);

// A chain hook anchored mid-chain registers its level and everything below,
// and nothing above.
static_assert(prox::ProxyRegistered<marshal, constable>);
static_assert(prox::ProxyRegistered<gunslinger, constable>);
static_assert(!prox::ProxyRegistered<ranger, constable>);
static_assert(Proxiable<constable, marshal>);
static_assert(Proxiable<constable, gunslinger>);
static_assert(!Proxiable<constable, ranger>);

// Diamond composition. The shared ancestor's methods flatten to one slot
// each (dedup by facade identity), so the diamond is legal, `Extends` holds
// through both paths, and the whole shape validates. Before dedup, this
// detonated on the duplicate-name check.
static_assert(prox::Extends<posse_leader, marshal>);
static_assert(prox::Extends<posse_leader, bounty_hunter>);
static_assert(prox::Extends<posse_leader, gunslinger>);
static_assert(Proxiable<trail_boss, posse_leader>);
static_assert(Proxiable<trail_boss, marshal>);
static_assert(Proxiable<trail_boss, bounty_hunter>);
static_assert(Proxiable<trail_boss, gunslinger>);
static_assert(prox::validate_api<posse_leader>());

// The single-path `api` keeps the diamond handle at two pointers. (When it
// briefly inherited both base `api`s, the two same-type empty
// `gunslinger::api` subobjects needed distinct addresses and the handle
// measured three pointers; see the facade comment.)
static_assert(sizeof(proxy_view<posse_leader>) == 2 * sizeof(void*));

// Sibling collisions. The composition is legal outright (every facade
// carries a name, so the qualified spelling is always available),
// conformance stays per facade (including the deliberately different
// `reload` bindings), and the base levels still validate their own `api`s.
//
// Diagnostics on record (clang 22, captured 2026-07-09). Omitting the
// `name` entry from a facade detonates at first use of its machinery, as a
// single clean error with the fold spelled out over the entries: "static
// assertion failed due to requirement '0 + entry_name<method<...>,
// ...>::is_name_v == 1': every facade must carry exactly one name entry".
// Composing two facades whose name<> entries match (`copycat` claiming
// name<"gunslinger">) detonates even without a method collision:
// "static assertion failed ... 'owner_names_unique_against<...>()': facade
// names must be unique within a composition", spelling the slot list, with
// declaring facades, in the requirement, followed by the usual single "no
// type named 'vtable_t'" noise error.
static_assert(prox::Extends<war_correspondent, gunslinger>);
static_assert(prox::Extends<war_correspondent, camera>);
static_assert(Proxiable<photographer, war_correspondent>);
static_assert(Proxiable<photographer, camera>);
static_assert(Proxiable<photographer, gunslinger>);
static_assert(prox::SpecCarriesImpl<camera, photographer>);
static_assert(!prox::SpecCarriesImpl<gunslinger, photographer>);
static_assert(prox::validate_api<camera>());

// The same-signature collision is a lazy call-site error through the sugar:
// the merged `reload` forwarders are an ambiguous overload set on the
// composed handle, while each base handle keeps its own working forwarder.
//
// The unqualified core spelling is the same lazy error, via static_assert
// rather than overload resolution, so it cannot be probed by a concept.
// Diagnostic on record instead (clang 22, captured 2026-07-09, message
// re-worded 2026-07-10 when per-name overloads made the qualify-the-key
// advice insufficient on its own): `wc.call<"reload">()` fires "static
// assertion failed due to requirement 'ndx != ...::ambiguous_v': ambiguous
// method call; qualify the key with the facade name, or match one
// overload's arguments exactly", followed by the accepted "tuple index out
// of bounds" noise, matching the unknown-name case.
template<typename P>
concept CanReloadSugar = requires(P& p) { p.reload(); };
static_assert(CanReloadSugar<proxy_view<gunslinger>>);
static_assert(CanReloadSugar<proxy_view<camera>>);
static_assert(!CanReloadSugar<proxy_view<war_correspondent>>);

// Per-name overload sets (`arsenal`, and `armory` across the level
// boundary). Legal when each same-name pair differs in arguments or
// constness, whether the declarations share a facade or span an extends
// chain; overloading on the result type or on `noexcept` alone stays a
// collision, and a same-signature recurrence in a chain stays an error
// (that is redeclaration). Registration validates the overloaded `api` by
// default.
//
// Diagnostics on record (clang 22, captured 2026-07-10). A pair differing
// only in the result type (`method<"x", int()>` plus `method<"x", long()>`)
// detonates at first use of the facade's machinery: "static assertion
// failed ... 'no_chain_collision_against<...>()': a method name may recur
// within one extends chain only as overloads differing in arguments or
// constness", both slots spelled out in the requirement, followed by the
// usual single "no type named 'vtable_t'" noise error. A pair differing
// only in `noexcept` fires the same assert.
static_assert(Proxiable<quartermaster, arsenal>);
static_assert(Proxiable<quartermaster, armory>);
static_assert(prox::validate_api<arsenal>());
static_assert(prox::validate_api<armory>());

// Self-conformance holds through overload sets: the library bindings
// forward through qualified keys, which narrow the candidates to one
// facade's and then resolve by arguments like any overloaded call.
static_assert(Proxiable<proxy_view<arsenal>, arsenal>);
static_assert(Proxiable<proxy_view<armory>, arsenal>);
static_assert(Proxiable<proxy<armory>, arsenal>);

// The const pair's result types pin the object-parameter preference: a
// mutable handle resolves `count` to the mutable accessor, and const
// handles to the read-only query.
static_assert(std::same_as<
    decltype(std::declval<proxy_view<arsenal>&>().call<"count">()), int&>);
static_assert(std::same_as<
    decltype(std::declval<const proxy_view<arsenal>&>().call<"count">()),
    int>);
static_assert(std::same_as<
    decltype(std::declval<const_proxy_view<arsenal>&>().call<"count">()),
    int>);

// At the sugar level the forwarders are a plain C++ overload set, so an
// argument both `aim`s can only convert to (a `long`) is a probeable
// ambiguity; the unqualified core spelling is the same lazy error via
// static_assert (diagnostic below). A `short` promotes into the int
// overload through both spellings alike, because `resolve` hands ranking to
// the compiler; the runtime checks live in "Per-name overloads".
//
// Diagnostic on record (clang 22, captured 2026-07-10, re-verified
// unchanged 2026-07-11 after ranking moved to the compiler):
// `v.call<"aim">(1L)` fires "static assertion failed due to requirement
// 'ndx != ...::ambiguous_v': ambiguous method call; qualify the key with
// the facade name, or match one overload's arguments exactly", followed by
// the accepted "tuple index out of bounds" noise.
template<typename P, typename A>
concept CanAimSugar = requires(P& p, A a) { p.aim(a); };
static_assert(CanAimSugar<proxy_view<arsenal>, int>);
static_assert(CanAimSugar<proxy_view<arsenal>, double>);
static_assert(CanAimSugar<proxy_view<arsenal>, short>);
static_assert(!CanAimSugar<proxy_view<arsenal>, long>);

// Cross-level overloads: `armory`'s two-argument `issue` joins the
// inherited pair on derived handles, and an upcast handle sees only the
// base's set. In the `api`, the derived forwarder hides the inherited ones
// until a using-declaration merges them (see the `armory` fixture), the
// same convention as sibling collisions, and forgetting it is caught: the
// validation probe drives the base slots by natural name through the base
// boilerplate, where the hidden forwarders fail to resolve.
//
// Diagnostics on record (clang 22, captured 2026-07-10). Removing the
// `using arsenal::api::issue;` from `armory::api` fails at the validating
// registration with "no matching member function for call to 'issue'" at
// `t.issue(rifles)` and `t.issue()` in `arsenal::boilerplate` (the
// candidate note names the two-argument `armory` forwarder: "requires 2
// non-object arguments, but 1 was provided"); any sugar call sites using
// the hidden overloads fail the same way. Before the chain relaxation,
// declaring `armory`'s `issue(int, int)` at all detonated with the
// phase-7 message ("a method name may recur within one facade only as
// overloads differing in arguments or constness, and never across an
// extends chain").
template<typename P, typename... As>
concept CanIssueSugar = requires(P& p, As... as) { p.issue(as...); };
static_assert(CanIssueSugar<proxy_view<armory>>);
static_assert(CanIssueSugar<proxy_view<armory>, int>);
static_assert(CanIssueSugar<proxy_view<armory>, int, int>);
static_assert(!CanIssueSugar<proxy_view<arsenal>, int, int>);

// Handles of a derived facade satisfy the base facade too (Rust: a `dyn
// Derived` meets a `Base` bound).
static_assert(Proxiable<proxy_view<marshal>, gunslinger>);
static_assert(Proxiable<proxy_view<ranger>, marshal>);
static_assert(Proxiable<proxy<ranger>, gunslinger>);

// Upcast conversions exist in exactly the safe directions: no downcasts, no
// regaining mutability, and no views of temporary proxies.
static_assert(
    std::convertible_to<proxy_view<marshal>, proxy_view<gunslinger>>);
static_assert(std::convertible_to<proxy_view<ranger>, proxy_view<gunslinger>>);
static_assert(std::convertible_to<const_proxy_view<ranger>,
    const_proxy_view<gunslinger>>);
static_assert(
    std::convertible_to<proxy_view<marshal>, const_proxy_view<gunslinger>>);
static_assert(
    !std::convertible_to<proxy_view<gunslinger>, proxy_view<marshal>>);
static_assert(!std::constructible_from<proxy_view<gunslinger>,
    const_proxy_view<marshal>&>);
static_assert(std::convertible_to<proxy<marshal>&, proxy_view<marshal>>);
static_assert(std::convertible_to<proxy<marshal>&, proxy_view<gunslinger>>);
static_assert(
    std::convertible_to<proxy<marshal>&, const_proxy_view<gunslinger>>);
static_assert(
    std::convertible_to<const proxy<marshal>&, const_proxy_view<gunslinger>>);
static_assert(
    !std::constructible_from<proxy_view<gunslinger>, const proxy<marshal>&>);
static_assert(
    !std::constructible_from<proxy_view<gunslinger>, proxy<gunslinger>>);

// Owning upcast: an rvalue proxy converts to a proxy of any facade its
// facade extends, one or more levels up. Downcasts, lvalue sources, const
// sources, and unrelated facades do not convert.
static_assert(std::convertible_to<proxy<ranger>&&, proxy<marshal>>);
static_assert(std::convertible_to<proxy<ranger>&&, proxy<gunslinger>>);
static_assert(std::constructible_from<proxy<gunslinger>, proxy<marshal>&&>);
static_assert(!std::constructible_from<proxy<ranger>, proxy<marshal>&&>);
static_assert(!std::constructible_from<proxy<marshal>, proxy<ranger>&>);
static_assert(!std::constructible_from<proxy<marshal>, const proxy<ranger>&&>);
static_assert(!std::constructible_from<proxy<gunslinger>, proxy<lockbox>&&>);
static_assert(Proxiable<small_box, vault>);
static_assert(prox::Extends<vault, lockbox>);

// Storage policies size the handle: a `heap_only` proxy carries no buffer,
// so it is two words like a view, and a bigger buffer grows the handle.
static_assert(
    sizeof(proxy<lockbox, policies::heap_only>) == 2 * sizeof(void*));
static_assert(
    sizeof(proxy<lockbox, policies::big_inline>) > sizeof(proxy<lockbox>));
static_assert(
    proxy<lockbox, policies::big_inline>::inline_size ==
    policies::big_inline.inline_size);

// Policies never foreclose an rvalue conversion: the destination
// accommodates whatever target arrives, changing its storage mode when its
// own policy demands. Exactly the conversions that might change the mode
// (or fail to, under inline_only) are not noexcept; everything else, including
// every same-policy move, is.
static_assert(std::constructible_from<proxy<lockbox, policies::big_inline>,
    proxy<lockbox>&&>);
static_assert(std::constructible_from<proxy<lockbox>,
    proxy<lockbox, policies::big_inline>&&>);
static_assert(std::constructible_from<proxy<lockbox>,
    proxy<lockbox, policies::heap_only>&&>);
static_assert(std::constructible_from<proxy<lockbox, policies::heap_only>,
    proxy<lockbox>&&>);
static_assert(std::constructible_from<proxy<lockbox>,
    proxy<lockbox, policies::inline_only>&&>);
static_assert(std::constructible_from<proxy<lockbox, policies::inline_only>,
    proxy<lockbox>&&>);
static_assert(std::constructible_from<proxy<lockbox, policies::inline_only>,
    proxy<lockbox, policies::heap_only>&&>);
static_assert(std::constructible_from<proxy<lockbox, policies::big_inline>,
    proxy<vault>&&>);
static_assert(std::is_nothrow_constructible_v<proxy<lockbox>, proxy<vault>&&>);
static_assert(std::is_nothrow_constructible_v<
    proxy<lockbox, policies::big_inline>, proxy<lockbox>&&>);
static_assert(!std::is_nothrow_constructible_v<proxy<lockbox>,
    proxy<lockbox, policies::big_inline>&&>);
static_assert(!std::is_nothrow_constructible_v<
    proxy<lockbox, policies::heap_only>, proxy<lockbox>&&>);
static_assert(
    std::is_nothrow_constructible_v<proxy<lockbox, policies::heap_only>,
        proxy<lockbox, policies::heap_only>&&>);
static_assert(!std::is_nothrow_constructible_v<
    proxy<lockbox, policies::inline_only>, proxy<lockbox>&&>);
static_assert(
    std::is_nothrow_constructible_v<proxy<lockbox, policies::inline_only>,
        proxy<lockbox, policies::inline_only>&&>);

// Downcasting exists on every owning proxy, both views, and the shared
// proxy, priced in the tables rather than the handle, and only toward a
// facade that strictly extends the handle's own; whether the birth ancestry
// actually contains the target facade is `try_downcast`'s runtime answer. A
// weak proxy deliberately has none: access goes through `lock()`.
template<typename P, typename D>
concept CanTryDowncast = requires(P p) {
  std::move(p).template try_downcast<D>();
};
static_assert(CanTryDowncast<proxy<gunslinger>, ranger>);
static_assert(CanTryDowncast<proxy<gunslinger, policies::heap_only>, ranger>);
static_assert(!CanTryDowncast<proxy<gunslinger>, lockbox>);
static_assert(!CanTryDowncast<proxy<gunslinger>, gunslinger>);
static_assert(CanTryDowncast<proxy_view<gunslinger>, ranger>);
static_assert(CanTryDowncast<const_proxy_view<gunslinger>, ranger>);
static_assert(CanTryDowncast<shared_proxy<gunslinger>, ranger>);
static_assert(!CanTryDowncast<weak_proxy<gunslinger>, ranger>);
static_assert(!CanTryDowncast<proxy_view<gunslinger>, gunslinger>);
static_assert(!CanTryDowncast<shared_proxy<gunslinger>, lockbox>);

// The shared proxy is copyable, where the unique-owning proxy is not; it
// upcasts by copy, satisfies base-facade bounds like every handle, and stays
// deep-const as an instance.
static_assert(std::copyable<shared_proxy<gunslinger>>);
static_assert(
    std::convertible_to<shared_proxy<ranger>, shared_proxy<gunslinger>>);
static_assert(
    !std::constructible_from<shared_proxy<ranger>, shared_proxy<gunslinger>>);
static_assert(Proxiable<shared_proxy<ranger>, gunslinger>);
static_assert(CanFire<shared_proxy<gunslinger>>);
static_assert(!CanFire<const shared_proxy<gunslinger>>);
static_assert(CanDescribe<const shared_proxy<gunslinger>>);

// Ownership converts one way: an rvalue proxy becomes shared, but shared
// ownership cannot become unique again (there is no race-free way to
// recover it, which is also why `std::shared_ptr` has no release), and a
// weak proxy only ever observes a shared one.
static_assert(
    std::constructible_from<shared_proxy<gunslinger>, proxy<ranger>&&>);
static_assert(
    !std::constructible_from<shared_proxy<gunslinger>, proxy<ranger>&>);
static_assert(
    !std::constructible_from<proxy<gunslinger>, shared_proxy<gunslinger>&&>);
static_assert(
    !std::constructible_from<proxy<gunslinger>, shared_proxy<ranger>&&>);
static_assert(
    !std::constructible_from<weak_proxy<gunslinger>, proxy<gunslinger>&&>);

// Weak proxies upcast among themselves, like every other handle; downcasts
// do not exist.
static_assert(std::convertible_to<weak_proxy<ranger>, weak_proxy<gunslinger>>);
static_assert(
    !std::constructible_from<weak_proxy<ranger>, weak_proxy<gunslinger>&&>);

// The call-site vocabulary is exported to `corvid::meta`, so this file uses
// the unqualified spellings throughout. Only authoring (facades, impls,
// registration) needs `prox::`. These confirm both spellings name the same
// entity.
static_assert(std::same_as<proxy<gunslinger>, prox::proxy<gunslinger>>);
static_assert(
    std::same_as<proxy_view<gunslinger>, prox::proxy_view<gunslinger>>);
static_assert(std::same_as<const_proxy_view<gunslinger>,
    prox::const_proxy_view<gunslinger>>);

// The `api` mixin is stateless, so empty-base optimization keeps the views at
// two pointers, with or without one (`lockbox` defines no `api`).
static_assert(sizeof(proxy_view<gunslinger>) == 2 * sizeof(void*));
static_assert(sizeof(const_proxy_view<gunslinger>) == 2 * sizeof(void*));
static_assert(sizeof(proxy_view<lockbox>) == 2 * sizeof(void*));
static_assert(sizeof(proxy_view<ranger>) == 2 * sizeof(void*));

// The sugar carries `noexcept` when the facade author marks the forwarders.
static_assert(noexcept(std::declval<proxy_view<hair_trigger>&>().fire(1)));
static_assert(!noexcept(std::declval<proxy_view<gunslinger>&>().fire(1)));

// The `api` is validated automatically at registration: `make_proxy_spec`
// plays the boilerplate impl (which invokes members by natural name) against
// the `api` (which declares those members), exactly type-checked at both
// ends.
// `mortar` above deviates deliberately and opts out with `api_check::off`.
// The standalone asserts below exercise the public spelling, which a facade
// author can place before any registration exists; here they are redundant
// with the registrations.
//
// Diagnostics on record (clang 22, captured 2026-07-08). Drifting the
// `api`'s
// `fire` parameter from `int` to `long long`, with these standalone asserts
// removed, fails through `lawman`'s registration alone, at the forwarder's
// own line: "no matching member function for call to 'call' ... candidate
// template ignored: constraints not satisfied [with K = ... \"fire\", Args =
// <long long &>] ... because 'vtbuild_t<gunslinger>::template exact_args<...
// \"fire\", long long &>()' evaluated to false". Before the check existed,
// the same drift compiled silently, with the sugar truncating wide arguments
// at the thunk boundary.
static_assert(prox::validate_api<gunslinger>());
static_assert(prox::validate_api<hair_trigger>());

// For a composed facade, the same chain also validates the inherited
// forwarders (brought in here by deriving the `api` from the base facade's)
// against the flattened method list.
static_assert(prox::validate_api<marshal>());
static_assert(prox::validate_api<ranger>());

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Registration and dispatch

TEST_CASE("Boilerplate impl through registration", "[proxy]") {
  lawman l;
  proxy_view<gunslinger> pv{l};

  // This case deliberately exercises the core `call<>` spelling end to end.
  // Real call sites prefer the `api` sugar, which the rest of this file uses
  // wherever a facade defines one; "Member-call sugar" below pins the
  // equivalence of the two spellings.
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

TEST_CASE("Registration-carried impl", "[proxy]") {
  // `robber` conforms through the impl its registration names.
  robber r;
  proxy_view<gunslinger> pv{r};

  CHECK(pv.fire(6) == 6);
  CHECK(r.fired == 6);
  CHECK(pv.describe() == "robber"s);
  CHECK(pv.shots() == 6);

  pv.reload();
  CHECK(r.fired == 0);
}

TEST_CASE("Non-class target", "[proxy]") {
  // An `int` conforms to `till` through the carried impl: a scalar target
  // erases like any other, owned or viewed.
  auto p = make_proxy<till, int>(42);
  CHECK(p.call<"amount">() == 42);

  int cash = 7;
  proxy_view<till> pv{cash};
  CHECK(pv.call<"amount">() == 7);
  // The store is read back through the view's erased pointer, which the
  // analyzer cannot see through.
  // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores)
  cash = 9;
  CHECK(pv.call<"amount">() == 9);
}

TEST_CASE("Partially-overridden boilerplate impl", "[proxy]") {
  sheriff s;
  proxy_view<gunslinger> pv{s};

  // The overriding binding: "fire" routes to `shoot`.
  CHECK(pv.fire(4) == 4);
  CHECK(s.rounds_fired == 4);

  // The inherited boilerplate bindings still serve the rest.
  CHECK(pv.describe() == "sheriff"s);
  CHECK(pv.shots() == 4);
  pv.reload();
  CHECK(s.rounds_fired == 0);
}

TEST_CASE("Carried impl outranks the boilerplate", "[proxy]") {
  // `turncoat`'s names line up, so the boilerplate would serve it, but its
  // registration carries an impl overriding one binding. The rest still
  // dispatch through the inherited boilerplate bindings.
  turncoat t;
  proxy_view<gunslinger> tv{t};
  CHECK(tv.fire(2) == 2);
  CHECK(t.rounds_fired == 2);
  CHECK(tv.describe() == "undercover"s);
}

TEST_CASE("Heterogeneous dispatch", "[proxy]") {
  lawman l;
  robber r;
  std::vector<proxy_view<gunslinger>> gang{l, r};

  std::string roll_call;
  // Mutable references. The "fire" method no longer dispatches through a const
  // view.
  for (auto& pv : gang) {
    pv.fire(2);
    roll_call += pv.describe();
    roll_call += ' ';
  }
  CHECK(roll_call == "lawman robber "s);
  CHECK(l.rounds_fired == 2);
  CHECK(r.fired == 2);

  // Copies are shallow, so both views alias the same target.
  auto pv2 = gang[0];
  pv2.fire(1);
  CHECK(l.rounds_fired == 3);
}

#pragma endregion
#pragma region Views and emptiness

TEST_CASE("Views rebind by assignment", "[proxy]") {
  lawman l;
  robber r;
  proxy_view<gunslinger> pv{l};
  CHECK(pv.describe() == "lawman"s);

  // Assignment rebinds both the target and the dispatch table.
  pv = proxy_view<gunslinger>{r};
  CHECK(pv.describe() == "robber"s);

  // A default-constructed view is empty until assigned a target; calling
  // through it before that is undefined behavior.
  proxy_view<gunslinger> ev;
  CHECK(!ev);
  ev = pv;
  CHECK(ev);
  CHECK(ev.describe() == "robber"s);

  const_proxy_view<gunslinger> cev;
  CHECK(!cev);
  cev = pv;
  CHECK(cev);
  CHECK(cev.describe() == "robber"s);
}

TEST_CASE("Const view", "[proxy]") {
  // A const target binds directly. Only const methods exist on this view.
  const lawman cl{};
  const_proxy_view<gunslinger> cv{cl};
  CHECK(cv.describe() == "lawman"s);

  // A temporary owner must not lend a const view: the viewing constructors
  // bind const references, so rvalue sources are explicitly deleted. Lvalue
  // owners still convert.
  static_assert(!std::is_constructible_v<const_proxy_view<gunslinger>,
      proxy<gunslinger>>);
  static_assert(!std::is_constructible_v<const_proxy_view<gunslinger>,
      shared_proxy<gunslinger>>);
  static_assert(std::is_constructible_v<const_proxy_view<gunslinger>,
      proxy<gunslinger>&>);
  static_assert(std::is_constructible_v<const_proxy_view<gunslinger>,
      shared_proxy<gunslinger>&>);

  // A const instance of the mutable view enforces the same restriction.
  lawman l;
  const proxy_view<gunslinger> cpv{l};
  CHECK(cpv.describe() == "lawman"s);

  // The mutable view converts implicitly, and const views rebind by
  // assignment like any view.
  proxy_view<gunslinger> pv{l};
  const_proxy_view<gunslinger> cv2{pv};
  CHECK(cv2.describe() == "lawman"s);
  cv = cv2;
  CHECK(cv.describe() == "lawman"s);

  // All-const facades keep the invariant. Generic code constrained on the
  // facade accepts the concrete const target and the const view alike.
  // `census` defines no `api`, so `call<>` is its real spelling (and needs
  // the dependent-name `template` keyword in this generic context).
  auto describe_it = [](const Proxiable<census> auto& t) {
    const_proxy_view<census> v{t};
    return v.template call<"describe">();
  };
  CHECK(describe_it(cl) == "lawman"s);
  const_proxy_view<census> census_view{cl};
  CHECK(describe_it(census_view) == "lawman"s);
}

TEST_CASE("Empty handles propagate emptiness", "[proxy]") {
  // Lending from an empty proxy yields an empty view, exactly as copying an
  // empty view does, and the same holds when the lend upcasts. What a call
  // through the result does is pinned in the next two cases.
  proxy<marshal> pm;
  proxy_view<marshal> pv = pm;
  CHECK(!pv);
  const_proxy_view<marshal> cv = pm;
  CHECK(!cv);
  const proxy<marshal>& cpm = pm;
  const_proxy_view<marshal> ccv = cpm;
  CHECK(!ccv);
  proxy_view<gunslinger> pvb = pm;
  CHECK(!pvb);
  const_proxy_view<gunslinger> cvb = pm;
  CHECK(!cvb);
  CHECK(!make_proxy_view<gunslinger>(pm));

  // View-to-view upcasts of empty views yield empty views, along every
  // route: mutable to mutable, const to const, and mutable to const, both
  // same-facade and upcasting.
  proxy_view<marshal> evm;
  proxy_view<gunslinger> evb = evm;
  CHECK(!evb);
  const_proxy_view<marshal> ecm;
  const_proxy_view<gunslinger> ecb = ecm;
  CHECK(!ecb);
  const_proxy_view<gunslinger> ecb2 = evm;
  CHECK(!ecb2);
  const_proxy_view<marshal> ecm2 = evm;
  CHECK(!ecm2);

  // Lending from an empty shared_proxy propagates the same way.
  shared_proxy<marshal> sm;
  proxy_view<marshal> svm = sm;
  CHECK(!svm);
  proxy_view<gunslinger> svb = sm;
  CHECK(!svb);
  const_proxy_view<gunslinger> scb = sm;
  CHECK(!scb);
}

// Diagnostics on record: `proxy<hair_trigger, policies::strict>` (strict
// enforcement over a facade of noexcept methods, which cannot raise) fires one
// error per offending method, each under an instantiation note naming it, and
// then the messageless trailing error from the proxy's own assert:
//
//   error: static assertion failed due to requirement 'value':
//   policy_enforcement::strict: this method cannot take the policy's on_empty
//   behavior exactly on an empty proxy (a noexcept method cannot raise, and
//   silent needs a value-initializable result); see the instantiation note
//   for the method, and choose a behavior every method admits, or lenient
//   enforcement
//   note: in instantiation of template class 'corvid::prox::details::
//   empty_fit_check<corvid::prox::method<corvid::basic_fixed_string<char,
//   5UL - 1>{"jams"}, bool () const noexcept>, corvid::on_empty::raise>'
//   requested here
//   (the same error and note for `method<{"fire"}, int (int) noexcept>`)
//   error: static assertion expression is not an integral constant expression
//   note: in instantiation of template class 'corvid::prox::proxy<
//   hair_trigger, invocable_policy{16, 16, 3, 1, 1}>' requested here
//
// Calling through moved-from handles is the subject of the next two cases,
// so the moved-from diagnostics are suppressed for both. Those calls are
// spelled through `call` so the use is reported here rather than inside the
// `api` forwarder.
// NOLINTBEGIN(bugprone-use-after-move, clang-analyzer-cplusplus.Move)
TEST_CASE("Empty proxies honor on_empty", "[proxy]") {
  // The default is raise, as with `std::function`, through either spelling
  // and through a const proxy.
  proxy<gunslinger> p;
  CHECK(!p);
  CHECK_THROWS_AS(p.fire(1), std::bad_function_call);
  CHECK_THROWS_AS(p.call<"describe">(), std::bad_function_call);
  CHECK_THROWS_AS(p.shots(), std::bad_function_call);
  const auto& cp = p;
  CHECK_THROWS_AS(cp.describe(), std::bad_function_call);

  // `silent` returns a value-initialized result, or nothing. The value is a
  // floor per method: `shots` returns a reference, which cannot be
  // value-initialized, so it raises instead.
  proxy<gunslinger, policies::silent> s;
  CHECK(s.fire(1) == 0);
  CHECK(s.describe().empty());
  CHECK_NOTHROW(s.reload());
  CHECK_THROWS_AS(s.shots(), std::bad_function_call);

  // A noexcept method cannot raise, so under the default floor it terminates
  // on an empty proxy, which a unit test cannot observe; under `silent` its
  // results (int, bool) value-initialize without throwing, so it is silent.
  proxy<hair_trigger, policies::silent> h;
  CHECK(h.fire(3) == 0);
  CHECK(!h.jams());
  static_assert(noexcept(h.fire(3)));

  // The behavior is the type's own: a moved-from proxy reverts to it, and
  // nothing travels with the target into a proxy of another policy.
  auto live = make_proxy<gunslinger, lawman, policies::silent>();
  proxy<gunslinger> taken = std::move(live);
  CHECK(taken.fire(2) == 2);
  CHECK(!live);
  CHECK(live.call<"fire">(1) == 0);
  proxy<gunslinger> hollow = std::move(taken);
  CHECK_THROWS_AS(taken.call<"fire">(1), std::bad_function_call);
  CHECK(hollow.fire(1) == 3);

  // An empty source upcasts to an empty proxy of the destination's behavior.
  proxy<marshal, policies::silent> em;
  proxy<gunslinger> eg = std::move(em);
  CHECK(!eg);
  CHECK_THROWS_AS(eg.fire(1), std::bad_function_call);
  CHECK(em.call<"fire">(1) == 0);

  // A failed downcast leaves the source on its behavior, and yields a result
  // on its own type's.
  proxy<gunslinger, policies::silent> unborn;
  auto still_empty = std::move(unborn).try_downcast<marshal>();
  CHECK(!still_empty);
  CHECK(still_empty.fire(1) == 0);
  CHECK(unborn.call<"fire">(1) == 0);

  // Strict enforcement admits a floor every method takes exactly: raise over
  // throwing methods, or silent over noexcept methods whose results
  // value-initialize without throwing. The refusal is recorded above.
  proxy<gunslinger, policies::strict> st;
  CHECK_THROWS_AS(st.fire(1), std::bad_function_call);
  proxy<hair_trigger, policies::strict_silent> hs;
  CHECK(hs.fire(1) == 0);
}

TEST_CASE("Empty views and shared proxies raise, or mirror their lender",
    "[proxy]") {
  // A handle with no policy of its own raises when built empty, including
  // after an upcast.
  proxy_view<gunslinger> v;
  CHECK_THROWS_AS(v.fire(1), std::bad_function_call);
  const_proxy_view<gunslinger> cv;
  CHECK_THROWS_AS(cv.describe(), std::bad_function_call);
  proxy_view<marshal> vm;
  proxy_view<gunslinger> vg = vm;
  CHECK_THROWS_AS(vg.fire(1), std::bad_function_call);
  const_proxy_view<gunslinger> cvg = vm;
  CHECK_THROWS_AS(cvg.describe(), std::bad_function_call);

  // A view lent from an empty proxy keeps the proxy's behavior, whether the
  // lend upcasts, drops mutability, or both, and a view of that view keeps
  // it too.
  proxy<marshal, policies::silent> pm;
  proxy_view<marshal> lent = pm;
  CHECK(!lent);
  CHECK(lent.fire(1) == 0);
  CHECK(!lent.arrest(1));
  proxy_view<gunslinger> lent_up = pm;
  CHECK(lent_up.fire(1) == 0);
  const_proxy_view<gunslinger> lent_c = pm;
  CHECK(lent_c.describe().empty());
  const_proxy_view<gunslinger> again = lent;
  CHECK(again.describe().empty());

  // Downcasting an empty view fails, and the result is a view built empty.
  auto down = lent_up.try_downcast<marshal>();
  CHECK(!down);
  CHECK_THROWS_AS(down.fire(1), std::bad_function_call);

  // A shared proxy raises when built empty, and mirrors an empty proxy it
  // adopts, as do the views it lends.
  shared_proxy<gunslinger> sp;
  CHECK_THROWS_AS(sp.fire(1), std::bad_function_call);
  shared_proxy<gunslinger> mirrored{std::move(pm)};
  CHECK(!mirrored);
  CHECK(mirrored.fire(1) == 0);
  proxy_view<gunslinger> from_shared = mirrored;
  CHECK(from_shared.fire(1) == 0);
  shared_proxy<gunslinger> mirrored_copy = mirrored;
  CHECK(mirrored_copy.fire(1) == 0);

  // A moved-from shared proxy raises, whether the move is a plain move, an
  // upcast, or a transferring downcast.
  auto sm = make_shared_proxy<marshal, texas_ranger>();
  auto sm2 = std::move(sm);
  CHECK(!sm);
  CHECK_THROWS_AS(sm.call<"fire">(1), std::bad_function_call);
  CHECK(sm2.fire(1) == 1);
  shared_proxy<gunslinger> sg = std::move(sm2);
  CHECK_THROWS_AS(sm2.call<"fire">(1), std::bad_function_call);
  CHECK(sg.fire(1) == 2);
  auto back = std::move(sg).try_downcast<marshal>();
  CHECK(back);
  CHECK_THROWS_AS(sg.call<"fire">(1), std::bad_function_call);
  CHECK(back.fire(1) == 3);
  sm = std::move(back);
  CHECK_THROWS_AS(back.call<"fire">(1), std::bad_function_call);

  // Locking an expired weak proxy yields an empty handle that raises, and an
  // empty weak proxy upcasts and locks the same way.
  weak_proxy<marshal> w = sm;
  sm = shared_proxy<marshal>{};
  CHECK(w.expired());
  auto locked = w.lock();
  CHECK(!locked);
  CHECK_THROWS_AS(locked.fire(1), std::bad_function_call);
  weak_proxy<marshal> wm;
  weak_proxy<gunslinger> wg = wm;
  CHECK_THROWS_AS(wg.lock().fire(1), std::bad_function_call);
}
// NOLINTEND(bugprone-use-after-move, clang-analyzer-cplusplus.Move)

TEST_CASE("Generic code accepts concrete and erased alike", "[proxy]") {
  // Facade-constrained generic code. Erase, then call. `Proxiable` is the
  // trait bound for the static-dispatch half.
  auto fire_twice = [](Proxiable<gunslinger> auto& g) {
    auto pv = make_proxy_view<gunslinger>(g);
    pv.fire(1);
    return pv.fire(1);
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

  // An owning proxy satisfies the facade too, so it erases the same way.
  auto p = make_proxy<gunslinger, lawman>();
  CHECK(fire_twice(p) == 2);
}

#pragma endregion
#pragma region Owning proxies

TEST_CASE("Owning proxy, inline target", "[proxy]") {
  life_stats stats;
  if (true) {
    auto p = make_proxy<lockbox, small_box>(stats);
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
    auto p = make_proxy<lockbox, big_box>(stats);
    CHECK(stats.constructed == 1);
    CHECK(p.call<"add">(7) == 7);

    // Heap targets move by pointer steal, so there's no target activity at
    // all.
    auto q = std::move(p);
    CHECK(stats.moves == 0);
    CHECK(stats.constructed == 1);
    CHECK(stats.destroyed == 0);
    CHECK(q.call<"gold">() == 7);

    // Move assignment over a live target destroys the old target. (The
    // analyzer cannot see that emptiness is keyed by the table.)
    // NOLINTBEGIN(clang-analyzer-cplusplus.NewDeleteLeaks)
    q = make_proxy<lockbox, big_box>(stats);
    CHECK(stats.constructed == 2);
    // NOLINTEND(clang-analyzer-cplusplus.NewDeleteLeaks)
    CHECK(stats.destroyed == 1);
    CHECK(q.call<"gold">() == 0);
  }
  CHECK(stats.destroyed == stats.constructed);
}

TEST_CASE("Owning proxy is deep-const", "[proxy]") {
  // Only const-qualified facade methods dispatch through a const proxy. The
  // negative half is covered by the `CanFire` static_asserts above.
  const auto p = make_proxy<gunslinger, lawman>();
  CHECK(p.describe() == "lawman"s);
}

TEST_CASE("Noexcept facade methods", "[proxy]") {
  lawman l;
  proxy_view<hair_trigger> pv{l};
  CHECK(pv.fire(2) == 2);
  CHECK(!pv.jams());

  // The `call<>` asserts pin the erased call's own noexcept; the sugar's is
  // pinned at file scope.
  auto p = make_proxy<hair_trigger, lawman>();
  CHECK(p.fire(4) == 4);
  static_assert(noexcept(p.call<"fire">(4)));
  const auto& cp = p;
  CHECK(!cp.jams());
  static_assert(noexcept(cp.call<"jams">()));

  // The conversion half: the argument conversions are the caller's, so a
  // noexcept method called with an argument whose conversion can throw is
  // not noexcept, in either spelling. Moving in the declared type converts
  // by nothrow move, so both spellings are noexcept.
  crier c;
  proxy_view<town_crier> tv{c};
  tv.cry("oyez");
  CHECK(c.last == "oyez");
  std::string words{"oyez"};
  static_assert(!noexcept(tv.call<"cry">("oyez")));
  static_assert(!noexcept(tv.cry("oyez")));
  static_assert(noexcept(tv.call<"cry">(std::move(words))));
  static_assert(noexcept(tv.cry(std::move(words))));
  tv.cry(std::move(words));
  CHECK(c.last == "oyez");
}

#pragma endregion
#pragma region Facade composition and overloads

TEST_CASE("Member-call sugar via the api mixin", "[proxy]") {
  lawman l;
  proxy_view<gunslinger> pv{l};

  // The forwarders are sugar over `call`: same slots, same dispatch table,
  // interchangeable mid-stream.
  CHECK(pv.fire(3) == 3);
  CHECK(pv.call<"fire">(2) == 5);
  CHECK(pv.describe() == "lawman"s);

  // Reference return through the sugar.
  pv.shots() = 42;
  CHECK(l.rounds_fired == 42);
  pv.reload();
  CHECK(l.rounds_fired == 0);

  // In a template context the sugar needs no dependent-name `template`
  // keyword, unlike spelling `call` directly.
  auto fire_once = [](Proxiable<gunslinger> auto& g) {
    auto v = make_proxy_view<gunslinger>(g);
    return v.fire(1);
  };
  CHECK(fire_once(l) == 1);

  // The owning proxy inherits the same `api`. Deep const holds: only the
  // const forwarder dispatches through a const proxy.
  auto p = make_proxy<gunslinger, robber>();
  CHECK(p.fire(6) == 6);
  const auto& cp = p;
  CHECK(cp.describe() == "robber"s);

  // The const view exposes the const forwarders.
  const lawman cl{};
  const_proxy_view<gunslinger> cv{cl};
  CHECK(cv.describe() == "lawman"s);

  // Noexcept forwarders, propagation confirmed by the static_asserts above.
  proxy_view<hair_trigger> ht{l};
  CHECK(ht.fire(2) == 3);
  CHECK(!ht.jams());

  // A deliberately deviating `api`, registered with `api_check::off`: the
  // widening forwarder still dispatches through the same table.
  howitzer h;
  proxy_view<mortar> mv{h};
  CHECK(mv.lob(3) == 3);
}

TEST_CASE("Facade-qualified method keys", "[proxy]") {
  lawman l;
  proxy_view<gunslinger> pv{l};
  int n{};

  // A named facade's methods answer to their qualified spelling as well as
  // the plain one: same slot, same dispatch table, interchangeable.
  CHECK(pv.call<"gunslinger::fire">(3) == (n += 3));
  CHECK(pv.call<"fire">(3) == (n += 3));
  CHECK(pv.fire(3) == (n += 3));
  CHECK(pv.call<"gunslinger::describe">() == "lawman"s);
  CHECK(pv.call<"describe">() == "lawman"s);
  CHECK(pv.describe() == "lawman"s);
  pv.call<"gunslinger::reload">();
  pv.call<"reload">();
  pv.reload();
  CHECK(l.rounds_fired == 0);

  // Every method keeps its declaring facade's qualifier through a derived
  // handle: each level of the chain contributes its own prefix.
  texas_ranger tr;
  proxy_view<ranger> rv{tr};
  n = 0;
  CHECK(rv.call<"gunslinger::fire">(2) == (n += 2));
  CHECK(rv.call<"fire">(2) == (n += 2));
  CHECK(rv.fire(2) == (n += 2));
  CHECK(rv.call<"gunslinger::describe">() == "texas_ranger"s);
  CHECK(rv.call<"describe">() == "texas_ranger"s);
  CHECK(rv.describe() == "texas_ranger"s);
  CHECK(rv.call<"marshal::arrest">(1));
  CHECK(rv.call<"arrest">(1));
  CHECK(rv.arrest(1));
  CHECK(rv.call<"ranger::track">() == 3);
  CHECK(rv.call<"track">() == 3);
  CHECK(rv.track() == 3);

  // Qualified keys through the const view and the owning proxy.
  const lawman cl{};
  const_proxy_view<gunslinger> cv{cl};
  CHECK(cv.call<"gunslinger::describe">() == "lawman"s);
  CHECK(cv.call<"describe">() == "lawman"s);
  CHECK(cv.describe() == "lawman"s);
  auto p = make_proxy<gunslinger, robber>();
  n = 0;
  CHECK(p.call<"gunslinger::fire">(6) == (n += 6));
  CHECK(p.call<"fire">(6) == (n += 6));
  CHECK(p.fire(6) == (n += 6));
}

TEST_CASE("Facade composition", "[proxy]") {
  texas_ranger tr;
  proxy_view<marshal> mv{tr};

  // Inherited and own methods dispatch through the same flattened table,
  // through the inherited and own `api` forwarders alike.
  CHECK(mv.fire(2) == 2);
  CHECK(mv.describe() == "texas_ranger"s);
  CHECK(mv.arrest(1));
  CHECK(tr.arrested == 1);

  // The core spelling hits the same slot, inherited or not.
  CHECK(mv.call<"fire">(1) == 3);
  CHECK(mv.call<"arrest">(2));
  CHECK(tr.arrested == 3);

  // Two composition levels down, same story.
  proxy_view<ranger> rv{tr};
  CHECK(rv.track() == 3);
  CHECK(rv.fire(1) == 4);
  CHECK(rv.arrest(1));
  CHECK(rv.track() == 4);

  // An owning proxy of a composed facade dispatches the whole chain too.
  auto p = make_proxy<ranger, texas_ranger>();
  CHECK(p.fire(5) == 5);
  CHECK(p.arrest(1));
  CHECK(p.track() == 1);

  // A mid-chain conformer dispatches through the levels it registered for.
  constable c;
  proxy_view<marshal> cmv{c};
  CHECK(cmv.arrest(2));
  CHECK(c.arrested == 2);
  proxy_view<gunslinger> cgv = cmv;
  CHECK(cgv.describe() == "constable"s);
}

TEST_CASE("Sibling method collisions", "[proxy]") {
  photographer ph;
  proxy_view<war_correspondent> wc{ph};

  // Different signatures merge into a working overload set, through the api
  // sugar (one using-declaration per base) and through unqualified `call<>`
  // alike: the arguments pick the slot, exactly as `using A::f; using
  // B::f;` would in plain C++.
  CHECK(wc.fire(3) == 3);
  CHECK(wc.fire() == "click"s);
  CHECK(wc.call<"fire">(2) == 5);
  CHECK(wc.call<"fire">() == "click"s);

  // The qualified spelling always works, collision or not.
  CHECK(wc.call<"gunslinger::fire">(1) == 6);
  CHECK(wc.call<"camera::fire">() == "click"s);
  CHECK(wc.call<"correspondent::byline">() == "photo credit"s);

  // The same-signature collision is reachable through qualified keys. The
  // two slots carry genuinely different bindings for the same concrete
  // type: the gun reloads at the gunslinger level, the film winds at the
  // camera level. (The unqualified spellings are the pinned lazy call-site
  // errors; the sugar half is the `CanReloadSugar` static_asserts.)
  wc.call<"gunslinger::reload">();
  CHECK(ph.rounds_fired == 0);
  wc.call<"camera::reload">();
  CHECK(ph.film_wound == 1);
  // Note that `wc.reload();` would be ambiguous and fail at compile time.
  // In contrast, `ph.reload();` is unambiguous and works, because
  // `photographer` uses that name for the `gunslinger` binding.

  // An upcast handle sees only its own level's list, so each name is
  // unambiguous again.
  proxy_view<camera> cam = wc;
  cam.reload();
  CHECK(ph.film_wound == 2);
  CHECK(cam.fire() == "click"s);
  proxy_view<gunslinger> gun = wc;
  gun.fire(4);
  gun.reload();
  CHECK(ph.rounds_fired == 0);
  CHECK(ph.film_wound == 2);
}

TEST_CASE("Per-name overloads", "[proxy]") {
  quartermaster q;
  proxy_view<arsenal> v = q;

  // Arity overloads resolve by argument count, through the sugar and the
  // core spelling alike.
  CHECK(v.issue(2) == 18);
  CHECK(v.issue() == 17);
  CHECK(v.call<"issue">(2) == 15);
  CHECK(v.call<"issue">() == 14);

  // Type overloads resolve by exact argument match.
  CHECK(v.aim(3) == 3);
  CHECK(v.aim(0.5) == 50);
  CHECK(v.call<"aim">(7) == 7);
  CHECK(v.call<"aim">(0.25) == 25);

  // Ranking is the compiler's own, so promotions rank through the core
  // spelling exactly as through the sugar: a short promotes into the int
  // overload and a float into the double one, where mere convertibility (a
  // long) stays the ambiguity error.
  CHECK(v.aim(short{9}) == 9);
  CHECK(v.call<"aim">(short{9}) == 9);
  CHECK(v.aim(0.5F) == 50);
  CHECK(v.call<"aim">(0.5F) == 50);

  // The const pair: a mutable handle prefers the non-const member, so the
  // accessor writes through; const handles dispatch the read-only query.
  v.count() = 30;
  CHECK(q.stock_ == 30);
  CHECK(std::as_const(v).count() == 30);
  const_proxy_view<arsenal> cv = v;
  CHECK(cv.count() == 30);

  // The overload sets survive extends flattening and the qualified
  // spelling, on owning and shared handles alike.
  auto p = make_proxy<armory, quartermaster>();
  CHECK(p.issue(4) == 16);
  p.count() = 8;
  CHECK(std::as_const(p).count() == 8);
  CHECK(p.call<"arsenal::issue">() == 7);
  p.lock();

  // Overloads span levels: `armory`'s own two-argument `issue` dispatches
  // alongside the inherited pair, by argument count unqualified and through
  // each level's qualified spelling; an upcast handle sees only the base's
  // set (the sugar probe is the `CanIssueSugar` static_asserts).
  CHECK(p.issue(2, 3) == 1);
  CHECK(p.call<"armory::issue">(1, 1) == 0);
  CHECK(p.call<"arsenal::issue">(10) == -10);
  proxy_view<arsenal> base_view = p;
  CHECK(base_view.issue() == -11);

  auto sp = make_shared_proxy<arsenal, quartermaster>();
  CHECK(sp.issue() == 19);
  CHECK(sp.count() == 19);
}

TEST_CASE("Overload absorption blind spot", "[proxy]") {
  // The registration validates and this assert passes despite the missing
  // `weigh(int)` forwarder, because the probe's int-slot call is absorbed by
  // the long forwarder and lands exactly on the sibling slot.
  static_assert(prox::validate_api<assayer>());

  prospector p;
  proxy_view<assayer> v = p;

  // The core spelling reaches both slots by exact match.
  CHECK(v.call<"weigh">(5) == 5);
  CHECK(v.call<"weigh">(5L) == -5);

  // The sugar has only the long forwarder to select, so an int argument
  // converts and dispatches the sibling slot: the misrouting the blind spot
  // hides, observed live.
  CHECK(v.weigh(5) == -5);
}

TEST_CASE("Diamond composition", "[proxy]") {
  trail_boss tb;
  proxy_view<posse_leader> plv{tb};

  // Every level dispatches through the flattened table: the shared
  // ancestor's methods (one slot each), both mid-level facades', and the
  // diamond's own.
  CHECK(plv.fire(2) == 2);
  CHECK(plv.describe() == "trail_boss"s);
  CHECK(plv.arrest(1));
  CHECK(plv.claim(3) == 3);
  plv.rally();
  CHECK(tb.rallies == 1);

  // Upcasting to the shared ancestor through either mid-level path, or
  // directly, lands on the same target and the same base table.
  proxy_view<marshal> mv = plv;
  proxy_view<bounty_hunter> bv = plv;
  proxy_view<gunslinger> via_marshal = mv;
  proxy_view<gunslinger> via_bounty = bv;
  proxy_view<gunslinger> direct = plv;
  CHECK(&via_marshal.shots() == &via_bounty.shots());
  CHECK(&via_marshal.shots() == &direct.shots());
  CHECK(via_bounty.fire(1) == 3);
  CHECK(tb.rounds_fired == 3);

  // An owning proxy of the diamond dispatches the whole shape too.
  auto p = make_proxy<posse_leader, trail_boss>();
  CHECK(p.fire(4) == 4);
  CHECK(p.claim(2) == 2);
  CHECK(p.arrest(1));
  p.rally();
}

#pragma endregion
#pragma region Upcasts

TEST_CASE("Upcasting views", "[proxy]") {
  texas_ranger tr;
  proxy_view<ranger> rv{tr};

  // Upcast to the immediate base and to the root. Every view aliases the
  // same target, so mutations are visible through all of them.
  proxy_view<marshal> mv = rv;
  proxy_view<gunslinger> gv = rv;
  CHECK(gv.fire(2) == 2);
  CHECK(mv.fire(1) == 3);
  CHECK(tr.rounds_fired == 3);
  CHECK(mv.arrest(1));
  CHECK(gv.describe() == "texas_ranger"s);

  // An upcast view is indistinguishable from a directly-built one.
  proxy_view<gunslinger> direct{tr};
  CHECK(&direct.shots() == &gv.shots());

  // Upcasts rebind by assignment like any view.
  lawman l;
  gv = proxy_view<gunslinger>{l};
  CHECK(gv.describe() == "lawman"s);
  gv = mv;
  CHECK(gv.describe() == "texas_ranger"s);

  // Const upcasts: from the mutable view, and from a const view of a const
  // target.
  const_proxy_view<gunslinger> cgv = rv;
  CHECK(cgv.describe() == "texas_ranger"s);
  const texas_ranger ctr{};
  const_proxy_view<ranger> crv{ctr};
  const_proxy_view<gunslinger> cgv2 = crv;
  CHECK(cgv2.describe() == "texas_ranger"s);
}

TEST_CASE("Viewing an owning proxy", "[proxy]") {
  auto p = make_proxy<ranger, texas_ranger>();

  // The view re-points at the stored target rather than wrapping the handle,
  // so it aliases the proxy's state, same facade or upcast.
  proxy_view<ranger> rv = p;
  proxy_view<gunslinger> gv = p;
  CHECK(rv.fire(2) == 2);
  CHECK(gv.shots() == 2);
  gv.reload();
  CHECK(p.shots() == 0);

  // A const proxy yields only the const view.
  const auto& cp = p;
  const_proxy_view<gunslinger> cgv = cp;
  CHECK(cgv.describe() == "texas_ranger"s);

  // Facade-constrained generic code accepts the derived handles under the
  // base bound (cross-facade self-conformance), and `make_proxy_view`
  // re-points rather than wrapping.
  auto fire_once = [](Proxiable<gunslinger> auto& g) {
    auto v = make_proxy_view<gunslinger>(g);
    return v.fire(1);
  };
  CHECK(fire_once(p) == 1);
  CHECK(fire_once(rv) == 2);
}

TEST_CASE("Heterogeneous ownership", "[proxy]") {
  std::vector<proxy<gunslinger>> gang;
  gang.push_back(make_proxy<gunslinger, lawman>());
  gang.push_back(make_proxy<gunslinger, robber>());

  std::string roll_call;
  for (auto& p : gang) {
    p.fire(2);
    roll_call += p.describe();
    roll_call += ' ';
  }
  CHECK(roll_call == "lawman robber "s);
}

TEST_CASE("Owning upcast", "[proxy]") {
  // Moving a derived-facade proxy into a base-facade one transfers the
  // target: same object, narrowed interface, source left empty.
  auto pr = make_proxy<ranger, texas_ranger>();
  pr.fire(3);
  CHECK(pr.arrest(2));
  proxy<marshal> pm = std::move(pr);
  CHECK(!pr); // NOLINT(bugprone-use-after-move): probing moved-from state.
  REQUIRE(pm);
  CHECK(pm.shots() == 3);
  CHECK(pm.arrest(1));

  // A second hop reaches the grandparent; a single hop from `ranger` would
  // work just as well.
  proxy<gunslinger> pg = std::move(pm);
  CHECK(!pm); // NOLINT(bugprone-use-after-move): probing moved-from state.
  CHECK(pg.describe() == "texas_ranger"s);
  CHECK(pg.shots() == 3);

  // Upcast assignment goes through the same conversion.
  auto fresh = make_proxy<ranger, texas_ranger>();
  pg = std::move(fresh);
  CHECK(pg.shots() == 0);

  // An empty source yields an empty proxy.
  proxy<ranger> empty_ranger;
  proxy<gunslinger> pe = std::move(empty_ranger);
  CHECK(!pe);
}

TEST_CASE("Owning upcast through a diamond", "[proxy]") {
  // Both diamond paths reach the shared ancestor, and dispatch through the
  // upcast proxies agrees with the derived one.
  auto pl = make_proxy<posse_leader, trail_boss>();
  pl.fire(2);
  CHECK(pl.claim(1) == 1);
  proxy<bounty_hunter> pb = std::move(pl);
  CHECK(pb.shots() == 2);
  CHECK(pb.claim(1) == 2);
  proxy<gunslinger> pg = std::move(pb);
  CHECK(pg.shots() == 2);
  CHECK(pg.describe() == "trail_boss"s);
}

TEST_CASE("Owning upcast lifetimes", "[proxy]") {
  life_stats stats;
  if (true) {
    // An inline target relocates, one move like any proxy move.
    auto pv = make_proxy<vault, small_box>(stats);
    CHECK(pv.call<"add">(5) == 5);
    proxy<lockbox> pl = std::move(pv);
    CHECK(stats.moves == 1);
    CHECK(stats.constructed == 2);
    CHECK(stats.destroyed == 1);
    CHECK(pl.call<"gold">() == 5);
  }
  CHECK(stats.destroyed == stats.constructed);

  life_stats heap_stats;
  if (true) {
    // A heap target moves by pointer steal, with no target activity.
    auto pv = make_proxy<vault, big_box>(heap_stats);
    CHECK(pv.call<"add">(7) == 7);
    proxy<lockbox> pl = std::move(pv);
    CHECK(heap_stats.moves == 0);
    CHECK(heap_stats.constructed == 1);
    CHECK(heap_stats.destroyed == 0);
    CHECK(pl.call<"gold">() == 7);
  }
  CHECK(heap_stats.destroyed == heap_stats.constructed);
}

#pragma endregion
#pragma region Storage and downcasts

// Diagnostics on record: constructing an inline_only proxy over an ineligible
// target, e.g. `make_proxy<lockbox, big_box, policies::inline_only>(stats)`,
// fires a single clean error from the in-place constructor: "static
// assertion failed due to requirement
// 'corvid::invocables::invocable_policy{16, 16, 1, 1, 0}.storage !=
// corvid::invocables::storage_policy::inline_only ||
// details::inline_eligible(corvid::invocables::invocable_policy{16, 16, 1, 1,
// 0})': the target is not eligible for an inline_only proxy's inline buffer".
TEST_CASE("Storage policies", "[proxy]") {
  // A buffer sized for `big_box` stores it inline, so moves relocate instead
  // of stealing a heap pointer.
  static_assert(sizeof(big_box) <= policies::big_inline.inline_size);
  life_stats stats;
  if (true) {
    auto p = make_proxy<lockbox, big_box, policies::big_inline>(stats);
    CHECK(p.call<"add">(3) == 3);
    auto q = std::move(p);
    CHECK(stats.moves == 1);
    CHECK(q.call<"gold">() == 3);
  }
  CHECK(stats.destroyed == stats.constructed);

  // A heap_only proxy keeps even an inline-eligible target on the heap, so its
  // address is stable and moves steal the pointer.
  life_stats heap_stats;
  if (true) {
    auto p = make_proxy<lockbox, small_box, policies::heap_only>(heap_stats);
    CHECK(p.call<"add">(4) == 4);
    auto q = std::move(p);
    CHECK(heap_stats.moves == 0);
    CHECK(heap_stats.constructed == 1);

    // Converting to the default policy hands the heap target over intact.
    proxy<lockbox> r = std::move(q);
    CHECK(heap_stats.moves == 0);
    CHECK(r.call<"gold">() == 4);
  }
  CHECK(heap_stats.destroyed == heap_stats.constructed);

  // An inline_only proxy accepts an eligible target; converting it into a
  // default-policy proxy of a base facade upcasts and relocates in one move.
  life_stats sbo_stats;
  if (true) {
    auto p = make_proxy<vault, small_box, policies::inline_only>(sbo_stats);
    CHECK(p.call<"add">(5) == 5);
    proxy<lockbox> r = std::move(p);
    CHECK(sbo_stats.moves == 1);
    CHECK(r.call<"gold">() == 5);
  }
  CHECK(sbo_stats.destroyed == sbo_stats.constructed);

  // An inline target arriving at a heap_only proxy re-boxes onto the heap
  // (one move, upcasting in the same conversion); moves from then on are
  // pointer steals.
  life_stats rebox_stats;
  if (true) {
    auto p = make_proxy<vault, small_box>(rebox_stats);
    CHECK(p.call<"add">(8) == 8);
    proxy<lockbox, policies::heap_only> h = std::move(p);
    CHECK(!p); // NOLINT(bugprone-use-after-move): probing moved-from state.
    CHECK(rebox_stats.moves == 1);
    CHECK(rebox_stats.constructed == 2);
    CHECK(rebox_stats.destroyed == 1);
    CHECK(h.call<"gold">() == 8);
    auto h2 = std::move(h);
    CHECK(rebox_stats.moves == 1);
    CHECK(h2.call<"gold">() == 8);
  }
  CHECK(rebox_stats.destroyed == rebox_stats.constructed);

  // Adoption accommodates per target, not per policy: from the same
  // big-buffer source, an oversized inline target re-boxes onto the
  // destination's heap while a small one relocates into its buffer and
  // stays inline.
  life_stats shrink_stats;
  if (true) {
    auto p = make_proxy<lockbox, big_box, policies::big_inline>(shrink_stats);
    p.call<"add">(2);
    proxy<lockbox> d = std::move(p);
    CHECK(shrink_stats.moves == 1);
    CHECK(d.call<"gold">() == 2);
    auto d2 = std::move(d);
    CHECK(shrink_stats.moves == 1);
    CHECK(d2.call<"gold">() == 2);
  }
  CHECK(shrink_stats.destroyed == shrink_stats.constructed);
  life_stats fit_stats;
  if (true) {
    auto p = make_proxy<lockbox, small_box, policies::big_inline>(fit_stats);
    p.call<"add">(3);
    proxy<lockbox> d = std::move(p);
    CHECK(fit_stats.moves == 1);
    auto d2 = std::move(d);
    CHECK(fit_stats.moves == 2);
    CHECK(d2.call<"gold">() == 3);
  }
  CHECK(fit_stats.destroyed == fit_stats.constructed);

  // A heap target un-boxes into an inline_only proxy's buffer when it fits,
  // and throws when it cannot, leaving the source intact. `can_adopt`
  // answers up front, so the throw is avoidable; only an inline_only
  // destination can ever refuse, and an empty source is always adoptable.
  life_stats unbox_stats;
  if (true) {
    using sbo_proxy = proxy<lockbox, policies::inline_only>;
    auto p = make_proxy<lockbox, small_box, policies::heap_only>(unbox_stats);
    p.call<"add">(4);
    CHECK(sbo_proxy::can_adopt(p));
    sbo_proxy s = std::move(p);
    CHECK(unbox_stats.moves == 1);
    CHECK(s.call<"gold">() == 4);
    auto s2 = std::move(s);
    CHECK(unbox_stats.moves == 2);
    CHECK(s2.call<"gold">() == 4);

    auto big = make_proxy<lockbox, big_box, policies::heap_only>(unbox_stats);
    big.call<"add">(6);
    CHECK(!sbo_proxy::can_adopt(big));
    CHECK(proxy<lockbox>::can_adopt(big));
    proxy<lockbox, policies::heap_only> no_target;
    CHECK(sbo_proxy::can_adopt(no_target));
    CHECK_THROWS_AS(sbo_proxy{std::move(big)}, std::length_error);
    REQUIRE(big); // NOLINT(bugprone-use-after-move): kept whole on failure.
    CHECK(big.call<"gold">() == 6);
  }
  CHECK(unbox_stats.destroyed == unbox_stats.constructed);
}

TEST_CASE("Downcasting a proxy", "[proxy]") {
  // Every owning proxy remembers the facade its target was born as (in the
  // owning table, not the handle), so an upcast can be undone, one level at
  // a time or straight back down.
  auto pr = make_proxy<ranger, texas_ranger>();
  pr.fire(3);
  proxy<gunslinger> pg = std::move(pr);
  CHECK(pg.shots() == 3);
  auto pm = std::move(pg).try_downcast<marshal>();
  REQUIRE(pm);
  CHECK(!pg); // NOLINT(bugprone-use-after-move): probing moved-from state.
  CHECK(pm.arrest(2));
  auto pr2 = std::move(pm).try_downcast<ranger>();
  REQUIRE(pr2);
  CHECK(!pm); // NOLINT(bugprone-use-after-move): probing moved-from state.
  CHECK(pr2.track() == 2);
  CHECK(pr2.shots() == 3);

  // A downcast below the birth facade fails, and failure does not consume
  // the source; the matching cast then succeeds. An empty source fails.
  auto pc = make_proxy<marshal, constable>();
  proxy<gunslinger> pg2 = std::move(pc);
  auto too_deep = std::move(pg2).try_downcast<ranger>();
  CHECK(!too_deep);
  REQUIRE(pg2); // NOLINT(bugprone-use-after-move): kept whole on failure.
  // NOLINTNEXTLINE(bugprone-use-after-move): the failed cast kept it whole.
  auto pm2 = std::move(pg2).try_downcast<marshal>();
  REQUIRE(pm2);
  CHECK(pm2.arrest(1));
  proxy<gunslinger> unborn;
  auto still_empty = std::move(unborn).try_downcast<marshal>();
  CHECK(!still_empty);

  // Through a diamond, the common base can sidecast to either sibling.
  auto pl = make_proxy<posse_leader, trail_boss>();
  pl.fire(2);
  proxy<gunslinger> pgun = std::move(pl);
  auto pb = std::move(pgun).try_downcast<bounty_hunter>();
  REQUIRE(pb);
  CHECK(pb.claim(1) == 1);
  CHECK(pb.shots() == 2);

  // The birth is the facade the proxy was constructed as, not the concrete
  // type's full conformance: a texas_ranger born mid-chain downcasts back
  // to its birth facade but never past it, even though the type conforms
  // further up.
  auto born_mid = make_proxy<marshal, texas_ranger>();
  proxy<gunslinger> pg3 = std::move(born_mid);
  auto not_ranger = std::move(pg3).try_downcast<ranger>();
  CHECK(!not_ranger);
  REQUIRE(pg3); // NOLINT(bugprone-use-after-move): kept whole on failure.
  // NOLINTNEXTLINE(bugprone-use-after-move): the failed cast kept it whole.
  auto back_to_birth = std::move(pg3).try_downcast<marshal>();
  REQUIRE(back_to_birth);
  CHECK(back_to_birth.describe() == "texas_ranger"s);
}

TEST_CASE("Downcast lifetimes", "[proxy]") {
  // The inline target relocates on each cast; everything balances.
  life_stats stats;
  if (true) {
    auto pv = make_proxy<vault, small_box>(stats);
    pv.call<"add">(4);
    proxy<lockbox> pl = std::move(pv);
    CHECK(stats.moves == 1);
    auto back = std::move(pl).try_downcast<vault>();
    REQUIRE(back);
    CHECK(stats.moves == 2);
    CHECK(back.call<"gold">() == 4);
  }
  CHECK(stats.destroyed == stats.constructed);

  // The heap target's allocation is handed through untouched.
  life_stats heap_stats;
  if (true) {
    auto pv = make_proxy<vault, big_box>(heap_stats);
    pv.call<"add">(6);
    proxy<lockbox> pl = std::move(pv);
    auto back = std::move(pl).try_downcast<vault>();
    REQUIRE(back);
    CHECK(heap_stats.moves == 0);
    CHECK(heap_stats.constructed == 1);
    CHECK(back.call<"gold">() == 6);
  }
  CHECK(heap_stats.destroyed == heap_stats.constructed);

  // Mode changes keep the birth memory usable: a mode-changing adoption
  // lands on the table's other-mode sibling, which carries its own mode's
  // ancestry. Re-boxing into heap_only, then downcasting (a steal);
  // un-boxing into inline_only, then downcasting (a relocation).
  life_stats rebox_stats;
  if (true) {
    auto pv = make_proxy<vault, small_box>(rebox_stats);
    pv.call<"add">(7);
    proxy<lockbox, policies::heap_only> pl = std::move(pv);
    CHECK(rebox_stats.moves == 1);
    auto back = std::move(pl).try_downcast<vault>();
    REQUIRE(back);
    CHECK(rebox_stats.moves == 1);
    CHECK(back.call<"gold">() == 7);
  }
  CHECK(rebox_stats.destroyed == rebox_stats.constructed);
  life_stats unbox_stats;
  if (true) {
    auto pv = make_proxy<vault, small_box, policies::heap_only>(unbox_stats);
    pv.call<"add">(9);
    proxy<lockbox, policies::inline_only> pl = std::move(pv);
    CHECK(unbox_stats.moves == 1);
    auto back = std::move(pl).try_downcast<vault>();
    REQUIRE(back);
    CHECK(unbox_stats.moves == 2);
    CHECK(back.call<"gold">() == 9);
  }
  CHECK(unbox_stats.destroyed == unbox_stats.constructed);
}

TEST_CASE("Downcasting views", "[proxy]") {
  // A view built directly over a target is born as its own facade: the
  // upcast is undoable, and the result is a new view over the same target.
  // The source is copied from, never consumed, so it stays usable; that is
  // why the view flavor is const where the owning one is an rvalue method.
  texas_ranger tex;
  proxy_view<marshal> vm = tex;
  proxy_view<gunslinger> vg = vm;
  vg.fire(3);
  auto back = vg.try_downcast<marshal>();
  REQUIRE(back);
  CHECK(back.arrest(2));
  CHECK(tex.arrested == 2);
  CHECK(vg.shots() == 3);

  // The birth is the view's facade, so a downcast past it fails even though
  // the type conforms further up; failure yields an empty view.
  CHECK(!vg.try_downcast<ranger>());

  // A view lent from an owning proxy inherits the owner's birth: this
  // gunslinger view recovers the full ranger, which the directly built view
  // above could not.
  auto pr = make_proxy<ranger, texas_ranger>();
  pr.arrest(4);
  proxy_view<gunslinger> lent = pr;
  auto vr = lent.try_downcast<ranger>();
  REQUIRE(vr);
  CHECK(vr.track() == 4);

  // A view lent from a shared proxy inherits its birth the same way.
  auto sp = make_shared_proxy<marshal, texas_ranger>();
  proxy_view<gunslinger> vs = sp;
  CHECK(vs.try_downcast<marshal>());
  CHECK(!vs.try_downcast<ranger>());

  // The const view downcasts to another const view, so mutability never
  // reopens.
  const_proxy_view<gunslinger> cg = vm;
  auto cm = cg.try_downcast<marshal>();
  REQUIRE(cm);
  CHECK(cm.describe() == "texas_ranger"s);

  // Through a diamond, a view of the common base sidecasts to either
  // sibling.
  trail_boss boss;
  proxy_view<posse_leader> vp = boss;
  proxy_view<gunslinger> vgun = vp;
  auto vb = vgun.try_downcast<bounty_hunter>();
  REQUIRE(vb);
  CHECK(vb.claim(1) == 1);
  auto vm2 = vgun.try_downcast<marshal>();
  REQUIRE(vm2);
  CHECK(vm2.arrest(1));

  // An empty view downcasts to an empty view.
  proxy_view<gunslinger> unbound;
  CHECK(!unbound.try_downcast<marshal>());
}

TEST_CASE("Downcasting a shared_proxy", "[proxy]") {
  // The lvalue flavor shares: the result is another owner of the one
  // target, and the source keeps its own share, so the target outlives any
  // subset of its owners.
  life_stats stats;
  if (true) {
    auto sv = make_shared_proxy<vault, small_box>(stats);
    sv.call<"add">(5);
    shared_proxy<lockbox> sl = sv;
    auto back = sl.try_downcast<vault>();
    REQUIRE(back);
    CHECK(back.call<"gold">() == 5);
    back.call<"add">(1);
    CHECK(sl.call<"gold">() == 6);
    REQUIRE(sl);
    sv = {};
    sl = {};
    CHECK(stats.destroyed == 0);
    CHECK(back.call<"gold">() == 6);
    CHECK(stats.constructed == 1);
  }
  CHECK(stats.destroyed == stats.constructed);

  // The rvalue flavor transfers, consuming the source only on success: a
  // cast past the birth fails and keeps the source whole.
  shared_proxy<gunslinger> sg{make_proxy<marshal, texas_ranger>()};
  auto too_deep = std::move(sg).try_downcast<ranger>();
  CHECK(!too_deep);
  REQUIRE(sg); // NOLINT(bugprone-use-after-move): kept whole on failure.
  // NOLINTNEXTLINE(bugprone-use-after-move): the failed cast kept it whole.
  auto back_down = std::move(sg).try_downcast<marshal>();
  REQUIRE(back_down);
  CHECK(!sg); // NOLINT(bugprone-use-after-move): probing moved-from state.
  CHECK(back_down.arrest(2));

  // A birth adopted from a consumed proxy carries over: born a ranger as a
  // unique proxy, still a ranger after becoming shared.
  auto pr = make_proxy<ranger, texas_ranger>();
  pr.fire(3);
  shared_proxy<gunslinger> shared_gun{std::move(pr)};
  auto sr = shared_gun.try_downcast<ranger>();
  REQUIRE(sr);
  CHECK(sr.shots() == 3);
  REQUIRE(shared_gun);
  CHECK(shared_gun.shots() == 3);

  // An empty handle downcasts to an empty handle.
  shared_proxy<gunslinger> nobody;
  CHECK(!nobody.try_downcast<marshal>());

  // A moved-from handle is empty too, and downcasts to empty, even though
  // it keeps a stale table pointer internally.
  auto sm = make_shared_proxy<marshal, texas_ranger>();
  shared_proxy<gunslinger> upcast_away{std::move(sm)};
  REQUIRE(upcast_away);
  CHECK(!sm); // NOLINT(bugprone-use-after-move): probing moved-from state.
  // NOLINTNEXTLINE(bugprone-use-after-move): probing moved-from state.
  CHECK(!sm.try_downcast<ranger>());
}

#pragma endregion
#pragma region Cloning

TEST_CASE("Cloning", "[proxy]") {
  // Cloneability is a runtime property of the erased target: `strongbox` is
  // move-only, so its proxy answers no. Cloning it anyway is graceful: the
  // clone is empty, no copy is attempted, and the source is untouched. An
  // empty proxy clones to an empty proxy.
  life_stats box_stats;
  auto locked = make_proxy<lockbox, small_box>(box_stats);
  locked.call<"add">(5);
  CHECK(!locked.can_clone());
  CHECK(!locked.clone());
  CHECK(box_stats.copies == 0);
  CHECK(locked.call<"gold">() == 5);
  proxy<lockbox> nothing;
  CHECK(nothing.can_clone());
  CHECK(!nothing.clone());

  // Inline path: the clone copy-constructs into its own buffer, and the two
  // targets are independent.
  life_stats stats;
  if (true) {
    auto p = make_proxy<lockbox, small_coffer>(stats);
    CHECK(p.can_clone());
    p.call<"add">(9);
    const auto q = p.clone();
    CHECK(stats.copies == 1);
    CHECK(q.call<"gold">() == 9);
    p.call<"add">(1);
    CHECK(p.call<"gold">() == 10);
    CHECK(q.call<"gold">() == 9);
  }
  CHECK(stats.destroyed == stats.constructed);

  // Heap path: the clone allocates its own copy. Cloning is const-clean, so
  // a const proxy clones too (into a mutable clone, like copying a const
  // value).
  life_stats heap_stats;
  if (true) {
    const auto p = make_proxy<lockbox, big_coffer>(heap_stats);
    auto q = p.clone();
    CHECK(heap_stats.copies == 1);
    CHECK(q.call<"add">(2) == 2);
    CHECK(p.call<"gold">() == 0);
  }
  CHECK(heap_stats.destroyed == heap_stats.constructed);
}

TEST_CASE("Over-aligned targets", "[proxy]") {
  // Alignment is enforced independently of size. Under the default
  // `inline_align`, an over-aligned target goes to the heap even when the
  // buffer is roomy enough; raising `inline_align` brings it inline.
  life_stats heap_stats;
  if (true) {
    auto p = make_proxy<lockbox, ingot, policies::big_inline>(heap_stats);
    p.call<"add">(7);
    auto q = std::move(p);
    // A heap target moves by pointer, without relocating the ingot.
    CHECK(heap_stats.moves == 0);
    CHECK(q.call<"gold">() == 7);
  }
  CHECK(heap_stats.destroyed == heap_stats.constructed);

  life_stats inline_stats;
  if (true) {
    auto p = make_proxy<lockbox, ingot, policies::big_align>(inline_stats);
    p.call<"add">(8);
    auto q = std::move(p);
    // An inline target relocates on proxy move.
    CHECK(inline_stats.moves == 1);
    CHECK(q.call<"gold">() == 8);

    // An inline_only destination cannot align it and refuses up front; the
    // default policy re-boxes the inline arrival to the heap.
    using sbo_proxy = proxy<lockbox, policies::inline_only>;
    CHECK(!sbo_proxy::can_adopt(q));
    proxy<lockbox> d = std::move(q);
    CHECK(inline_stats.moves == 2);
    CHECK(d.call<"gold">() == 8);

    // Un-boxing a heap ingot into the inline_only buffer fails the same way,
    // throwing and leaving the source whole.
    CHECK(!sbo_proxy::can_adopt(d));
    CHECK_THROWS_AS(sbo_proxy{std::move(d)}, std::length_error);
    REQUIRE(d); // NOLINT(bugprone-use-after-move): kept whole on failure.
    CHECK(d.call<"gold">() == 8);
  }
  CHECK(inline_stats.destroyed == inline_stats.constructed);
}

TEST_CASE("Clone exception safety", "[proxy]") {
  // When the target's copy throws, `clone` propagates it, produces no clone,
  // leaks nothing, and leaves the source untouched. The inline and heap
  // paths each clean up their own half-built destination.
  life_stats stats;
  bool poison = false;
  if (true) {
    auto p = make_proxy<lockbox, cursed_coffer, policies::big_inline>(stats,
        poison);
    p.call<"add">(3);
    const auto q = p.clone();
    CHECK(stats.copies == 1);

    poison = true;
    CHECK(p.can_clone());
    CHECK_THROWS_AS(p.clone(), std::runtime_error);
    CHECK(p.call<"gold">() == 3);
    CHECK(stats.copies == 1);

    auto h =
        make_proxy<lockbox, cursed_coffer, policies::heap_only>(stats, poison);
    h.call<"add">(4);
    CHECK_THROWS_AS(h.clone(), std::runtime_error);
    CHECK(h.call<"gold">() == 4);
  }
  CHECK(stats.destroyed == stats.constructed);
}

#pragma endregion
#pragma region Ownership interop

TEST_CASE("unique_ptr interop", "[proxy]") {
  life_stats stats;
  if (true) {
    // Moving a unique_ptr in adopts the allocation: no copy, no move, and
    // the target's address stays stable even though it would fit inline.
    auto up = std::make_unique<small_coffer>(stats);
    const auto* address = up.get();
    auto p = make_proxy<lockbox>(std::move(up));
    CHECK(!up); // NOLINT(bugprone-use-after-move): probing moved-from state.
    CHECK(stats.constructed == 1);
    CHECK(stats.moves == 0);
    CHECK(p.call<"add">(6) == 6);

    // Moving the target out hands the exact allocation back.
    auto out = p.extract<small_coffer>();
    REQUIRE(out);
    CHECK(!p);
    CHECK(out.get() == address);
    CHECK(out->gold() == 6);
    CHECK(stats.moves == 0);
  }
  CHECK(stats.destroyed == stats.constructed);

  life_stats sbo_stats;
  if (true) {
    // Extraction verifies the type at runtime: the wrong type comes back
    // null and leaves the proxy intact. An inline target moves onto the
    // heap; an empty proxy has nothing to extract.
    auto p = make_proxy<lockbox, small_coffer>(sbo_stats);
    p.call<"add">(3);
    CHECK(!p.extract<big_coffer>());
    CHECK(p);
    auto out = p.extract<small_coffer>();
    REQUIRE(out);
    CHECK(!p);
    CHECK(out->gold() == 3);
    CHECK(sbo_stats.moves == 1);
    CHECK(!p.extract<small_coffer>());
  }
  CHECK(sbo_stats.destroyed == sbo_stats.constructed);

  // An inline_only proxy un-boxes an adopted unique_ptr at construction, where
  // the concrete type makes the fit a compile-time fact.
  life_stats unbox_stats;
  if (true) {
    auto up = std::make_unique<small_coffer>(unbox_stats);
    up->add(5);
    auto p = make_proxy<lockbox, policies::inline_only>(std::move(up));
    CHECK(unbox_stats.moves == 1);
    CHECK(p.call<"gold">() == 5);
  }
  CHECK(unbox_stats.destroyed == unbox_stats.constructed);
}

TEST_CASE("Shared ownership", "[proxy]") {
  life_stats stats;
  weak_proxy<lockbox> wk;
  weak_proxy<vault> wexpired;
  CHECK(!wk.lock());
  if (true) {
    auto sv = make_shared_proxy<vault, small_coffer>(stats);
    REQUIRE(sv);
    CHECK(sv.call<"add">(5) == 5);

    // A copy, here upcasting as it goes, shares the one target rather than
    // cloning it, and mutations are visible through every handle.
    shared_proxy<lockbox> sl = sv;
    CHECK(stats.constructed == 1);
    CHECK(sl.call<"add">(1) == 6);
    CHECK(sv.call<"gold">() == 6);

    // Views lend from a shared proxy exactly as from an owning one.
    proxy_view<lockbox> v = sv;
    CHECK(v.call<"gold">() == 6);
    const_proxy_view<lockbox> cv = std::as_const(sv);
    CHECK(cv.call<"gold">() == 6);

    // A weak proxy observes without owning; locking regains a shared owner.
    wk = sv;
    CHECK(!wk.expired());
    auto locked = wk.lock();
    REQUIRE(locked);
    CHECK(locked.call<"gold">() == 6);

    // Weak proxies also upcast among themselves, without locking.
    weak_proxy<vault> wv = sv;
    weak_proxy<lockbox> wl = wv;
    auto relocked = wl.lock();
    REQUIRE(relocked);
    CHECK(relocked.call<"gold">() == 6);
    wexpired = wv;
  }
  // Every owner is gone, so the target is too, exactly once, and the weak
  // proxy answers accordingly.
  CHECK(stats.constructed == 1);
  CHECK(stats.destroyed == stats.constructed);
  CHECK(wk.expired());
  CHECK(!wk.lock());

  // An expired weak proxy upcasts too (here by move), still observing the
  // dead target; expiry stays lock's business.
  weak_proxy<lockbox> from_expired = std::move(wexpired);
  CHECK(from_expired.expired());
  CHECK(!from_expired.lock());
}

TEST_CASE("Shared ownership interop with std", "[proxy]") {
  // A unique_ptr converts into shared ownership on the way in.
  auto up = std::make_unique<lawman>();
  shared_proxy<gunslinger> sg{std::move(up)};
  CHECK(sg.fire(2) == 2);

  // Sharing with an outside shared_ptr keeps the object somewhere canonical:
  // the typed holder and the erased handle see the same target.
  auto sr = std::make_shared<robber>();
  shared_proxy<gunslinger> sg2{sr};
  CHECK(sg2.fire(3) == 3);
  CHECK(sr->fired == 3);
  sr->rearm();
  CHECK(sg2.shots() == 0);

  // An owning proxy converts into shared ownership, consuming the source: a
  // heap target keeps its allocation (the owning table's destroy slot
  // becomes the control block's deleter), an inline target moves onto the
  // heap first, and either can upcast on the way.
  life_stats stats;
  if (true) {
    auto heap_owner = make_proxy<vault, big_box>(stats);
    heap_owner.call<"add">(4);
    shared_proxy<lockbox> shared_heap{std::move(heap_owner)};
    CHECK(!heap_owner); // NOLINT(bugprone-use-after-move): probing.
    CHECK(stats.moves == 0);
    CHECK(shared_heap.call<"gold">() == 4);
    auto another = shared_heap;
    CHECK(stats.constructed == 1);
    CHECK(another.call<"gold">() == 4);

    auto inline_owner = make_proxy<lockbox, small_box>(stats);
    inline_owner.call<"add">(2);
    shared_proxy<lockbox> shared_inline{std::move(inline_owner)};
    CHECK(stats.moves == 1);
    CHECK(shared_inline.call<"gold">() == 2);
  }
  CHECK(stats.destroyed == stats.constructed);
}

#pragma endregion
#pragma region Codegen

TEST_CASE("Codegen", "[proxy]") {
  // `prox::codegen<F>(os)` writes the canonical `api` and `boilerplate` for
  // pasting into the facade body. The goldens pin every generated shape:
  // value, void, and reference returns; const and noexcept flavors; overload
  // sets; the const pair's trailing requires-clause; base `api` inheritance
  // with the using-declarations that un-hide inherited names; redeclared
  // forwarders for methods the inherited path does not cover (the
  // single-path diamond shape); and the facade-wide `arg_N` numbering, one
  // sequence spanning all methods (covered slots consume numbers, hence the
  // gaps), so a rename after pasting hits the `api` and `boilerplate`
  // together.
  std::ostringstream oss;

  // The plain facade: every return shape, a const method, one parameter.
  constexpr std::string_view gunslinger_golden = R"(  struct api {
    int fire(this auto&& self, int arg_1) {
      return self.template call<"fire">(arg_1);
    }
    std::string describe(this const auto& self) {
      return self.template call<"describe">();
    }
    void reload(this auto&& self) {
      self.template call<"reload">();
    }
    int& shots(this auto&& self) {
      return self.template call<"shots">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"fire">, T& t, int arg_1) {
      return t.fire(arg_1);
    }
    static std::string on(method_key<"describe">, const T& t) {
      return t.describe();
    }
    static void on(method_key<"reload">, T& t) {
      t.reload();
    }
    static int& on(method_key<"shots">, T& t) {
      return t.shots();
    }
  };
)";
  prox::codegen<gunslinger>(oss);
  CHECK(oss.str() == gunslinger_golden);

  // Noexcept flavors mark the forwarders and the bindings alike.
  constexpr std::string_view hair_trigger_golden = R"(  struct api {
    int fire(this auto&& self, int arg_1) noexcept {
      return self.template call<"fire">(arg_1);
    }
    bool jams(this const auto& self) noexcept {
      return self.template call<"jams">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"fire">, T& t, int arg_1) noexcept {
      return t.fire(arg_1);
    }
    static bool on(method_key<"jams">, const T& t) noexcept {
      return t.jams();
    }
  };
)";
  oss.str({});
  prox::codegen<hair_trigger>(oss);
  CHECK(oss.str() == hair_trigger_golden);

  // Overload sets, including the const pair with its requires-clause.
  constexpr std::string_view arsenal_golden = R"(  struct api {
    int issue(this auto&& self, int arg_1) {
      return self.template call<"issue">(arg_1);
    }
    int issue(this auto&& self) {
      return self.template call<"issue">();
    }
    int aim(this auto&& self, int arg_2) {
      return self.template call<"aim">(arg_2);
    }
    int aim(this auto&& self, double arg_3) {
      return self.template call<"aim">(arg_3);
    }
    int& count(this auto&& self)
    requires(requires {
      { self.template call<"count">() } -> std::same_as<int&>;
    })
    {
      return self.template call<"count">();
    }
    int count(this const auto& self) {
      return self.template call<"count">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"issue">, T& t, int arg_1) {
      return t.issue(arg_1);
    }
    static int on(method_key<"issue">, T& t) {
      return t.issue();
    }
    static int on(method_key<"aim">, T& t, int arg_2) {
      return t.aim(arg_2);
    }
    static int on(method_key<"aim">, T& t, double arg_3) {
      return t.aim(arg_3);
    }
    static int& on(method_key<"count">, T& t) {
      return t.count();
    }
    static int on(method_key<"count">, const T& t) {
      return t.count();
    }
  };
)";
  oss.str({});
  prox::codegen<arsenal>(oss);
  CHECK(oss.str() == arsenal_golden);

  // Extends with a cross-level overload: the base api is inherited by its
  // C++ type name, the hidden inherited name is merged back with a using,
  // and the numbering continues past the covered base slots.
  constexpr std::string_view armory_golden = R"(  struct api: arsenal::api {
    using arsenal::api::issue;
    int issue(this auto&& self, int arg_4, int arg_5) {
      return self.template call<"issue">(arg_4, arg_5);
    }
    void lock(this auto&& self) {
      self.template call<"lock">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static int on(method_key<"issue">, T& t, int arg_4, int arg_5) {
      return t.issue(arg_4, arg_5);
    }
    static void on(method_key<"lock">, T& t) {
      t.lock();
    }
  };
)";
  oss.str({});
  prox::codegen<armory>(oss);
  CHECK(oss.str() == armory_golden);

  // Two unrelated bases: the heavier one is inherited, the lighter one's
  // methods are redeclared, and collided names get their usings. Note the
  // base spelled by TYPE name (`gunslinger`), where a formal-name spelling
  // would also have worked here but breaks for facades like
  // `war_correspondent` itself, whose formal name is "correspondent".
  constexpr std::string_view war_correspondent_golden =
      R"(  struct api: gunslinger::api {
    using gunslinger::api::fire;
    using gunslinger::api::reload;
    std::string fire(this const auto& self) {
      return self.template call<"fire">();
    }
    void reload(this auto&& self) {
      self.template call<"reload">();
    }
    std::string byline(this const auto& self) {
      return self.template call<"byline">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static std::string on(method_key<"byline">, const T& t) {
      return t.byline();
    }
  };
)";
  oss.str({});
  prox::codegen<war_correspondent>(oss);
  CHECK(oss.str() == war_correspondent_golden);

  // The diamond, generated in the recommended single-path shape: inherit
  // the heavier chain, redeclare the lighter sibling's own method. This
  // matches the hand-written `posse_leader` api exactly.
  constexpr std::string_view posse_leader_golden =
      R"(  struct api: marshal::api {
    int claim(this auto&& self, int arg_3) {
      return self.template call<"claim">(arg_3);
    }
    void rally(this auto&& self) {
      self.template call<"rally">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
    static void on(method_key<"rally">, T& t) {
      t.rally();
    }
  };
)";
  oss.str({});
  prox::codegen<posse_leader>(oss);
  CHECK(oss.str() == posse_leader_golden);

  // A heaviest base with no `api` (`lockbox`, supported but not recommended)
  // is not inherited: the generated `api` spells every flattened forwarder
  // itself. The `boilerplate` stays empty because `vault` adds no methods of
  // its own.
  constexpr std::string_view vault_golden = R"(  struct api {
    int add(this auto&& self, int arg_1) {
      return self.template call<"add">(arg_1);
    }
    int gold(this const auto& self) {
      return self.template call<"gold">();
    }
  };
  template<typename T>
  struct boilerplate: proxy_impl_base {
  };
)";
  oss.str({});
  prox::codegen<vault>(oss);
  CHECK(oss.str() == vault_golden);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
