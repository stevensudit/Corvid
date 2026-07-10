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
// Inheriting `prox_impl` is optional sugar: it carries the `method_key`
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
  struct boilerplate: prox_impl {
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
  struct as_gunslinger: prox_impl {
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
  struct boilerplate: prox_impl {
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
  struct as_hair_trigger: prox_impl {
    static int on(method_key<"fire">, robber& r, int rounds) {
      return r.shoot(rounds);
    }
    static bool on(method_key<"jams">, const robber&) { return false; }
  };
  return prox::make_proxy_spec<hair_trigger, robber, as_hair_trigger>();
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
  struct boilerplate: prox_impl {
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
  struct boilerplate: prox_impl {
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
  struct boilerplate: prox_impl {
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
  struct boilerplate: prox_impl {
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
// lookup ambiguous until a using-declaration pulls in one path) plus one
// padding word in every handle (same-type subobjects need distinct
// addresses, which empty-base optimization cannot paper over). There is no
// automatic merge to reach for: using-declarations cannot be pack-expanded
// over arbitrary names before reflection, and virtual inheritance would put
// a vbptr in every handle.
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
  struct boilerplate: prox_impl {
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
  struct boilerplate: prox_impl {
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
  struct boilerplate: prox_impl {
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

// Lifetime accounting shared by the owning-proxy targets.
struct life_stats {
  int constructed{};
  int destroyed{};
  int moves{};
};

// `strongbox` is a move-only lifetime-counting target. `Pad` scales the
// footprint so one instantiation fits the proxy's inline buffer and another
// forces the heap path.
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

// Registration for every `strongbox` size, via a template hook.
template<std::size_t Pad>
consteval auto corvid_proxy_spec(lockbox*, strongbox<Pad>*) {
  return prox::make_proxy_spec<lockbox, strongbox<Pad>>();
}

// One size fits the inline buffer exactly, the other forces the heap path.
using small_box = strongbox<4>;
using big_box = strongbox<64>;
static_assert(sizeof(small_box) <= proxy<lockbox>::sbo_size);
static_assert(sizeof(big_box) > proxy<lockbox>::sbo_size);

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
static_assert(!prox::method<"fire", int(int)>::noexcept_v);
static_assert(prox::method<"fire", int(int) noexcept>::noexcept_v);
static_assert(prox::method<"jams", bool() const noexcept>::const_v);
static_assert(prox::method<"jams", bool() const noexcept>::noexcept_v);

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
  struct as_census: prox_impl {
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
// inherited name (`facade<extends<gunslinger>, method<"fire", int(int)>>`)
// detonates at first use of the facade's machinery: "static assertion
// failed ... 'no_chain_collision_against<...>()': a facade cannot declare a
// method name twice or redeclare an inherited one", with the flattened slot
// list, duplicate and declaring facades included, spelled out in the
// requirement, followed by one follow-on "no type named 'vtable_t'" noise
// error. (Before the rework, the same shape failed 'unique_method_names'
// with "method names must be unique across the facade and its extends
// bases".)
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
// Diagnostic on record instead (clang 22, captured 2026-07-09):
// `wc.call<"reload">()` fires "static assertion failed due to requirement
// 'ndx != ...::ambiguous_v': ambiguous method name; qualify the key with
// the facade name", followed by the accepted "tuple index out of bounds"
// noise, matching the unknown-name case.
template<typename P>
concept CanReloadSugar = requires(P& p) { p.reload(); };
static_assert(CanReloadSugar<proxy_view<gunslinger>>);
static_assert(CanReloadSugar<proxy_view<camera>>);
static_assert(!CanReloadSugar<proxy_view<war_correspondent>>);

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

    // Move assignment over a live target destroys the old target.
    q = make_proxy<lockbox, big_box>(stats);
    CHECK(stats.constructed == 2);
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
}

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
  // Note that `wc.reload();` would be ambigous and fail at compile time.
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

// NOLINTEND(readability-function-cognitive-complexity)
