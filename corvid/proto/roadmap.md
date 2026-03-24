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
- **[done]** `stream_conn` / `stream_conn_ptr` -- non-blocking connected
  stream-socket wrapper driven by an `epoll_loop`; `stream_conn` is the state
  object inheriting from `io_conn`, holding the send queue and a persistent
  `recv_buffer`; `stream_conn_ptr` is the move-only owning handle
  (`shared_ptr<stream_conn>` internally; destructor calls `hangup()`); three
  factory methods on `stream_conn_ptr`: `adopt()` for already-connected
  sockets, `connect()` for outbound async connect, `listen()` for accept loops;
  `send(string&&)` / `close()` / `hangup()` / `shutdown_read()` /
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
- **Future:** `io_uring_loop` -- `io_uring`-based event loop with the same
  interface as `epoll_loop`; higher layers unchanged

If datagram support is needed later, add a separate `dgram_conn` abstraction
on top of `epoll_loop` rather than broadening `stream_conn`.

## Layer 3: HTTP

HTTP server built incrementally from an HTTP 0.9 baseline to full HTTP/1.1,
followed by client and proxy support.

- **[done]** `http_server` (HTTP 0.9) -- minimal server that listens for TCP or
  UDS/ANS connections, parses each request line with `terminated_text_parser`,
  and sends a canned HTML response for any `GET /path` request, then closes the
  connection; constructed via `create(loop, endpoint)`, which accepts an
  optional shared `epoll_loop` or starts its own `epoll_loop_runner`
- Improve `http_server` incrementally to full HTTP/1.1: persistent connections,
  request/response headers, chunked transfer encoding, content negotiation,
  and keep-alive
- `http_client` -- HTTP/1.1 client built on `stream_conn`
- `http_proxy` -- HTTP proxy support
- **Future:** HTTP/2

## Layer 4: WebSockets

WebSocket protocol built on top of the HTTP/1.1 upgrade mechanism.

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
