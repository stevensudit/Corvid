# Proxy design

Status: phases 1 and 2 built and tested ([proxy.h](proxy.h),
[proxy_test.cpp](../../tests/portable/proxy_test.cpp)); phase 3
pending. Plan for `corvid/meta/proxy.h`, a
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
| `proxy_view<F>`               | `pro::proxy_view<F>`     | `&dyn T`             |
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

Conformance is two-layered. The facade author writes a boilerplate impl
once: a partial specialization of `proxy_impl`, generic over any registered
`T`, forwarding each method to the natural member name. Only the facade
author can write it, because only code written with the names in view can
spell `t.fire`. A conforming type whose method names line up then costs one
registration line; a type whose names do not line up writes its own impl,
and a full specialization beats the boilerplate partial by the ordinary
specialization-ordering rules, with no extra machinery.

```cpp
// The facade: the interface definition.
struct gunslinger : facade<method<"fire", void(int)>,
                       method<"reload", bool()>> {};

// Boilerplate impl, written once by the facade author. `on` is the fixed
// hook name, overloaded on the method key; a fixed name is what keeps the
// mechanism spellable without macros.
template<typename T>
requires ProxyRegistered<gunslinger, T>
struct proxy_impl<gunslinger, T> {
  static void on(method_key<"fire">, T& t, int rounds) { t.fire(rounds); }
  static bool on(method_key<"reload">, T& t) { return t.reload(); }
};

// Conforming a type whose methods line up: pure registration. The ADL
// hook mirrors `corvid_enum_spec`; declare it in the namespace of either
// the facade or the type.
consteval auto corvid_proxy_spec(gunslinger*, lawman*) {
  return make_proxy_spec<gunslinger, lawman>();
}

// Conforming a type whose methods do not line up: write the impl.
template<>
struct proxy_impl<gunslinger, robber> {
  static void on(method_key<"fire">, robber& r, int rounds) {
    r.shoot(rounds);
  }
  static bool on(method_key<"reload">, robber& r) { return r.rearm(); }
};

proxy<gunslinger> p = make_proxy<gunslinger, lawman>(/*ctor args*/);
p.call<"fire">(3); // Core spelling: compile-time name -> slot lookup.
```

The string NTTP rides on the existing
[fixed_string.h](fixed_string.h). `call<"fire">` resolves at
compile time to an index into the facade's method list; no runtime name
lookup exists anywhere.

### Partial override of the boilerplate

When a type's names line up except for one method, a full custom impl
re-spells every binding just to change one. The facade author can enable a
cheaper middle tier by factoring the boilerplate's bindings into a plain,
inheritable class template, with the partial specialization deriving from
it. A near-conforming type then writes a full specialization that inherits
the factored class, re-exposes its `on` overloads with a using-declaration,
and declares only the divergent binding:

```cpp
// Facade author: factor the bindings so they are inheritable.
template<typename T>
struct proxy_impl_gunslinger {
  static void on(method_key<"fire">, T& t, int rounds) { t.fire(rounds); }
  static bool on(method_key<"reload">, T& t) { return t.reload(); }
};

template<typename T>
requires ProxyRegistered<gunslinger, T>
struct proxy_impl<gunslinger, T>: proxy_impl_gunslinger<T> {};

// `sheriff` lines up except that `fire` is spelled `shoot`.
template<>
struct proxy_impl<gunslinger, sheriff>: proxy_impl_gunslinger<sheriff> {
  using proxy_impl_gunslinger<sheriff>::on;
  static void on(method_key<"fire">, sheriff& s, int rounds) {
    s.shoot(rounds);
  }
};
```

Mechanics: a derived `on` with the identical parameter list excludes the
inherited one from the set the using-declaration introduces, so the
override wins with no ambiguity. The using-declaration is load-bearing:
without it, the derived `on` hides all the inherited overloads and
conformance fails on the rest. The hidden base binding is never
instantiated (class-template members instantiate only on use), so its body
naming the absent member is harmless. As with any full specialization, no
registration is involved.

The trade is that the tier exists only if the facade author exposes the
bindings as a named class rather than writing them inline in the partial
specialization. It overlaps in purpose with the spec-carried member-pointer
binding sketched under Future; the two can coexist. Exercised by `sheriff`
in the test.

### Member-call sugar (pinned, leading candidate)

`p->fire(3)` spelling cannot be minted by a C++23 library without macros.
The leading candidate is an optional hand-written `api` mixin on the facade,
one forwarding line per method, using deducing `this`; `proxy<F>` and
`proxy_view<F>` inherit `F::api` when present:

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

Hosting the `api` inside `proxy_impl` (grouping the sugar next to the `on`
bindings) was considered and rejected. The erased handle knows only `F`, so
its sugar base must be nameable from `F` alone, while `proxy_impl` is keyed
on the (facade, type) pair; the only workaround is a sentinel
specialization like `proxy_impl<F, void>`, a magic convention that corrupts
the impl's contract. Ownership also differs: impls have two authors (facade
author's boilerplate, third parties' custom impls), while the `api` is
facade-authored and singular; per-impl copies could drift into inconsistent
sugar for one interface. The facade body keeps "one facade, one sugar"
structural. The api duplicates only the name spelling (the third of the
three-spellings-per-method floor), never the bindings: its methods forward
through `call<>` and the same dispatch table.

