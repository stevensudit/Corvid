# Proto Module Overview

The `corvid::proto` module provides networking primitives and an
async I/O loop with C++20 coroutine support.

## Layer 1: POSIX/Linux Primitives

### `os_file`

RAII wrapper around a raw OS file descriptor. Movable, non-copyable. Core
operations: `read(string&)` fills a pre-sized buffer and trims it to the
actual byte count, treating `EAGAIN`/`EWOULDBLOCK` as success with no
progress; `write(string_view&)` writes as much as possible and removes the
written prefix from the view. Also provides `close()`, `release()` (give up
ownership without closing), and `handle()`. The `control()` template wraps
`fcntl` variadically; `get_flags()` and `set_nonblocking()` are named
helpers built on top of it.

### `ipv4_addr` and `ipv6_addr`

Value types wrapping `in_addr` and `in6_addr` respectively. Both support
construction from string notation (dotted-decimal for IPv4, colon-hex for
IPv6), integer/byte-array representations, and named factories (`any()`,
`loopback()`, `broadcast()`). Classification predicates cover loopback,
multicast, private ranges, etc. Comparison operators and `std::format` /
`std::ostream` integration are provided. On POSIX platforms each type
interops with its corresponding `in_addr` / `in6_addr` struct.

### `net_endpoint`

 Tagged union over the concrete socket address types used by the module
 (IPv4, IPv6, and Unix domain / abstract-namespace sockets). It
 default-constructs to an empty state (checkable via `empty()`). For TCP/IP
 endpoints it parses "1.2.3.4:80" and "[2001:db8::1]:80" notation and formats
 back to the same. Unix domain endpoints are represented as path-like
 strings, with Linux abstract-namespace sockets distinguished in their
 textual form (for example, by a leading '@'). Named factories `any_v4()`
 and `any_v6()` produce wildcard bind addresses for the IP variants. On
 POSIX, `net_endpoint` interops with the corresponding `sockaddr_in`,
 `sockaddr_in6`, Unix-domain `sockaddr` type, and `sockaddr_storage`.

### `net_socket`

RAII socket handle derived from `os_file`; movable, non-copyable; inherits
fd-level operations directly. Type-safe
`set_option<T>(level, optname, value)` / `get_option<T>(level, optname)`
wrap `setsockopt` / `getsockopt`. Socket I/O adds `send(string_view&)` and
`recv(string&, flags)` wrappers on top of the inherited fd operations. Named
helpers cover the most common options: `set_reuse_addr`, `set_reuse_port`,
`set_nodelay`, `set_keepalive`, `set_recv_buffer_size`,
`set_send_buffer_size`. Can adopt an existing `os_file` by move, or create a socket directly via
`net_socket(domain, type, protocol)`.

### `dns_resolver`

Resolves hostnames to `net_endpoint` values via `getaddrinfo`. `find_all()`
returns a `std::vector<net_endpoint>` with an optional address-family filter
(`AF_UNSPEC`, `AF_INET`, or `AF_INET6`) and `max_results` cap. `find_one()`
returns the first result as a `net_endpoint`, or a default-constructed
(invalid) endpoint on failure.

## Layer 2: Stream I/O Loop

### `recv_buffer` and `recv_buffer_view`

`recv_buffer` is the persistent flat receive buffer owned by each connection.
It backs a `std::string buffer` whose `size` equals its `capacity`. Two atomic
indexes, `begin` (release-stored by the parser via `consume`) and `end`
(release-stored by the polling thread after each `recv`), delimit the live
region. `reads_enabled` (loop-thread-only) gates `EPOLLIN` desire.
`view_active` (loop-thread-only) suppresses `on_data` re-dispatch while a
parser holds a view.

`resize(capacity)` initializes or reinitializes backing storage; safe only
when reads are disabled and no view is live. `active()` returns a
`string_view` of the unconsumed region. `compact(target)` optionally resizes
and/or compacts the buffer (memmove active bytes to offset 0), subject to
hysteresis rules: it grows when the active capacity falls below
`min_capacity`, shrinks when the buffer bloats past 2x `min_capacity` and the
active data fits, and skips the memmove entirely when write space is still
ample.

`recv_buffer_view` is the limited-interface token handed to the parser via the
`on_data` callback. At most one view is live at a time per connection.
`active_view()` (or the implicit `std::string_view` conversion) returns the
currently unconsumed region and records the observed `end` in
`last_seen_end_`. `consume(n)` advances `begin` by `n` bytes (clamped to the
last-seen `end`). `update_active_view(tail)` is a convenience wrapper that
computes consumed bytes from a pointer difference. `buffer_capacity()` returns
the backing string's full capacity. `expand_to(n)` requests buffer growth on
the next compact. `try_take_full(out, view)` bulk-transfers the entire backing
buffer when it is completely full, swapping ownership to the caller's string
and resetting the connection's buffer to its previous capacity without
zero-initialization.

