# Proto Module Roadmap

Items marked **[done]** are implemented and tested.

## Layer 1: POSIX/Linux Primitives (C++ wrappers)

Thin, zero-overhead C++ wrappers around POSIX and Linux networking primitives.

- **[done]** `os_file` -- RAII wrapper around a raw file descriptor; non-copyable,
  movable; provides `read`, `write`, `get_flags`, `set_flags`, and `is_open`;
  used as the fd-owning base for `ip_socket`
- **[done]** `ipv4_addr` -- wraps `in_addr`; construction from dotted-decimal
  string, host-byte-order `uint32_t`, and four octets; named factories
  (`any()`, `loopback()`, `broadcast()`); classification predicates;
  comparison; formatting
- **[done]** `ipv6_addr` -- wraps `in6_addr`; similar interface to `ipv4_addr`;
  construction from colon-hex string, `uint8_t[16]` array, and
  `uint16_t[8]` array; named factories and classification predicates
- **[done]** `ip_endpoint` -- address + port pair; wraps both `sockaddr_in` and
  `sockaddr_in6` in a tagged union; round-trip through POSIX `sockaddr`
  pointers; comparison; formatting
- **[done]** `ip_socket` -- RAII socket handle (fd on POSIX); movable,
  non-copyable; `make_tcp_socket()` / `make_udp_socket()` factory helpers;
  type-safe `set_option` / `get_option` (e.g., `SO_REUSEADDR`,
  `TCP_NODELAY`); `set_nonblocking()`, `bind()`, `connect()`, `listen()`,
  `accept()`; `set_send_buffer_size()` convenience method
- **[done]** `dns_resolve` -- thin wrapper around `getaddrinfo`; `dns_resolve()`
  returns `std::vector<ip_endpoint>`; `dns_resolve_one()` returns
  `std::optional<ip_endpoint>`; both accept an optional address-family
  filter

## Layer 2: TCP I/O Loop (epoll-based, io_uring later)

Non-blocking TCP I/O with an event loop. Initial implementation uses `epoll`;
the interface is designed so `epoll` can later be swapped for `io_uring`
without changing higher layers.

- **[done]** `io_loop` -- `epoll`-based event loop; `add` /
  `register_socket` / `unregister_socket` manage fd registrations; `set_writable` toggles `EPOLLOUT`
  without changing stored handlers; `post(fn)` schedules work on the loop
  thread from any thread (wakes `epoll_wait` via an internal `eventfd`);
  `run()` / `run_once(timeout_ms)` / `stop()` drive dispatch; `io_conn` is
  an abstract base with virtual `on_readable` / `on_writable` / `on_error`
  so higher-level types inherit from it directly to avoid a separate
  handler-lambda allocation
- **[done]** `tcp_conn` -- non-blocking TCP connection driven by an `io_loop`;
  movable handle owning a `shared_ptr<state>` (one heap allocation per
  connection); `send(string&&)` / `close()` are thread-safe via `post()`;
  supports both a callback mode (`tcp_conn_handlers`: `on_data`, `on_drain`,
  `on_close`) and a C++20 coroutine mode (`async_read()` / `async_send()`)
  -- the two modes are mutually exclusive per connection
- **[done]** `loop_task` -- fire-and-forget coroutine return type for `io_loop`
  handlers; `initial_suspend` is `suspend_never` (eager start);
  `final_suspend` is `suspend_never` (self-destroying frame); enables
  `co_await conn.async_read()` / `co_await conn.async_send(buf)` patterns
- `tcp_listener` -- binds and listens on an `ip_endpoint`; produces accepted
  `tcp_conn` instances via a callback or coroutine
- `tcp_client` -- initiates an outbound non-blocking `connect()` to an
  `ip_endpoint` and registers the fd with `io_loop`; delivers a `tcp_conn`
  on success via the same mechanism used by `tcp_listener`
- **Future:** replace `epoll` backend with `io_uring` (`io_uring_loop`) behind
  the same `io_loop` interface

## Layer 3: HTTP

HTTP/1.1 client and server built on top of the TCP I/O loop.

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
- `ws_conn` -- wraps an upgraded `tcp_conn`; exposes `send_text`,
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
  logic; both modes are supported by `tcp_conn` and are mutually exclusive
  per connection.
- Linux is the target OS, but platform dependencies (`epoll`, `io_uring`,
  POSIX primitives) are isolated to dedicated headers so that porting to
  Windows (IOCP, Winsock) or macOS (kqueue) is a matter of providing
  alternative implementations, not restructuring the library.
