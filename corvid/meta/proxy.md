# Proxy design

Status: phases 1 through 3 built and tested ([proxy.h](proxy.h),
[proxy_test.cpp](../../tests/portable/proxy_test.cpp)): the `api` mixin, its
`validate_api` drift check, and `extends<Base>` composition with upcasting.
Plan for `corvid/meta/proxy.h`, a
registration-based runtime-polymorphism ("proxy") system: type-erased handles
over an interface definition, without inheritance, vtable pointers in the
target type, or macros.

## Lineage and positioning

Prior art is [ngcpp/proxy](https://github.com/ngcpp/proxy) (formerly
microsoft/proxy, on the standards track as P3086) and Rust trait objects
(`dyn Trait`). We lean toward ngcpp naming because this is C++, but diverge
on one deliberate axis: conformance is nominal (registered), not structural
(duck-typed). ngcpp accepts any type whose members happen to match the
facade; we require an explicit registration, the same philosophy as the
Corvid registered-enum system (registration, not reflection). Registration
also dissolves ngcpp's need for macros: their `PRO_DEF_MEM_DISPATCH` macros
exist solely to mint accessor functions with caller-chosen names, and when
the user writes the binding explicitly there is no name to mint.

The one-method ancestor within Corvid is
[fixed_function.h](fixed_function.h): a `proxy` is a `fixed_function`
generalized from a single anonymous `operator()` to a named suite of
methods, and the owning flavor reuses the same storage ideas (inline SBO
buffer plus dispatch pointer).

## Naming map

| Corvid                        | ngcpp/proxy              | Rust                 |
| ----------------------------- | ------------------------ | -------------------- |
| `proxy<F>`                    | `pro::proxy<F>`          | `Box<dyn T>`         |
| `proxy_view<F>`               | `pro::proxy_view<F>`     | `&mut dyn T`         |
| `const_proxy_view<F>`         | (none)                   | `&dyn T`             |
| `facade`                      | facade (`facade_builder`)| `trait`              |
| `method<"fire", void(int)>`   | dispatch + convention    | trait method         |
| `proxy_impl<F, T>`            | (none; structural)       | `impl Trait for Type`|
| `corvid_proxy_spec(F*, T*)`   | (none)                   | empty `impl` block   |
| `make_proxy_spec<F, T>()`     | (none)                   | (spec payload)       |
| `"name"_method` UDL           | (none)                   | (none)               |
| `Proxiable<T, F>`             | `proxiable<P, F>`        | `T: Trait` bound     |
| `make_proxy<F, T>(...)`       | `make_proxy`             | `Box::new`           |
| `make_proxy_view<F>(t)`       | `make_proxy_view`        | `&x as &dyn T`       |
| `extends<Base>`               | `add_facade`             | supertrait           |
| (internal) dispatch table     | meta                     | vtable + drop glue   |

Naming notes:

- Concepts follow the house PascalCase convention: `Facade`, `Proxiable`,
  `ProxyRegistered`.
- `facade` over `trait`: matches ngcpp, and "trait(s)" is irreversibly
  loaded in C++ (`std::char_traits`, type traits).
- `method` collapses ngcpp's dispatch (the what) and convention (the
  signature) into one entity; we do not support overload sets per name in
  the MVP.
- Nominal identity lives at the facade level: `proxy_impl<F, T>` is keyed on
  the pair, so declaring `method<"draw", ...>` inline in two unrelated
  facades cannot cross-contaminate. Conforming to a `weapon` facade says
  nothing about a `canvas` facade, even if both have a `"draw"`.

## User-facing shape

