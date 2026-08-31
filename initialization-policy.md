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
| `string_view s = "hi";`  | The value is the point, or braces check nothing  |
| `auto x = expr;`         | The type comes from the expression, deliberately |
| `int x{expr};` etc.      | Value-init, narrowing check, aggregate, value    |
| `Connection c(a, b);`    | Operational construction (active logic)          |

Two independent questions live in every declaration, and most disputes here
come of arguing one on the other's terms. What stands on the left, `auto` or
a spelled type, is settled by the principle below. What joins it to the
right, `=` or `{}` or `()`, is settled by the four forms above.

**Names carry meaning; types carry constraint.** Saying what a variable
means is the name's job, so `auto` is right whenever the name does that
job. The type's job is to bound what may be in the variable and to pick the
representation, which `auto` does not interfere with: in
`auto on_error = settings.on_error;`, the name says what it is for and
`failure_policy` says it must be `log`, `terminate`, or `ignore`. It
follows that a spelled type is worth writing only when it constrains
(a narrowing check, a conversion, a category the reader holds) and not
merely when it labels. It also follows that an unclear `auto` is usually a
naming defect, not a missing type: `auto floor = ...` reads badly because
`floor` is a noun, and `on_floor` fixes it without the type's help. Booleans
in particular should read as English predicates, with `is`, `was`, `can`,
or `did` either spelled or implied, as `jump_ready` implies "jump is
ready". Where a type is being asked to explain what the name should have
said, the code is relying on `float x` here and `bool x` there to tell two
things apart, and the names are at fault.

## `=` with a spelled-out type: literals, plus no-narrow rulings

- **Rule:** a literal initializer where the specific value is the point
  takes `=`. What stands on the left follows the literal ruling below:
  `auto` when the literal's own type is obvious (`auto x = 5;`), the
  spelled type when it needs emphasis or converts the literal
  (`std::string_view marker = "(null)";`).
- Loop counters follow from this: `for (auto ndx = 0; ndx <= 5; ++ndx)`. The
  initial value is a range endpoint that pairs with the bound, so it is
  spelled where an accumulator's zero would be blanked. The two rulings below
  settle what stands on the left.
- **Ruling: a `size_t` counter drops the spelled type and suffixes the
  endpoint, `for (auto ndx = 0UZ; ndx != n; ++ndx)`.** A for-header init is a
  literal-initialized local like any other, so the named-constant rule below
  governs it: the suffix already names the type, which leaves `size_t` as
  pure repetition, and dropping it foregrounds the endpoint that pairs with
  the bound. The counter's type still matches the bound's by construction,
  which is what the spelled type had been there to show. This applies only
  to a literal endpoint; a counter that starts from an expression,
  `for (size_t ndx = start; ...)`, keeps the spelled type unless the
  expression itself names it. Comparisons in the same header stay bare: the
  bound converts harmlessly and `0UZ` there would be noise.
- **Ruling: an `int` counter whose type is incidental drops the type too,
  `for (auto ndx = 0; ndx < 4; ++ndx)`.** No suffix names `int`, but none is
  needed: a bare `0` already deduces it. The spelled type stays where it
  carries information the deduction would lose. Signedness that the loop
  depends on is the usual case, `for (int ndx{name_array.size() - 1}; ndx >= 0;
  --ndx)`, where the braces also check the narrowing that produced the
  endpoint. A container's own `size_type` alias likewise stays spelled, since
  no suffix names it and it is not necessarily `size_t`:
  `for (size_type ndx = 0; ndx < n; ++ndx)`.
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
  type is fixed (below). The no-narrow rulings further down carve out where
  `=` survives anyway.
- Deliberate narrowing is spelled with an explicit cast, never by falling back
  to `=` to duck the brace error.
