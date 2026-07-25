# Initialization Policy (C++23)

House style for this library. The fundamental principle: use the syntax that
communicates the intended initialization semantics. Uniformity here is a
bug-finding tool, not decoration: when the mandated spelling fails to compile
(a narrowing brace, a rejected implicit conversion), that is the policy
working. Do not switch forms just to make an error go away; understand what it
caught first.

Four forms, each with one meaning:

| Form                     | Meaning                                          |
| ------------------------ | ------------------------------------------------ |
| `int x = 5;`             | The specific value is the point (literals only)  |
| `auto x = expr;`         | The type comes from the expression, deliberately |
| `int x{expr};` etc.      | Value-init, narrowing check, aggregate, value    |
| `Connection c(a, b);`    | Operational construction (active logic)          |

## `=` with a spelled-out type: literals only

- **Rule:** `Type x = expr;` is reserved for literal initializers where the
  specific value is the point: `int x = 5;`.
- Loop counters follow from this: `for (int i = 0; i <= 5; ++i)`. The initial
  value is a range endpoint that pairs with the bound; `int i{}` would hide
  half the range.
- A single literal where the value is the point takes `=` even into a
  string-like type: `std::string_view marker_ = "(null)";`. Braces would
  add no check there (no narrowing applies, no `initializer_list` hazard),
  so requiring them would be uniformity without a payoff. Braces return the
  moment the initializer is non-literal or multi-argument, where the
  conversion or the components are the point.
- **Rule:** For a non-literal initializer, do not write `int x = y;`. The
  initializer's type can drift silently under `=`. Pick a side instead:
  `auto x = y;` when `x` should track `y`'s type, or a brace form when `x`'s
  type is fixed (below).
- Deliberate narrowing is spelled with an explicit cast, never by falling back
  to `=` to duck the brace error.
- **Ruling: `bool` from a boolean expression.** A boolean expression
  initializes a spelled-out `bool`: `const bool ok = p && p->is_good();`.
  The RHS of a bool is typically coercions (a pointer used bool-like) and
  comparisons, with nothing spelled `bool` anywhere in it, so `auto` hides
  the one fact that matters; braces add nothing, because a boolean
  expression cannot narrow. Parenthesize comparisons:
  `const bool ws = (a != b);`, `p && (count() == 0)`. The parens group the
  comparison with its own operands; without them, `x = y != z` makes the
  reader stop and reparse. They belong to the comparison, not the
  initializer: `&&` does not look like an assignment, so a bare conjunction
  takes none. This began as a scoped exception; the ruling below names the
  deeper rule it instantiates.
- **Ruling: ternary predicates containing comparisons take parens.** When
  the predicate of a `?:` contains a comparison operator, parenthesize the
  predicate as a whole: `(sizeof(C) == 1) ? a : b`,
  `(pos != npos) ? pos : end`. Same motivation as the comparison parens
  above: without them, the comparison and the `?` blur together and the
  reader stops to reparse. A predicate with no comparison in it (a bool, a
  call, a bare conjunction of those) takes none.
- **Ruling: spelled types where braces could add nothing.**
  `Type x = expr;` extends beyond literals when both of these hold: the
  spelled type states a fact the initializer does not make evident, and
  braces could add no machine check because the initializer cannot narrow.
  The bool case is one instance. Another is a pointer pinned by an invariant
  rather than tracking: `const char* p = ok ? name.data() : "thread";`,
  where `p` must point at what `name` wraps no matter how the initializer
  drifts, and a pointer initializer cannot narrow. When either condition
  fails, the base rules stand: `auto` when the type should track, braces
  when the conversion or narrowing check has value.

## `auto` with `=`

- `auto x = y;` when the type should track the initializer.
  `auto x = static_cast<size_t>(n);` when converting; the cast names the
  destination type. `auto x = size_t{5};` is fine for a typed literal.
- Lean toward `const` on locals as the default posture:
  `const auto first = sv.front();` over `auto first = sv.front();` when the
  value never changes. Not a requirement everywhere, just the side to err
  on.
- **Rule:** No braces on the right-hand side of `auto x = ...;` unless a
  `std::initializer_list` is actually wanted, which is rare. `auto x{5};`
  (an `int`) and `auto y = {5};` (an `initializer_list<int>`) mean different
  things; avoid both spellings entirely.
- Spelling the type instead of `auto` for a non-literal initializer is
  justified only by a particular reason to emphasize the type, and then a
  brace form carries it, since `=` stays reserved for literals.
- A temporary copy whose type must track the source is `auto`:
  `auto tmp = std::move(*this);` in a `swap`, not
  `fixed_function tmp{std::move(*this)};`, which repeats a type that could
  never legitimately differ.
- **Ruling: long lambda initializers spell their return type.** A lambda
  initializer long enough that its result type is not apparent at the
  declaration (an immediately invoked one, say, with platform branches)
  keeps `auto` on the left and carries an explicit trailing return type:
  `[] -> std::string { ... }`. The fact goes where the value is produced,
  and the compiler checks every `return` statement, each branch at its own
  site, against the declared type; spelling the left-hand type instead
  checks only the one conversion of the already-deduced result, at the
  bottom.

## Braces

Braces express "this object takes on these values", and they reject narrowing.

- **Rule: every variable gets an initializer.** An uninitialized declaration
  is permitted only as a considered choice, and then a comment must say so;
  without the comment it reads as an oversight. The canonical qualifying
  case is a buffer filled and read entirely within the function, with only
  the written cells ever read. A single-value target handed to another
  function (a `from_chars`-style out-param) does not qualify: clearing a
  word is dirt cheap and the compiler drops a store it can prove dead, so
  `E e{};` costs nothing. A sized destination buffer handed to a pure
  writer sits in between and is judged by cost: the log prefix buffer keeps
  its non-init, with a comment, because zeroing 256 bytes per log line is
  real and the optimizer cannot prove it dead.