When the view destructs, `resume_cb_(new_buffer_size_, last_seen_end_)` is
called; this callback captures a `shared_ptr` to the owning connection to keep
it alive and posts compact / re-enable-reads / optional re-dispatch to the
loop thread.

### `epoll_loop`

`epoll`-based I/O event loop; non-copyable and non-movable. `run()` may be
called at most once on a given instance. `epoll_loop_runner` wraps one in a
dedicated background thread for the common case.

`register_socket(shared_ptr<io_conn>, readable, writable)` registers a
connection (the `io_conn` owns its `net_socket`) with the initial read/write
interest. `set_readable(sock, bool)` and `set_writable(sock, bool)` toggle
`EPOLLIN` and `EPOLLOUT` without disturbing the registered `io_conn`.
`EPOLLERR`, `EPOLLHUP`, and `EPOLLRDHUP` are always armed; the loop operates
level-triggered. `unregister_socket(sock)` removes a registration.

All four socket-management methods are safe to call from any thread: when
invoked off the loop thread they automatically promote into a `post()` and
return `true`.

`post(fn)` is thread-safe: it locks a queue, pushes the callback, then writes
to an internal `eventfd` to interrupt a sleeping `epoll_wait`. `post_and_wait(fn)`
blocks the caller until `fn` runs on the loop thread, returning its result; on
the loop thread it executes `fn` inline. `execute_or_post(fn)` runs `fn`
inline when already on the loop thread, otherwise posts it asynchronously.

`run()` / `run_once(timeout_ms)` / `stop()` drive dispatch. `stop()` and
`wait_until_running(timeout_ms)` are thread-safe. `is_loop_thread()` returns
true when the current thread is the active polling thread. `poll_thread_scope()`
marks the current thread as the loop thread without entering the dispatch loop
(primarily for testing).

`io_conn` is an abstract base holding a `net_socket sock_` (passed at
construction) with virtual `on_readable`, `on_writable`, and `on_error`; the
default `on_error` delegates to `on_readable`. Higher-level types inherit from
`io_conn` directly to avoid a separate handler-lambda allocation per connection.

### `stream_conn` and `stream_conn_ptr`

Non-blocking connected stream socket driven by an `epoll_loop`. `stream_conn`
is the state object (inheriting from `io_conn`) that holds the socket, send
queue, and persistent `recv_buffer`. `stream_conn_ptr` is the move-only owning
handle wrapping a `shared_ptr<stream_conn>`; its destructor calls `hangup()`
unless `close()` was already called.

`stream_conn_ptr` provides three static factory methods:

- `adopt(loop, sock, remote, handlers, recv_buf_size)` -- adopts an
  already-connected non-blocking socket and registers it with the loop.
- `connect(loop, remote, handlers, local, recv_buf_size)` -- creates a
  non-blocking socket, optionally binds `local`, calls `connect(2)`, and
  registers with `EPOLLOUT`. When the kernel reports the outcome,
  `handle_connect` transitions to connected state (firing `on_drain`) or
  closes the connection (firing `on_close`).
- `listen(loop, local, handlers, reuse_port)` -- creates a non-blocking
  listening socket, sets `SO_REUSEADDR`, binds, calls `listen(2)`, and
  registers with the loop. `EPOLLIN` events drain all pending connections via
  `accept4`; each accepted connection becomes a self-owning `stream_conn` with
  a copy of the listener's handlers.

Thread safety: `send()`, `close()`, `hangup()`, `shutdown_read()`,
`shutdown_write()`, and the destructor of `stream_conn_ptr` are safe to call
from any thread via `epoll_loop::execute_or_post()` or `post()`.

Send path: `send(string&&)` is thread-safe (routes via
`execute_or_post`). On the loop thread, `enqueue_send` attempts an
immediate `::write`; any unsent tail is pushed onto a `deque<string>` and
`EPOLLOUT` is armed. Subsequent `EPOLLOUT` events drain the queue; when
empty, `EPOLLOUT` is disarmed and `on_drain` fires.

Receive path: `EPOLLIN` is armed while `recv_buf_.reads_enabled` is true.
When it fires, bytes are appended to the persistent `recv_buffer` and the
active `on_data` handler is invoked with a `recv_buffer_view` token. The
parser advances `begin` via `consume`; when the view destructs, compaction
and `EPOLLIN` re-arming are posted back to the loop. `set_recv_buf_size(n)`
adjusts the per-connection buffer target; `recv_buf_size()` reports it.