Conformance is tiered. The facade author writes a boilerplate impl
once: a class template named `boilerplate`, nested in the facade body,
generic over any registered `T`, forwarding each method to the natural
member name. Only the facade author can write it, because only code written
with the names in view can spell `t.fire`. A library-provided constrained
partial specialization of `proxy_impl` (a C++20 partial specialization with
the primary's own argument list, legal because it is more constrained)
delegates every registered pair to the nested boilerplate, so the facade
needs no namespace-scope impl at all and the boilerplate sits next to the
method list and `api` it mirrors. A conforming type whose method names line
up then costs one registration line; a type whose names do not line up
carries its own impl in the registration. The namespace-scope spelling of
the boilerplate, a `proxy_impl` partial specialization gated on
`ProxyRegistered`, predates the nested form and remains equivalent and
supported (it also outranks the library's delegation by partial ordering, so
the two styles cannot collide).

```cpp
// The facade: the interface definition, carrying its own boilerplate.
struct gunslinger : facade<method<"fire", void(int)>,
                       method<"reload", bool()>> {
  // Written once by the facade author. `on` is the fixed hook name,
  // overloaded on the method key; a fixed name is what keeps the mechanism
  // spellable without macros. Inheriting `impl_base` is optional sugar,
  // supplying the `method_key` alias so the bindings spell it unqualified.
  template<typename T>
  struct boilerplate : impl_base {
    static void on(method_key<"fire">, T& t, int rounds) { t.fire(rounds); }
    static bool on(method_key<"reload">, T& t) { return t.reload(); }
  };
};

// Conforming a type whose methods line up: pure registration. The ADL
// hook mirrors `corvid_enum_spec`; declare it in the namespace of either
// the facade or the type.
consteval auto corvid_proxy_spec(gunslinger*, lawman*) {
  return make_proxy_spec<gunslinger, lawman>();
}

// Conforming a type whose methods do not line up: the registration carries
// the impl, here local to the hook itself, so the whole conformance is one
// self-contained declaration.
consteval auto corvid_proxy_spec(gunslinger*, robber*) {
  struct as_gunslinger : impl_base {
    static void on(method_key<"fire">, robber& r, int rounds) {
      r.shoot(rounds);
    }
    static bool on(method_key<"reload">, robber& r) { return r.rearm(); }
  };
  return make_proxy_spec<gunslinger, robber, as_gunslinger>();
}

proxy<gunslinger> p = make_proxy<gunslinger, lawman>(/*ctor args*/);
p.call<"fire">(3); // Core spelling: compile-time name -> slot lookup.
```

The registration-carried impl is the spec's first knob: the three-type
`make_proxy_spec<F, T, Impl>()` returns a `proxy_spec` whose `impl_t` names
the binding class, a library partial specialization of `proxy_impl` installs
it for the pair (`SpecCarriesImpl` is the detecting concept), and it
outranks the facade's boilerplate as the closer declaration. Registration is
therefore the sole act of conformance, with fewer distinctions between the
routes: every binding is either the facade's boilerplate or a carried impl,
and both are opt-in. The unregistered per-type full specialization of
`proxy_impl`, which was the original wrong-names tier, is subsumed by
carried impls and dropped from the supported surface: it offered nothing the
carried impl does not (foreign types take a hook in the facade's namespace;
generic families take a constrained template hook, like `strongbox` in the
test), while uniquely enabling conformance with no opt-in declaration and
inviting ODR mischief, since any TU could silently override another's
binding. The language cannot forbid such specializations, and `proxy_impl`
necessarily stays a public name for the namespace-scope boilerplate
spelling, so this is a contract boundary rather than a mechanical one. A
namespace-scope boilerplate partial should add `!SpecCarriesImpl<F, T>` to
its gate to preserve carried-impl precedence, since raw partial ordering
would prefer it (the nested boilerplate gets this arbitration from the
library).

The binding class can live anywhere a type can. Local to the hook, as
above, the registration is fully self-contained, which is also the only
self-contained way to conform a type you do not own; this is sound because
local classes are ordinary template arguments, their static member
functions are ordinary runtime functions even inside a consteval hook, and
a consteval function is implicitly inline, so the local type is
ODR-consistent across translation units (local classes do forgo static data
members and member templates, which bindings do not need). Nested in the
type it serves, the impl additionally reaches the type's private members
(`turncoat` in the test). Namespace scope works too, but buys nothing over
the other placements.

Whatever its placement, a binding class may inherit `prox::impl_base`, an
otherwise-empty base whose one member is a `method_key` alias, so the
bindings spell the key unqualified (base-class members participate in
unqualified lookup where namespace-scope names do not; a literally empty
base would change nothing). It is optional, and a binding class that
inherits a boilerplate already has it through that base.

The string NTTP rides on the existing
[fixed_string.h](fixed_string.h). `call<"fire">` resolves at
compile time to an index into the facade's method list; no runtime name
lookup exists anywhere.

### Partial override of the boilerplate

When a type's names line up except for one method, a full custom impl
re-spells every binding just to change one. The nested boilerplate, being an
ordinary inheritable class template, provides a cheaper middle tier for
free: a near-conforming type registers a carried impl that inherits
`F::boilerplate<T>`, re-exposes its `on` overloads with a using-declaration,
and declares only the divergent binding:

```cpp
// `sheriff` lines up except that `fire` is spelled `shoot`.
consteval auto corvid_proxy_spec(gunslinger*, sheriff*) {
  struct as_gunslinger : gunslinger::boilerplate<sheriff> {
    using gunslinger::boilerplate<sheriff>::on;
    static void on(method_key<"fire">, sheriff& s, int rounds) {
      s.shoot(rounds);
    }
  };
  return make_proxy_spec<gunslinger, sheriff, as_gunslinger>();
}
```

Mechanics: a derived `on` with the identical parameter list excludes the
inherited one from the set the using-declaration introduces, so the
override wins with no ambiguity. The using-declaration is load-bearing:
without it, the derived `on` hides all the inherited overloads and
conformance fails on the rest. The hidden base binding is never
instantiated (class-template members instantiate only on use), so its body
naming the absent member is harmless.

Before the boilerplate moved into the facade, this tier required the facade
author to factor the bindings into a separately-named class for the
namespace-scope partial to derive from; with the nested form it is inherent.
It overlaps in purpose with the spec-carried member-pointer binding sketched
under Future; the two can coexist. Exercised by `sheriff` in the test.

### Member-call sugar (the `api` mixin, built)

`p->fire(3)` spelling cannot be minted by a C++23 library without macros.
The answer is an optional hand-written `api` mixin on the facade, one
forwarding line per method, using deducing `this`; all three handles
(`proxy<F>`, `proxy_view<F>`, `const_proxy_view<F>`) inherit `F::api` when
present:

```cpp
struct gunslinger : facade<method<"fire", void(int)>,
                       method<"reload", bool()>> {
  struct api {
    void fire(this auto&& self, int n) { self.template call<"fire">(n); }
    bool reload(this auto&& self) { return self.template call<"reload">(); }
  };
};
// ...
p.fire(3);
```

This is the "accept a limitation ngcpp will not" trade: they generate these
accessors with macros; we write them, once per facade (not per conforming
type). Alternatives considered and rejected: `->*` sugar (unbearable), tag
objects as free-function customization points (free-function call syntax
reads as C), `p()<"fire">(3)` (grammatically impossible: explicit template
arguments may only follow a name that names a template, so the expression
parses as chained relational operators). A nearby legal family exists via a
string-literal UDL operator template producing a key object,
`p("fire"_k, 3)` or `p["fire"_k](3)`; recorded as alternates in case the
mixin disappoints.

Mechanics of the built form: `details::api_base_t<F>` yields `F::api` when
the facade defines one and an empty `no_api` stand-in otherwise. The
selection is a lazy specialization rather than a `std::conditional_t`,
because naming `F::api` when it does not exist is ill-formed. The views pick
the base up through their shared `details::view_base`, keeping each view a
single-inheritance chain; the owning `proxy`, which has no other base,
inherits it directly. Deducing `this` still sees the complete handle type
regardless of where in the hierarchy the forwarders sit. The mixin is
stateless, so empty-base optimization keeps the views at two pointers.

Caveats, all on the facade author's side of the contract:

- Forwarders should declare concrete return types, not `decltype(auto)`: a
  deduced return type forces body instantiation during mere overload
  resolution, which turns misuse into errors in contexts that only probe.
- Plain forwarders are unconstrained declarations, so deep const is enforced
  inside the forwarder's `call` (a clear hard error at the point of use)
  rather than at overload resolution. A `requires` probe of the sugar on a
  const handle therefore succeeds where the same probe of `call<>` fails. A
  facade author who wants probe-visible sugar can add a trailing
  requires-clause repeating the `call` expression.
- The `noexcept` qualifier does not propagate through an unmarked forwarder;
  the author marks the forwarders of noexcept methods `noexcept` themselves
  (see `hair_trigger` in the test). Nothing checks the forwarders against
  the method flavors; the sugar is by-hand by design.

The silent-drift half of the risk (a hand-written forwarder whose types are
merely convertible to the facade's, which compiles and truncates) is closed
by `validate_api`, below.

Hosting the `api` inside `proxy_impl` (grouping the sugar next to the `on`
bindings) was considered and rejected. The erased handle knows only `F`, so
its sugar base must be nameable from `F` alone, while `proxy_impl` is keyed
on the (facade, type) pair; the only workaround is a sentinel
specialization like `proxy_impl<F, void>`, a magic convention that corrupts
the impl's contract. Ownership also differs: impls have two authors (facade
author's boilerplate, third parties' custom impls), while the `api` is
facade-authored and singular; per-impl copies could drift into inconsistent
sugar for one interface. The facade body keeps "one facade, one sugar"
structural. The `api` duplicates only the name spelling (the third of the
three-spellings-per-method floor), never the bindings: its methods forward
through `call<>` and the same dispatch table.

### API validation (`validate_api`, built)

The `api` is verified against the facade's method list while spelling
neither the names nor the signatures again, and correctness is opt-out
rather than opt-in: `make_proxy_spec` runs the check at every registration
of an `api`-bearing facade. Registration is the right moment because it is
the first time all three artifacts are necessarily in view: the concrete
type motivates the facade, the facade carries the `api`, and the
boilerplate impl (which the registration exists to unlock) must already be
visible. A facade whose `api` deliberately deviates (say, a widening
convenience signature)
registers with `api_check::off`; a registration hook that is itself a
template defers the check to its own instantiation. The standalone
spelling, `static_assert(prox::validate_api<F>());`, remains for a facade
author to assert at the definition site, before any registration exists.
Handles themselves perform no check: embedding one there would make a
handle's validity depend on which impl headers a TU happens to include, and
would detonate in arbitrary consumer code for what is the facade author's
bug.

The insight is that two independent hand-written respellings of the
name-to-key binding already exist: the boilerplate impl invokes members by
natural name (`t.fire(rounds)`), and the `api` declares members with those
names. The check plays them against each other. `validate_api` instantiates
the dispatch table for a library-internal probe type (`details::api_probe`)
that inherits `F::api` and exposes a deliberately strict `call`: argument
types must match the facade's declared parameters exactly (after stripping
cv and references, so value-category spelling is ignored but a
merely-convertible type is rejected), and the result is a `strict_result`
that converts only to exactly the declared result type. The chain, thunk ->
boilerplate `on` -> `api` forwarder -> strict `call`, is anchored to the
facade's exact types at both ends, so convertibility drift anywhere in the
middle fails to compile with the error pointing at the drifting line. It
validates the boilerplate as much as the `api`; real conforming types never
would, since real calls convert legally. The probe is the one type the
library registers itself (a generic `corvid_proxy_spec` overload in
`details`), which is what admits it to the registration-gated boilerplate;
that registration passes `api_check::off`, since a validating one would
recurse into itself through the boilerplate-visibility check.

