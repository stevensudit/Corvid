# `flexi_string` and `readonly_string` design

Status: design agreed, not yet implemented. This document is the brief for the implementation.

## 1. Summary

`basic_flexi_string` is a `std::string` alternative for the Corvid library that differs from `std::string` mainly in how memory is handled. One class template produces two families of types:

- **`flexi_string`** (FS): a mutable string with the `std::string` interface, plus guarantees about buffer transfer on move, control over inline storage (for SSO), a null state, and the ability to hand its buffer in and out.
- **`readonly_string`** (RS): a string whose characters cannot be edited through it, but which can be reassigned. It can own its buffer, borrow someone else's, or point at static data. It replaces the "string or string_view" variant antipattern.

The motivating use case is a `std::map` keyed by `readonly_string` that is initialized at runtime, where keys that come from literals cost no buffer allocation (only the tree nodes), keys from other sources are owned copies, and no key can dangle by accident.

More deeply, it is motivated by a rejection of some highly limiting decisions by the committee, such as going with `resize_and_overwrite` instead of `resize_uninitialized`, failing to guarantee that address and capacity survive moving from a non-inline source string, and generally doing their level best to make strings nonviable for efficient use as general-purpose buffers.

The `basic_flexi_string` classes go in the extreme opposite direction, providing stronger guarantees, fine-tuned controls, and even explicit buffer attachment and detachment. Aside from ownership, they offer two levels of borrowing, allowing the coder to decide which one to use.

I would argue that this is more faithful to the C++ spirit of only paying for what you use and actually trusting the coder. Call me a revanchist, but it sometimes seems to me as though the C++ committee is the greatest enemy of C++, far deadlier than Rust or Go or Charles Nelson Reilly.

Having said this, the class family has significant guardrails built in to make it safe to use this flexibility. This includes the automatic promotion of unchecked borrowing to ownership during copy, more restrictive construction and assignment for `readonly_string`, and a nuanced and sensible distinction between empty and null.

Fundamentally, this is not a literal drop-in replacement for `std::string`. It is a distinct type, and there is no way to adopt a `std::string`'s buffer, so conversion in either direction copies. But, hey, if `std::string` knew how to play well with others, we probably wouldn't need this class in the first place. (Section 10 describes the interop.)

## 2. Goals and non-goals

Goals:

- Moving a string transfers its dynamic buffer: capacity is retained and, when the string is not in inline mode, `data()` is unchanged.
- A `readonly_string` can point directly at static data, including as a `constexpr` variable.
- Borrowing is always spelled out at the call site. No construction or assignment borrows by default.
- An unsafe borrow cannot spread by copying.
- Null and empty are distinct states, conserved wherever the code path can see them, consistent with `string_view_wrapper`.
- The buffer is not a black box: it can be attached and detached through `string_buffer`.
- Const never silently becomes mutable.
- Promise only what can be delivered. Every guarantee in this document is about the string's current value, not about values assigned later.
- Reuse `std::basic_string_view` for searching and comparison.
- No endianness assumptions and no stolen bits in the v1 layout.
- `constexpr` wherever possible.

Non-goals:

- ABI or type compatibility with `std::string`.
- Custom character traits for changing semantics.
- Copy-on-write or reference counting of any kind.
- Any promise that assignment preserves a string's address (section 15 describes a future policy that can).

## 3. Types and template shape

```cpp
template<typename CharT,                       // const-qualified => read-only
    flexi_string_policy Policy = flexi_string_policy::basic>
class basic_flexi_string;

using flexi_string    = basic_flexi_string<char>;
using readonly_string = basic_flexi_string<const char>;
```

- **One template, two families.** A const-qualified `CharT` selects the read-only family, in the way `std::span<const T>` relates to `std::span<T>`. The template detects the const and strips it for `value_type`, the traits type and the view type.
- **Siblings, not parent and child.** `basic_flexi_string<char>` and `basic_flexi_string<const char>` are unrelated types. There is no inheritance and no reference conversion between them. Unlike `span`, converting FS to RS is not free; section 7 gives the costs.
- **Mutators are constrained.** Every member that edits characters or changes size in place has `requires` restricting it to the non-const family. So does `reserve`, because a `readonly_string` can never append to its buffer and has no use for spare capacity. The read-only family also gives only const access to its characters: `data()`, `operator[]`, `at`, `front`, `back` and the iterators all return pointers, references and iterators to const, whether or not the string object is const. The read-only family keeps construction, assignment, `swap`, `reset`, `clear`, buffer attach and detach, and all const members. `clear` is there because, on a `readonly_string`, it counts as an assignment and not as a resizing. It is equivalent to assigning an empty value (section 6).
- **Concepts name the two families.** `FlexiString` is satisfied by `basic_flexi_string<char, Policy>` under any policy, and `ReadOnlyString` by `basic_flexi_string<const char, Policy>` under any policy. `FlexiStringOf<CharT>` and `ReadOnlyStringOf<CharT>` are the general forms for other character types, and both take the unqualified character type, so `ReadOnlyStringOf<char16_t>` matches `basic_flexi_string<const char16_t, Policy>`. All four look through cv-qualifiers and references, so they also work on a forwarding reference. They are a convenience inside the implementation, but their main purpose is to encourage templated code to take `const FlexiString auto&` where it would have taken `const flexi_string&`, so that it accepts every policy.
- **Character types.** The unqualified character type must satisfy the existing `CharType` concept, which admits `char`, `wchar_t`, `char8_t`, `char16_t` and `char32_t`. A volatile-qualified `CharT` therefore does not compile.
- **Traits** are `std::char_traits` of the unqualified character type. `string_view_wrapper` takes only `CharT`, and this design does the same.
- **Policy** is a value-type template parameter in the style of `invocable_policy`, with its own struct (section 12).
- **Allocation.** In v1 the string allocates with `new value_type[n]` and frees with `delete[]`. (The element type is the unqualified `value_type` in both families, because a buffer allocated as `const char` could never be filled.) There is no allocator parameter. This is what makes buffer exchange exactly paired (section 11). Allocator support is deferred (section 15).
- **`size_type` is `size_t`** in the public interface, whatever the policy does to the stored fields.
- **Track the standard.** Support current `std::string` members, including C++26's `subview`. The `string_view_wrapper` family should gain `subview` wherever it offers `substr`.

Names marked _(placeholder)_ should be aligned with existing Corvid conventions.

## 4. Storage categories

Every string is in exactly one of these states:

| Category     | Meaning                                                                                                                 | Buffer freed on destruction | `flexi_string` | `readonly_string` |
| ------------ | ----------------------------------------------------------------------------------------------------------------------- | --------------------------- | -------------- | ----------------- |
| Inline (SSO) | Characters live inside the object                                                                                       | Nothing to free             | Yes            | Yes               |
| Owned        | Dynamic buffer owned by this string                                                                                     | Yes                         | Yes            | Yes               |
| Static       | Safely borrowed from a buffer that is promised to outlive every use, by this string or any copy of it, and never change | No                          | No             | Yes               |
| Unchecked    | Unsafely borrowed from a buffer owned by someone else, with no lifetime promise (just like `std::string_view`)          | No                          | No             | Yes               |
| Null         | No buffer at all; distinct from empty (section 9)                                                                       | Nothing to free             | Yes            | Yes               |

Static and Unchecked have the same representation: a pointer to a terminated buffer that the string does not own and cannot modify. The category records only whether the borrow is trusted. That one bit is what lets copies and `promote_unchecked` fix unsafe borrows while leaving literals alone.

Invariant for `flexi_string`: **it is always Inline, Owned or Null.** It never borrows, which is what makes it impossible to write into static or foreign memory. (With `string_buffer`, there is a mechanism to allow taking full ownership of a buffer and therefore safely writing into it.)

A `readonly_string` writes characters only to store an entire new string value, and only into memory it knows to be writable, which is limited to its own inline buffer or a dynamic buffer it has just allocated. After this initialization, it does not modify that dynamic buffer again, nor does it modify the inline buffer except to initialize it with another assigned value.

One reason for this discipline is that the dynamic buffer may have come from `attach`ing a `string_buffer<const char>`, which the `readonly_string` now owns, that never offered write access. As a result, such a buffer must come with the string already terminated.

When a `string_buffer<char>` is `attach`ed, the `readonly_string` does zero out the terminator, but afterward treats the dynamic buffer, which it now owns, as const.

In contrast, a `flexi_string` is free to modify and reuse its dynamic buffers, as it only ever `attach`es `string_buffer<char>`.

`capacity()` returns `size()` for Static and Unchecked, and 0 for Null.

## 5. Layout (v1, default policy: inline or dynamic)

The libstdc++ layout, chosen for v1 because `data()` and `size()` are plain loads (Appendix A compares it with the other layouts in use):

```cpp
CharT*     ptr_;                    // points at the characters; nullptr when null
size_t     size_;
union {
  size_t     cap_;                  // valid only when ptr_ != buf_
  value_type buf_[inline_cap + 1];  // 16 bytes by default
};
```

- 32 bytes on a 64-bit target with the default 16-byte inline buffer, which holds 15 characters of `char`, 7 of `char16_t` or 3 of `char32_t` (section 12).
- **`ptr_` is a `CharT*`, so in a `readonly_string` it is a `const char*`.** The inline buffer is an array of the unqualified `value_type` in both families. A `readonly_string` writes to it by naming `buf_` directly, never through `ptr_`, and only to store a new value, whether by assignment or by its moral equivalent, such as `clear`, `swap` or `shrink_to_fit` (section 4).
- **Inline is detected by `ptr_ == buf_`.** This test is relative to the object itself and is safe across module boundaries.
- **`cap_` overlaps `buf_`.** Always test for inline mode before reading `cap_`.
- **Non-local capacity sentinels:**

