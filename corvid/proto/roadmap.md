# Proto Module Roadmap

Items marked **[done]** are implemented and tested.

## Layer 1: POSIX/Linux Primitives (C++ wrappers)

Thin, zero-overhead C++ wrappers around POSIX and Linux networking primitives.

- **[done]** `os_file` -- RAII wrapper around a raw file descriptor; non-copyable,
  movable; provides `read`, `write`, `get_flags`, `set_flags`, and `is_open`;
  used as the fd-owning base for `net_socket`
- **[done]** `ipv4_addr` -- wraps `in_addr`; construction from dotted-decimal
  string, host-byte-order `uint32_t`, and four octets; named factories
  (`any()`, `loopback()`, `broadcast()`); classification predicates;
  comparison; formatting
- **[done]** `ipv6_addr` -- wraps `in6_addr`; similar interface to `ipv4_addr`;
  construction from colon-hex string, `uint8_t[16]` array, and
  `uint16_t[8]` array; named factories and classification predicates
- **[done]** `net_endpoint` -- address + port pair; wraps both `sockaddr_in` and
  `sockaddr_in6` in a tagged union; round-trip through POSIX `sockaddr`
  pointers; comparison; formatting
- **[done]** `net_socket` -- RAII socket handle (fd on POSIX); movable,
  non-copyable; type-safe `set_option` / `get_option` (e.g., `SO_REUSEADDR`,
  `TCP_NODELAY`); `set_nonblocking()`, `bind()`, `connect()`, `listen()`,
  `accept()`; `set_send_buffer_size()` convenience method
- **[done]** `dns_resolver` -- thin wrapper around `getaddrinfo`; `find_all()`
  returns `std::vector<net_endpoint>`; `find_one()` returns
  `net_endpoint`; both accept an optional address-family filter

## Layer 2: Stream I/O Loop (epoll-based, io_uring later)

Non-blocking stream-socket I/O with an event loop. Initial implementation uses
`epoll`;
the interface is designed so `epoll` can later be swapped for `io_uring`
without changing higher layers.

- **[done]** `epoll_loop` -- `epoll`-based event loop; non-copyable, non-movable;
  `run()` may be called at most once; `epoll_loop_runner` wraps it in a
  background thread for the common case; `register_socket(shared_ptr<io_conn>,
  readable, writable)` / `unregister_socket` manage fd registrations; `io_conn`
  owns its `net_socket`; `set_readable` / `set_writable` toggle `EPOLLIN` /
  `EPOLLOUT` without changing stored handlers; `EPOLLERR`, `EPOLLHUP`, and
  `EPOLLRDHUP` are always armed; level-triggered operation throughout;
  `register_socket`, `unregister_socket`, `set_readable`, and `set_writable`
  auto-promote to `post()` when called from off the loop thread; `post(fn)`
  schedules work from any thread (wakes `epoll_wait` via an internal `eventfd`);
  `post_and_wait(fn)` blocks the caller until `fn` runs on the loop thread and
  returns its result; `execute_or_post(fn)` runs inline on the loop thread,
  otherwise posts; `run()` / `run_once(timeout_ms)` / `stop()` drive dispatch;
  `wait_until_running(ms)` blocks until the loop is running; `is_loop_thread()`
  and `poll_thread_scope()` support thread identity checks and testing; `io_conn`
  is an abstract base with a `net_socket` member and virtual `on_readable` /
  `on_writable` / `on_error` so higher-level types inherit from it directly to
  avoid a separate handler-lambda allocation
- **[done]** `recv_buffer` / `recv_buffer_view` -- persistent flat receive buffer
  owned by each `stream_conn`; `recv_buffer` holds a `std::string buffer`
  (size == capacity) with atomic `begin` / `end` indexes and loop-thread-only
  `reads_enabled` / `view_active` flags; `compact(target)` resizes and/or
  memmoves active bytes subject to hysteresis; `recv_buffer_view` is the
  limited-interface token delivered to `on_data`: `active_view()` /
  implicit-`string_view` conversion reads the unconsumed region, `consume(n)`
  / `update_active_view(tail)` advance `begin`, `expand_to(n)` requests growth,
  `try_take_full(out, view)` bulk-transfers a full buffer; destructor posts
  compact / re-enable-reads / optional re-dispatch to the loop
