# Meta roadmap

Status and next steps for `corvid/meta`. Proxy-specific future work stays in
[proxy.md](proxy.md); this file tracks the shared `invocable_policy` effort
and `flexi_function`.

## In progress: proxy catch-up

`flexi_function<Sig, Policy>` generalizes `fixed_function` over
`invocable_policy` (inline-only, inline-or-heap, heap-only storage, plus the
empty-call behavior), with `fixed_function` reduced to the `inline_only`
alias. The wrapper itself is landed and green, and `proxy` now honors the
empty-call behavior (below); what remains is the rest of the shared
contract.

The pending `proxy` changes, in the order to land them:

- Landed: the vocabulary sweep in `proxy.h` / `proxy.md`. The owning
  table's mode-changing slots are `to_heap`/`to_inline` with their
  `heap_table`/`inline_table` siblings, the copy thunks are
  `heap_copy`/`inline_copy`, and the prose says "boxed" for an
  inline-to-heap move, agreeing with the shared policy vocabulary
  (inline_size / inline_align / inline_only / inline_or_heap) and the
  wrapper. Nothing in proxy says "sbo" or "re-boxed" now.
- Landed: `const_shared_proxy` and `const_weak_proxy`, the shared tier's
  guarantee-level constness, mirroring `const_proxy_view`, with a
  `details::shared_base<F, Access>` under the two shared flavors the way
  `view_base` sits under the views.
- Wanted: `try_share<T>()`, a verified `std::shared_ptr<T>` out of a shared
  handle; needs a `type_tag` on the view table. Recorded in proxy.md's
  future work.
- Ruled out (2026-08-26): `storage_mode::direct` for `proxy`. The proxy
  contract resolves a target address at every turn (views lend it, impls
  take a `T&`, `extract` and `shared_proxy` adopt the allocation), so a
  target stored nowhere has nothing to offer them, and the two payoffs
  direct brings `flexi_function` do not transfer: a proxy call is already
  one thunk with `T` known, and an empty target already costs nothing
  inline. The one allocation it would save, under `heap_only`, would break
  that policy's stable-address promise. `details::storage_mode_of` says so.
- Ruled out (2026-08-26): a reference qualifier on a `method` signature.
  `flexi_function` binds `this` on `&` and `&&` because the wrapper is the
  callable, so the two value categories are one (`std::move_only_function`'s
  rule). A facade signature serves owning, viewing, and shared handles
  through one dispatch table, and a view's or shared handle's value
  category says nothing about the target's, so an `&&` method would let
  them move out of an object they do not own; `&` could only refuse a call
  on a temporary owner. `std::function_ref` draws the same line (`const`
  and `noexcept` only), and `proxy_view` is a `function_ref`. Consuming
  operations belong in the binding or in `extract`. The `static_assert` in
  `method` stays, and its comment carries the reason.

## Landed: meta/invoke

These headers had grown large. `invocable_policy.h`, `invocable_common.h`,
`proxy_codegen.h`, `flexi_function.h`, and `fixed_function.h` now live in
`corvid/meta/invoke/`, with `proxy.md` and `flexi_function.md` beside them;
the includers, tests, and the `meta.h` umbrella followed. The subdirectory
has no umbrella of its own, and the proxy family stays out of `meta.h` as
before.

`proxy.h` split along its regions into `proxy_common.h` (method and key,
facade, registration and binding, dispatch, `Proxiable`, API validation;
the owning-table machinery stays here because it is built inside the same
table builder as the view table), `proxy_view.h` (the two views and
`make_proxy_view`), `owning_proxy.h` (`proxy` and `make_proxy`), and
`shared_proxy.h` (`shared_proxy`, `weak_proxy`, `make_shared_proxy`), each
including the one before it, plus the `proxy.h` umbrella over the whole
family. The division gives one (or sometimes two) classes per header with
the mechanics in their own, which is what makes the code manageable to
maintain and to read; the handles still work as a unit, so a user includes
`proxy.h` rather than picking and choosing. Each header exports to
`corvid::meta` the names it defines, and `proxy_codegen.h` needs only
`proxy_common.h`.

## In progress: shared code between proxy and flexi_function