Handler dispatch: `stream_conn` holds `own_handlers_` (a
`stream_conn_handlers` value) and an atomic pointer `active_handlers_` that
normally points to `own_handlers_`. `stream_async_base` subclasses (see
below) temporarily redirect `active_handlers_` to their own handler struct
via an atomic CAS; the destructor restores the pointer. `stream_conn`
dispatches every event through `active_handlers_` and is unaware of the
facade hierarchy.

Half-close: `shutdown_read()` shuts down the local read side. `shutdown_write()`
shuts down the local write side and discards unsent data. `can_read()` and
`can_write()` query each direction. `local_endpoint()` and `remote_endpoint()`
return the socket addresses.

Close: `close()` is graceful -- defers the socket close until the send queue
drains. `hangup()` discards pending outbound data and closes immediately.
The destructor of `stream_conn_ptr` calls `hangup()` unless `close()` was
already called.

**Callback mode** (`stream_conn_handlers` installed at construction):
`on_data(conn, view)` fires on each `EPOLLIN` event, delivering a
`recv_buffer_view`; `on_drain(conn)` fires when the send queue empties (or an
async connect succeeds); `on_close(conn)` fires once on peer EOF, I/O error,
or failed async connect. If no `on_close` handler is installed, a graceful
close is initiated automatically so that connections without an external owner
are fully torn down rather than left half-open.

Two additional per-call async models are in `stream_async.h` and operate by
temporarily redirecting `active_handlers_`.

### `stream_async_base`, `stream_async_cb`, and `stream_async_coro`

These classes live in `stream_async.h` and provide alternative async interfaces
on top of a `stream_conn` without modifying it. Each is a facade that
temporarily installs its own `stream_conn_handlers` by atomically swapping the
connection's `active_handlers_` pointer.

`stream_async_base` is the non-copyable, non-movable base. On construction
(via `install_handlers`) it performs a CAS on `active_handlers_`; if another
facade already owns the slot, `conn_` is cleared and `is_valid()` returns
false. The destructor restores `active_handlers_` and posts
`restore_reads` to the loop. Static trampolines (`enable_reads`,
`restore_reads`, `make_cleared_view_for`, etc.) give derived classes
friend-level access to `stream_conn` private members without relying on
lambda-inherited friendship.

`stream_async_cb` provides one-shot callback I/O:

- `read(cb)` -- registers a single callback invoked with a `recv_buffer_view`
  on the next data arrival (or an empty view on connection close). Posts
  `enable_reads` asynchronously to guarantee ordering after the
  `enable_reads(false)` posted by `install_handlers`.
- `write(buf, cb)` -- takes ownership of `buf`, enqueues it, and invokes
  `cb(bool completed)` when the send queue drains (`true`) or the connection
  closes before all bytes are sent (`false`).

`stream_async_coro` provides coroutine I/O:

- `read()` -- returns an awaitable that suspends until one batch of bytes
  arrives; resumes with the received bytes as a `std::string` (empty on close).
- `write(buf)` -- returns an awaitable that enqueues `buf` and suspends until
  the send queue drains or the connection closes.

Coroutine resumption is always deferred through `post()`, never inline, to
avoid use-after-free when `do_close_now` fires from within `await_suspend`.
The `write_awaitable` sets `write_coro_` before calling `do_enqueue_send` so
that a synchronous write failure triggering `on_close` still resumes the
coroutine correctly.

```cpp
// Coroutine usage example:
loop_task handle_conn(stream_conn_ptr conn) {
  stream_async_coro coro{conn.pointer()};
  while (coro.is_open()) {
    std::string data = co_await coro.read();
    if (data.empty()) break;
    co_await coro.write(make_reply(data));
  }
}
```

### `loop_task`

Fire-and-forget coroutine return type for `epoll_loop`-driven handlers. The
coroutine body starts eagerly on the call site (`initial_suspend` returns
`suspend_never`) and the frame self-destroys on completion (`final_suspend`
returns `suspend_never`). Unhandled exceptions call `std::terminate`. See
the `stream_async_coro` example above for typical usage.

### `terminated_text_parser`

Incremental sentinel-terminated text frame parser for line-oriented protocols
(HTTP, SMTP, POP3, etc.). A `state` object is stored per connection and carries
across `on_data` calls; it holds the sentinel bytes (e.g., `"\r\n"`), a
`max_length` limit, and the count of bytes already scanned. `parse(input,
frame)` scans `input` for the sentinel, advances `input` past all consumed
bytes (including the sentinel on a match), and returns `std::optional<bool>`:
empty when more data is needed, `true` when a complete frame is found in
`frame`, and `false` when `max_length` bytes are scanned without finding the
sentinel. `reset()` clears `bytes_scanned` for the next frame. The parser never
copies data; `frame` is a `string_view` into the caller's buffer.

### `stream_sync`