- **[done]** `iov_msghdr` -- scatter/gather socket I/O via `sendmsg` /
  `recvmsg`; template `iov_msghdr<SENDER>` with `iov_msghdr_sender` and
  `iov_msghdr_receiver` aliases; segment list backed by a `vector<iovec>` with
  the `msghdr` kept in sync; `append(iovec)` / `set(span<iovec>)` / `clear()`
  to manage segments; `send(net_socket)` / `recv(net_socket)` perform the
  syscall and return an `op_results` with total byte count and segment-level
  position; used internally by `stream_conn` for zero-copy multi-buffer sends
- **[done]** `stream_conn` / `stream_conn_ptr` -- non-blocking connected
  stream-socket wrapper driven by an `epoll_loop`; `stream_conn` is the state
  object inheriting from `io_conn`, holding the send queue and a persistent
  `recv_buffer`; `stream_conn_ptr` is the move-only owning handle
  (`shared_ptr<stream_conn>` internally; destructor calls `hangup()`); three
  factory methods on `stream_conn_ptr`: `adopt()` for already-connected
  sockets, `connect()` for outbound async connect, `listen()` for accept loops;
  `send(Bufs&&...)` variadic template accepts one or more `string` buffers,
  skips any empty ones, and returns false if all are empty; non-empty buffers
  are enqueued atomically; internally uses `iov_msghdr_sender` for
  scatter/gather writes; `close()` / `hangup()` / `shutdown_read()` /
  `shutdown_write()` are thread-safe via `execute_or_post()` / `post()`;
  `can_read()` / `can_write()` query half-close state; `local_endpoint()` /
  `remote_endpoint()` return socket addresses; supports persistent callback
  mode via `stream_conn_handlers` (`on_data(conn, recv_buffer_view)`,
  `on_drain`, `on_close`); holds `own_handlers_` and an atomic
  `active_handlers_` pointer that facade classes temporarily redirect
- **[done]** `stream_async_base` / `stream_async_cb` / `stream_async_coro` -- in
  `stream_async.h`; facade classes that provide per-call async I/O on top of a
  `stream_conn` by atomically swapping `active_handlers_` for the duration of
  their lifetime; `stream_async_base` is the non-copyable, non-movable base
  (CAS-based handler install, static trampolines for friend access);
  `stream_async_cb` provides one-shot callback I/O: `read(cb)` delivers a
  `recv_buffer_view` on the next arrival, `write(buf, cb)` invokes
  `cb(bool completed)` when the queue drains or the connection closes;
  `stream_async_coro` provides C++20 coroutine I/O: `read()` / `write(buf)`
  return awaitables; `EPOLLIN` is gated by `reads_enabled` and armed only when
  a waiter is registered; all coroutine resumptions are deferred through
  `post()` to avoid use-after-free
- **[done]** `loop_task` -- fire-and-forget coroutine return type for `epoll_loop`
  handlers; `initial_suspend` is `suspend_never` (eager start);
  `final_suspend` is `suspend_never` (self-destroying frame); enables
  `co_await coro.read()` / `co_await coro.write(buf)` patterns via
  `stream_async_coro`
- **[done]** `tcp_listener` -- now integrated as `stream_conn_ptr::listen()`;
  creates a non-blocking listening socket, binds, and calls `listen(2)`; drains
  accepted connections via `accept4` on `EPOLLIN`, creating self-owning
  `stream_conn` instances with a copy of the listener's handlers
- **[done]** `tcp_client` -- now integrated as `stream_conn_ptr::connect()`;
  creates a non-blocking socket, optionally binds the local end, calls
  `connect(2)`, and notifies the caller via `on_drain` on success or `on_close`
  on failure
- **[done]** `terminated_text_parser` -- incremental sentinel-terminated text
  frame parser; `state` is stored per connection and survives across `on_data`
  calls; `parse(input, frame)` returns `std::optional<bool>` (empty = need
  more, `true` = complete frame, `false` = max-length exceeded without finding
  the sentinel); `reset()` clears scan state for the next frame; no copies --
  `frame` is a `string_view` into the caller's buffer
- **[done]** `stream_sync` -- blocking synchronous stream-socket client for
  tests and small tools; wraps a blocking-mode `net_socket`; optional
  per-syscall timeout via `SO_RCVTIMEO` / `SO_SNDTIMEO`; any error closes the
  connection and subsequent calls fail immediately; `send(data)` loops on
  partial writes; `recv()` returns the first available chunk; `recv_exact(n)`
  loops to accumulate exactly `n` bytes; `recv_until(delim)` accumulates until
  the delimiter is found, leaving trailing bytes in an internal buffer