Both owners answer the same questions about a target and a result, and the
answers had been written twice. The shared parts now live in
`invocable_common.h`, leaving `invocable_policy.h` as the policy and its
`details`:

- `empty_call_traits<R>`: `is_silenceable`, `is_nothrow_silenceable`, `admits`
  (one behavior exactly, given `noexcept`), `resolve_floor` (proxy's
  mildest-at-or-above rule), and `invoke<Behavior>` (the empty call
  itself). `flexi_thunks::empty_invoke_impl` and
  `method_traits_base::make_empty_thunk` are one-line wrappers over it, and
  `method_traits_base::empty_behavior` forwards to `resolve_floor`;
  `flexi_function`'s three `static_assert`s keep their messages and read
  the two bools. Truth table in meta_test.cpp.
- `constant_fn`, `runtime_fn`, and `is_runtime_fn_v`, moved verbatim.
- Namespaces, on proxy's pattern. The shared vocabulary is
  `corvid::meta::invocables` (not inline) with `policy_details` folded into
  its `details`, and `flexi_function.h` and `fixed_function.h` are
  `corvid::meta::flexi` (not inline) with `fn_details` likewise. Each
  header ends in an Exports region using-declaring only what a caller
  spells at a call site (`invocable_policy`, `on_empty`, `constant_fn`,
  `runtime_fn`; `flexi_function`, `fixed_function`, `fixed_function_of`,
  the two `is_` traits), and an exported name carries its own qualifier
  because the wide namespace no longer supplies one. `invocable_alloc`
  became `storage_policy` with the member `storage`, unexported: a caller
  uses the fluent starting points. `flexi` and `prox` open with
  `using namespace invocables;`.
- `method` and `method_traits` sit on `signature_traits` instead of
  re-matching the signature: one primary each (the four `const` x
  `noexcept` specializations of both are gone, `method_traits` recovering
  its parameter pack through a defaulted `FunctionT = M::function_t`, as
  `flexi_function` does). A ref-qualified method signature is now refused
  by a `static_assert` rather than an incomplete type. The naming rule that
  settled alongside: a value taken as a parameter is an enum, so it is
  self-documenting (`const_qual` for `rank_probe`, `noexcept_spec` for
  `empty_call_traits`), while a traits class demuxes its input into
  properties and may expose those as bools with an `is_` prefix and no `_v`
  (`is_const`, `is_noexcept` on `signature_traits` and `method`;
  `is_silenceable`, `is_nothrow_silenceable`, `is_nothrow<Behavior>` on
  `empty_call_traits`; `is_noexcept`, `is_invocable<F>`,
  `is_callable_through<Self>` on `flexi_thunks`). The `_v` suffix stays on
  namespace-scope variable templates that mirror std. A non-bool sentinel
  gets a noun: `flexi_thunks::refusal`. Landed since:
  `ConstOnly` and `view_base`'s flag are `access_mode` (a handle declares
  how it accesses, a different fact from a qualifier found on a signature,
  and the enum keeps the two apart), and the owning-table builders take
  `storage_mode` in place of `bool Sbo`, with `details::storage_mode_of` the
  one place proxy chooses a mode. `allocation_mode` is now `storage_mode`
  (the enum says where a target is kept, and in two of its three values
  nothing is allocated; the old name also invited confusion with container
  allocators), the two mode-choosing functions are `storage_mode_of`, and
  the parameters that carry the enum are `StorageMode` (proxy) and
  `SourceStorage` (flexi), so they no longer share a generic `Mode` with
  `access_mode`. The bool
  predicates and constants now all lead with `is_`/`has_`/`are_`
  (`is_all_bound`, `is_bound`, `is_exact`, `is_viable`, `is_name`,
  `is_nothrow_args_v`, `is_legal_overload_pair`, `is_entry_listed_once`,
  `are_base_boilerplates_visible`, `has_exact_args`,
  `have_same_chain_owners`, `is_declared_in`, `is_direct_eligible`,
  `is_inline_eligible`). The non-bool `_v` class constants (`count_v`,
  `name_v`, `none_v`, `ambiguous_v`, `base_count_v`) keep the suffix, as
  ruled: `fixed_bitset` and the ECS use it on their count constants too, so
  it is the repo's convention for a class-scope constant, and the `is_`
  prefix was what carried the meaning for the bools. The two owners' raw
  storage members are both `storage_area_`, distinct from
  `invocable_policy::storage` (see the next bullet).
