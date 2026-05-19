# QUIC and HTTP/3 Roadmap

Plans for ngtcp2-based QUIC and nghttp3-based HTTP/3, layered on top of the
existing `iou_dgram_router` / `iou_dgram_session` substrate in
`corvid/proto/io_uring/`.

## Decisions

- **Crypto backend: OpenSSL 3.5+.** ngtcp2 is BYO crypto and ships a separate
  `ngtcp2_crypto_<backend>` shim per choice. OpenSSL 3.5 added upstream QUIC API
  support, giving the cleanest distro story without vendoring BoringSSL.
- **Sequencing: ngtcp2 first, nghttp3 second.** We want a working "QUIC echo
  over `iou_dgram_router`" milestone before layering HTTP/3 on top. ngtcp2 is
  where the C-ugliness concentrates (large callback table, manual pacing and
  expiry plumbing, congestion control hooks); nghttp3 reuses the same wrapping
  patterns and is comparatively tame.
- **CID-keyed routing is a hard prerequisite.** The router was designed with
  CIDs in mind (the plugin's `key_t` and `extract` already abstract the
  routing key), so this is a new plugin rather than a router change. It lands
  before ngtcp2 wiring.
- **Timeouts: ngtcp2's own expiry by default; idle-timeout port deferred.**
  ngtcp2 owns loss-detection, ACK delay, idle-timeout, PTO, and keep-alive
  timers. We will drive its single rearmable expiry through
  `iou_loop::timeouts()` (the same `timeout_sweeper` that powers
  `iou_stream_conn`'s idle timeouts). Porting `iou_stream_conn`'s read/write
  idle timeouts to `iou_dgram_session` is a *possible* extra layer for
  non-QUIC datagram protocols, not assumed to be needed for QUIC itself.

## Prerequisites

### CID-keyed routing in `iou_dgram_router`

The router currently demuxes by whatever `key_t` the plugin extracts. For
QUIC, the plugin parses each datagram's header to recover the DCID and
returns it as the key.

We do not hand-roll the parser. ngtcp2 ships `ngtcp2_pkt_decode_version_cid`
for long-header packets (handles version negotiation, variable-length CIDs,
and future header types) and a short-header decoder that takes the locally
issued CID length as a parameter. The CID-routing plugin depends on ngtcp2
being available; the build integration milestone must land first.

Short headers have no on-wire CID length field, so the router needs to know
the length of the CIDs *we* issued. We pick a single constant CID length
(8 bytes is the common choice) at compile time or router-config time, and
pass it into both ngtcp2 (so it issues CIDs of that length) and the
short-header decoder.

The session plugin maintains a set of CIDs it owns. The existing
`add_session(key, self)` / `remove_session(key)` API already supports the
multiple-keys-per-session case the router was designed for. Two new code
paths matter:

- **Issuing a new CID** (`ngtcp2_callbacks::get_new_connection_id`): session
  calls `router.add_session(new_cid, self)` so future packets routed by that
  CID land here.
- **Retiring a CID** (`ngtcp2_callbacks::remove_connection_id`): session calls
  `router.remove_session(retired_cid)`.

When a packet arrives whose DCID has no registered session, the router calls
the plugin's `create_session`. For QUIC, this should only happen for
long-header Initial packets from new clients; short-header packets to unknown
CIDs are dropped (or we send a stateless reset, TBD).

This work is the gating change. It is not large but must be done before
ngtcp2 lands so the connection wrapper has a stable place to register/retire
CIDs.

### (Maybe) idle timeouts in `iou_dgram_session`

`iou_stream_conn` already integrates `concurrency::idle_timeout` for
read/write directions, driven by `iou_loop::timeouts()`. The same pattern
ports cleanly to `iou_dgram_session` if we need transport-level idle behavior
for non-QUIC datagram protocols (DNS, custom UDP). For QUIC, ngtcp2 handles
idle-close internally per RFC 9000, so we likely do not need this layer.
**Not blocking on ngtcp2 work.** Revisit if a non-QUIC datagram protocol
needs it, or if we want a watchdog beneath ngtcp2's own timers.

## Milestones

- **[done] ngtcp2 + OpenSSL 3.5+ build integration.** OpenSSL 3.5.6 (static)
  built one-time via `scripts/build_openssl_quic.sh` into
  `tests/.local/openssl/`; ngtcp2 v1.22.1 fetched into
  `tests/.fetchcontent/` and built against that OpenSSL via the
  `ngtcp2_crypto_ossl` backend (`ENABLE_OPENSSL=ON`). Both libs are
  static-only (`ENABLE_STATIC_LIB=ON`, `ENABLE_SHARED_LIB=OFF`), example
  clients suppressed (`ENABLE_LIB_ONLY=ON`). Linked into every test target
  unconditionally; the static linker drops unreferenced archive members, so
  non-QUIC tests pay nothing. Smoke-tested by `tests/quic_smoke_test.cpp`.
  **nghttp3 is deferred** to the HTTP/3 milestone: its top-level
  CMakeLists.txt unconditionally creates an `add_custom_target(check ...)`
  that collides with ngtcp2's same-named target when both are loaded in one
  CMake configure; patching around it is doable but unnecessary until we
  actually need HTTP/3.
- **[done, stub session] CID-keyed router plugin.**
  `corvid/proto/quic/quic_dgram_router.h` defines `quic_dgram_protocol`
  with a `router_plugin` (DCID extraction via `quic_version_cid` for both
  long and short headers; only long-header packets create sessions) and a
  stub `session_plugin` (single-DCID registration, no-op recv/sent pending
  the `quic_conn` wrapper). CID length defaults to
  `quic_version_cid::default_scid_length` (16 bytes). Tested with synthetic
  packets in `tests/quic_dgram_router_test.cpp`; ngtcp2-generated-packet
  coverage will arrive with the `quic_conn` milestone.
- **[planned] `quic_conn` wrapper.** RAII around `ngtcp2_conn`, callback
  trampoline table, error-code -> `[[nodiscard]] bool` translation, expiry
  timer driven through `iou_loop::timeouts()`. Stateless: no streams, no
  application data yet. Verifies handshake completion.
- **[planned] QUIC echo server.** First end-to-end milestone. A session
  plugin that opens a bidirectional stream on handshake completion and echoes
  application bytes. Validates handshake, packet send/recv pacing, ACK
  handling, key updates, and graceful close. Likely tested against the
  `ngtcp2` reference client.
- **[planned] Connection ID rotation, path validation, migration.** Extends
  the session plugin to handle NEW_CONNECTION_ID / RETIRE_CONNECTION_ID
  exchanges, address validation tokens, and 4-tuple migration. Exercises the
  router's add/remove_session under load.
- **[planned] nghttp3 build integration + wrapping.** Bring in nghttp3 via
  FetchContent (with the `check`-target collision patched, likely via a
  `PATCH_COMMAND` that renames it to `nghttp3_check`). `http3_conn`
  analogous to `quic_conn`: static trampolines, RAII handle, error
  translation. QPACK encoder/decoder context owned alongside.
- **[planned] HTTP/3 server echo.** First HTTP/3 milestone. Decodes request
  HEADERS, emits response HEADERS + DATA. Verifies stream multiplexing,
  flow control, and HEADERS encoding round-trip. Tested against `curl
  --http3` and `nghttp3`'s reference client.
- **[planned] HTTP/3 streaming bodies, trailers, server push (maybe).**
  Wire up request/response body streaming through nghttp3's data callbacks.
  Server push is RFC-permitted but deprecated in practice; treat as optional.

## Wrapping conventions

- **C callback tables -> static trampolines off a typed C++ context.** Same
  shape as `iou_io_conn`'s virtuals: the trampoline recovers the typed `this`
  from the `user_data` pointer and forwards to a member function.
- **`ngtcp2_conn` / `nghttp3_conn` -> RAII handles.** Custom-deleter
  `unique_ptr` (or a small RAII struct), with `[[nodiscard]] bool` return
  translation from ngtcp2's int error codes per Corvid convention.
- **Buffers.** Reuse `iou_loop::buffer` end-to-end. ngtcp2's
  `ngtcp2_path_storage` for per-packet endpoint info maps cleanly onto our
  existing `iov_msghdr` and `net_endpoint` types.
- **Expiry.** A single `concurrency::timeout_sweeper` entry per `quic_conn`,
  rearmed from `ngtcp2_conn_get_expiry` on every send/recv cycle.

## Open questions

- **Where it lives.** Two new directories: `corvid/proto/quic/` (ngtcp2
  layer) and `corvid/proto/http3/` (nghttp3 layer). Sibling to
  `corvid/proto/io_uring/`. Alternative: nest under
  `corvid/proto/io_uring/quic/` since QUIC currently runs over io_uring's
  datagram router. Leaning toward the sibling layout because the QUIC
  wrapping itself is transport-agnostic; only the router substrate is
  io_uring-specific.
- **External dependency strategy.** Vendor ngtcp2/nghttp3/OpenSSL as git
  submodules or use CMake `FetchContent`? Distro packages are out: Ubuntu
  Noble ships OpenSSL 3.0.13 and we need 3.5+. `FetchContent` is probably
  the right answer for staying buildable on stock distros, at the cost of
  one fresh OpenSSL build per clean build (mitigable via a cached/preflight
  step similar to `scripts/build_msan_libcxx.sh`).
- **Address validation tokens.** Stateless (HMAC over peer address + time)
  is the simplest first pass and avoids cross-restart state. Persisted
  tokens (for 0-RTT replay protection) are out of scope for v1.
- **0-RTT support.** Out of scope until 1-RTT is solid.
- **Stateless reset.** Send when a short-header packet arrives for an
  unknown CID? Required by RFC 9000 but adds state (reset-token map).
  Decide before the migration milestone.
- **CID length.** Constant `8` is the common pick; ngtcp2 examples use it.
  Worth making a router-config knob in case we want a different default
  later.