- **Ruling: a named constant takes the same narrowing test as anything else,
  and `constexpr` does not exempt it.** `static constexpr auto
  max_events = 64UZ;` spells no type because the suffix already carries the
  target's own, and
  `static constexpr size_t read_throttle_size = slab_size * 3 / 4;` keeps
  `=` because the expression is already `size_t`. When the initializer's
  type differs from the declared type, braces do the coercion:
  `constexpr uint32_t prime_x{374761393U};`. Braces coerce
  the RESULT, which is a separate matter from the width the arithmetic is
  performed in. Within a product, the suffix belongs on the operand that
  names a size or a unit, never on a bare multiplier: `4096UZ * 4` is
  four 4k blocks, and `2 * 1024UZ * 1024UZ` is two binary megabytes, the
  unit literals carrying the type while the count stays plain. When an
  operand with a real type already names the size, no suffix is needed
  at all: `4 * hugepage_size`. A suffixed operand must still sit early
  enough that the arithmetic runs wide: multiplication associates left,
  so `2 * 1024UZ` is already `size_t`, where a product of bare narrow
  literals would overflow before any widening ever happened, which is
  what clang-tidy's `bugprone-implicit-widening-of-multiplication-result`
  watches for. Note also that
  `constexpr` weakens the narrowing check without removing it. A constant
  that fits is exempt, so braces reject only a value out of range for the
  target, such as a computed constant that went negative.