- The pass over `proxy.h` and `flexi_function.h` for further overlap,
  factoring only where the two coincide (the proxy handles also vary among
  themselves, so not everything that looks alike is the same thing), found
  four borders and landed them. `invocable_policy` answers
  `admits_inline()`, `admits_heap()`, `buffer_size()`, and `buffer_align()`,
  replacing the `storage` comparisons spelled at every site and the buffer
  geometry each owner derived for itself. The adoption rule is one
  statement, `details::adoption_of` (over `fits_inline`, the value-level
  eligibility test `is_inline_eligible` now calls), answering with an
  `adoption` route: `flexi_thunks::lifespan_impl` and `proxy::do_adopt`
  (one switch, replacing `do_take_inline`/`do_take_heap`, its
  `adoption_for` reading the table's `relocate`/`to_inline`/`size`/`align` as
  the erased arrival's witnesses) act on the route, and both `can_adopt`s
  are `!= refuse`. The housekeeping primitives live in invocable_common.h
  as `destroy_inline`, `destroy_heap`, `relocate_inline`, `box`, and
  `unbox`: proxy's owning tables point at them (in place of `sbo_destroy`,
  `heap_destroy`, `sbo_relocate`, `sbo_to_heap`, `heap_to_sbo`), and
  flexi's `move_inlined`/`move_dynamic`/`do_destroy` call them. Both owners
  keep a `details::storage_area<Size, Align>` union named `storage_area_`
  (the byte-array/union distinction that separated the names is gone, as
  ruled), and flexi's thunks work over the erased `void*` throughout, so
  the heap pointer is read and written as the pointer it is instead of
  through a `reinterpret_cast` of the byte array. Left alone, as different
  things or bare one-liners with their own messages: the empty-state
  reinstall after a move, the policy-validity and store-time asserts, and
  the accessors flexi has and proxy lacks. Review notes, applied: the two
  `details` namespaces take the shared `details` in whole with one
  using-directive (symbol-by-symbol is for publishing outward, not for
  pulling a `details` into another); the shared housekeeping primitives are
  typed on the target (`T*` in, a `void*` destination only for raw buffer
  space), flexi's thunks re-type the erased storage at entry (`stored_fn`
  returns `F*`, and `store_block` is the one place a block's address is
  erased into the pointer slot), and proxy erases at its table slots with
  lambdas, the last place the type is seen; the `storage_area` pointer is a
  `void*` because the storage is one member of an owner that holds any
  target type, so neither owner can name the type there. The shared headers
  no longer enumerate their callers as "both owners": they are lower than
  `proxy` and `flexi_function` and must not presume to know who uses them
  (a copyable sibling of `flexi_function` is a live prospect). flexi's
  pointer-slot `static_assert` covers every policy with a buffer, since
  the union would otherwise silently grow to a pointer's size. Both owners
  report inline capacity as 0 under `heap_only` (`proxy::inline_size` now
  agrees with `flexi_function::inline_size`, renamed from `storage_size`
  for the same reason): the pointer kept in the buffer's place is not
  capacity, and `invocable_policy::buffer_size()` is the byte count for
  anyone who wants that instead. The shared `invocables::details` is now
  `invocables::implementation`: a `details` namespace is private to its
  file, so importing from one is a smell, while working parts an owner
  builds on are semi-private and may be imported whole.

## Landed: empty calls for proxy

An empty `proxy` now runs its policy's `on_empty` behavior instead of being
undefined to call through, and the policy-less handles have a defined
behavior too. The shape, with the rulings that fixed it:

- One empty table per (facade, `on_empty` value), `empty_owning_vtable_for`
  over `empty_vtable_for`, whose dispatch slots hold per-method empty thunks
  and whose housekeeping slots are null. An empty handle's table pointer
  names it, so the call path has no branch, and emptiness is the pointer
  compare. Its `relocate` slot is a no-op so `target` resolves to the
  buffer rather than reading a pointer never written, and its base pointers
  lead to the same-value empty tables of the base facades, so an empty
  handle upcasts and lends through the ordinary walk.