Blocking synchronous stream-socket client. Intended for tests and small tools
that need to talk to a server without the overhead of an `epoll_loop`. Wraps a
blocking-mode `net_socket`. An optional per-syscall timeout is set at
construction via `SO_RCVTIMEO` / `SO_SNDTIMEO`; any error (EOF, hard error, or
timeout) marks the connection closed and subsequent calls fail immediately.
`send(data)` loops on partial writes. `recv()` returns whatever arrives first.
`recv_exact(n)` loops until exactly `n` bytes are accumulated. `recv_until(delim)`
accumulates data until the delimiter appears, leaving any trailing bytes in an
internal buffer for the next call.

## Layer 3: HTTP

### `http_head_codec`

HTTP/1.x data types and codec. All types are in `corvid::proto::http_proto`.

Enums: `http_version` (`invalid`, `http_0_9`, `http_1_0`, `http_1_1`);
`http_method` (`GET`, `HEAD`, `POST`, `PUT`, `DELETE`, `OPTIONS`, `PATCH`,
`CONNECT`, `TRACE`); `after_response` (`close`, `keep_alive`);
`http_status_code` (full 1xx-5xx range); `content_type_value` (`text_html`,
`text_plain`, `application_json`); `transfer_encoding_value` (`identity`,
`chunked`); `upgrade_value` (`websocket`). All enums have stream operators.

`http_headers` is an ordered multimap with O(1) average lookup. It stores
fields in insertion order (a `vector`) and indexes them by canonical
title-case name (an `unordered_map`). `add(name, value)` normalizes the name
to title case, drops unknown fields into an `others` bucket, and returns false
on duplicate detected-header fields. `add_raw(name, value)` bypasses
normalization. `get(name)` returns the first value for a canonical name.
`get_combined(name)` returns all values joined by `", "`.

`request_options` and `response_options` carry decoded semantic fields parsed
from the header collection: `connection` (`after_response`), `content_length`
(`optional<size_t>`), `transfer_encoding` (`transfer_encoding_value`),
`upgrade` (`upgrade_value`), and the `keep_alive(version)` helper that
derives the keep-alive disposition from the `Connection` header and version.

`request_head` parses a raw HTTP request head (request line + header fields)
via `parse(string_view)`. Populates `method`, `target`, `version`, `headers`,
and `options`. `clear()` resets all fields for reuse across keep-alive
requests.

`response_head` builds and serializes a response head. `serialize()` produces
the full status line + header fields as a `string` ready to write to the wire.
`make_error_response(keep_alive, version, code, phrase)` is a static factory
that returns a complete serialized error response.

### `http_server`

HTTP/1.1 server (with HTTP/0.9 and HTTP/1.0 fallback) built on `stream_conn`.
Constructed via `create(endpoint, loop, wheel, request_timeout, write_timeout)`.
If `loop` is null the server starts its own `epoll_loop_runner`; if `wheel` is
null it starts its own `timing_wheel_runner` (from `corvid::concurrency`).
Returns null if the listen socket cannot be bound.

Connection state is held in `stream_conn_with_state<http_conn_state>`, which
bundles a `terminated_text_parser::state`, sequencing counters for
`timer_fuse`, the current `http_phase`, a reusable `request_head`, and a
leading-CRLF count.

Request parsing uses two phases of `terminated_text_parser`:

- Phase 1 (`request_line`): seeks `"\r\n"` (max 8192 bytes). Leading bare
  CRLFs are silently discarded per RFC 9112 section 2.2. HTTP/0.9 requests
  (no version token) are dispatched immediately after this phase.
- Phase 2 (`header_lines`): seeks `"\r\n\r\n"` for the complete header block.
  A blank line immediately after the request line (no headers) is also
  accepted.

`on_data` loops over all complete header blocks already in the receive buffer,
queuing a response for each. Because `stream_conn::send` is FIFO, responses
are delivered in request order (pipelining).

Persistent connections: keep-alive by default for HTTP/1.1; close by default
for HTTP/1.0; never for HTTP/0.9. The `Connection` header is honored.

Validation: HTTP/1.1 requests without a `Host` header receive a 400 response.
Unsupported methods receive a 405 response. The path encodes an optional
response body padding size for testing.

Timeouts: `request_timeout` (default 30 s) is armed after each accepted
connection and re-armed after each parsed request on keep-alive connections. If
the deadline expires before a complete request arrives, `timer_fuse` posts a
`hangup` to the loop. `write_timeout` (default 5 s) is armed before each
response is queued and disarmed in `on_drain` when the send queue empties.

## What comes next

See `roadmap.md` for the full plan. Layer 3 has a complete HTTP/1.1 server;
remaining work includes request body reading, `POST`/`PUT` support, chunked
transfer encoding, and an HTTP client and proxy. Layer 4 adds WebSockets. If
datagram support is needed later, it should arrive as a separate `dgram_conn`
abstraction built on `epoll_loop`.