When `ptr_` does not point to `buf_`, the string is not using the inline buffer, and `cap_` encodes its state:

| `cap_`         | State                                            |
| -------------- | ------------------------------------------------ |
| 0              | Static borrow; or when `ptr_` is `nullptr`, Null |
| 1              | Unchecked borrow                                 |
| greater than 1 | Owned; the value is the real capacity            |

- Every owned dynamic buffer must have a string capacity of at least 2, which means an allocation of at least 3 elements. There is no other threshold; a dynamic buffer capacity smaller than the inline capacity is representable, though the string never produces one itself.
- The destructor frees only when the string is not local and `cap_ > 1`.

### Null representation

A null string is `ptr_ == nullptr`, size 0, capacity 0. It has no buffer and no terminator, and there is only one kind of null.

- The test is per object, so it is safe across module (DLL or shared object) boundaries. We never encode state by comparing against the address of a static.
- No `flexi_string`'s stored pointer refers to a shared terminator. A non-null but empty `flexi_string` in this layout is in inline mode with its own writable terminator; this is what `data()` returns.
- A shared constant appears in two places. For a null string, `c_str()` refers to a const static empty string, exactly as `cstring_view::c_str` does today. And a `readonly_string` that is emptied borrows that same string as Static (section 6), which is safe because a `readonly_string` never writes through its pointer. In both places, the address of the constant is never compared, and it cannot be written through.
- `c_view()` returns a null `cstring_view`. This likewise returns a `nullptr` from its `data()` and likewise guarantees that its `c_str()` refers to a const static empty string.

### Known costs of this layout

- **Not trivially relocatable.** The self-pointer means a move needs a branch and a fix-up.
- **Non-static `constexpr` locals in inline mode do not compile.** A self-pointer into automatic storage is not a valid constant result under C++23. The C++26 draft relaxes the rule, so that a `constexpr` local may point at automatic storage of the same function (P2686), but clang 23 does not accept that yet, even in C++26 mode. Until compilers catch up, a `constexpr` local can be Static or Null, but it cannot hold an inline value, at least in the v1 layout. (No `constexpr` variable can own a dynamic buffer, in any layout, because an allocation cannot outlive constant evaluation.) A self-pointer into static storage is a valid constant, so this layout could hold an inline value in a `constexpr` variable at namespace scope or in a `static` local, but the design does not promise it (section 7).
- **Only 15 of 32 bytes are usable for inline storage.** The v2 layout in section 15 removes the first and third costs. It trades the second for a different one, because it cannot use inline storage during constant evaluation at all.

### Constant evaluation

A `constexpr` variable must have every subobject initialized. Fill the whole inline buffer only during constant evaluation, and write only the terminator at runtime:

```cpp
if consteval {
  for (size_t ndx = 0; ndx <= inline_cap; ++ndx) buf_[ndx] = value_type();
}
```

The loop assigns by index because that is what makes `buf_` the active member of the union. `std::fill` writes through a pointer, which does not, and clang rejects it in constant evaluation.

Do not give the array a default member initializer. Runtime construction must never pay for zeroing the unused part of the inline buffer, however large it is. Bytes after the terminator are otherwise unspecified and are not re-zeroed by mutators.

### Storage classes and access between specializations

Implement storage as separate specializations per storage policy (section 12) behind one private interface. The interface exposes operations, not fields: queries such as "is local" and "which category", and transitions such as "adopt this buffer with this capacity". The bulk of the string class is written in terms of these wrappers, and they are the only code that touches raw members. This is also what keeps a later change of layout contained.

Copies, moves and conversions between different specializations go through the source's accessors, never its fields, because the two sides may have different layouts. Specializations of a template are not friends of each other by default, so the class needs a template friend declaration for those accessors to stay private.

## 6. Move, copy and capacity semantics

### Move