- **Ruling: a literal-initialized declaration spells its type only when
  that type differs from the literal's own.** The declared type is worth
  writing when it is converting and not when it merely labels, which is
  the governing principle applied to literals. A suffix that already pins
  the type leaves nothing to say, so `static constexpr auto contact_eps =
  1.0e-3F;` and `constexpr auto max_steps = 256;`. A mutable local reads
  the same way, so `auto twg = -1.0F;`: the value is the point and the
  type is obvious, which are not in tension. What makes a type obvious is
  not restricted to a suffix: `true` and `false` name `bool` as plainly as
  `F` names `float`, so a flag whose value is the point is
  `auto first = true;`. A small integer literal is as plain, `5` is `int`
  on sight, so `auto x = 5;`, with the spelled type reserved for the
  unusual case where the precise type wants emphasis. Note that this rule
  settles only which type to write, never whether to spell the value at
  all, which the zeroish rule below settles first and usually answers with
  `{}`. So `bool found{};`
  remains better than `auto found = false;` in most cases; the latter is
  not wrong, and is what to reach for when spelling the `false` out earns
  its keep, as when it pairs with a neighboring declaration.
  - **An expression initializer follows the same test.** `constexpr auto
    kz = std::numbers::inv_sqrt3_v<float>;` and `constexpr auto per_degree
    = std::numbers::pi_v<float> / 180.0F;`: the expression already produces
    `float` and names it, so the declared type converts nothing. What
    decides is whether the initializer's own type differs, not whether it
    is a literal. Check the whole chain before assuming, since one widening
    operand makes the spelled type load-bearing again.
    - **Obvious means obvious to a reader, not merely guaranteed by the
      language.** A requires-expression yields `bool` by definition, and
      `static constexpr bool bound_v = requires(...) { ... };` still spells
      it, because the feature is new enough that its type is not yet
      blatant on sight. A name that forwards to another trait is the same
      case: `bound_v = method_traits<M>::template bound_v<F, T>` and
      `all_bound_v = (entry_traits<Es>::template bound_v<F, T> && ...)`
      keep `bool`, since confirming it means going to look. Contrast the
      literal in the same file, `static constexpr auto bound_v = true;`,
      where nothing has to be looked up. This one is expected to age: as a
      feature becomes familiar its type becomes obvious, and the ruling
      should be revisited rather than treated as settled.
  - **When the literal's type is the thing that does not match, fix the
    literal.** `auto half = 0.5F;`, never `float half = 0.5;`. The
    mismatched form is a silent double-to-float narrowing dressed as a
    declaration, and suffixing the literal removes the conversion instead
    of spelling a type to absorb it. Same move as the `4U` ruling below:
    when a literal's type blocks the form you want, the literal is what is
    wrong.
  - **A non-static data member stays spelled, because the language leaves
    it no choice.** `auto` is not a valid declarator for a member, so an
    NSDMI keeps its type however obvious the literal is:
    `float disc_height = 0.32F;`. This is not an exception anyone chose,
    and it is the reason a struct's members and a function's locals will
    disagree about the identical literal, `float gravity = 20.0F;` beside
    `auto shadow_factor = 1.0F;`. Reading that as an inconsistency to
    clean up is the mistake; the member has only one spelling available.
  - **Fixed-width types always stay spelled**: `uintN_t` and friends.
    Fixing the literal is not available here, since
    the language has no suffix that names a fixed width, so the declared
    type is the only place the fact can be written. Beyond that, whether a
    given literal happens to match one is an accident of its magnitude
    that a reader would have to compute.
    `static constexpr uint32_t all_mask = 0xffffffff;` would in fact
    survive `auto`, because a hex literal too large for `int` lands on
    `unsigned int`; one digit shorter it would not, since `0xff` is plain
    `int` and `auto` would lose both the width and the signedness. Two
    adjacent lines of identical shape with opposite answers is not a rule
    anyone applies correctly at reading speed. The role types `size_t` and
    `ptrdiff_t` left this bullet when C++23 gave them suffixes: `64UZ`
    (and `64Z`) names the role in the literal itself, so fixing the
    literal is available after all and the ordinary rule takes over,
    `constexpr auto max_events = 64UZ;`. A non-static data member still
    spells the type, with the literal suffixed: `size_t width = 70UZ;`.
    Defaulted function parameters and template parameters read the same
    way, `size_t max_bytes = 4096UZ`, since `auto` there would change the
    declaration's meaning (an abbreviated function template, a deduced
    NTTP) rather than its spelling. A zeroish default is `= {}`, the
    closest a parameter can come to the local's `size_t x{}`:
    `size_t header_length = {}`. The spelled `0UZ` returns only when the
    zero earns its spelling, as when it pairs with nonzero siblings in
    one signature: `do_repeated(size_t init = 0UZ, size_t inc = 1UZ,
    size_t limit = 42UZ)`. Template parameters are the carve-out: nvcc's
    EDG frontend cannot parse a braced NTTP default (clang, gcc, and MSVC
    all can), so a zeroish NTTP default spells the value,
    `template<size_t width = 0UZ>`. A template argument position is a
    call site rather than a declaration, and call arguments stay bare:
    `impl<C, 0, Storages...>`, never `0UZ` there. The conversion is a
    converted constant expression, so it cannot narrow silently and the
    suffix would have nothing to check.
  - **Fixed-width types also take braces, even for a bare
    literal.** The fact that forces the type to be spelled, that the
    literal cannot express the target's width, is the same fact that stops
    a reader from checking the fit, so the check belongs to the compiler:
    `constexpr uint32_t prime_x{374761393U};`. This is not decoration.
    `constexpr uint32_t x = 5000000000U;` compiles silently under
    `-Wall -Wextra -Werror` and stores 705032704, where the brace form is a
    hard error that names the value. `size_t` and `ptrdiff_t` are exempt
    for the same reason they left the bullet above: a `UZ` literal IS the
    target's own type, so there is nothing for a check to catch and `=`
    is licensed. Idiomatic wraparound is the exception
    and keeps `=`: `static constexpr size_t npos = -1;` is the traditional
    shorthand for the target's maximum, and a negative literal against an
    unsigned target is obvious rather than subtle, so there is nothing
    there for a check to catch. Ordinary types are untouched by this,
    since an `int` or `float` literal already carries the target's type.