Caught: a missing or misspelled forwarder, wrong arity, wrong const flavor
of `self`, a parameter or declared result type that is merely convertible
to the facade's (including the silently-truncating kind), and a forwarder
body dispatching a key with a different signature. Not caught: a missing
`noexcept` on a forwarder (degrades `noexcept(p.fire(1))`, not behavior);
by-value versus by-reference parameter spellings (an extra copy, not a
bug); reference-to-value decay of a declared result (a conversion operator
cannot distinguish binding a reference from copying out of one); and a body
dispatching the wrong key with an identical signature, which no shape check
can see. Closing that last hole would take a behavioral probe (record which
key each forwarder dispatches and compare) or C++26 reflection, which
deletes the whole problem by generating the `api` from the facade.

Two structural limits: failures are hard compile errors rather than a
`false` (a fully generic `FulfillsApi<H, F>` concept is impossible in
C++23, since no mechanism turns a `fixed_string` into an identifier to
probe `h.fire(...)`), and the facade must have a boilerplate impl (a nested
`boilerplate`, or a namespace-scope `proxy_impl` partial gated on
`ProxyRegistered`) for the chain to exist, since that impl is the only
artifact that invokes the members by name. A registration that cannot see
such a boilerplate fails a friendly `static_assert` that names the opt-out;
a nested boilerplate is visible wherever the facade is, so only the
namespace-scope spelling can trip it.