- **Move construction transfers the buffer.** For a non-inline source, the destination has the same `data()` and the same capacity. For an inline source, the characters are copied.
- **A move always steals a dynamic buffer when there is one**, even if the contents would fit in the destination's inline buffer, and even when the destination is a different specialization with a larger inline buffer. The only exception is a destination that cannot hold a dynamic buffer.
- **The moved-from string is in the default-constructed state** (null). This is a guarantee. `std::string` promises only that a moved-from string is valid but unspecified, so code that expects it to be empty is relying on the implementation. Here, the state is part of the contract.
- **Move assignment and `swap`** give the same guarantee. A move assignment replaces the destination completely. The destination's old buffer is always released, never reused and never handed to the source, so the source is null after assignment, just like it is after construction. A source that is not in inline mode has its buffer stolen. A source in inline mode has no buffer to steal, so its characters are copied into the destination's inline buffer, which leaves the destination in inline mode even if it was in dynamic mode before. Deallocation cannot throw, and between strings of the same type, an inline value always fits the inline buffer, so this is `noexcept`. (Between specializations, the destination's inline buffer may be too small, and the move then allocates a dynamic buffer; see section 12.)
- **The move constructor must be `noexcept` and must not allocate**, in every policy. Otherwise `std::vector` copies on reallocation, which defeats the address guarantee.
- **A move preserves the category.** Static stays Static and Unchecked stays Unchecked. Moving is how a caller deliberately keeps an unchecked borrow.

### Copy

- **`flexi_string` copy** is a deep copy. A short string copies into the inline buffer even if the source was in dynamic mode.
- **`readonly_string` copy:**

| Source category | Result                                                   |
| --------------- | -------------------------------------------------------- |
| Static          | Shares the buffer; no allocation                         |
| Unchecked       | **Promoted: deep copy into an Owned (or Inline) string** |
| Owned           | Deep copy                                                |
| Inline          | Copy                                                     |
| Null            | Null                                                     |

Promotion on copy is the central safety rule. A function that receives a `const readonly_string&` and copies it into a map key cannot store a dangling key. An unchecked borrow is confined to the object it was created in and to moves of that object.

Places where a copy happens without looking like one (both promote, so they are safe but may allocate):

- returning a non-local, such as a member or an lvalue-reference parameter, by value;
- initializer lists, for example `std::map<readonly_string, int> m{{key, 1}}`.

Returning a non-const local by name does not copy: it is elided or implicitly moved, so an Unchecked local stays Unchecked when returned. (Other ways of returning a local can copy, and so promote: a `const` local whose copy is not elided, or a conditional expression that picks between two locals.) That is the same hazard as returning a `std::string_view` to a local and is documented, not prevented. The function may also use `promote_unchecked` on the value it's about to return, to prevent dangling.

### Dynamic buffer mode

- **Becoming null gives up the buffer, ending dynamic mode.** `reset`, being moved from, and assignment of a null value all leave the default state. A value assigned afterwards may land in the inline buffer.
- **Other than by becoming null or being moved onto, a string in dynamic mode returns to inline mode only through `shrink_to_fit`.** This rule of staying in dynamic mode applies to copies and to edits in place. A move replaces the destination completely (see Move above).
- **A `flexi_string` in dynamic mode keeps its buffer.** `clear`, `erase`, `resize` and copy assignment of a non-null value that fits all reuse it, even when the value would also fit the inline buffer. A new dynamic buffer is automatically allocated only when the value does not fit the current one.
- **A `readonly_string` in dynamic mode stays in dynamic mode, but never reuses its buffer**, because it cannot write to it (section 4). Copy assignment of a value that it must own (an Unchecked, Owned or Inline source in the copy table above) allocates a fresh dynamic buffer and frees the old one, even when the value would fit the inline buffer. A caller who wants inline mode back calls `reset` first. Copy assignment of a Static source shares the source's buffer, as the copy table says, and frees the old one.
- **An empty value never costs a `readonly_string` an allocation.** When a `readonly_string` in dynamic mode is assigned an empty value that it must own, it frees its buffer and becomes a Static borrow of the shared const empty string, the one that `c_str()` returns for a null string. Allocating a dynamic buffer to hold a lone terminator would be wasteful, and the inline buffer is ruled out because its address does not survive a move. The shared string has neither problem. `clear()` on a `readonly_string` is this assignment under another name, in every mode. A string in dynamic mode becomes the Static empty string, and any other string, a null one included, becomes empty in its inline buffer. After the switch to Static, the string is no longer in dynamic mode, so a short value assigned later may land in the inline buffer.
- **`shrink_to_fit` does real work on both families.** When a string in dynamic mode holds a value that fits the inline buffer, `shrink_to_fit` copies the value there and frees the dynamic buffer, which returns the string to inline mode. When the value does not fit inline and the dynamic buffer is much larger than the value needs, it moves the value to a smaller dynamic buffer. It does not reallocate just to reach an exact fit, and how much slack it tolerates is up to the implementation. A `readonly_string` can do both, because each one writes only to the inline buffer or to a buffer that was just allocated (section 4). On an Inline, Static, Unchecked or Null string, `shrink_to_fit` does nothing.
- **Growth doubles.** When a `flexi_string` outgrows its buffer, the new capacity is at least twice the old one, or the capacity the operation needs if that is larger. Other growth factors, and a minimum size for the first dynamic allocation, are possible policy settings (section 15).
- The design exposes `is_inline_buffer`, `is_dynamic_buffer`, and the static constant `inline_buffer_capacity` (section 12).
- `reserve` exists only on `flexi_string` (section 3). It is eager, as in `std::string`: after `reserve(n)`, an edit in place that keeps the size within the capacity does not relocate the buffer. Replacing the value is another matter. `reset`, move assignment, `swap`, `detach`, `shrink_to_fit` and the assignment of null can all change the address, however much was reserved. `reserve(0)` is a no-op and leaves a null string null. Any other reserve converts the null to an empty with at least the required capacity. Note that even something like `reserve(6)`, which does not switch from inline to dynamic mode, converts null to empty.
  (An alternative was considered but ultimately rejected. It would have involved lazy allocation when non-zero `reserve` was called on a null string, by reusing `cap_`. This was incompatible with the eagerness requirement, however.)

### The three promotion calls

Each call promises something about the string's **current value**. None of them says anything about values assigned later.

| Call                  | Promise                                           | Effect                                                            |
| --------------------- | ------------------------------------------------- | ----------------------------------------------------------------- |
| `promote_unchecked()` | The value cannot dangle                           | Unchecked becomes Owned or Inline                                 |
| `stabilize_address()` | Any move of the value keeps its address           | Inline becomes dynamic                                            |
| `make_dynamic()`      | Both of the above, and the string owns its memory | Inline, Static and Unchecked all become Owned in a dynamic buffer |

- The first two are independent axes: lifetime and address. Calling both, `promote_unchecked` first, gives a value that cannot dangle and keeps its address across moves without copying Static data. There is deliberately no dedicated method for that combination, as the cost of calling one and the other in sequence is low.
- **The promise starts after the call.** A promotion call may itself move the value to a new buffer, which invalidates any pointer taken before it. It is the address that `data()` returns afterwards that is stable.
- **`stabilize_address` covers moves only**, and only moves that can transfer the buffer. Those are moves between strings of the same type, and moves into another specialization that can hold a dynamic buffer. A move into an inline-only string copies, and so does building a `flexi_string` from a `readonly_string`. Growing past capacity still reallocates. Calling this method guarantees that when this value is moved into something else, the address remains stable. It can't promise that the buffer won't be reallocated by something like `append` or that it won't be deleted when a new value is moved in. We do guarantee that a copy into a `flexi_string` that is in dynamic mode will reuse that dynamic buffer if the source fits into it, even when the source is small enough for inline storage. A `readonly_string` destination gives no such guarantee, because it never reuses a buffer. A move gives none either: a move from a source in inline mode lands in the destination's inline buffer, and a move from anything else steals the buffer.
- **`make_dynamic` offers the maximal guarantee:** it also copies Static data, which is what code needs if it must survive the unloading of the module that held the literal, or if the Static data was actually backed by an intern table that will go away.
- **All three are fluent, through ref-qualified overloads.** Called on an lvalue, each returns an lvalue reference to the string, so the calls chain: `key.promote_unchecked().stabilize_address()`. Called on an rvalue, each returns an rvalue reference, so a promoted string can go straight to whatever stores it: `m.emplace(std::move(key).promote_unchecked(), value)`. The call itself moves nothing, and the `std::move` at the call site shows where the string is given up. The doc comment on each of the three functions shows the insertion example.
- **On a null string all three are no-ops.** A null string has no address to change and nothing to dangle.
- **All three exist on both families**, as no-ops where they have nothing to do, so generic code compiles against either. On a `flexi_string`, `stabilize_address` and `make_dynamic` are identical and `promote_unchecked` does nothing.
- They can throw only on allocation.
- A string that must never be in inline mode, whatever is assigned to it and even after it's been `reset`, uses the dynamic-only policy (section 12).

## 7. Construction and assignment rules

The rule for both families: **without a borrow wrapper, the string owns.** The exception, as outlined above, is that copying a `readonly_string` with a Static borrowed value to a `readonly_string` just shares the value.

### `readonly_string`

| Form                                                                                                        | Result                               |
| ----------------------------------------------------------------------------------------------------------- | ------------------------------------ |
| `"abc"_rs`                                                                                                  | Static                               |
| `readonly_string ro{borrow_static(x)}` or `ro = borrow_static(x)`                                           | Static, on the caller's word         |
| `readonly_string ro{borrow_unchecked(x)}` or `ro = borrow_unchecked(x)`                                     | Unchecked                            |
| `readonly_string ro{x}` for any non-family type convertible to `std::string_view`, including `cstring_view` | Deep copy; Owned or Inline           |
| `readonly_string ro{p}` for a character pointer                                                             | Deep copy; **explicit**              |
| `readonly_string ro{fs}`                                                                                    | Deep copy, always                    |
| `readonly_string ro{std::move(fs)}`                                                                         | Steals the buffer; `fs` is left null |
| `readonly_string ro{other_ro}`                                                                              | Copy rules from section 6            |
| `readonly_string ro{nullptr}` or default                                                                    | Null                                 |
| Any of the above, during constant evaluation                                                                | Same as at runtime (see below)       |

The read-only family is deliberately stricter than `std::string`:

- **Every deep-copying constructor is explicit**, including the one from a raw character pointer. `readonly_string ro = "abc";` does not compile; write `"abc"_rs`. Someone holding only a `char*` who wants anything other than a copy passes it to a borrow wrapper, which says what they want.
- **Assignment from a view, a `std::string`, a character pointer or an FS lvalue does not compile.** Write `ro = readonly_string{sv};`. Assignment compiles from another `readonly_string`, from an FS rvalue, from a borrow wrapper, and from `nullptr`.

Notes:

- **Construction from `std::string_view` must own.** A plain view has no terminator, and a `readonly_string` must provide `c_str()`, so there is nothing to borrow.
- **Construction from `cstring_view` also owns.** It could be borrowed, but "no wrapper means own" still applies, so you have to use a borrow wrapper to avoid a deep copy.
- **Constant evaluation.** Constructors behave at compile time as they do at runtime. Without a borrow wrapper, they own. A `constexpr` function that builds, copies and returns strings therefore gives the same values in both.
- **A `constexpr` variable is Static or Null.** It cannot own a dynamic buffer, because an allocation cannot outlive constant evaluation. An inline value is not supported there either. Whether one can work depends on the layout and on the variable's storage duration (section 5 and Appendix A), and nothing needs it, since the initializer of a constant is static data that can be borrowed without a copy. So a `constexpr readonly_string` gets its value by borrowing it as Static, and it has to say so, as every borrow does. `constexpr readonly_string k = "abc"_rs;` is the short form. A literal operator cannot name a policy, so every other specialization writes the wrapper out: `constexpr basic_flexi_string<const char, my_policy> k = borrow_static("abc"_csv);`. Both forms work at any length and in any scope. `constexpr readonly_string k{"abc"_csv};` asks for an owned copy, and is not supported. A `constexpr flexi_string` variable can only be null, because a `flexi_string` never borrows. (Section 15 describes a literal type that would remove the need for the wrapper.)
- **A constant that will be stored in `readonly_string`s is best declared as a `constexpr readonly_string`,** so that every copy of it shares its buffer. This is a recommendation, not a rule. `constexpr` constants of type `cstring_view` and `std::string_view` keep their place, for example as arguments to functions that take views. They do not replace a Static `readonly_string`, because building a `readonly_string` from one copies it, unless it goes through `borrow_static`.
- **`fixed_string` needs no special support**, and `readonly_string` must not include its header. `fixed_string::c_view()` returns a `cstring_view`, which can go through a borrow wrapper for Static borrowing.

### Borrow wrappers

`borrow_static(x)` and `borrow_unchecked(x)` are function templates. Each deduces the character type and returns a small wrapper holding a `cstring_view` of its argument. `readonly_string` is implicitly constructible and assignable from the wrappers, so they work in assignment, as function arguments, and through forwarding: `m.emplace(borrow_static(k), v)` constructs the key in place. Both are `constexpr`, so `borrow_static` can initialize a `constexpr readonly_string`.

- **Only terminated sources are accepted:** `cstring_view`, `std::string`, FS, RS, and character pointers. A `std::string_view` is rejected at compile time.
- **A string that holds its value in its own buffer cannot be assigned a borrow of that buffer.** When a `readonly_string` is Inline or Owned, assigning it a Static or Unchecked borrow that overlaps its own buffer, as in `ro = borrow_unchecked(ro.c_str() + 2)`, is a precondition violation, checked with an `assert`. The assignment would free or overwrite the characters it borrows. When the string is already borrowing, the same assignment is harmless.
- **The same precondition covers move assignment and `swap`.** A second `readonly_string` can carry a borrow of the first one's buffer into it: `readonly_string borrower{borrow_unchecked(owner.c_str() + 2)}; owner = std::move(borrower);`. The move releases `owner`'s buffer and then adopts a pointer into it, which is the same violation reached in two steps, and it gets the same `assert`. A `swap` with a string that borrows the other's inline buffer is also a violation, because the characters change objects and the borrow does not follow them. (In the v1 layout, a borrow that starts at the destination's own inline buffer would also be mistaken for inline mode.) Copy assignment from such a borrower is supported. It copies the characters before it releases or overwrites the old buffer, and it allows for the two ranges overlapping.
- **A null source yields a null string.** `borrow_static` or `borrow_unchecked` of a null `cstring_view` or a null character pointer produces Null, not a Static or Unchecked string that holds a null pointer.
- **Overloads for rvalue owning strings are deleted.** `borrow_unchecked(std::string{"x"})` would dangle at the end of the statement.
- **`borrow_static` is a promise, not a proof.** It is correct only if the buffer is never modified and outlives every copy. Using it for strings held in an intern table is intended. Using it on a mutable FS is almost always wrong; this is where Unchecked borrowing makes more sense. But if the caller says "trust me", the library does, so it is important never to abuse this trust.
- **`borrow_unchecked` asks for everything a `std::string_view` does, and one thing more.** The buffer must stay alive and in place, as for a view. The character at the borrowed size must also stay a terminator. A `std::string` that is appended to within its capacity keeps its address but overwrites that terminator, and `c_str()` on the borrower then runs on into the appended text, where a `std::string_view` of the same range would still be fine. The other characters may change, which a Static borrow does not allow. A copy (or `promote_unchecked` or `make_dynamic`) repairs the borrow with a deep copy, but only while the borrow is still valid. Nothing repairs one that has already dangled.

