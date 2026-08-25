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

- Prefer the heap handover in adoption. A heap-stored target moving to a
  heap-admitting destination is handed over as a pointer, never un-boxed,
  even when it would fit inline; the allocation is already paid for. The
  shared policy doc already promises this; verify `do_adopt` matches and add
  a test that pins it.
- `reset()` and `nullptr` assignment, for parity with `flexi_function`.
  Both empty the proxy and reinstall its type's empty table.
- Replace the `bool Sbo` template parameters on the owning-table machinery
  (`owning_vtable_for`, `make_owning_vtable`, `make_ancestor_table`,
  `ancestor_table_for`, `ancestry_for`) with `allocation_mode`, the same
  cure applied to `flexi_function`'s `UseHeap`: a bare `bool` at a call site
  says nothing.
- Vocabulary sweep in `proxy.h` / `proxy.md`. The internal table identifiers
  (`to_sbo`, `sbo_table`, `sbo_to_heap`, `heap_to_sbo`) still say "sbo", and
  the prose still says "re-boxed" for an inline-to-heap move, which falsely
  implies the target was boxed before. The shared policy vocabulary has
  already moved to inline_size / inline_align / inline_only / inline_or_heap
  and the wrapper says "boxed"; the internals should follow. In the same
  sweep, `method` adopts the `signature_traits` enums (`const_qual`,
  `noexcept_spec`) in place of its `bool Const, bool Noexcept` flags, as
  sequenced under "Landed: qualified signatures" below.
- Honor, or explicitly document as ignored, `allocation_mode::direct` (a
  direct-eligible target stored nowhere), which landed with the direct-call
  work below and which `proxy` does not yet use.
- Once the above lands, both types share the same empty-call, handover, and
  vocabulary contracts, and the proxy.md storage-policy section can point at
  `invocable_policy.h` as the single description.

## In progress: shared code between proxy and flexi_function

Both owners answer the same questions about a target and a result, and the
answers had been written twice. The shared parts now live in
`invocable_common.h`, leaving `invocable_policy.h` as the policy and its
`policy_details`:

- `empty_call_traits<R>`: `silenceable`, `nothrow_silenceable`, `admits`
  (one behavior exactly, given `noexcept`), `resolve_floor` (proxy's
  mildest-at-or-above rule), and `invoke<Behavior>` (the empty call
  itself). `flexi_thunks::empty_invoke_impl` and
  `method_traits_base::make_empty_thunk` are one-line wrappers over it, and
  `method_traits_base::empty_behavior` forwards to `resolve_floor`;
  `flexi_function`'s three `static_assert`s keep their messages and read
  the two bools. Truth table in meta_test.cpp.
- `constant_fn`, `runtime_fn`, and `is_runtime_fn_v`, moved verbatim.

Next is a pass over `proxy.h` and `flexi_function.h` for further overlap,
factoring only where the two coincide. The proxy handles also vary among
themselves, so not everything that looks alike is the same thing; the aim
is to find the actual borders.

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
  (`invocable_v<F>`, the nothrow variant under `noexcept`). An unqualified
  and an `&` signature invoke the target identically; they differ only in
  which wrappers may call.
- One call operator. A deducing-`this` member with a `requires` clause
  (`callable_through_v<Self>`) admits exactly the wrapper constness and value
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

Qualified signatures landed before the proxy catch-up, as sequenced.
Proxy's method descriptors already carry their own `Const`/`Noexcept` flags
(`method_key`), and the empty-vtable work must reason about each method's
qualifiers, so `method_key` adopts the `signature_traits` vocabulary when
the proxy work lands rather than having it retrofitted.

## Landed: direct calls for flexi_function

A stored function pointer costs two indirect calls per invocation: the
erased invoke thunk, then the call through the pointer it loads from the
buffer. The second is removable only when the target is in the type rather
than in the data, and a captureless lambda that names the function already
put it there; the thunk generated for that closure type calls the function
outright. So the landed design is not a new construction path but a
storage mode plus two spellings, with the mechanism in
[flexi_function.md](flexi_function.md):

- `allocation_mode::direct`, beside `inlined` and `dynamic`. A target whose
  type is `direct_eligible` (no data members, trivial default construction
  and destruction) is stored nowhere: the invoke thunk names an instance
  the object model requires and the optimizer erases, the lifespan thunk
  has nothing to size, destroy, or move, and every policy, `heap_only`
  included, allocates nothing for it. `size()` reports 0. The lifespan
  refusal sentinel moved off 0 (`flexi_thunks::refused`) to make that
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