- Best effort, per method. A facade mixes methods that vary in result type
  and `noexcept`, and composition multiplies them, so a single behavior
  applied uniformly would asymptote toward `terminate` as the method count
  grows. The policy's `empty` is therefore a floor, and each method takes
  the mildest behavior at or above it that its signature admits (`silent`
  needs a value-initializable result, nothrow under `noexcept`; `raise`
  needs a method that may throw; `terminate` needs nothing). This is the
  one place proxy departs from `flexi_function`, whose single signature is
  chosen deliberately and whose static_asserts name the alternatives.
- `policy_enforcement`, a single flag in place of `runtime_fn_policy`,
  because both were the same intent: strict or lenient enforcement of the
  policy, with what strictness rejects depending on the owner. For
  `flexi_function` it is the bare-pointer refusal at the border; for
  `proxy` it is any method that would take a behavior other than the floor
  itself, reported per method through one `empty_fit_check` instantiation
  per slot, so an audit sees every offender at once rather than the first.
- Views, `shared_proxy`, and `weak_proxy` carry no policy, so a handle built
  empty (or emptied by a move, or an expired `lock`) is on the `raise`
  table: `terminate` is harsh where an exception is possible, and `silent`
  would hide an error. (Rust is the precedent: a trait object is never
  empty, absence is `Option`, and `unwrap` on `None` panics, which unwinds.)
  A view or shared proxy lent or adopted from an empty owner mirrors that
  owner's behavior, because the owning table already embeds the view table
  and the handle conversions read it. Two consequences: two empty views of
  one type can behave differently by provenance (the same fact the birth
  key already established), and a failed downcast of an empty handle yields
  a handle built empty, since an empty table has no ancestry to search.
- `shared_proxy`'s moves are now user-defined, so a moved-from handle is on
  the `raise` table rather than keeping its live table with a null target.

Coverage: "Empty proxies honor on_empty" and "Empty views and shared proxies
raise, or mirror their lender" in proxy_test.cpp, with the strict-enforcement
refusal captured as a comment.

## Landed: qualified signatures for flexi_function

`flexi_function` accepts the twelve `cv ref noexcept` signature variants
that `std::move_only_function` does, with the same meaning, and without its
twelve partial specializations (libstdc++ stamps one body header six times
under `cv`/`ref` macros, each templated on the `noexcept` flag). The shape,
for reference when proxy catches up:

- Pattern-match once. `signature_traits<Sig>` in meta/traits.h holds the
  twelve trivial partial specializations over a shared base supplying
  `result_t`, `args_t` (a tuple), `function_t` (every qualifier stripped,
  `noexcept` included), and one enum-typed constant per axis:
  `const_qualifier` (`const_qual`), `ref_qualifier` (`ref_qual`, three-way),
  and `noexcept_specifier` (`noexcept_spec`). The undefined primary is the
  gate for what counts as a signature. The class body sits behind one
  specialization on `function_t`, reached through a defaulted third template
  parameter, `FunctionT = signature_function_t<Sig>`, and `flexi_thunks`
  does the same.
- Invoke as the standard does. The thunks apply the signature's qualifiers
  to the stored target (`qualified_target_t<F>`: `F cv&`, or `F cv&&` under
  `&&`) and constrain construction on invocability through that type
  (`is_invocable<F>`, the nothrow variant under `noexcept`). An unqualified
  and an `&` signature invoke the target identically; they differ only in
  which wrappers may call.
- One call operator. A deducing-`this` member with a `requires` clause
  (`is_callable_through<Self>`) admits exactly the wrapper constness and value
  category the signature permits, and is `noexcept` exactly when the
  signature is.
- Constrain, do not deduce. A `noexcept` signature does not follow the
  stored callable's own `noexcept` (type erasure cannot); it admits only
  nothrow-invocable callables and gets a `noexcept` call operator in
  exchange.
