# CLAUDE.md

`enum_spec_v<>` is the central access point for enum metadata, populated per-enum by an ADL-found `corvid_enum_spec` hook — the strings subsystem uses it for enum↔string conversion, and bitmask/sequence/bool enum adapters all build on it.

## How registration works

To opt a scoped enum into the system, declare a `corvid_enum_spec` overload for it in the enum's own namespace (it is found by ADL):

```cpp
consteval auto corvid_enum_spec(my_enum*) {
  return make_sequence_enum_spec<my_enum, "a, b, c">();
  // or make_bitmask_enum_spec<my_enum, "x, y, z">();
}
```

This is what unlocks the operator overloads and string conversion. Unregistered enums get none of it. The pointer parameter is never dereferenced; it only carries the type.

## Operators enabled by registration

Both `SequentialEnum` and `BitmaskEnum` get:
- `operator*` — extracts the underlying integer value (modeled on `std::optional`). When you see `*some_id` in ECS or container code, this is not a pointer dereference.
- `operator!` — tests for zero

`SequentialEnum` additionally gets `++`, `--`, `+`, `-` (with optional wrapping to stay in range).

`BitmaskEnum` additionally gets `|`, `&`, `^`, `~`, and `+`/`-` as aliases for set/clear bits.

## The `wrapclip::limit` flag

Both adapters accept an optional `wrapclip::limit` flag at registration time. For sequences it wraps arithmetic to stay in `[min, max]`; for bitmasks it clips `operator~` and `make` to valid bits only. Off by default.