### Composition (`extends`, built)

A facade extends others by listing `extends<Base>` entries alongside its
methods, conventionally first:

```cpp
struct marshal : facade<extends<gunslinger>,
                     method<"arrest", bool(int)>> {};
```

The derived facade's effective method list is the flattening of its bases'
lists, in declaration order, followed by its own, and every handle of the
derived facade dispatches inherited and own methods alike. Names must be
unique across the flattened list, enforced by a `static_assert` detonator at
first use of the facade's machinery: a facade cannot redeclare (or override)
an inherited method, and diamonds are rejected. Both restrictions have
relaxation paths recorded under Future.

Conformance is per facade, as with Rust supertraits: `Proxiable<T, marshal>`
requires `marshal`'s own methods bound through `proxy_impl<marshal, T>` plus
`Proxiable<T, gunslinger>`, and the derived boilerplate spells only the new
methods. The alternative, one derived impl covering the whole flattened
list, was rejected because it lets a directly-built `proxy_view<gunslinger>`
and an upcast one dispatch different bindings for the same method. Per-facade
binding defines each inherited method's behavior exactly once, so the two
are identical by construction.

Registration does not multiply with the chain, though: the idiomatic
spelling is a single template hook constrained on `InChainOf` (`Extends`
made reflexive and argument-flipped), which registers a type for the derived
facade and every facade it extends in one declaration:

```cpp
template<prox::InChainOf<ranger> F>
consteval auto corvid_proxy_spec(F*, texas_ranger*) {
  return prox::make_proxy_spec<F, texas_ranger>();
}
```