## Mechanism

- `proxiable<T, F>` is a concept synthesized from the facade definition: for
  every `method` of `F`, `proxy_impl<F, T>::on(method_key<...>, T&, ...)` is
  invocable and returns the right type. Concepts gate (the converting
  constructor, static-dispatch template bounds); they cannot generate, since
  C++ has no introspection over requires-expressions. The facade is the
  source of truth and the concept is derived from it, never the reverse. An
  explicit `proxy_impl` specialization satisfies the concept directly; the
  boilerplate partial satisfies it exactly when the pair is registered, so
  registration is required precisely when relying on the boilerplate.
- The registration slot mirrors the enum registry idiom: an ADL-found
  `corvid_proxy_spec(F*, T*)` hook returns a spec object created by
  `make_proxy_spec<F, T>()` (as `make_sequence_enum_spec` is returned from
  `corvid_enum_spec`), read through a central `auto` variable template
  `proxy_spec_v<F, T>`, with `ProxyRegistered<F, T>` as the derived
  predicate. The hook keeps the `corvid_` protocol prefix because it is
  declared in user namespaces; the maker keeps the spec type's name out of
  user code, resolving the hook-vs-type name confusion. Because either
  namespace can host the hook, a type you do not own can be conformed to a
  facade you do not own (the case Rust's orphan rule forbids). The MVP spec
  type carries no knobs; since each specialization of an `auto` variable
  template deduces its own type and readers detect capabilities via
  concepts on the spec (precedent: `NamedSequentialEnum` detecting
  `intern_name` on the enum spec), richer spec types are additive later,
  with no change to existing registrations. Maximal slot, minimal values.
- Conversion to a proxy instantiates one thunk per method
  (`+[](void* p, args...) { return proxy_impl<F, T>::on(...); }`) and
  stores a pointer to the resulting per-`(F, T)` `static constexpr` table.
  Same cost model as a vtable call, and as Rust `dyn`: the table pointer
  moves out of the object and into the (fat) handle.
- Method signatures come in four flavors: `const` crossed with `noexcept`.
  For a `noexcept` method, conformance additionally requires the binding
  itself to be noexcept-invocable, the thunk pointer type carries
  `noexcept`, and `call` through either handle is itself conditionally
  noexcept. Supported in the MVP rather than deferred because the qualifier
  is baked into the erased ABI (the thunk pointer types), where a retrofit
  would be a break.
- The owning table carries housekeeping slots (destroy, relocate/move) in
  addition to the facade methods, the analog of Rust's drop glue.
  `proxy_view` carries none, which is why the view is built first.
- Invariant: `proxy<F>` and `proxy_view<F>` themselves satisfy
  `proxiable<_, F>`, so generic code constrained on the facade accepts
  concrete and erased arguments interchangeably (Rust: `dyn Trait`
  implements `Trait`). Implemented as library-provided `proxy_impl`
  bindings whose `on` forwards through `call` with conditional `noexcept`
  (so the invariant survives noexcept methods); the owning proxy's binding
  has const and non-const overloads to respect its deep const.

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
   `std::in_place_type_t` constructor). Move-only, and deep-const: only
   const-qualified methods dispatch through a const proxy, enforced by a
   constraint on the const `call` overload so the rejection is visible to
   `requires` probes (a body `static_assert` would not be; and a
   requires-expression outside a template gets no SFINAE, so the negative
   test needed a concept wrapper). Storage is a two-pointer inline buffer
   for targets that fit, are no more aligned than `std::max_align_t`, and
   are nothrow-move-constructible; anything else is a unique-owned heap
   allocation. The owning dispatch table extends the view's with destroy
   and relocate slots; a null relocate slot marks the heap path, which
   moves by pointer steal with no target activity. Default-constructed and
   moved-from proxies are empty (`operator bool`); calling through one is
   undefined behavior. Also in this phase: `noexcept` method flavors and
   the self-conformance invariant, both described under Mechanism.
   Verified: lifetime-counting fixtures balancing construct/destroy/move
   on both the SBO and heap paths, move-assignment over a live target,
   emptiness after move, deep-const positive and negative probes,
   noexcept conformance both ways (a binding lacking `noexcept` fails the
   facade), `noexcept(call)` propagation, and heterogeneous ownership in a
   container.
3. `api` mixin support and `extends<Base>` composition, including
   `proxy<derived>` -> `proxy_view<base>` conversion (Rust trait
   upcasting). Verify: mixin call parity with `call<>`, upcast dispatch
   correctness.

Header: `corvid/meta/proxy.h`, namespace `corvid::meta::prox`, deliberately
NOT inline: `facade`, `method`, and `key` are too generic to dump into
`corvid`. Promote to a `corvid/proxy/` family only if it sprawls. Tests:
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

## Open questions

- The member-call sugar pin: the `api` mixin is the leading candidate
  (judged plausible); confirm once real call sites exist.
- Const flavor of views: whether `proxy_view<F>` needs a distinct
  const-view type (the `&T` vs `&mut T` split) or const-qualified methods
  suffice. The owning proxy answered its half of the question (deep const,
  in phase 2); the view remains shallow-const pending this.