- **Value-initialization (empty state):** `int n{};`, `ptr_t p{};`,
  `some_enum e{};`. The point is that the object starts empty or zero, not
  that it holds an interesting value. Accumulators are the canonical case:
  `size_t total{};` starts at the additive identity, "nothing yet", which is
  what distinguishes it from a loop counter whose 0 is a range endpoint. In
  generic code `T t{};` is correct regardless of whether `T` is scalar.
  - **Rule:** Do not brace-init a class whose default constructor already
    initializes it safely: `std::string s;`, not `std::string s{};`
    (clang-tidy flags it). Generic code is exempt, since `T` may be scalar.
  - **Rule:** The empty state of a pointer is value-init, `ptr_t p{};`,
    never `= nullptr`. The `nullptr` literal should be rare in general:
    pointers are tested as bools (`if (p)`, never `p != nullptr`), and the
    literal appears only where a null is explicitly passed, returned, or
    assigned as a reset.
- **Narrowing firewall:** when the initializer's type may differ from the
  target's, braces turn a silent conversion into a compile error:
  `int x{calculate_total()};`. If that return type later changes to a wider
  or differently-signed type, the line correctly breaks. This may
  legitimately force a `static_cast`; that visibility is the point.
- **Aggregate and designated initialization:** `Point p{4, 6};`,
  `fill(str, {.left = '(', .right = ')'});`.
- **Ruling: anonymous values use braces, not functional casts.** A temporary
  built from a single value is `CharT{'{'}`, `index_t{0}`, never the
  functional cast `CharT('{')`: the cast spelling has `static_cast`
  semantics, silently permitting narrowing while dressed as construction,
  and the brace form performs the same construction checked (a constant
  that fits still compiles). A member initialized to such a value
  constructs directly, `CharT fill{' '};`, rather than building a temporary
  just to assign it. A genuine conversion is spelled `static_cast<T>(x)`,
  which names its intent. Carve-out: the enum operators' terse
  enum-from-underlying casts (`E(*l | *r)`) stay as functional casts; they
  are real casts whose brace form would not compile against promoted
  arithmetic, and spelling `static_cast` at every operator would bury the
  logic.
- **Value-like construction:** classes whose constructor conceptually just
  takes on the given value use braces, and this composes with `explicit`
  strong-type constructors: `Port p{80};`. `std::string` and
  `std::unique_ptr` count as value-like: the allocation and copying are the
  mechanism, not the meaning.

## Parens: operational construction

- **Rule:** Use parens when the constructor performs active logic on its
  arguments rather than the object simply taking on the values:
  `Connection c(host, port, options);` (it connects),
  `std::unique_lock lk(m);` (it locks; retaining the pointer is incidental),
  `std::ifstream f(path);` (it opens).
- **Rule:** Use parens when braces would select an unintended
  `std::initializer_list` overload: `std::vector<int> v(10);` is ten
  value-initialized elements; `std::vector<int> v{10};` is one element.
- The line takes judgment at the edges: `Image blank{1024, 1024};` but
  `Image i(filename);` (it loads a file). Note that mixing the two modes in
  one overload set, a value-like constructor alongside an operational one, is
  itself a code smell. A class with only operational constructors is fine
  (`std::ofstream`); when the modes coexist, the operational one is a good
  candidate for a named factory (`Image::load_file(filename)`), which says
  the work is loading a file rather than, say, initializing an image whose
  pixels spell out that text. The style is supposed to flush out bad code,
  not hide it. When a case is genuinely on the fence, ask for a ruling
  rather than guessing; rulings accrete here.

## Member initialization

- Constructor member-init lists lean harder toward braces than locals do:
  `: width_{1024}`, `: option_{option}`, even for literals (there is no `=`
  form to prefer). Parens only for operational construction or to dodge an
  `initializer_list` hijack, same as above.
- NSDMIs do have an `=` form, and they follow the local-variable rules:
  `int width_ = 1024;` is fine even when a constructor spells the same member
  `width_{width}`.
- **Ruling:** An NSDMI default whose value is the point takes the `=` form
  when the initializer cannot narrow:
  `size_t word_ndx_ = word_count_v;` (an end sentinel),
  `index_t ndx_ = npos;`, `resource_id_type resource_ = null_v;`. This is
  the no-narrow rule applied to members: braces could add no check, and
  `= value` reads as the default it is. Value-init `{}` stays the spelling
  for the empty state, and braces stay for conversions worth checking and
  for value-like class construction. A meaningful zero is spelled, not
  blanked: a begin iterator's `word_ndx_{0}` is a range endpoint pairing
  with its bound, where `{}` would falsely claim "nothing yet".
- **Ruling:** A member of scalar, enum, pointer, or similar raw type carries
  an NSDMI even when every constructor initializes it: `E enum_{};`.
  Consistency (a compiler-provided constructor initializes it too), safety
  (a future constructor cannot silently leave it), and zero cost (a
  constructor's init-list entry overrides the NSDMI, so there is no double
  store). The class-type carve-out stands (`std::string s;`), a generic
  member type that need not be default-constructible cannot comply, and a
  storage buffer whose non-init is the point (an SBO buffer keyed by a
  discriminant) is the commented exception, judged by cost as with locals.

## Generic code

- `T t{};` for value-initialization.
- **Rule:** Forwarded construction uses parens, matching `std::make_unique`
  and the standard `emplace` functions: `T(std::forward<Args>(args)...)`, and
  likewise for `new T(...)` and placement new. Braces here change meaning for
  any `T` with an `initializer_list` constructor.