### Literal operators

- **`"abc"_rs`** is `consteval` and yields a Static `readonly_string`. A string literal is static and terminated by the language's own rules. It yields the default specialization only.
- **`"abc"_fs`** yields a `flexi_string`. It always copies, into the inline or dynamic buffer. It is a convenience in the manner of `"abc"s`. It likewise yields only the default specialization.
- Both operators use the length that the compiler supplies, so a literal with embedded zero characters keeps all of them. Embedded zeros are supported throughout, as they are in `std::string` and `cstring_view`.

### `flexi_string`

FS offers a rough superset of `std::string`, so that it passes the shared tests (section 13):

- implicit construction from a character pointer and from `nullptr`;
- an explicit, templated constructor from any non-family type convertible to `std::string_view` and not convertible to a character pointer;
- implicit **assignment** from the same set of types. An implicit constructor would hide a copy at a call site (`foo(sv)` silently building a temporary); an assignment shows both sides. That is why `std::string` allows one and not the other.

**A `flexi_string` can be constructed with a capacity.** `std::string` has a constructor that takes a count and a fill character, where the count is the `resize` count, and none that takes a capacity, which is the `reserve` count. `flexi_string` adds one through a wrapper function in the style of the borrow wrappers: `flexi_string s{reserve_capacity(n)};` _(placeholder)_ is equivalent to default construction followed by `reserve(n)`. The result is empty and non-null, with a capacity of at least `n`, or null when `n` is 0. It is a wrapper and not a bare integer because a bare count would be read as the size, and because a wrapper forwards as a single argument, as in `v.emplace_back(reserve_capacity(256))`. `readonly_string` has no such constructor, for the same reason that it has no `reserve`.

**Construction of an FS from an RS always deep-copies**, lvalue or rvalue, whatever the category. A `readonly_string`'s buffer may be const memory (section 11), and const never silently becomes mutable. Only the other direction steals: an FS moved into an RS.

### Null sources

Every constructor and assignment from a non-family type calls `is_null_string` on the original object before converting it, and produces a null string when it returns true (section 9).

### Guidance for parameters

- Read-only parameters should generally be `std::string_view` or `cstring_view`.
- A function that stores the string takes `readonly_string` by value, lvalue reference, or rvalue reference, depending on your needs. If the parameter is copied at any point, that automatically performs the moral equivalent of `promote_unchecked`. If it's only ever moved, you will need to call this explicitly. The call is fluent (section 6), so the insertion can read `m.emplace(std::move(key).promote_unchecked(), value)`.

## 8. Comparison, hashing and views

- **Searching and comparison delegate to `std::basic_string_view`.** Replicate the `std::string` overload set exactly, including the templated overloads for types convertible to `string_view` and their constraints, so overload resolution matches `std::string`.
- **Comparison operators** follow the `string_view_wrapper` pattern: `operator==` and `operator<=>` are hidden friend templates whose right-hand side is any type convertible to the view, and each compares the two views. Mixed comparisons among FS, RS, `std::string`, views and literals then resolve without ambiguity, and so do comparisons between two family types.
- **`operator==` checks identity first.** If the pointers and sizes match, the strings are equal without comparing characters. Borrowed buffers make this relatively common for `readonly_string`.
- **`view()`** returns `std::basic_string_view`. **`c_view()`** returns `cstring_view`. `fixed_string::cview()` is to be renamed `c_view()` to match, echoing `c_str()`.
- The conversion to `std::basic_string_view` is implicit. The conversion to `cstring_view` is explicit.
  (We considered converting implicitly to `cstring_view`, since that better preserves the nullability and termination, but it breaks the basic idea that strings convert to `std::string_view`, and you can always use `c_view` explicitly if you want a `cstring_view`.)
- **Required integrations:** `operator<<`, `std::hash`, and `std::formatter` (with range formatting disabled, as for other Corvid string types). Probably easiest to implement by just formatting the `c_view()` return value.
- **`std::hash` matches `std::hash<std::basic_string_view>`** over the same characters, as the `string_view_wrapper` family's hash does. Transparent lookup in unordered containers depends on it.
- **Map usage:** works as a key, with transparent lookup by `std::string_view` when the container has a transparent comparator (`std::less<>`), or, for an unordered container, a transparent hash and `std::equal_to<>`.
- **Thread safety** is the same as `std::string`. There is no shared mutable state.

## 9. Null semantics

These follow `string_view_wrapper` and `cstring_view`: null is a distinct state that behaves like empty except where asked.

- **Default construction yields null**, for both families.
- **Constructible from `nullptr` and from a null character pointer.** Both yield null, as in `opt_string_view`.
- **Null arises only from** default construction, construction or assignment from a null source, being moved from, and `reset()`. Every other mutator leaves the string non-null. For example, `clear()` on a null string yields an empty, non-null string (which retains the dynamic buffer, if any, hence preserving capacity).
- **Accessors on a null string:** `null()` is true, `empty()` is true, `size()` is 0.
- **The optional-style interface of `string_view_wrapper` is provided**, on both families and under every policy, with null as the absent state: `has_value`, `explicit operator bool`, `value`, `value_or`, `operator*`, `operator->` and `as_optional`. We also allow explicit construction from `std::optional<std::string>`.
- **`data()` returns `nullptr` exactly when `null()` is true**, in every policy. For an empty string it returns a pointer to the terminator.
- **`c_str()` never returns `nullptr`.** For a null string it returns a pointer to a const static empty string, as `cstring_view::c_str` does. This keeps `printf("%s", s.c_str())` safe on a default-constructed string.
- **A null string has no characters, not even a terminator.** `s[0]` on a null string is a precondition violation. The behavior is undefined, and in practice it is a null dereference. `std::string` permits reading and writing `CharT()` at `s[s.size()]`; here that holds for every non-null string but not for null.
- **Growing a null string** in the default policy enters inline mode when the result fits; it does not go straight to the dynamic buffer.
- **Capacity-only calls:** `reserve(0)` and `shrink_to_fit` leave a null string null. A `reserve` with a non-zero argument makes it non-null and empty, entering inline mode if the request fits there.
- **Comparison and hashing ignore nullness.** `==`, `<=>` and `std::hash` treat null and empty as equal. `same()` distinguishes them.
- **The conversion to `std::basic_string_view` maps null to a view with a null `data()`.** In the pointer layouts this is free. Nullness therefore survives a round trip through code that checks for it. Someone who builds a view by hand from `c_str()` gets what they get.
- **Formatting and streaming** handle null exactly as `opt_string_view` and `cstring_view` do today, through the same formatter support (including its option for depicting null explicitly).

### `is_null_string`

A templated free function that decides whether an object counts as a null string. It is general-purpose and lives outside the `flexi_string` header, in `string_view_wrapper.h`, which is its first customer. Its list is therefore potentially wider than the set of types the strings convert from, and the paths for the other types are simply never reached from here. It returns true for:

- `nullptr`;
- a null character pointer;
- any object with a `null()` member that returns true;
- an empty `std::optional`;
- a `std::basic_string_view` whose `data()` is null (consistent with how `string_view_wrapper` already reads views);
- a `std::unique_ptr<char[]>` or `std::shared_ptr<char[]>` or other smart pointer or pointer-like type that returns a null pointer to the character.

`string_view_wrapper` should be updated to use this to detect a `null()` member on its source during conversion.

## 10. Interop with `std::string`

- **FS or RS to `std::string` needs nothing new.** Because both convert implicitly to `std::string_view`, `std::string{fs}`, `str = fs` and `str.append(fs)` already compile through `std::string`'s own templates. There is no conversion operator to `std::string`; it would be redundant. Null becomes empty.
- **`std::string` to FS or RS** goes through the explicit templated constructor in section 7, and for FS also through assignment.
- **Borrowing a `std::string` into an RS** requires the borrow wrappers.
- These conversions can lose nullness in the `std::string` direction, since `std::string` cannot represent it.

## 11. Buffer exchange: `string_buffer`

The string's buffer can be handed in and out. `string_buffer` is the exchange currency and the public face of the buffer. It is not the internal representation.

```cpp
template<typename T, typename Deleter = std::default_delete<T[]>>
struct string_buffer {
  std::unique_ptr<T[], Deleter> data;
  size_t string_size{};  // characters in use, excluding the terminator
  size_t buffer_size{};  // elements in the allocation
};
```

- **A plain move-only aggregate**, constructible from raw parts.
- **Invariant for a non-null buffer: `string_size < buffer_size`**, so there is always room for a terminator. A null `data` goes with both sizes being 0, and represents a null string.
- **Allocation contract.** The string allocates with `new value_type[buffer_size]`, so the default deleter is exactly paired. In v1, the string accepts only the default deleter.
- **`string_buffer<char>` converts implicitly to `string_buffer<const char>`**, and not the reverse, mirroring `unique_ptr<char[]>` and `unique_ptr<const char[]>`.

### Which family takes which buffer

|                   | Accepts                                               | `detach` returns            |
| ----------------- | ----------------------------------------------------- | --------------------------- |
| `flexi_string`    | `string_buffer<char>`                                 | `string_buffer<char>`       |
| `readonly_string` | `string_buffer<char>` and `string_buffer<const char>` | `string_buffer<const char>` |

