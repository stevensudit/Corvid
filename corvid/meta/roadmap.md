# Meta roadmap

Status and next steps for `corvid/meta`. Proxy-specific future work stays in
[proxy.md](proxy.md); this file tracks the shared `invocable_policy` effort
and `flexi_function`.

## In progress: flexi_function

`flexi_function<Policy, Sig>` generalizes `fixed_function` over
`invocable_policy` (inline-only, inline-or-heap, heap-only storage, plus the
empty-call behavior), with `fixed_function` reduced to the `inline_only`
alias. The wrapper itself is landed and green; what remains is bringing
`proxy` up to the same contract.

The pending `proxy` changes, in the order to land them:

- Honor `invocable_policy::empty`. An empty proxy today is undefined to call
  through (null vtable). Needs a per-facade empty vtable per policy whose
  slots raise `std::bad_function_call` or return a value-initialized result,
  with the per-method fallback to `raise` when a method's result cannot be
  value-initialized. The behavior is fixed by the proxy's type, as in
  `flexi_function`: no runtime override, and no behavior traveling with a
  target across a converting move. Remove the "not yet honored" caveat in
  proxy.md when done.
- Decide what views do. `proxy_view` and `const_proxy_view` carry no policy,
  so an empty view stays undefined to call through unless views grow a
  policy of their own or borrow the raise behavior unconditionally. Default
  answer: leave views as they are and say so.
- Prefer the heap handover in adoption. A heap-stored target moving to a
  heap-admitting destination is handed over as a pointer, never un-boxed,
  even when it would fit inline; the allocation is already paid for. The
  shared policy doc already promises this; verify `do_adopt` matches and add
  a test that pins it.
- `reset()` and `nullptr` assignment, for parity with `flexi_function`.
  Both empty the proxy and reinstall its type's empty behavior.
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
  and the wrapper says "boxed"; the internals should follow.
- Once the above lands, both types share the same empty-call, handover, and
  vocabulary contracts, and the proxy.md storage-policy section can point at
  `invocable_policy.h` as the single description.

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

## Deferred: direct-call thunks for compile-time-known targets

Wanted, after the storage generalization. A stored function pointer costs
two indirections per call: the erased invoke thunk, then the call through
the pointer it loads from the buffer. That second level is removable
whenever the target is known at compile time: take the function as an NTTP
(a `template<auto Fn>` factory or constructor tag) and install a thunk
whose body calls `Fn(args...)` directly. No load, no second indirection,
the callee visible to the inliner, and nothing stored in the buffer at
all. One indirect call is the floor for any type-erased wrapper; this
reaches it.

C++26's `std::function_ref` standardizes exactly this shape via its
`std::nontype` constructors, which serves as precedent for the interface,
not as a dependency: we are on C++23 (and clang's library has not fully
caught up even to that), so the mechanism is built here. Additive: a new
construction path; the existing runtime paths are untouched. Composes with
the qualified-signature work above, so sequence it with or after that.

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
`fixed_function`'s `SZ` already means. `heap_only` has no such need: its
three-word layout is padding-free, and line isolation for any wrapper is the
container's job (`alignas` on the element or enclosing struct), not the
policy's.