- **[removed]** `iouring_loop` / `iou_stream_conn` -- an `io_uring`-based event
  loop and stream connection were prototyped (using `IORING_OP_POLL_ADD` with
  `IORING_POLL_ADD_MULTI` for persistent multi-shot fd readiness polling) but
  removed after the experiment revealed that zero-copy requires a different
  buffer-management model and that avoiding `liburing` meant reinventing too
  much plumbing. See the io_uring notes below.
- **[done]** bilateral close -- `stream_conn` supports
  `set_shutdown(coordination_policy::bilateral)`; `close()` drains writes,
  issues `SHUT_WR`, then discards incoming data until peer EOF before closing.
  Timeouts against a non-cooperative peer are handled at the application layer
  via `timer_fuse` (see `http_server`).

If datagram support is needed later, add a separate `dgram_conn` abstraction
on top of `epoll_loop` rather than broadening `stream_conn`.

## Layer 3: HTTP

HTTP server built incrementally from an HTTP 0.9 baseline to full HTTP/1.1,
followed by client and proxy support.

- **[done]** `http_head_codec` -- HTTP/1.x data types in `http_head_codec.h`;
  `http_version` enum (`invalid`, `http_0_9`, `http_1_0`, `http_1_1`);
  `http_method` enum (`GET`, `HEAD`, `POST`, `PUT`, `DELETE`, `OPTIONS`,
  `PATCH`, `CONNECT`, `TRACE`); `after_response` enum (`close`, `keep_alive`);
  `http_status_code` enum (full range); `content_type_value`,
  `transfer_encoding_value`, `upgrade_value` enums for decoded header fields;
  `http_headers` ordered multimap with O(1) average lookup (insertion-order
  `vector` + `unordered_map` index keyed by canonical name), case-insensitive
  via title-case canonicalization, `add` / `add_raw` / `get` / `get_combined`;
  `request_options` / `response_options` structs with `keep_alive`,
  `content_length`, `is_chunked`, `connection`, `upgrade` accessors parsed from
  headers; `request_head` with `parse(string_view)` (populates method, target,
  version, headers, and options); `response_head` with `serialize()` producing
  the full HTTP wire format and `make_error_response()` static factory; stream
  operators on enums for diagnostics
- **[done]** `http_transaction` -- base class for HTTP/1.x request-response
  transactions (in `http_transaction.h`); `handle_data(recv_buffer_view&)` and
  `handle_drain(send_fn&)` virtual methods return `stream_claim` to retain or
  release the read/write stream; intrusive `next` pointer for pipeline queuing
  managed by `transaction_queue`; `close_after` flag controls keep-alive
  disposition; optional `on_data` / `on_drain` callbacks used as defaults;
  `transaction_factory` type alias (`function<shared_ptr<http_transaction>
  (request_head&&)>`) for route handler factories
- **[done]** `http_server` (HTTP/1.1) -- two-phase request parsing via
  `terminated_text_parser`: Phase 1 seeks `"\r\n"` for the request line (max
  8192 bytes), Phase 2 seeks `"\r\n\r\n"` for header fields; HTTP/0.9 requests
  (no version token) dispatch after Phase 1 only; leading bare CRLFs silently
  skipped per RFC 9112 section 2.2; explicit `http_phase` state machine
  (`request_line`, `header_lines`, `body`, `response`, `done`); persistent
  connections (keep-alive by default for HTTP/1.1, close for HTTP/1.0);
  pipelining via an `on_data` parse loop that processes all complete header
  blocks in the receive buffer, relying on `stream_conn::send` FIFO ordering
  for response sequencing; `Host` header validation for HTTP/1.1 (returns 400
  if absent); `Connection` header honored; `request_timeout` (default 30 s) and
  `write_timeout` (default 5 s) enforced via `timer_fuse` / `timing_wheel`
  (from `corvid::concurrency`); connection state carried in
  `stream_conn_with_state<http_conn_state>` to avoid separate allocations;
  routing via `add_route(host_path_key, transaction_factory)` registered in an
  `unordered_map` with transparent `host_path` lookup (exact match, then
  hostname-wildcard, then path-wildcard, then catch-all `"/"`)
- Deferred: request body reading (`Content-Length` / chunked transfer),
  `POST` / `PUT` methods, chunked response encoding, content negotiation,
  encoding decode (percent-hex `%20`, RFC 2047 MIME word, RFC 5987)
- `http_client` -- HTTP/1.1 client built on `stream_conn`
- `http_proxy` -- HTTP proxy support
- **Future:** HTTP/2