- A `flexi_string` can hand its buffer to a `readonly_string` to keep. Nothing comes back the other way as mutable. You would need to do some sort of `const_cast` to bypass this.
- Someone who owns a const buffer can give it to a `readonly_string`.

### Operations

| Operation                                     | Behavior                                                                                                                                                                          |
| --------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `attach(string_buffer&&)`                     | Frees the current buffer and takes ownership. A null buffer makes the string null. Returns `bool`: whether the buffer was adopted in place                                        |
| `detach()`                                    | Returns the buffer, terminated, and leaves the string null. Does not throw when Owned; allocates and copies when Inline, Static or Unchecked. A null string returns a null buffer |
| `reset()`                                     | Frees the buffer and returns to null                                                                                                                                              |
| `explicit` constructor from `string_buffer&&` | Same as `attach`                                                                                                                                                                  |

### Rules

- **Termination.** A mutable buffer does not need to be terminated when handed in; `attach` writes the terminator at `string_size`. A const buffer cannot be written, so it must already be terminated there. The write comes last, after validation and after any allocation, so a call that throws leaves the caller's buffer unmodified as well as unadopted.
- **A bad buffer throws, in every build.** A null pointer with non-zero sizes, `string_size >= buffer_size`, or a const buffer that is not terminated, throws `std::invalid_argument`. The guarantee is strong: the caller still owns the buffer. A wrong `buffer_size` cannot be detected, at least not reliably in a cross-platform way.
- **A valid buffer is never refused.** It is adopted in place, or, when it is too small to represent, its contents are copied (below).
- **`attach` preserves the address of a buffer that it adopts in place.** Afterwards `data()` equals the buffer's pointer and the string is in dynamic mode, even if the contents would fit inline.
- **Too-small buffers are copied.** An allocation of 1 or 2 elements cannot be represented as Owned in the v1 encoding. Its contents (at most one character) are copied and the buffer is freed. The copy goes into the inline buffer, or for a dynamic-only policy into a fresh buffer of the normal minimum capacity. `attach` returns `false` in this case.
- **Round trips work.** Anything `detach` produces, `attach` on the same family accepts in place. Detaching an inline-mode string therefore allocates at least 3 elements.
- **Buffers move freely between specializations** with different inline buffer sizes, because the threshold does not depend on the inline capacity.
- **Exchange across modules needs a shared allocator.** The buffer is released with `delete[]` by whichever module destroys the string or the `string_buffer`. That is safe only when both modules allocate from the same heap, which on Windows means that they share one C runtime instance. The same limit applies to moving any owning string across a module boundary, as it does for `std::string`.
- **Inline-only policies have no `attach` or `detach`.**

## 12. Policy

`basic_flexi_string` takes its own policy struct. It reuses the `storage_policy` enum that `invocable_policy.h` defines today, which moves to a new shared header, `corvid/meta/storage_policy.h`. As part of the move, "heap" is renamed to "dynamic" across the board. The enumerators become `inline_only`, `dynamic_only` and `inline_or_dynamic`, and the rest of the invocable vocabulary follows: `invocable_policy::heap`, `admits_heap`, and the prose in the headers, docs and tests of `flexi_function` and `proxy`. The other `invocable_policy` fields do not apply to strings.

Fields the string policy needs:

- the storage mode;
- the inline buffer size, in bytes, including the terminator (see below);
- overflow behavior: grow, throw `std::length_error`, or terminate. Inline-only cannot use "grow". Other uses of this field are deferred (section 15).

The inline buffer is sized in bytes, not characters, so that one default (16) serves every character type. The value is checked at compile time against the `CharT` it is used with. It must be a multiple of `sizeof(CharT)`, and it must be no smaller than the minimum buffer for that `CharT`, which is the `sizeof(size_t)` bytes the union occupies anyway, or two characters under inline-only. The class derives its capacity from the byte count and exposes it as the `static constexpr size_t inline_buffer_capacity`. That constant is a true capacity: a count of `CharT`, not bytes, which excludes the terminator.

The policy parameter is part of the type from v1. v1 implements the default policy and `static_assert`s on combinations that it does not yet support or that contradict each other. The order in which other policies are implemented is not fixed; dynamic-only is wanted.

### Dynamic-only

- No inline buffer at all. Layout is pointer, size, capacity: 24 bytes.
- Address stability on move is unconditional, for every value the string ever holds. That is the reason to choose this policy.
- **Null has no buffer.** Default-constructed and moved-from strings are a null pointer.
- **Every non-null `flexi_string` owns a dynamic buffer, including an empty one.** An empty string paying for an allocation is the accepted price. `clear()` keeps the buffer; `reset()` frees it.
- The first mutation of a null string allocates a buffer of the normal minimum capacity (likely 15 characters).
- **`clear()` on a null `flexi_string` allocates**, like any other first mutation, so `clear()` is not `noexcept` in this specialization. A further policy setting could avoid this by making `clear()` leave a null dynamic-only string null. It is noted here and not designed yet.
- A dynamic-only `readonly_string` that is empty and non-null can be Static, pointing at a const empty string. `clear()` produces that state (section 6), so on a `readonly_string` it does not allocate.
- `stabilize_address` is a no-op.

### Inline-only

- Layout is a size field followed by the buffer. There is no pointer and no capacity field; capacity is part of the type.
- **Size field type:** the smallest unsigned type that both holds the capacity and is at least as wide as `CharT`. The compiler aligns the buffer; do not use packing pragmas or a raw byte buffer.
- **A minimum buffer size is required** (room for at least two characters).
- **Null is encoded in `buf_[1]`** when size is 0. Both null and empty have a terminator in `buf_[0]`, and `buf_[1]` is otherwise unused. `data()` checks this and returns `nullptr` for null, at the cost of a runtime check in this one layout.
- **No borrowing.** Borrowing requires a pointer and this layout has none, so an inline-only `readonly_string` is always Inline or Null. It is still assignable.
- **Trivially copyable by design**, for both families. Consequences: a move is a copy and leaves the source unchanged (the "moved-from is null" guarantee does not apply); a same-type move cannot throw; reallocating a vector of them can be a block copy; and stale bytes after the terminator are copied along with the object.
- **The destination keeps its address.** The characters live inside the object, so nothing assigned to it changes `data()`, apart from the null case.
- Exceeding capacity follows the policy's overflow behavior.
- No `stabilize_address`, `make_dynamic`, `attach` or `detach`.

### Enlarged inline buffer

- The union's buffer is simply larger. Inline mode detection is still pointer equality and the sentinel rule is unchanged.
- No `[[no_unique_address]]` is needed for the buffer. That attribute is for members eliminated by policy.

### Conversion between specializations

- **A move steals the buffer** when both sides can hold a dynamic buffer, the families allow it (FS into RS, or same family), and the target accepts the source's category.
- Otherwise the characters are copied, and an inline-only target that is too small follows its overflow behavior.
- These conversions are conditionally `noexcept`, following the model of `flexi_function`'s adoption rules.

## 13. Testing

Development is test-driven.

1. **Start with the overlap.** Write a unit test suite against `flexi_string` and `readonly_string` defined as aliases of `std::string`, exercising the interface they share with it.
2. **Keep it as a typed suite.** The same tests run against `std::string` and against the real types, permanently, so `std::string` remains the oracle after the alias is gone. Group the tests by method family so the overlap with flexi-only behavior is minimal. Anything involving null, categories or the strict `readonly_string` rules lives outside the shared suite. A test of a member that the standard library in use may not have yet, such as C++26's `subview`, joins the shared suite only when the library's feature-test macro (`__cpp_lib_string_subview`) says that `std::string` has it.

Flexi-specific tests:

- **Negative-compile tests:**
  - `readonly_string ro = "abc";`, `ro = sv;` and `ro = fs;` are rejected;
  - `borrow_unchecked` and `borrow_static` reject `std::string_view` and rvalue owning strings;
  - character mutators and `reserve` do not exist on `readonly_string`;
  - a `flexi_string` cannot attach a `string_buffer<const char>`;
  - a `constexpr readonly_string` that borrows a local, non-static character array through `borrow_static` is rejected (a pointer to automatic storage is not a constant under C++23; the C++26 draft allows one within the same function, so this test holds only until compilers implement that rule, and it is not yet verified on all compilers).
- **Move guarantee:** `data()` and `capacity()` are unchanged across move construction, move assignment and `swap` for non-inline strings; a short dynamic-buffer string moved into another string does not land in the inline buffer; the moved-from string is null; a `std::vector` of non-inline strings keeps every `data()` pointer across reallocation.
- **Dynamic buffer mode:** on a `flexi_string`, `clear`, `erase`, `resize` and short copy assignment keep the buffer; on a `readonly_string`, short copy assignment of an owned value allocates a fresh dynamic buffer of the smallest size the layout allows for the value and does not land inline; on a `readonly_string` in dynamic mode, `clear` and the assignment of an empty value both free the buffer and leave a Static borrow of the shared empty string, with no allocation; on both families, `shrink_to_fit` moves a short value from a dynamic buffer to the inline buffer, and replaces a badly oversized dynamic buffer with a smaller one; being moved onto from an inline source returns to inline mode; assigning null or calling `reset` frees the buffer (and also re-enables inline mode).
- **Capacity:** growth by appending at least doubles the capacity; `flexi_string s{reserve_capacity(n)}` matches default construction followed by `reserve(n)`, including the null result for 0.
- **Comparison:** `==` and `<=>` for every pairing of FS, RS, `std::string`, `std::string_view`, `cstring_view` and a literal, in both orders.
- **Promotion calls:** each row of the table in section 6, including the no-ops on null and on `flexi_string`; each call returns an lvalue reference when called on an lvalue and an rvalue reference when called on an rvalue.
- **Concepts:** each of the four against both families under more than one policy, against other character types, against cv-qualified and reference forms, and against strings outside the family such as `std::string`.
- **Category matrix:** every row of the tables in sections 6 and 7, checking the resulting category and whether an allocation happened. In particular: copying an Unchecked string promotes it, moving it does not, copying a Static string shares, and an FS built from an RS always copies.
- **Null conservation:** `is_null_string` on every type in its list; construction and assignment from every null source the strings accept; `same()` versus `==`; equal hashes for null and empty; `data()` is null exactly when `null()` is true; `c_str()` is never null.
- **Buffer exchange:** address preserved by `attach`; round trips; too-small buffers copied; exchange between specializations with different inline buffer sizes; each bad-buffer case throws and leaves the buffer with the caller; a const buffer without a terminator is rejected.
- **Constant evaluation:** `constexpr` variables in the Static and Null states, at namespace scope and as locals; a `constexpr` function that builds, copies and returns strings gives the same values at compile time and at runtime; an owning constructor never produces Static.
- **Invariants:** an FS is never Static or Unchecked.
- Run under AddressSanitizer and ThreadSanitizer.
- Tests that depend on a policy v1 does not implement, such as exchange between specializations, arrive with that policy.