- **Ruling: fix the literals when signedness is what blocks braces.** When a
  brace form fails only because a literal initializer is signed and the target
  is not, suffix the literals (`4U`, or `UL` when that is not enough) rather
  than retreating to `=` or reaching for a cast:
  `const uint32_t mask_len{is_mask ? 4U : 0U};`. The brace error was pointing
  at a real signed/unsigned mismatch in the source, and the fix belongs where
  the mismatch is. The ternary is also what puts this line under the brace
  rule at all. A bare literal initializer takes `=` when the target is an
  ordinary type, where the literal already carries that type and so the check
  could never fire. That reasoning does not reach a fixed-width type: there
  the constant's fit turns on a width the literal knows nothing about, and
  the check fires for real. Those take braces, per the bullet above. A role
  type escapes the same way it escaped the braces bullet: `UZ` names the
  target's own type, so once the arms carry it there is nothing left for a
  check to catch, the spelled type has nothing left to say, and the
  all-literal ladder rule takes over:
  `const auto mask_len = is_mask ? 4UZ : 0UZ;`, which is how the site that
  taught this ruling reads today.
- **Ruling: an initializer that is already `bool` takes `auto`; spelling
  `bool` marks a conversion.** A comparison, a `&&` or `||`, a `!`, and a
  predicate call all yield `bool` by definition, so `auto` deduces exactly
  `bool` and the spelled type restates what the initializer already says:
  `const auto on_floor = contact.on_floor();`,
  `const auto ok = p && p->is_good();`, `const auto ws = (a != b);`. The
  spelled `bool` is reserved for an initializer that is NOT bool and is
  being converted to one, where `auto` would deduce something else
  entirely: `const bool ok = p;` on a pointer, `const bool any = flags &
  mask;` on an integer. That is the same reason
  `auto x = static_cast<size_t>(n);` names its destination. Prefer
  restructuring over the conversion where it reads better (`p != nullptr`
  is banned, but a predicate call rarely is).
  - Parenthesize comparisons either way:
    `const auto ws = (a != b);`, `p && (count() == 0)`. The parens group
    the comparison with its own operands; without them, `x = y != z` makes
    the reader stop and reparse. They belong to the comparison, not the
    initializer, so a bare conjunction takes none.
  - This supersedes the earlier reading, which spelled `bool` for any
    boolean expression on the grounds that "nothing is spelled `bool`
    anywhere in it". That is true of the operands and false of the
    operator: `&&` and `==` are as plainly bool-valued as a function
    named `on_floor`. Applied across the repo the narrower rule left the
    spelled form with no instances at all, since every site was already
    bool-valued.
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
- **Ruling: a reference binding takes `auto&`, all the more so.**
  `auto& io = get_io();`, not `ImGuiIO& io = get_io();`. This is the same
  rule as for values, with a stronger case behind it: a copy merely takes
  its type from the call, and is thereafter its own object, whereas a
  reference stays bound to that call's result for its whole lifetime.
  Spelling the type by hand there is a second place to be wrong about one
  fact. The character-type ruling below still overrides it.
