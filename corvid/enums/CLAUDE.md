# CLAUDE.md

`enum_spec_v<>` is the central specialization point for enum metadata — the strings subsystem uses it for enum↔string conversion, and bitmask/sequence/bool enum adapters all build on it.

## How registration works

To opt a scoped enum into the system, specialize `registry::enum_spec_v` for it:

```cpp
template<>
constexpr inline auto registry::enum_spec_v<my_enum> =
    make_sequence_enum_spec<my_enum, "a, b, c">();
    // or make_bitmask_enum_spec<my_enum, "x, y, z">();
```

This is what unlocks the operator overloads and string conversion. Unregistered enums get none of it.

## Operators enabled by registration

Both `SequentialEnum` and `BitmaskEnum` get:
- `operator*` — extracts the underlying integer value (modeled on `std::optional`). When you see `*some_id` in ECS or container code, this is not a pointer dereference.
- `operator!` — tests for zero

`SequentialEnum` additionally gets `++`, `--`, `+`, `-` (with optional wrapping to stay in range).

`BitmaskEnum` additionally gets `|`, `&`, `^`, `~`, and `+`/`-` as aliases for set/clear bits.

## The `wrapclip::limit` flag

Both adapters accept an optional `wrapclip::limit` flag at registration time. For sequences it wraps arithmetic to stay in `[min, max]`; for bitmasks it clips `operator~` and `make` to valid bits only. Off by default.
