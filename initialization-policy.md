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
- A single literal where the value is the point takes `=` into a class type
  the literal converts to implicitly:
  `std::string_view marker_ = "(null)";`,
  `duration_t tick_interval = 100ms;`. A string-like target is the common
  example, not a carve-out. What licenses `=` is that the target's
  converting constructor is implicit, so braces could add no check (the
  constructor already gatekeeps the conversion, and no `initializer_list`
  hazard applies), and requiring them would be uniformity without a payoff.
  Chrono is the clearest case: a lossy conversion such as `seconds` from
  `100ms` fails to compile under either spelling, because the constructor
  itself refuses it. When the conversion instead needs the target's
  `explicit` constructor, `=` does not compile at all and braces are
  required, as in `duration_t tick_interval{100}`, where a bare number is
  not a duration, and `Port p{80}`. That is value-like construction, below.
  Braces likewise return the moment the initializer is non-literal or
  multi-argument, where the conversion or the components are the point.
- **Rule:** For a non-literal initializer, do not write `int x = y;`. The
  initializer's type can drift silently under `=`. Pick a side instead:
  `auto x = y;` when `x` should track `y`'s type, or a brace form when `x`'s
  type is fixed (below).
- Deliberate narrowing is spelled with an explicit cast, never by falling back
  to `=` to duck the brace error.
- **Ruling: a named constant takes the same narrowing test as anything else,
  and `constexpr` does not exempt it.** `static constexpr size_t
  max_events = 64;` keeps `=` because a bare literal is the point, and
  `static constexpr size_t read_throttle_size = slab_size * 3 / 4;` keeps it
  because the expression is already `size_t`. When the initializer's type
  differs from the declared type, braces do the coercion:
  `static constexpr size_t hugepage_size{2UL * 1024 * 1024};`. Braces coerce
  the RESULT, which is a separate matter from the width the arithmetic is
  performed in. When the initializer is a multiplication whose result widens,
  the first operand must still carry the wide type via a suffix, as in
  `2UL * 1024 * 1024`. That is not redundant type-restating. It stops the
  product from overflowing in the narrow type before the widening ever
  happens, and clang-tidy enforces it via
  `bugprone-implicit-widening-of-multiplication-result`. Note also that
  `constexpr` weakens the narrowing check without removing it. A constant
  that fits is exempt, so braces reject only a value out of range for the
  target, such as a computed constant that went negative.
- **Ruling: fix the literals when signedness is what blocks braces.** When a
  brace form fails only because a literal initializer is signed and the target
  is not, suffix the literals (`4U`, or `UL` when that is not enough) rather
  than retreating to `=` or reaching for a cast:
  `const size_t mask_len{is_mask ? 4U : 0U};`. The brace error was pointing at
  a real signed/unsigned mismatch in the source, and the fix belongs where the
  mismatch is. The ternary is also what puts this line under the brace rule at
  all. A bare literal initializer takes `=`, because a constant that provably
  fits is exempt from the narrowing check, so there the check can never fire
  and braces would add nothing.
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
  destination type.
- **Ruling: `auto` must not hide a character type.** A pointer or cursor into
  a character buffer spells its type: `const char* base = buf.data() + b;`,
  `const CharT* p = first;`, never `const auto*`. We are not trying to track
  the character type there, and `auto` only obscures it. The cast form above
  still applies when the destination is named on the right, as in
  `const auto* f = reinterpret_cast<const char*>(first);`.
- **Ruling: construct directly when the initializer is only a construction.**
  When the initializer is nothing but a construction of a spellable type,
  declare directly: `view_t spec{ctx.begin(), ctx.end()};`, not
  `auto spec = view_t{...};`. The `auto =` form adds ceremony without
  information: `auto` is for initializers whose type the reader should not
  have to restate (calls, casts, moves), not for restating one already
  written on the right. CTAD counts as spelling the type:
  `std::array line_breaks{CharT{'\r'}, CharT{'\n'}};`. A default
  construction composes with the class-default rule and loses its braces
  too: `hash_combiner combiner;`, not `auto combiner = hash_combiner{};`.
  A typed literal follows as `size_t x = 5;` under the literal rule, not
  `auto x = size_t{5};`. Exempt: structured bindings, whose syntax forces
  `auto`, and a variable template whose specializations vary the type
  (`enum_spec_v`), where the `auto` is the point.
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
  - **Ruling:** This reaches past the value-init spelling to any redundant
    default. A member whose default constructor already yields the wanted
    value takes no initializer at all, even when that constructor is
    `explicit` with a defaulted argument: write
    `notifiable<std::atomic_bool> started;`, not `started{false}`. Restating
    a value the type already supplies is noise, and it contradicts the
    empty-state spelling used everywhere else.
  - **Ruling: a `bool` whose default means "not yet" is empty state.** A
    held-input flag, a "have we seen one" latch, and anything else whose
    `false` says nothing has happened yet takes `{}`: `bool looking{};`,
    `bool jump{};`, not `= false`. The spelled literal stays for a bool
    whose value is genuinely the point, which in practice is nearly always
    `= true`: `bool active_ = true;`, `bool validate_utf8 = true;`. This is
    the same line accumulators draw against loop counters, one step up in
    abstraction. It does not revive the redundant `{false}` spelling, which
    remains wrong either way.
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

