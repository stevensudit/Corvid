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
- **[done]** `stream_conn` / `stream_conn_ptr` -- non-blocking connected
  stream-socket wrapper driven by an `epoll_loop`; `stream_conn` is the state
  object inheriting from `io_conn`; `stream_conn_ptr` is the move-only owning
  handle (`shared_ptr<stream_conn>` internally; destructor calls `hangup()`);
  three factory methods on `stream_conn_ptr`: `adopt()` for already-connected
  sockets, `connect()` for outbound async connect, `listen()` for accept loops;
  `send(string&&)` / `close()` / `hangup()` / `shutdown_read()` /
  `shutdown_write()` are thread-safe via `execute_or_post()` / `post()`;
  `can_read()` / `can_write()` query half-close state; `local_endpoint()` /
  `remote_endpoint()` return socket addresses; supports persistent callback
  mode via `stream_conn_handlers` (`on_data`, `on_drain`, `on_close`); two
  additional per-call async models are provided by `stream_async.h`
- **[done]** `stream_async_coro` -- now in `stream_async.h`; C++20 coroutine
  wrapper for `stream_conn` using the `stream_async_base` handler-redirect
  mechanism; `async_read()` / `async_send()` return awaitables; `EPOLLIN` is
  armed only while a read coroutine is suspended (via `handlers_.on_data`
  toggling) to prevent data loss between reads
- **[done]** `loop_task` -- fire-and-forget coroutine return type for `epoll_loop`
  handlers; `initial_suspend` is `suspend_never` (eager start);
  `final_suspend` is `suspend_never` (self-destroying frame); enables
  `co_await conn.async_read()` / `co_await conn.async_send(buf)` patterns
- **[done]** `tcp_listener` -- now integrated as `stream_conn_ptr::listen()`;
  creates a non-blocking listening socket, binds, and calls `listen(2)`; drains
  accepted connections via `accept4` on `EPOLLIN`, creating self-owning
  `stream_conn` instances with a copy of the listener's handlers
- **[done]** `tcp_client` -- now integrated as `stream_conn_ptr::connect()`;
  creates a non-blocking socket, optionally binds the local end, calls
  `connect(2)`, and notifies the caller via `on_drain` on success or `on_close`
  on failure
- **Future:** `io_uring_loop` -- `io_uring`-based event loop with the same
  interface as `epoll_loop`; higher layers unchanged

If datagram support is needed later, add a separate `dgram_conn` abstraction
on top of `epoll_loop` rather than broadening `stream_conn`.

## Layer 3: HTTP

HTTP/1.1 client and server built on top of the stream I/O loop.

- `http_request` -- parsed request: method, target, version, headers, body
- `http_response` -- status line, headers, body; supports chunked transfer
- `http_parser` -- incremental request/response parser fed from an `io_buffer`
- `http_server` -- accepts connections via `tcp_listener`; dispatches parsed
  `http_request` objects to a handler callback; manages connection keep-alive
- `http_client` -- sends `http_request`, delivers `http_response` via callback
  or coroutine
- `http_router` -- optional path-dispatch layer on top of `http_server`
- **Future:** HTTP/2 (HPACK, streams, flow control)

## Layer 4: WebSockets

WebSocket protocol built on top of the HTTP upgrade mechanism.

- `ws_handshake` -- performs the HTTP/1.1 upgrade handshake (Sec-WebSocket-Key,
  Sec-WebSocket-Accept, subprotocol negotiation)
- `ws_frame` -- encodes and decodes WebSocket frames (opcode, masking, payload
  length variants)
- `ws_conn` -- wraps an upgraded `stream_conn`; exposes `send_text`,
  `send_binary`, `send_ping`, and a message-received callback
- `ws_server` -- integrates with `http_server` to intercept upgrade requests
  and produce `ws_conn` instances
- `ws_client` -- initiates a WebSocket connection from the client side

## Design Principles

- Each layer depends only on the layer(s) below it; higher layers are optional.
- No external dependencies beyond libc++ and standard POSIX/Linux headers.
- Headers are self-contained; no source files (library remains header-only).
- RAII throughout: no manual resource management in user code.
- Async model: callbacks for simple cases, C++20 coroutines for sequential
  logic; both modes are supported by `stream_conn` and are mutually exclusive
  per direction (read or write independently).
- Linux is the target OS.