- **Ruling: a character cursor spells its type, because `auto*` files it
  under the wrong category.** `const char* base = buf.data() + b;`,
  `const CharT* p = first;`, never `const auto*`. The reason is not that
  `auto` hides the type, which the principle above would not care about.
  It is that a reader holds "character pointer" as its own category, a
  string or a cursor into text, while `auto*` asserts pointer-ness as the
  salient fact. Pointer-ness is the incidental part here, an accident of C
  history: `std::string` is not called `string_ptr` and has no pointer
  semantics, and a newer language would spell the same thing `char[]`. So
  `auto*` sends the reader to the wrong drawer. It stays right where the
  thing really is a pointer to an object, as in `auto* rtv = get_rtv();`.
  Plain `const auto base = ...` is not wrong, since it claims nothing about
  pointers at all, but it is not better either, so there is little call for
  it. The cast form still applies when the destination is named on the
  right: `const auto* f = reinterpret_cast<const char*>(first);`.
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
  A typed literal follows the literal rule instead: `auto x = 5UZ;` where a
  suffix names the type, `auto x = 5;` where the bare literal's is obvious,
  never `auto x = size_t{5};`. Exempt: structured bindings, whose syntax forces
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
  justified only by a particular reason to emphasize the type. A brace form
  carries it when the initializer could narrow. When it cannot, the no-narrow
  rulings in the `=` section license `=` instead, as with the spelled `bool`
  and the pinned pointer.
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
  - **Ruling: `{}` is the spelling for the zeroish default; a spelled value
    is for a value that is not the zeroish default.** There is no useful
    distinction between "this is initialized" and "this is initialized to
    the zero value", because the zero value is the only thing `{}` can
    produce and we always know what it is: `0`, a null pointer, an `empty()`
    string or vector, `false`. In every case `if (!x)` holds right after.
    So `bool ready{};` and `bool ready = false;` say exactly the same
    thing, and the shorter one wins. What a spelled value can say that
    `{}` cannot is a value that is NOT zeroish: `bool live_ = true;`,
    `int width_ = 1024;`, `index_t ndx_ = npos;`. A sentinel that happens
    to equal the zero value is therefore just `{}`. The one carve-out is a
    zero that pairs with a visible bound,
    `for (auto ndx = 0; ndx <= 5; ++ndx)`, where hiding it would show half a
    range; that is about the pair, not about the zero. Sibling declarations can pair the same way: `auto p =
    1.0F; auto q = 0.0F;` for the two components of one tangent-frame
    coordinate keeps them legible as a pair, where blanking only `q`
    would split them. `float q{};` stays the canonical spelling, and this
    permits the other, so do not go looking for pairs to spell out.
    - **For an enum member, zeroish means the zero enumerator is the
      obvious default, the one a reader would assume unprompted.** "No
      tool selected" and "target the first one" are such defaults, so
      `active_tool tool_{};` and `TargetMode targetMode{};`. When the
      zero enumerator instead encodes a choice the reader would have to
      look up, the name stays spelled even though its value is zero:
      `update_strategy send_strategy_ = update_strategy::full;` (the
      full snapshot is a deliberate opening move, not the steady state),
      `http_phase phase = http_phase::request_line;` (a state machine's
      starting state), `mode mode_ = mode::binary;` (one output format
      among three). This is the shot_type lesson stated from the other
      side: `{}` on an enum member is only right when the empty state is
      what you mean, and whether the enum has two values or five is not
      the test.
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
  - **Ruling: in an aggregate that any call site initializes partially,
    every NSDMI is load-bearing.** Do not strip a member's initializer as
    redundant there, not even when the member's own type has gained
    initializers of its own and now initializes itself. A designated
    initializer may omit only fields that HAVE a default member
    initializer, and a positional one that stops short trips
    `-Wmissing-field-initializers`, so removing one breaks every partial
    call site at once. This overrides the redundant-default ruling above:
    in that shape the initializer is part of the type's interface, not
    noise. Learned three separate times, on `textwrap`'s `wrap_options`,
    on `epoll_stream_conn_handlers`, and on `avatar_rig`.
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
- **Ruling: an engine bound over its operand takes parens.**
  `evaluator ev(rt);`: what the object is is a provisioned evaluation engine
  (construction interns the special forms and stocks the builtins), and the
  runtime is the operand it evaluates against, not its value. That it holds
  the runtime and exposes its root environment is retrievability, the symptom
  above; arguing braces from "it holds the reference for its whole life" is
  exactly the reading the `unique_lock` example exists to block.
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
  - `std::span` from a pointer and a count is this case as of C++26, which
    adds `span(initializer_list<T>)`: `std::span<const value>{&v, 1}` is a
    span over two temporaries whenever `T` converts from a pointer (via
    `bool`) or an integer, so it takes parens, `std::span<const value>(&v,
    1)`, on every element type.
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