- **Rule: the test is identity.** Are the constructor's arguments part of
  what the object IS, or instructions for producing it? Part of it takes
  braces; instructions take parens. `Connection c(host, port, options);`
  connects, `std::ifstream f(path);` opens, and afterward neither the host
  nor the path is anything the object holds. Contrast `Port p{80};` and
  `Image blank{1024, 1024};`, where the arguments are the object's value and
  the allocation is mechanism, exactly as with `std::string` and
  `std::unique_ptr` above.
- Retrievability is a useful symptom of identity, since identity is usually
  exposed, so a `get`-style accessor often marks the braces case. It stays a
  symptom, though, never the test. `std::unique_lock` hands its mutex back
  via `mutex()` for `condition_variable` interop and still takes parens,
  because a held lock, not the mutex, is what the object is.
- **Ruling: every RAII scoping object takes parens.** `std::scoped_lock`,
  `std::unique_lock`, `std::lock_guard`, `std::stop_callback`, `scoped_value`,
  and `scope_exit`. This is a corollary of the identity test, not a separate
  rule: a guard's identity is the bracketed region, and its arguments only say
  which region. You cannot describe what a `scope_exit` is without naming the
  scope, and its lambda never enters that sentence, so it takes parens even
  though its constructor merely stores. Read the category off the guard, not
  off a per-constructor audit of whether that constructor does work.
  - **Bound:** an owning value type is not a scoping object, even though it is
    equally RAII. A smart pointer's identity IS the thing it holds, so
    `std::unique_ptr` and `epoll_stream_conn_ptr_with` take braces, no matter
    how much work the constructor does to acquire it.
- **Rule:** Use parens when braces would select an unintended
  `std::initializer_list` overload: `std::vector<int> v(10);` is ten
  value-initialized elements; `std::vector<int> v{10};` is one element. This
  overrides the identity test and is not evidence about it. By identity,
  `std::vector<T> v(10)` is value-like, and the count is even retrievable as
  `size()`; it takes parens only because braces resolve elsewhere. Do not
  cite it as precedent for a size-like argument taking parens.
  `Image blank{1024, 1024};` keeps braces because `Image` has no
  `initializer_list` constructor to collide with.
- When the identity test is genuinely ambiguous, the ambiguity is itself the
  finding, because it means the constructor is doing two jobs. `Image` is
  unsettling for exactly that reason: a real one would have both a dimensions
  constructor and a load-a-file constructor, which the ruling below resolves.
  A class with only operational constructors is fine (`std::ofstream`). The
  style is supposed to flush out bad code, not hide it. When a case stays on
  the fence, ask for a ruling rather than guessing; rulings accrete here.
- **Ruling: a mixed overload set resolves to a factory.** When a class can be
  constructed both on a value it merely holds (an already-open file
  descriptor) and on the parameters for producing that value (a filename it
  opens to get one), the operational constructor moves to a named factory:
  `epoll::create(flags)`, never a public `epoll(flags)`. The factory does the
  work and hands the result to the value-like constructor,
  `return epoll{os_file{::epoll_create1(flags)}};`, which leaves every
  remaining constructor about ownership and therefore braced. The named
  factory also documents the alternate path, where the constructor spelling
  hid it: `Image::load_file(filename)` says the work is loading a file,
  rather than initializing an image whose pixels spell out that text. Note
  that neither reading of `Image(std::string)` would have earned braces. A
  string is not part of an image's identity either way, so the ambiguity is
  a sign the constructor is the wrong shape, not a case to adjudicate. This
  does not retire the paren rule, which still marks any operational
  construction that remains; it says that in a mixed overload set the right
  fix is to stop having one.

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
  A class type that merely looks scalar is not a raw type for this purpose:
  `std::chrono::time_point` and `std::atomic<T>` both already initialize
  themselves, so they take the carve-out and stay bare. Adding `{}` there is
  redundant, and clang-tidy's `readability-redundant-member-init` says so.

## Generic code

- `T t{};` for value-initialization.
- **Rule:** Forwarded construction uses parens, matching `std::make_unique`
  and the standard `emplace` functions: `T(std::forward<Args>(args)...)`, and
  likewise for `new T(...)` and placement new. Braces here change meaning for
  any `T` with an `initializer_list` constructor.