## 14. Documented deviations from `std::string`

- It is a distinct type; conversion to and from `std::string` always copies.
- Default construction yields null (which behaves as empty) rather than empty.
- `data()` returns `nullptr` for a null string. `c_str()` never does.
- A null string has no terminator: `s[0]`, and writing `CharT()` at `s[s.size()]`, are precondition violations when `null()` is true.
- The conversion to `std::string_view` yields a view with a null `data()` for a null string.
- Construction from `nullptr` or a null character pointer is allowed and yields null.
- The moved-from state is specified (null), where `std::string` promises only "valid but unspecified", and non-inline moves are guaranteed to transfer the buffer. The inline-only policy is the exception to both (section 12).
- A non-null dynamic-buffer string stays in dynamic mode until `shrink_to_fit` or until it is moved onto. A `readonly_string` also leaves dynamic mode when it takes a Static value (section 6).
- A `constexpr` variable can be Static or Null. One that holds an inline value is not supported, and as a non-static local in the v1 layout it does not compile under C++23 rules.
- Bytes after the terminator are unspecified and may hold stale characters.
- `readonly_string` is stricter than `std::string` about implicit construction and assignment (section 7).
- **Module lifetime.** A Static string points into the data of the module that created it. If that module is unloaded while the string survives, the pointer dangles. `make_dynamic` is the remedy.
- We offer `resize_uninitialized` (proposed in P1072R0 but rejected) in addition to the official `resize_and_overwrite`. And even the latter quietly drops the unenforceable requirement that the callback initialize every character it keeps.

## 15. Deferred: wanted, but not in v1

### v2 layout: a tag in place of the pointer

To be decided by benchmark against the v1 baseline. It is `fbstring`'s idea with the tag moved out of the capacity word, which a 32-byte object has room for, so it needs no stolen bits and no endianness assumption. Shown for a one-byte character:

```cpp
union {
  struct {
    CharT*     ptr;
    size_t     size;
    size_t     cap;
    value_type spare[7];
    value_type tag;
  } external;
  value_type internal[32];  // internal[31] is the tag
};
```

- **The tag is the last element of the object, in both arms of the union.** It cannot sit after the union instead. The pointer gives the union 8-byte alignment, which pads a 31-character arm to 32 bytes, so a tag placed after it makes the object 40 bytes.
- In inline mode the tag holds the remaining capacity. A full 31-character string has 0 remaining, so the tag is the terminator.
- Otherwise the tag holds a category code (Owned, Unchecked, Static, Null), and the capacity sentinels disappear, along with the minimum buffer size for `attach`.
- **Gains:** 31 characters of inline buffer in 32 bytes instead of 15; trivial relocation, since nothing points into the object; inline-only becomes the same structure without the dynamic arm, with null as a tag value.
- **Costs:** `data()`, `size()` and `capacity()` become selects on the tag instead of plain loads; debuggers need a visualizer.
- **No inline mode during constant evaluation.** The tag says which arm of the union is in use, so it has to be read before the answer is known, through an arm that may not be the one in use. At runtime, the tag can be read as one of the object's bytes without naming either arm. Constant evaluation cannot do that, and it rejects a read through the arm that is not in use. So during constant evaluation, the string keeps to the `external` arm, and a value that would be inline at runtime gets a dynamic buffer. A `constexpr` variable is Static or Null either way (section 7). C++26's `std::is_within_lifetime` can ask which arm is active, and would lift the restriction where the library has it.
- **The tag bounds the inline buffer.** The remaining capacity shares one character with the category codes, and both are read as unsigned values. With a one-byte character, that caps the inline buffer at about 250 bytes. The class rejects a policy that asks for more at compile time, unless a different representation is added for it.
- **A possible extra state:** a tag value for "owns a buffer but is logically null", so that assigning null to a string could keep its buffer. v1 has no encoding for this.
- The question for the benchmark is whether the avoided allocations for strings of 16 to 31 characters outweigh the decoding. The arithmetic may well be branch-free, and memory speed usually matters more than a few adds. The storage interface in section 5 keeps the change contained.

### Other layout options

- **Inline buffer in the size bytes.** Keep the v1 pointer layout and inline mode detection, but in inline mode use one byte for the size and the other seven bytes of the size field as extra buffer, for about 22 characters in 32 bytes. It needs no magic bit, and no endianness assumption if the inline size is a separate byte member read after the inline mode test. `size()` becomes a select, though if this doesn't show up as impactful in benchmarks, then the layout is a much easier sell than the `fbstring`-like one above. (And while we won't make endian assumptions, the compiler's optimizer may well take advantage of them to turn the size read into a single fetch followed by an optional masking of the high bits.)

### Limits as policy

- **Maximum dynamic size.** A policy field capping how large a buffer the string will allocate. It lets the storage class pick the narrowest size and capacity fields that hold the cap (for example 32-bit fields under a 4 GiB cap), which either shrinks the object or leaves more room for the inline buffer. The public `size_type` stays `size_t`. It could also be useful for an API, to declaratively limit input sizes.
- **No implicit growth.** A dynamic-only string whose overflow behavior is "throw", with borrowing forbidden. Anything that would reallocate implicitly throws: `append`, `push_back`, `resize`, and copy assignment of a value that does not fit. `reserve` still reallocates, because it is explicit, and the first assignment to a null string allocates. The result is a string whose buffer address survives copy assignment. Move assignment still replaces the buffer. Open decision: whether assigning null frees the buffer (and the guarantee excludes null) or this policy maps a null source to empty.
- Both are limits whose violation throws `std::length_error`, and they belong in the same family of policy fields as the overflow behavior.

### Interning

- A has-a box around `readonly_string`, tagged with a type identifying its intern table, so values from different tables cannot be compared.
- `==`, `<` and `std::hash` on the box are all address-based, as in the existing `interned_string_view`. A map keyed on it works, with an arbitrary iteration order.
- No transparent lookup for interned keys; you search with an interned key.
- The box hands out `readonly_string`s through `borrow_static`, since the table keeps the buffers alive.
- Every module that takes part must share one intern table, since equality is by address.

### Custom deleters and allocators

- **A foreign deleter is a property of one buffer, not of an allocator.** The intended model is a further category, "Adopted": the string stores the deleter that came with a `string_buffer<T, Deleter>`; on first growth it allocates normally, calls the deleter on the old buffer, and becomes Owned. The cost is at least a stored function pointer per object, and more for a deleter that carries state, so it would be policy-gated.
- **The no-op deleter case needs nothing new.** Arena memory released as a block is a borrowed `readonly_string` today.
- **Allocator support**, including `std::pmr`, is the general alternative and would also cover "caller-supplied stack buffer with heap spill". Much of the deleter idea is a `memory_resource` whose `deallocate` calls the deleter and whose `allocate` throws. With allocators, the move guarantees hold only when allocators are equal or propagate.

### Additional policies

- Dynamic-only, inline-only and enlarged inline buffer as specified in section 12, plus conversion between specializations.
- **Growth tuning:** a growth factor other than 2 (4, for example), and a minimum size for the first dynamic allocation, such as twice the inline buffer, which skips the first few doublings.
- **A key type that never borrows unchecked:** a policy option that forbids the Unchecked category, so the string is only ever Static, Owned, Inline or Null and converts unchecked sources on the way in.
- An alias name for the inline-only string. `fixed_string` would match `fixed_function` but is taken by the existing template-parameter class, which also matches wider usage; it will not be renamed.

### Smaller items

- **Tail zeroing:** a method that zeroes the bytes between the terminator and the capacity, for byte-wise comparison, hashing or serialization of inline-only strings, whose whole value lies inside the object, and for scrubbing stale contents before any string's buffer is written to shared memory or a file. It does not make the other layouts comparable byte for byte, since equal strings can differ in pointer, capacity and category.
- **A static literal type:** `_rs` yields only the default specialization, so every other specialization has to write `borrow_static("abc"_csv)`. A literal operator that returns the static borrow wrapper itself (the type that `borrow_static` returns) would serve them all, since each specialization is implicitly constructible from the wrapper: `constexpr basic_flexi_string<const char, my_policy> k = "abc"_static;` _(placeholder)_. It is sound for the same reason `_rs` is. The larger form of the idea has `_csv` return a distinct literal type that converts to `cstring_view`, so that `ro = "abc"_csv` is itself Static.
- **Aliases for other character types** (`wchar_t`, `char8_t`, `char16_t`, `char32_t`), in both families.