## Layer 4: WebSockets

WebSocket protocol built on top of the HTTP/1.1 upgrade mechanism.

- **[done]** `endian.h` -- `hton16` / `hton32` / `hton64` / `hton128` and
  their `ntoh` inverses; implemented via `std::byteswap` on little-endian hosts
  and as identity on big-endian; used by `ws_frame_header_storage` for
  network-byte-order payload length fields
- **[done]** `base-64.h` -- RFC 4648 Base64 encode/decode; `encode(span<uint8_t>)`
  and `decode(string_view)` (returns `string`); used for `Sec-WebSocket-Key`
  generation and `Sec-WebSocket-Accept` computation
- **[done]** `sha-1.h` -- SHA-1 digest for WebSocket accept-key computation;
  `sha_1::digest(string_view)` returns `digest_t`
  (`array<uint32_t,5>`), and `sha_1::bytes(digest_t)` converts it to the raw
  20-byte `array<uint8_t,20>`; suitable only for non-security-critical
  protocol work (RFC 6455 handshake)
- **[done]** `http_websocket` -- callback-driven WebSocket message pump (in
  `http_websocket.h`); `ws_frame_control` bitmask enum (opcodes + `fin` bit);
  `ws_frame_header_storage` fixed-size wire-format header storage;
  `ws_frame_wrapper<ACCESS>` typed view over header storage with
  `header_length` / `payload_length` / `total_length` / `is_masked` accessors
  and `mask_payload_copy`; `ws_frame_codec` stateless codec with
  `parse_header`, `serialize_frame`, and `compute_accept_key`; `http_websocket`
  session class runs client or server side; `feed(recv_buffer_view&)` reassembles
  fragmented messages and fires `on_message(pump, payload, opcode)` and
  `on_close(pump, code, reason)`; `send_text` / `send_binary` / `send_close` /
  `send_ping` / `send_pong`; optional `deliver_fragments` mode fires per-frame;
  client-side masking uses `std::random_device` (RFC 6455 section 5.3);
  16 MiB per-frame size limit; `close_pending()` signals completed close
  handshake
- **[done]** `http_websocket_transaction` -- `http_transaction` subclass (in
  `http_websocket_transaction.h`) that performs the RFC 6455 upgrade handshake
  and delegates subsequent I/O to `http_websocket`; validates `Upgrade:
  websocket`, `Connection: Upgrade`, `Sec-WebSocket-Version: 13`, and
  `Sec-WebSocket-Key`; sends `101 Switching Protocols` with computed
  `Sec-WebSocket-Accept`; returns `stream_claim::claim` permanently from both
  `handle_data` and `handle_drain` until the close handshake completes;
  optional ping/pong keepalive via `enable_keepalive(loop, wheel,
  ping_interval, pong_timeout)` using `timer_fuse`; `make_factory(configure_fn)`
  builds a `transaction_factory` suitable for `http_server::add_route`

## Design Principles

- Each layer depends only on the layer(s) below it; higher layers are optional.
- No external dependencies beyond libc++ and standard POSIX/Linux headers.
- Headers are self-contained; no source files (library remains header-only).
- RAII throughout: no manual resource management in user code.
- Async model: callbacks for simple cases, C++20 coroutines for sequential
  logic; `stream_conn` handles persistent callbacks natively; per-call
  callback and coroutine modes are provided by `stream_async_cb` and
  `stream_async_coro` facades that temporarily redirect the handler pointer.
- Linux is the target OS.

## Notes on the post queue
- It's tempting to go down the rabbit hole with MPSC lockless queue designs, but there's no point in this for epoll-based loops because the cost of the lock is nothing compared to the syscall overhead. For now, we've improved the design to use double-buffering so to generally avoid memory allocation at the top of each loop.

## Notes on io_uring
- The vibe-coding experiment yielded useful data but not usable code. Without
  `liburing`, too much plumbing had to be reinvented, and the prototype made
  clear that zero-copy requires a fundamentally different buffer-management
  model. The experiment also showed that `io_uring` has built-in timeout
  support, which would eliminate the need for a separate timing wheel.
- If io_uring support is revived, the right approach is to evaluate off-the-shelf
  wrappers (Asio, liburing4cpp, Xynet, Condy) rather than building from scratch.
  Seastar (underlying ScyllaDB) and Meta's libunifex are also worth surveying.
- High-end performance requires a shared-nothing design: each loop pinned to its
  own CPU core, with interthread communication over lock-free ring buffers.
