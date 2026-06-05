---
name: enums
description: How to declare and use enums in Corvid - registering a `class enum` as a sequence or bitmask enum for string conversion, and the helpers that registration unlocks. Use when declaring a `class enum`, wiring up enum<->string conversion, or working with bitmask flags.
---

# Enums

Whenever you declare a `class enum`, consider whether it should be registered as
a bitmask or sequence enum. If so, declare a `corvid_enum_spec(E*)` overload in
the enum's own namespace, returning a `make_sequence_enum_spec` or
`make_bitmask_enum_spec` result; it is found by ADL and must precede any use of
the enum's string conversion. See `quic_status` in
[quic_header.h](corvid/proto/quic/quic_header.h) and `write_stream_flags` in
[quic_conn.h](corvid/proto/quic/quic_conn.h) for the shape.

For registered enums, take full advantage of what registration unlocks: prefer
`operator*` over `static_cast` to read the underlying value, and prefer the
`bitmask::` helpers (`has`, `has_all`, `missing`, `set_at`, etc., in
[bitmask_enum.h](corvid/enums/bitmask_enum.h)) over hand-rolled `!!(v & m)` or
other bit-twiddling.