### Related follow-up outside this design

Whether `flexi_function` can be usable in constant evaluation. Uninitialized buffer bytes are not the obstacle; constant evaluation forbids constructing an object inside a byte buffer of another type, so the inline path cannot run at compile time. The dynamic buffer path may be viable under C++26 rules, selected with `if consteval`, though the result could not persist to runtime.

## 16. Alternatives considered and rejected

| Alternative                                                                                            | Why rejected                                                                                                                                                                                                                                                             |
| ------------------------------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Mutable borrowed `flexi_string`                                                                        | Would allow writing to static memory; materializing on non-const access breaks `std::string`'s invalidation, `noexcept` and data-race rules                                                                                                                              |
| `flexi_string` borrowing from `std::span<char>`                                                        | Needs a real capacity plus a not-owned flag; aliasing silently stops once the string outgrows the span, and termination is problematic                                                                                                                                   |
| Read-only type as base class of the mutable one                                                        | Assignment or swap through a base reference could plant a borrowed buffer in a mutable string                                                                                                                                                                            |
| Borrowing by default, or whenever the source is a view                                                 | Silent dangling keys in the common "take a view, insert into a map" pattern; `explicit` does not help, because forwarding functions call explicit constructors                                                                                                           |
| An rvalue source as the signal for a trusted borrow                                                    | Moves happen without intent (returns, vector growth, swaps); most rvalue views are function results, not casts; and it would grant the trusted level on a guess                                                                                                          |
| A `consteval` constructor from `cstring_view`                                                          | Cannot coexist with runtime construction from the same type, and rules out assignment and in-place construction; `_rs` and `borrow_static` cover the need                                                                                                                |
| Constructors that borrow as Static during constant evaluation (`if consteval`)                         | A source edited later in the same evaluation is not a lifetime error, so one `constexpr` function could give different results at compile time and at runtime; and a function returning a string built from a local would compile only for runtime use                   |
| Factory returning a `const` value                                                                      | Top-level const is dropped by `auto` and by guaranteed elision                                                                                                                                                                                                           |
| Dropping the Static category and keeping only one kind of borrow                                       | Copies and `promote_unchecked` could no longer repair unsafe borrows without also copying every literal                                                                                                                                                                  |
| Copies of a borrowed string preserving the borrow                                                      | Lets a dangling borrow spread; promotion on copy confines it                                                                                                                                                                                                             |
| A tag argument for borrowing instead of wrapper functions                                              | Workable, but more verbose, reads like an expensive explicit construction, and cannot be forwarded as a single argument                                                                                                                                                  |
| A third promotion method, or an enum switch on `promote_unchecked`                                     | The middle ground is `promote_unchecked` followed by `stabilize_address`; a combined call would fold two independent axes together                                                                                                                                       |
| A null state that remembers a pledge or a pending capacity                                             | No call can promise that a later assignment keeps the address, so the state promised more than it could deliver; the dynamic-only policy covers the real need                                                                                                            |
| A lazy `reserve` on null                                                                               | Would report a capacity it does not have and break `std::string`'s pointer-validity guarantee after `reserve`                                                                                                                                                            |
| An explicit conversion operator to `std::string`                                                       | Redundant: `std::string`'s own templates accept anything convertible to `std::string_view`                                                                                                                                                                               |
| One comparison operator per class against `basic_string_view`, with a `std::type_identity_t` parameter | Ambiguous whenever both operands are family types, the same type included: each operand's operator, reversed, matches as well as the other's                                                                                                                             |
| `readonly_string` detaching a mutable buffer, or being stolen into a `flexi_string`                    | Would let const memory become mutable, and would prevent `readonly_string` from accepting const buffers at all                                                                                                                                                           |
| Custom character traits for interned equality                                                          | `Traits::compare` is used by every operation, and custom traits change the view type                                                                                                                                                                                     |
| A shared terminator as the object's stored pointer for null or empty                                   | If const, the terminator write that `std::string` permits faults. If writable, a stray write corrupts every empty string in the process. The exception is a `readonly_string`, which never writes and so may borrow a const one as Static when it is emptied (section 6) |
| `c_str()` on a null string pointing at the string's own size field                                     | Undefined for every character type but `char`, and impossible in constant evaluation; the shared const static only costs one static character                                                                                                                            |
| Encoding null by the address of a static buffer                                                        | Each module has its own copy of the buffer, so the test fails across DLL or shared-object boundaries                                                                                                                                                                     |
| `data()` returning non-null for a null string                                                          | Needs a shared terminator or a self-pointer in the default state; `c_str()` covers the C-API case                                                                                                                                                                        |
| Requiring every dynamic-only string to own a buffer, including default-constructed and moved-from ones | The move constructor would have to allocate and could not be `noexcept`                                                                                                                                                                                                  |
| Storing a self-relative offset instead of a pointer                                                    | The offset to a dynamic buffer changes on every move, so the fix-up only shifts to the dynamic case                                                                                                                                                                      |
| Capacity stored in a header before the buffer                                                          | Borrowed buffers have no room for a header; adds an indirection to every append                                                                                                                                                                                          |
| Zeroing the whole inline buffer at construction                                                        | Runtime cost grows with inline buffer size; `if consteval` satisfies constant evaluation for free                                                                                                                                                                        |
| Inline capacity specified in characters                                                                | One default cannot serve every character type: 15 characters of `char32_t` is a 64-byte buffer                                                                                                                                                                           |
| `std::unique_ptr<std::array<char, N>>` as the buffer handle                                            | Capacity is a runtime value and `N` is not                                                                                                                                                                                                                               |
| `attach` refusing buffers that would fit in the inline buffer                                          | Breaks exchange between specializations with different inline buffer sizes, and pushes the same fallback copy onto every caller                                                                                                                                          |
| `const_flexi_string` or `immutable_string` as the name                                                 | The object is assignable, and a borrowed buffer can change underneath it                                                                                                                                                                                                 |
| libc++ or fbstring layouts in v1                                                                       | Stolen bits and endian-specific layouts; the tag layout in section 15 gets the benefits without them                                                                                                                                                                     |

## 17. Existing Corvid code this builds on

| File                                    | Relevance                                                                                                                                                                               |
| --------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `corvid/strings/string_view_wrapper.h`  | Null and empty semantics, `null()`, `same()`, the optional-style interface and the comparison pattern; future home of `is_null_string`, which it will use to detect `null()` on sources |
| `corvid/strings/opt_string_view.h`      | Nullable view; conversion target; model for null formatting; offers `substr`, so it is to offer `subview` as well                                                                       |
| `corvid/strings/cstring_view.h`         | Terminated view with the `_csv` literal; the payload of the borrow wrappers; its `c_str()` is the model for null handling                                                               |
| `corvid/meta/fixed_string.h`            | `cview()` to be renamed `c_view()`; no dependency in the other direction                                                                                                                |
| `corvid/meta/invoke/flexi_function.h`   | Model for the policy parameter, policy-dependent storage and conversion between specializations                                                                                         |
| `corvid/meta/invoke/invocable_policy.h` | Current home of the `storage_policy` enum, which moves to `corvid/meta/storage_policy.h` with "heap" renamed to "dynamic"                                                               |

## Appendix A. Survey of string layouts

This appendix records how the well-known string classes lay out their objects, as background for the layout choice in sections 5 and 15. All sizes are for a 64-bit target with the default allocator. The details were read from the sources themselves: the libstdc++ 16 and libc++ 23 headers, and the current main branches of the Microsoft standard library, EASTL, folly, SIMDString and SSO-23.

### Summary

| Layout                                     | Bytes                                       | Inline characters: `char` / `char16_t` / `char32_t` | How it knows it is inline                          | Bits taken from the capacity                   | Growth                        |
| ------------------------------------------ | ------------------------------------------- | --------------------------------------------------- | -------------------------------------------------- | ---------------------------------------------- | ----------------------------- |
| libstdc++ `std::string`                    | 32                                          | 15 / 7 / 3                                          | The pointer equals the address of its own buffer   | None                                           | 2x                            |
| libc++ `std::string`                       | 24                                          | 22 / 10 / 4                                         | A flag bit in the first byte                       | 1; its position depends on endianness          | 2x                            |
| Microsoft `std::string`                    | 32                                          | 15 / 7 / 3                                          | The capacity is no larger than the inline capacity | None                                           | 1.5x                          |
| `eastl::string`                            | 24                                          | 23 / 11 / 5                                         | The top bit of the last byte is clear              | 1; its position depends on endianness          | 2x                            |
| `eastl::fixed_string<T, N>`                | 24, plus the allocator, plus `N` characters | `N - 1`                                             | The pointer equals the address of its own buffer   | None                                           | Only when overflow is enabled |
| `fbstring`                                 | 24                                          | 23 / 11 / 5                                         | The top two bits of the last byte are clear        | 2; their position depends on endianness        | 1.5x                          |
| `SIMDString<64>`                           | 80                                          | 63, `char` only                                     | The allocated size equals the buffer size          | None                                           | Twice the size requested      |
| SSO-23                                     | 24                                          | 23 / 11 / 5                                         | The low two bits of the last byte are clear        | None lost; displaced bits are stored elsewhere | Not implemented               |
| SSO-31 (sketched in the SSO-23 README)     | 32                                          | 31 / 15 / 7                                         | The low bit of the last byte is clear              | None                                           | Not implemented               |
| v1 (section 5)                             | 32                                          | 15 / 7 / 3                                          | The pointer equals the address of its own buffer   | None; two sentinel values                      | 2x                            |
| v1 with the size bytes reused (section 15) | 32                                          | 22 / 10 / 4                                         | The pointer equals the address of its own buffer   | None; two sentinel values                      | 2x                            |
| v2 (section 15)                            | 32                                          | 31 / 15 / 7                                         | The tag in the last byte                           | None                                           | 2x                            |
| 24-byte tagged (under consideration)       | 24                                          | 23 / 11 / 5                                         | The top two bits of the last byte are clear        | 2; their position depends on endianness        | 2x                            |

