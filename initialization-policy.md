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
- **Rule:** For a non-literal initializer, do not write `int x = y;`. The
  initializer's type can drift silently under `=`. Pick a side instead:
  `auto x = y;` when `x` should track `y`'s type, or a brace form when `x`'s
  type is fixed (below).
- Deliberate narrowing is spelled with an explicit cast, never by falling back
  to `=` to duck the brace error.

## `auto` with `=`

- `auto x = y;` when the type should track the initializer.
  `auto x = static_cast<size_t>(n);` when converting; the cast names the
  destination type. `auto x = size_t{5};` is fine for a typed literal.
- **Rule:** No braces on the right-hand side of `auto x = ...;` unless a
  `std::initializer_list` is actually wanted, which is rare. `auto x{5};`
  (an `int`) and `auto y = {5};` (an `initializer_list<int>`) mean different
  things; avoid both spellings entirely.

## Braces

Braces express "this object takes on these values", and they reject narrowing.

- **Value-initialization (empty state):** `int n{};`, `ptr_t p{};`,
  `some_enum e{};`. The point is that the object starts empty or zero, not
  that it holds an interesting value. Accumulators are the canonical case:
  `size_t total{};` starts at the additive identity, "nothing yet", which is
  what distinguishes it from a loop counter whose 0 is a range endpoint. In
  generic code `T t{};` is correct regardless of whether `T` is scalar.
  - **Rule:** Do not brace-init a class whose default constructor already
    initializes it safely: `std::string s;`, not `std::string s{};`
    (clang-tidy flags it). Generic code is exempt, since `T` may be scalar.
- **Narrowing firewall:** when the initializer's type may differ from the
  target's, braces turn a silent conversion into a compile error:
  `int x{calculate_total()};`. If that return type later changes to a wider
  or differently-signed type, the line correctly breaks. This may
  legitimately force a `static_cast`; that visibility is the point.
- **Aggregate and designated initialization:** `Point p{4, 6};`,
  `fill(str, {.left = '(', .right = ')'});`.
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

## Generic code

- `T t{};` for value-initialization.
- **Rule:** Forwarded construction uses parens, matching `std::make_unique`
  and the standard `emplace` functions: `T(std::forward<Args>(args)...)`, and
  likewise for `new T(...)` and placement new. Braces here change meaning for
  any `T` with an `initializer_list` constructor.
