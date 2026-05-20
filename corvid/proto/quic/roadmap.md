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
- **HTTP/3 layering: upper plugin owns nghttp3; `quic_conn` exposes a
  virtual `quic_conn_handlers` base.** The `QuicPlugin` template parameter
  on `quic_dgram_protocol` IS the adapter. It owns the `nghttp3_conn`
  directly and inherits `quic_conn_handlers` to receive `quic_conn`'s typed
  protocol-neutral upcalls. No extra adapter or handler class lives between
  the upper plugin and nghttp3. `quic_conn` itself stays non-templated and
  HTTP/3-unaware. Detailed below under [HTTP/3 layering](#http3-layering).

## HTTP/3 layering

The QUIC + HTTP/3 stack splits into three roles, with no extra adapter
class between them:

- **`quic_conn`** owns `ngtcp2_conn` / `SSL` / `ngtcp2_crypto_conn_ref`.
  Knows ngtcp2 streams structurally (IDs, opening, flow control) but knows
  nothing about HTTP/3 or nghttp3. Static ngtcp2 callback slots forward into
  typed, protocol-neutral upcalls declared on an abstract base
  `quic_conn_handlers`: `on_recv_stream_data`, `on_acked_stream_data_offset`,
  `on_stream_close`, `on_recv_datagram`, and flow-control feedback.
  `quic_conn` holds a `quic_conn_handlers*` set by the owning session after
  the upper plugin is constructed. Primitives exposed to the plugin:
  `read_pkt`, a stream-aware `writev_stream` over
  `ngtcp2_conn_writev_stream`, and stream-open.
- **`quic_dgram_protocol::session_plugin`** is the protocol-agnostic
  carrier. Owns `quic_conn`, runs the per-turn cycle
  (`read_pkt` -> upcalls fire into the plugin -> plugin's drain -> arm
  expiry), handles CID registration, expiry, and lifetime. Delegates all
  protocol-specific behavior to the upper plugin.
- **Upper plugin (the `QuicPlugin` template parameter)** IS the adapter.
  For HTTP/3 it owns the `nghttp3_conn` and inherits `quic_conn_handlers`.
  Sees only `quic_conn`, not ngtcp2. Bridges `quic_conn`'s typed upcalls
  into nghttp3's read/ack/block/unblock APIs. Owns ngtcp2 stream opening
  through `quic_conn::open_bidi_stream` and pairs each new stream with
  `nghttp3_conn_submit_request` on the client side. Drives the outbound
  drain.
- **Application handler** (above the upper plugin) is the HTTP/3 user.
  Operates at the HTTP level: start request, submit response, "body is
  available." Supplies body bytes through `nghttp3_data_reader::read_data`
  (must retain bytes until ngtcp2 reports them acked; may return
  `NGHTTP3_ERR_WOULDBLOCK` and later call `nghttp3_conn_resume_stream`).
  Touches neither ngtcp2 nor `quic_conn`. nghttp3 -- not the handler --
  chooses stream write order (control streams first, then HTTP priority).

### Hard invariants

- **No recursive writes from ngtcp2 callbacks.** ngtcp2 explicitly forbids
  calling `ngtcp2_conn_writev_stream` from inside an ngtcp2 callback.
  Inbound upcalls may only mark state and call nghttp3's non-write APIs
  (`read_stream`, `add_ack_offset`, `block_stream`, `unblock_stream`,
  `close_stream`). All packet emission happens in the loop-level drain that
  runs after `read_pkt` returns.
- **The wrapper is the channel.** Stream data and other higher-layer events
  reach the upper plugin through `quic_conn`'s typed upcalls, not through
  any side channel that bypasses it. `quic_conn` is the encapsulation seam;
  routing data around it would defeat the wrapper's purpose.
- **Buffer lifetime.** Bytes accepted by `ngtcp2_conn_writev_stream` must
  remain valid until ngtcp2 reports them acknowledged via
  `acked_stream_data_offset`. nghttp3 enforces the same contract on body
  bytes supplied through `nghttp3_data_reader::read_data`.

### Outbound drain loop

`session_plugin` calls the upper plugin's drain each turn after `read_pkt`.
The plugin's drain orchestrates the nghttp3 -> ngtcp2 hand-off:

1. `nghttp3_conn_writev_stream` produces `(stream_id, vec, fin, count)`.
   If nothing to send, returns 0 with `stream_id == -1`.
2. `quic_conn::writev_stream(stream_id, vec, fin)` packs the bytes into a
   QUIC packet via `ngtcp2_conn_writev_stream`. Passing `stream_id == -1`
   still gives ngtcp2 a chance to emit ACKs, `MAX_DATA`, and other
   non-stream frames.
3. `nghttp3_conn_add_write_offset(stream_id, accepted)` reports back how
   much ngtcp2 consumed (which may be less than offered, due to flow
   control or packet sizing).
4. Loop until both nghttp3 and ngtcp2 report nothing more to send.

### Dispatch choice: virtual base, not template, not `std::function`

`quic_conn` stays non-templated. The template-everywhere pattern elsewhere
(`iou_dgram_router<RouterPlugin>`, `iou_dgram_session<SessionPlugin>`,
`quic_dgram_protocol<QuicPlugin>::session_plugin`) saves an indirection
because those classes own their plugin BY VALUE. `quic_conn` cannot: the
upper plugin's constructor captures a ref to the session that *contains*
`quic_conn`, so the plugin must live next to (not inside) `quic_conn` and
be reached through a pointer. With one pointer chase mandatory either way,
the template's directness advantage is forfeited. A virtual
`quic_conn_handlers` base then wins on:

- **Interface shape.** All upcalls declared together as a single role
  contract.
- **Implementation swappability.** `quic_no_op_plugin`, `quic_echo_plugin`,
  and the future HTTP/3 plugin compose naturally as concrete subclasses.
- **Single concrete `quic_conn`.** No per-plugin instantiation in a header
  already heavy with ngtcp2 and OpenSSL transitively.

`std::function` was considered and rejected. Per-call dispatch cost is
roughly comparable to virtual (both terminate in an indirect call after a
couple of memory hops), but `std::function` loses the role grouping
(each callback assigned independently), introduces implicit allocation
considerations, and complicates the "this object holds the nghttp3 state
adjacent to its callbacks" property.

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
- **[done] `quic_conn` wrapper + TLS handshake.**
  `corvid/proto/quic/quic_conn.h` is a non-movable RAII wrapper that
  owns an `ngtcp2_conn`, a per-conn `SSL*`, and an
  `ngtcp2_crypto_ossl_ctx*`; `quic_ssl_ctx` (sibling header) wraps an
  `SSL_CTX` configured for TLS 1.3 with a single ALPN protocol, separate
  ctors for server (cert + key) and client (verify disabled). The
  crypto-shim callback set (`ngtcp2_crypto_*_cb`) is installed verbatim;
  the only app-supplied trampolines are `rand` (OpenSSL RAND_bytes),
  `get_new_connection_id2` (OpenSSL RAND_bytes for both CID and
  stateless-reset token), a v2-shape adapter over the shim's v1
  `get_path_challenge_data_cb`, and no-op `handshake_completed` /
  `remove_connection_id` / `recv_*_key` hooks reserved for the session
  layer to override. The `SSL` is put into accept/connect state after
  `ngtcp2_crypto_ossl_configure_*_session` (the shim wires callbacks but
  does not pick a direction). `tests/quic_test_cert.h` generates a
  fresh RSA-2048 self-signed cert in memory each run. `quic_conn_test`
  drives a full in-process handshake by ferrying datagrams between a
  client and server conn until both report `is_handshake_completed()`;
  convergence is two round-trips on the happy path.
- **[done, server-side I/O + expiry] Wire `quic_conn` into
  `quic_dgram_protocol::session_plugin`.** `quic_dgram_protocol` is now a
  template parameterized on a `quic_plugin` upper layer (default
  `quic_no_op_plugin`; `quic_echo_plugin` follows in the next milestone,
  `http3_plugin` later). The `session_plugin` owns a server-role
  `quic_conn`, drives `read_pkt` / `write_pkt` per datagram, and arms a
  single rearmable expiry-sweeper entry against `iou_loop::timeouts()`.
  `iou_basic_loop::run_once` now ticks `timeouts_` at the end of each
  batch, which was a pre-existing gap that blocked the expiry plumbing.
  Sessions register under both the client's original DCID and the
  server's freshly-generated primary SCID; additional SCIDs issued
  through `get_new_connection_id2` are deferred to the CID-rotation
  milestone, along with client-mode router/session support. Tested by
  `quic_dgram_router_test`'s "drives a server-side TLS 1.3 handshake
  through the live iou_dgram_router" case, which handshakes a manually
  driven client through the live router.
- **[planned] QUIC echo server.** First end-to-end milestone. Lands the
  `quic_conn_handlers` abstract base and grows the `quic_plugin` concept
  from a single `on_packet_receive` tick into the full upcall contract
  (`on_recv_stream_data`, `on_acked_stream_data_offset`, `on_stream_close`,
  `on_recv_datagram`, flow-control feedback, plus a per-turn `drain`).
  `quic_echo_plugin` is the first concrete handler: opens a bidirectional
  stream on handshake completion and echoes application bytes back.
  Validates handshake, packet send/recv pacing, ACK handling, key updates,
  and graceful close. Likely tested against the `ngtcp2` reference client.
- **[planned] Connection ID rotation, path validation, migration.** Extends
  the session plugin to handle NEW_CONNECTION_ID / RETIRE_CONNECTION_ID
  exchanges, address validation tokens, and 4-tuple migration. Exercises the
  router's add/remove_session under load.
- **[planned] nghttp3 build integration + wrapping.** Bring in nghttp3 via
  FetchContent (with the `check`-target collision patched, likely via a
  `PATCH_COMMAND` that renames it to `nghttp3_check`). RAII wrap of
  `nghttp3_conn` (custom-deleter `unique_ptr`, `[[nodiscard]] bool` error
  translation, QPACK encoder/decoder context owned alongside) shaped like
  `quic_conn`, but owned BY the HTTP/3 upper plugin -- not a separate
  adapter class between the plugin and nghttp3 (the upper plugin IS the
  adapter; see [HTTP/3 layering](#http3-layering)).
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