### Standard libraries

**libstdc++** stores a pointer, a size, and a union of the capacity with a 16-byte buffer. The pointer always points at the characters, and it points into the object itself when the string is inline. This is the layout that v1 copies.

```cpp
pointer   _M_p;
size_type _M_string_length;
union {
  _CharT    _M_local_buf[15 / sizeof(_CharT) + 1];
  size_type _M_allocated_capacity;
};
```

- `data()` and `size()` are plain loads.
- The object is not trivially relocatable, and every move tests for the inline case.
- Half of the object is unavailable to an inline value.

**libc++** overlays a long form and a short form, and spends one bit on telling them apart. The default layout on a little-endian target:

```cpp
struct __long {
  size_type __is_long_ : 1;
  size_type __cap_     : 63;
  size_type __size_;
  pointer   __data_;
};
struct __short {
  unsigned char __is_long_ : 1;
  unsigned char __size_    : 7;
  value_type    __data_[23 / sizeof(value_type)];  // after padding for wider characters
};
```

- 22 characters fit inline, because the first byte holds the flag and the short size, and the last holds the terminator.
- The flag is the lowest bit of the first byte, so the stored capacity is always even (`__endian_factor`). A big-endian target moves the flag. An alternate layout (`_LIBCPP_ABI_ALTERNATE_STRING_LAYOUT`) puts the pointer first and the flag in the last byte.
- `data()`, `size()` and `capacity()` all select on the flag.

**The Microsoft standard library** uses a union of the buffer with the pointer, followed by the size and the capacity:

```cpp
union {
  value_type _Buf[16 / sizeof(value_type)];
  pointer    _Ptr;
} _Bx;
size_type _Mysize;
size_type _Myres;  // capacity
```

- It is inline when the capacity is no larger than the inline capacity. Nothing points into the object, and no bits are taken.
- `size()` is a plain load. `data()` is a compare and a select.
- It grows by half (`_Old + _Old / 2`) and rounds the allocation up, so the first dynamic capacity is 31.

### Tagged 24-byte layouts

**`fbstring`** (folly) overlays three words with an array of characters, and reads the last byte of the object to find out which is in use:

```cpp
union {
  uint8_t bytes_[sizeof(MediumLarge)];              // for reading the last byte
  Char    small_[sizeof(MediumLarge) / sizeof(Char)];
  struct MediumLarge {
    Char*  data_;
    size_t size_;
    size_t capacity_;                               // category in the top two bits
  } ml_;
};
```

- In the small form, the last character holds the capacity that remains, `maxSmallSize - size`. A full 23-character string has none remaining, so that character is also the terminator.
- The top two bits of the last byte are the category: small, medium, or large. A medium string is an ordinary allocation that is copied eagerly, and it holds up to 254 characters. A large string is reference-counted and copied lazily, with the count stored just before the characters.
- On a little-endian target, those two bits are the top two bits of the capacity. A big-endian target uses the low two bits instead, and shifts the capacity and the small size by two.
- `size()` on a little-endian target is written so that it compiles to a subtraction and a conditional move, with no branch.
- It grows by half.

**`eastl::string`** is the same idea with one flag bit instead of two:

```cpp
struct HeapLayout {
  value_type* mpBegin;
  size_type   mnSize;
  size_type   mnCapacity;                 // top bit set means heap
};
struct SSOLayout {
  value_type mData[SSO_CAPACITY];         // 23 for char
  char       mnRemainingSize;             // last byte, after padding for wider characters
};
```

- The last byte holds the remaining capacity when the string is inline, and its top bit says that the string is on the heap. A big-endian target uses the low bit and shifts.
- It doubles when it grows.

**`eastl::fixed_string<T, N, bEnableOverflow>`** is not a small-string optimization. It derives from `basic_string` with a fixed allocator, and adds a buffer of `N` characters to the object. Its constructors set the base up in the heap form with the pointer aimed at that buffer and a capacity of `N - 1`. When overflow is enabled, a string that outgrows the buffer moves to the overflow allocator. When it is not, the allocation fails. Like libstdc++, it holds a pointer into itself.

**SSO-23** is a proof of concept of the same remaining-capacity trick that gives up no bits of the capacity. Its flag is a state that no real string can be in: a size whose top bit is set, with a capacity whose top bit is clear. It moves those two bits to the end of the object and inverts one of them, so that in the inline form the low two bits of the last byte are zero, and the byte can double as the terminator.

**SSO-31** is shorthand here for a layout that the SSO-23 README sketches under "Applications to current implementations". It is not a separate project. It applies the same trick to a 32-byte object such as the libstdc++ or Microsoft string. The long form is the three words followed by spare bytes. The short form is 31 characters followed by one character that holds the remaining capacity, and the low bit of that last byte says which form is in use. That makes 31 inline characters where both libraries have 15, and neither library does it. v2 is this layout with a category code in place of the single flag bit.

### A string that borrows

**SIMDString** (from G3D, published by Roblox Research) is `char` only, and trades size for speed. Its inline buffer is 64 bytes by default, aligned to 16, so that short strings are copied with a few vector instructions.

```cpp
union {
  char    m_buffer[INTERNAL_SIZE];  // 64 by default
  pointer m_ptr;
};
size_type m_length;
size_t    m_allocated;              // 0: constant segment; INTERNAL_SIZE: buffer; larger: heap
```

- It is the only surveyed string that borrows. A constructor or assignment from a `const char*` asks whether the pointer lies in the program's constant data, and if it does, the string points at it without copying. An edit copies first.
- The test is done at runtime by address. On Linux, it compares against the linker symbols `etext` and `edata`. Elsewhere, it accepts any address within 5 MB of a known literal.
- That is an automatic version of the Static category. This design has the caller say so instead, through `_rs` or `borrow_static`. That costs a few characters at the call site, and in exchange it works on any platform, for any buffer that the caller knows to be stable, and it never guesses.

### The layouts in this design

**v1** is the libstdc++ layout with two sentinel capacities, 0 for Static or Null and 1 for Unchecked (section 5).

**v1 with the size bytes reused** keeps the pointer and the inline test of v1. In inline mode, one byte of the size word holds the size, and the rest of that word joins the buffer (section 15):

```cpp
CharT* ptr_;
union {
  struct { size_t size; size_t cap; } external;                   // 8 further bytes unused
  struct { unsigned char size; value_type buf[23]; } internal;    // one-byte character
};
```

- `data()` stays a plain load. `size()` becomes a select on the inline test.
- The inline test is still a pointer comparison, so it reads neither arm of the union and works in constant evaluation.
- It is still not trivially relocatable.

**v2** gives the tag a byte of its own at the end of a 32-byte object (section 15). It takes no bits from the capacity and makes no endianness assumption.

**The 24-byte tagged layout** is `fbstring`'s, with its two category bits put to a different use. It is under consideration, not agreed.

```cpp
union {
  struct {
    CharT* ptr;
    size_t size;
    size_t cap;              // its top byte is the tag
  } external;
  value_type internal[24];   // internal[23] is the tag; one-byte character
};
```

- When the string is inline, the tag holds the remaining capacity, and its top two bits are clear.
- Otherwise, the top two bits name the category: Owned, Static or Unchecked. `fbstring` uses three of the four codes that two bits provide. This design uses all four, and Null needs no code of its own, because it is the external form with a null pointer.
- The capacity sentinels of v1 disappear, and so does the minimum buffer size for `attach`.
- The capacity loses its top two bits, and where they live depends on endianness, as in `fbstring`. `std::endian` makes that a compile-time choice, but the goal in section 2 of having no stolen bits would have to go.
- For wider characters, the tag is the last character, not the last byte, which leaves 11 characters of `char16_t` and 5 of `char32_t`.
- When the policy enlarges the inline buffer to 32 bytes or more, the tag no longer overlaps the capacity, and the layout becomes v2. The two are one layout at different sizes.

### What the survey shows

- **There are two families.** A string either points into itself (libstdc++, `eastl::fixed_string`, v1) or decodes a tag on every access (libc++, `fbstring`, `eastl::string`). The first gets plain loads for `data()` and pays in inline capacity and in relocation. The second gets up to 23 characters in 24 bytes and pays with a select in `data()`, `size()` and `capacity()`. The Microsoft layout sits between them, with no self-pointer, no tag and 15 characters.
- **The remaining-capacity trick is standard practice.** Every layout that reaches 23 characters in 24 bytes stores the remaining capacity in the last character, so that a full string's count is its own terminator.
- **Two bits are enough for this design's categories.** No fifth code is needed.
- **A tagged layout cannot use its inline form during constant evaluation in C++23.** The tag has to be read through an arm of the union that may not be the one in use, and constant evaluation rejects that. The string has to stay in the external form at compile time, which is acceptable only because a `constexpr` variable is Static or Null in any case (section 7). C++26's `std::is_within_lifetime` removes the limit where the library has it. The layouts that point into themselves do not have this limit, but a non-static `constexpr` local cannot hold an inline value in them either, until compilers implement the C++26 rule that allows it (section 5). Both statements were checked with clang 23.
- **Growth factors of 2 and 1.5 are both in use.** libstdc++, libc++ and EASTL double. The Microsoft library and `fbstring` grow by half.