The hook collapses only the opt-in ceremony; the bindings stay per facade.
Chain registration is always semantically safe, because conformance to the
derived facade requires base conformance anyway. The anchor facade names the
outermost level the type conforms to, so a type conforming only partway up a
chain anchors mid-chain and is registered for that level and everything
below (`constable` in the test). A base level that needs a
carried impl (its names diverge at that level only) takes its own plain hook
alongside the chain hook; overload resolution prefers the non-template hook
for that level, and the carried impl outranks the boilerplate. A
deliberately partial registration remains expressible with plain per-facade
hooks; it produces a type that is not proxiable at the derived facade at
all, failing loudly at first use (exercised by `vigilante` in the test).

The dispatch table of a composed facade carries the flattened thunks (bases'
first), with the inherited entries copied verbatim from each base's table,
plus the address of each direct base's table for the same target type. An
inherited call is therefore the same single indexed load as an own one, and
upcasting is reading an embedded pointer (walked transitively for
grandparents), which is Rust's dyn-upcast vtable layout. ngcpp reaches the
same user-visible feature differently: upward conversion is opt-in per
composition (`add_facade<F, true>`) and works as a dispatched conversion,
where the per-type thunk manufactures the target handle.

Handles upcast implicitly, like derived-to-base pointers: `proxy_view<D>`
and `const_proxy_view<D>` convert to their `B` counterparts (and mutable to
const, never back), and an lvalue owning proxy converts to a view of its own
facade or any base, re-pointing at the stored target rather than wrapping
the handle. The proxy-to-view constructors are lvalue-only so a temporary
proxy cannot leave a dangling view, and a const proxy yields only the const
view. Viewing an empty proxy is a precondition violation, like calling
through one. The generic target constructors exclude handles of the same or
an extending facade (`details::is_handle_for`), so the re-pointing
constructors always win over wrapping a handle as a target; wrapping a
handle of an unrelated facade that conforms via a custom impl still works.

The self-conformance invariant stretches across composition: the library
bindings generalize from `proxy_impl<F, handle<F>>` to
`proxy_impl<B, handle<D>>` for any `D` extending `B`, so a derived handle
satisfies a base-facade bound (Rust: `dyn Derived` meets a `Base` bound) and
`make_proxy_view<B>` accepts and upcasts derived handles.

A derived facade does not inherit the base's `api` automatically (facade
types are unrelated as C++ types); the convention is
`struct api : gunslinger::api { ... };`, adding only the new forwarders.
Deducing `this` sees the complete handle either way, so inherited forwarders
dispatch through the derived handle's flattened table. `validate_api` runs
through composition: the probe of a facade registers for every facade it
extends, each base's boilerplate drives the inherited forwarders at the
derived probe, and the whole flattened list is checked at the derived
facade's registration.

## Mechanism

- `proxiable<T, F>` is a concept synthesized from the facade definition: for
  every `method` of `F`, `proxy_impl<F, T>::on(method_key<...>, T&, ...)` is
  invocable and returns the right type. Concepts gate (the converting
  constructor, static-dispatch template bounds); they cannot generate, since
  C++ has no introspection over requires-expressions. The facade is the
  source of truth and the concept is derived from it, never the reverse.
  Both binding routes are registration-gated, so the concept is satisfied
  exactly when the pair is registered with a usable binding (the facade's
  boilerplate or a carried impl).