- Reconciled with `on_empty`. A `noexcept` signature refuses `raise` at
  compile time, admits `silent` only for a nothrow-value-initializable (or
  `void`) result, and always admits `terminate`. `flexi_function` also
  refuses `silent` on a result that cannot be value-initialized at all;
  proxy keeps its per-method fallback to `raise`, since a facade mixes
  methods. Termination on an empty call is never reached by accident, only
  asked for by name.

Coverage: truth tables in meta_test.cpp (decomposition) and
flexi_function_test.cpp (which callables are admitted, which wrappers may
call, and `noexcept`).

Qualified signatures landed before the proxy catch-up, as sequenced, and
`method` has since adopted the `signature_traits` vocabulary (see "In
progress: shared code" above).

## Landed: direct calls for flexi_function

A stored function pointer costs two indirect calls per invocation: the
erased invoke thunk, then the call through the pointer it loads from the
buffer. The second is removable only when the target is in the type rather
than in the data, and a captureless lambda that names the function already
put it there; the thunk generated for that closure type calls the function
outright. So the landed design is not a new construction path but a
storage mode plus two spellings, with the mechanism in
[flexi_function.md](flexi_function.md):

- `storage_mode::direct`, beside `inlined` and `dynamic`. A target whose
  type is `is_direct_eligible` (no data members, trivial default construction
  and destruction) is stored nowhere: the invoke thunk names an instance
  the object model requires and the optimizer erases, the lifespan thunk
  has nothing to size, destroy, or move, and every policy, `heap_only`
  included, allocates nothing for it. `size()` reports 0. The lifespan
  refusal sentinel moved off 0 (`flexi_thunks::refusal`) to make that
  honest.
- `constant_fn<Fn>`, a direct-eligible functor over a compile-time
  constant: a function, a member pointer (object as the first argument),
  or a captureless lambda. It is the C++23 stand-in for what C++26's
  `std::function_ref` takes as `std::constant_wrapper` (P3740 retired
  `std::nontype`), and `flexi_function` knows nothing about it; it is just
  a stateless callable. `runtime_fn{p}` is its counterpart for a pointer
  that really is a runtime value, nullable, stored and called through like
  a bare pointer.
- Two border refusals, both `static_assert`s in `do_store`. A function
  lvalue (`f = foo;`) is always refused, since it is the one spelling the
  type system can tell from a runtime pointer; `&foo` cannot be told from
  `get_handler()`. A policy with `policy_enforcement::strict` refuses bare
  function and member pointers too, so the caller must pick `constant_fn` or
  `runtime_fn`. Moves and sibling adoption never re-check. Flipping the
  field's default and rebuilding is a one-edit audit.

Landed alongside, from the same conversation: the template order is now
`flexi_function<Sig, Policy = invocable_policy::basic>` and
`fixed_function<Sig, Size = invocable_policy::fixed.size()>`; and
`invocable_policy` is spelled fluently from `basic`, `heap`, and `fixed`
through `with(on_empty)`, `with(policy_enforcement)`, `with_alignment`,
`with_size` (instance bytes, rounded up at the buffer alignment),
`with_storage_size`, and `size()`.

## Speculative: cache-line-sized wrappers

Not ruled on; recorded so the constraint is understood before anyone
trips over it.

The `inline_size` rule ("a multiple of `inline_align`") reasons about the
buffer in isolation, but a buffered `flexi_function` carries a two-pointer
header ahead of the buffer, so "buffer is a multiple of the alignment" and
"object is a multiple of the alignment" are different constraints. The
difference only shows at alignments above the default: a wrapper meant to
occupy exactly one 64-byte cache line (a per-core callback slot, say) wants a
48-byte buffer at 64-byte alignment, which the rule rejects; the nearest
legal policy is a 64-byte buffer, an 80-byte object, and `sizeof` rounded to
128.

If that shape is ever wanted, the rule would change to "the object size is a
multiple of `inline_align`" (`header + inline_size` padded), which is what
`fixed_function`'s `Size` already means. `heap_only` has no such need: its
three-word layout is padding-free, and line isolation for any wrapper is the
container's job (`alignas` on the element or enclosing struct), not the
policy's.