- The registration slot mirrors the enum registry idiom: an ADL-found
  `corvid_proxy_spec(F*, T*)` hook returns a spec object created by
  `make_proxy_spec<F, T>()` (as `make_sequence_enum_spec` is returned from
  `corvid_enum_spec`), read through a central `auto` variable template
  `proxy_spec_v<F, T>`, with `ProxyRegistered<F, T>` as the derived
  predicate. The hook keeps the `corvid_` protocol prefix because it is
  declared in user namespaces; the maker keeps the spec type's name out of
  user code, resolving the hook-vs-type name confusion. Because either
  namespace can host the hook, a type you do not own can be conformed to a
  facade you do not own (the case Rust's orphan rule forbids). Each
  specialization of an `auto` variable template deduces its own type and
  readers detect capabilities via concepts on the spec (precedent:
  `NamedSequentialEnum` detecting `intern_name` on the enum spec), so richer
  spec types are additive, with no change to existing registrations. Maximal
  slot, minimal values. The first knob to use this is the carried impl
  (`SpecCarriesImpl`), described under "User-facing shape".
- Conversion to a proxy instantiates one thunk per method
  (`+[](void* p, args...) { return proxy_impl<F, T>::on(...); }`) and
  stores a pointer to the resulting per-`(F, T)` `static constexpr` table.
  Same cost model as a vtable call, and as Rust `dyn`: the table pointer
  moves out of the object and into the (fat) handle.
- Method signatures come in four flavors, `const` crossed with `noexcept`.
  For a `noexcept` method, conformance additionally requires the binding
  itself to be noexcept-invocable, the thunk pointer type carries
  `noexcept`, and `call` through either handle is itself conditionally
  noexcept. Supported in the MVP rather than deferred because the qualifier
  is baked into the erased ABI (the thunk pointer types), where a retrofit
  would be a break.
- The owning table carries housekeeping slots (destroy, relocate/move) in
  addition to the facade methods, the analog of Rust's drop glue.
  `proxy_view` carries none, which is why the view is built first.
- Const is handled on two axes. Every handle is deep-const as an instance,
  meaning only const-qualified methods dispatch through a const handle,
  enforced by a constraint on the const `call` overload. For the copyable
  views this is a guardrail, not a guarantee (copying a `const proxy_view`
  yields a mutable view, like copying a `T* const` to a `T*`). The guarantee
  lives in `const_proxy_view`, the `&dyn` to `proxy_view`'s `&mut dyn`,
  where constness is part of the type. It binds const and mutable targets
  alike, converts implicitly from `proxy_view` with no path back, and
  dispatches only const methods while sharing the mutable view's
  per-(facade, type) dispatch table (the non-const slots are simply
  unreachable, so no const-sliced table or index remapping is needed). The
  two views share storage and the const-method `call` through
  `details::view_base<F, Const>`. The mutable view layers the unrestricted
  non-const overload on top and re-exposes the inherited one with a
  using-declaration.
- Invariant: `proxy<F>` and `proxy_view<F>` themselves satisfy
  `proxiable<_, F>`, so generic code constrained on the facade accepts
  concrete and erased arguments interchangeably (Rust: `dyn Trait`
  implements `Trait`). Implemented as library-provided `proxy_impl`
  bindings whose `on` forwards through `call` with conditional `noexcept`
  (so the invariant survives noexcept methods). The deep-const handles'
  bindings have const and non-const overloads to match. For
  `const_proxy_view` the invariant holds exactly for all-const facades (as
  with Rust `&dyn`, whose `&mut self` methods are uncallable). Its binding's
  `on` is constrained to const methods so a mixed facade fails conformance
  cleanly at overload resolution rather than erroring during return type
  deduction.

## Alternative considered: virtual-model erasure

The external-polymorphism ("Sean Parent" runtime-concept) idiom: a hidden
abstract `concept_t` with one pure virtual per method, a hidden `model_t<T>`
whose overrides call `t_.walk()`, and a non-template handle owning the model
and forwarding through natural member names. It meets both hard requirements
(no macros, natural `p.walk()` syntax), and its per-method typing cost ties
the table design at three name-spellings (virtual declaration, model
override, handle forwarder; versus `method<>` declaration, boilerplate `on`,
`api` forwarder). Call cost also ties: a virtual call and a table thunk are
the same shape.

Rejected on structural grounds: composition (`extends`) is table
concatenation instead of multiple inheritance; facade upcasting is a table
view instead of a cross-cast; SBO relocation is a table slot instead of
virtual clone/move on a polymorphic buffer; `proxy_view` stays two plain
pointers with no embedded polymorphic object; and the `method<"...">` list
keeps the facade enumerable, which the later tiers (member-pointer specs,
formatter bridge, reflection-derived boilerplate) lean on.

A facade-holding-`T*` variant without the hidden ABC (handle templates
deriving from a forwarding facade) was also sketched. It spells each method
once, but erases nothing: handles templated on the concrete type cannot
share a container or a non-template function signature, which is static
dispatch, already free via `proxiable auto&`. The general rule: once a call
crosses an erasure boundary, the method name must be spelled on both sides
of it, plus once for member-call sugar; roughly three spellings per method
is the C++23 floor in any architecture. C++26 reflection lowers the floor
to one (derive everything from the facade declaration), in either
architecture.

## Phases

1. DONE. Facade, `method`, `method_key`, `Proxiable`, and `proxy_view`. The
   view first, because with no lifetime slots the dispatch-table synthesis
   is exercised in isolation. Verified: positive and negative conformance
   `static_assert` tests, const-qualified methods (`std::string() const`
   dispatching on `const T&`), reference returns, call-through correctness,
   heterogeneous containers, view-of-view flattening in `make_proxy_view`,
   assignment rebinding, partial override of a factored boilerplate
   (`sheriff`), and the `"name"_method` UDL (a literal operator
   template in `prox::literals`, inline since `prox` itself is not).
   Both interesting compile errors are captured as comments in the test:
   non-conformance walks the constraint chain down to `all_bound_v`; an
   unknown method name fires the `static_assert` followed by `std::get`
   index noise, accepted deliberately (a guarding `if constexpr` was tried
   and dropped as a simplification: fixing the obvious message removes the
   noise). Compile-time-only machinery is `consteval`, not `constexpr`;
   only genuinely runtime-callable paths (`call`, the converting
   constructor, `make_proxy_view`) are `constexpr`. Notes from the build:
   `fixed_string.h` originally
   lived in the strings band, which inverted the layering; it was moved to
   `corvid/meta/` (2026-07-08) and joined the `meta.h` umbrella. proxy.h
   stays out of the umbrella to limit include weight (the formatting.h
   precedent); include `corvid/meta/proxy.h` directly.
   Also, in template contexts `call` needs the dependent-name `template`
   keyword (`pv.template call<"fire">(1)`), a real ergonomic wart the `api`
   mixin would hide; ammunition for the sugar pin.
2. DONE. Owning `proxy` with SBO, plus `make_proxy` (sugar over an
   `std::in_place_type_t` constructor). Move-only, and deep-const, so only
   const-qualified methods dispatch through a const proxy, enforced by a
   constraint on the const `call` overload so the rejection is visible to
   `requires` probes (a body `static_assert` would not be, and a
   requires-expression outside a template gets no SFINAE, so the negative
   test needed a concept wrapper). Storage is a two-pointer inline buffer
   for targets that fit, are no more aligned than `std::max_align_t`, and
   are nothrow-move-constructible. Anything else is a unique-owned heap
   allocation. The owning dispatch table extends the view's with destroy
   and relocate slots. A null relocate slot marks the heap path, which
   moves by pointer steal with no target activity. Default-constructed and
   moved-from proxies are empty (`operator bool`), and calling through one
   is undefined behavior. Also in this phase: `noexcept` method flavors,
   the self-conformance invariant, deep-const `proxy_view`, and
   `const_proxy_view`, all described under Mechanism.
   Verified: lifetime-counting fixtures balancing construct/destroy/move
   on both the SBO and heap paths, move-assignment over a live target,
   emptiness after move, deep-const positive and negative probes,
   noexcept conformance both ways (a binding lacking `noexcept` fails the
   facade), `noexcept(call)` propagation, and heterogeneous ownership in a
   container.
3. DONE. The `api` mixin is DONE: `details::api_base_t<F>` selects
   the facade's nested `api` (lazy specialization, empty `no_api` stand-in),
   and all three handles inherit it. Verified: forwarder parity with
   `call<>` through the mutable view, the const view, and the owning proxy
   (const and mutable), reference returns through the sugar, no
   dependent-name `template` keyword needed in generic code, `noexcept`
   propagation when the facade author marks the forwarders, and empty-base
   optimization keeping the views at two pointers. See the caveats under
   "Member-call sugar". The `validate_api` drift check is also DONE (see
   "API validation") and runs automatically at registration via
   `make_proxy_spec`, with `api_check::off` as the opt-out (exercised by
   `mortar`/`howitzer` in the test); verified empirically that an
   `int`-to-`long long` parameter drift in a forwarder compiled silently
   before the check existed and now fails through `lawman`'s registration
   alone, at the forwarder's own line, with the diagnostic on record in the
   test. `extends<Base>` composition is DONE (see "Composition"): flattened
   dispatch, per-facade conformance, implicit upcasting of views and of
   lvalue owning proxies (including `proxy<derived>` ->
   `proxy_view<base>`), cross-facade self-conformance, `api` inheritance by
   convention, and validation through the chain. Verified: inherited and own
   dispatch through all three handles, upcast target identity (mutations
   through the derived handle visible through the upcast view, and
   reference-return address equality against a directly-built view),
   two-level chains, deep-const preservation across upcasts, negative
   conformance with a missing base registration, and the duplicate-name
   detonator; diagnostics on record in the test. A follow-up ergonomics
   round, driven by reading the test as an end user, added chain
   registration (`InChainOf`), moved the boilerplate into the facade body,
   added registration-carried impls (the delegations described under
   "User-facing shape"; precedence pinned by `turncoat` in the test),
   dropped the unregistered full-specialization tier in their favor, and
   restyled the tests to prefer the `api` sugar over `call<>` wherever a
   facade defines one. A second pass added `proxy_impl` (the unqualified
   `method_key` spelling in binding classes), made both views
   default-constructible as empty with `operator bool`, matching the owning
   proxy, and pinned mid-chain anchoring of chain hooks (`constable` in the
   test).

Header: `corvid/meta/proxy.h`, namespace `corvid::meta::prox`, deliberately
NOT inline: `facade`, `method`, and `key` are too generic to dump into
`corvid`. The call-site vocabulary (`proxy`, `proxy_view`,
`const_proxy_view`, `make_proxy`, `make_proxy_view`, `Proxiable`) is
exported into `corvid::meta` by using-declarations, so consuming code spells
`proxy_view<foo_like>` unqualified. Only authoring (facades, impls,
registration) needs `prox::`, the domain those authors already work in.
Promote to a `corvid/proxy/` family only if it sprawls. Tests:
`tests/portable/proxy_test.cpp`. `method` derives from its `key`
(subsumption: a method tag is usable anywhere its key is; also the hook for
overloading bindings on the full method if per-name overload sets ever
happen).

## Non-goals (MVP)

Copyability opt-in, shared/weak ownership, operator dispatch, conversion
dispatch, allocator plumbing, RTTI, per-name overload sets.

## Future

- C++26 reflection plus annotations (P2996/P3394) enables deriving the
  boilerplate impl itself from the facade's method list, removing even the
  facade author's forwarding lines. Registration-first is what makes this
  additive rather than a rewrite.
- `std::formatter` bridge once the formatter forwarding helper exists (see
  [../strings/roadmap.md](../strings/roadmap.md) stage 2); the ngcpp analog
  is `skills::format`.
- Spec-carried member-pointer binding: a `corvid_proxy_spec` returning a
  spec that holds `&robber::shoot, &robber::rearm`, bound positionally to
  the facade's methods. Member pointers are spellable at compile time where
  member names are not, so this is a one-line middle tier for
  name-mismatched types that avoids a full custom impl. Overlaps in purpose
  with the partial-override pattern above, which needs no new machinery but
  does need the facade author's cooperation.
- Shared ownership tier, backed by `std::shared_ptr<void>`. The control
  block already type-erases the destroyer, so a shared-backed proxy needs
  no destroy slot in the dispatch table, is copyable for free, and gets a
  `weak_proxy` nearly free via `std::weak_ptr`. ngcpp built this bespoke
  (`make_proxy_shared`, compact internal refcounts) to beat `shared_ptr`
  overhead; reusing std is our accepted trade. The plain heap fallback in
  phase 2 stays unique-owned (destroy slot, move-only, cheaper).
- Owning upcast, `proxy<D>` -> `proxy<B>`: wanted, deferred. Needs the
  base's owning table (destroy and relocate slots) reachable for an erased
  target, either embedded like the view tables or via ngcpp's
  dispatched-conversion approach. The conversion is one-way: the D-ness of
  the stored target is unrecoverable without a downcast mechanism
  (RTTI-adjacent, a non-goal), the same permanence as Rust's
  `Box<dyn Derived>` -> `Box<dyn Base>`, and part of why ngcpp gates upward
  conversion behind an explicit opt-in flag.
- Diamond composition: wanted, deferred. The semantic half is already
  collapsed, because per-facade conformance yields one `proxy_impl<Base, T>`
  no matter how many paths reach `Base` (the same effect as Rust's
  coherence rule). The blocker is mechanical: flattening duplicates the
  shared ancestor's methods, tripping the unique-name check. The fix is
  deduplicating repeated ancestor facades during flattening; the shared
  ancestor's table pointer is the same object along every path.
- Per-name overload sets via distinct keys sharing an `api` spelling:
  `method<"foo-0", void()>` and `method<"foo-1", void(int)>` with two `foo`
  forwarders overloading on the arguments, the same move as C++ name
  mangling. Nothing in the machinery forbids it today, and on inspection
  `validate_api` is compatible (it drives each key's slot independently; the
  boilerplate's natural-name call resolves against the `api` overload set,
  and convertibility drift in the selection still fails the strict probe),
  but it is unsupported until a test exercises it. ngcpp supports overloads
  natively by listing several signatures in one convention.
- Facade-qualified method names: keys are opaque strings, so a
  "gunslinger::shoot" spelling convention is available at any time. Making
  it meaningful (unqualified lookup that searches own methods then bases,
  with the qualified spelling as the collision-breaker) would relax the
  unique-name rule from a definition-time error to a call-site ambiguity,
  which is Rust's model (same-named methods across traits, disambiguated by
  fully-qualified syntax). Upcasting already provides the other
  disambiguation route, since a `proxy_view<gunslinger>` only sees the base
  list. The qualifier itself is best deferred to C++26 reflection, where
  `identifier_of` recovers the facade type's own name with no extra
  spelling; a `name<"gunslinger">` facade-list entry (parallel to `extends`)
  is the pre-reflection fallback shape, at the cost of naming the facade a
  third time.
- Overriding an inherited method in a derived facade conflicts with the
  upcast-consistency invariant (a derived handle and an upcast base handle
  would disagree about one method), so if it ever happens it has to be
  shadowing plus qualified access, not a binding-level override. Recorded as
  a tension to resolve, not a plan.

## Open questions

The member-call sugar pin was an open question until phase 3. It was
resolved in favor of the `api` mixin, and the test code exercising it
confirmed the ergonomics.

The const flavor of views was an open question until phase 2. It was
resolved as the `&T` vs `&mut T` split (`const_proxy_view` alongside a
deep-const `proxy_view`), described under Mechanism.
