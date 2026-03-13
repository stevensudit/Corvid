# Proto Module Roadmap

## Layer 1: POSIX/Linux Primitives (C++ wrappers)

Thin, zero-overhead C++ wrappers around POSIX and Linux networking primitives.

- `ipv4_addr` -- wraps `in_addr`; construction from dotted-decimal string,
  `uint32_t`, and four octets; comparison; formatting
- `ipv6_addr` -- wraps `in6_addr`; similar interface to `ipv4_addr`
- `ip_endpoint` -- address + port pair; wraps `sockaddr_in` / `sockaddr_in6`
- `ip_socket` -- RAII wrapper around a socket handle (fd on POSIX, SOCKET on
  Windows); movable, non-copyable; exposes `close()`, `release()`, raw
  `handle()`; type-safe `set_option` / `get_option` methods (e.g.,
  `SO_REUSEADDR`, `TCP_NODELAY`); intended as a base for `tcp_socket` and
  `udp_socket` subclasses that partition non-overlapping functionality; a
  `socket_options` parameter struct may be added above the individual option
  methods as a convenience for bulk setup, if that proves necessary
- `dns_resolve` -- thin wrapper around `getaddrinfo` returning a range of
  `ip_endpoint` values

## Layer 2: TCP I/O Loop (epoll-based, io_uring later)

Non-blocking TCP I/O with an event loop. Initial implementation uses `epoll`;
the interface is designed so `epoll` can later be swapped for `io_uring`
without changing higher layers.

- `tcp_listener` -- binds and listens on an `ip_endpoint`; produces accepted
  `tcp_conn` instances
- `tcp_conn` -- represents a single TCP connection; owns a `tcp_socket`;
  provides async read/write via callbacks or coroutines (TBD)
- `io_loop` -- `epoll`-based event loop; registers/unregisters fds; drives
  readability/writability callbacks; single-threaded initially
- `tcp_client` -- initiates an outbound non-blocking `connect()` to an
  `ip_endpoint` and registers the fd with `io_loop`; delivers a `tcp_conn`
  on success via the same callback mechanism used by `tcp_listener`
- `io_buffer` -- fixed or growing byte buffer for scatter/gather I/O; shared
  between `tcp_conn` and higher layers
- **Future:** replace `epoll` backend with `io_uring` (`io_uring_loop`) behind
  the same `io_loop` interface

## Layer 3: HTTP

HTTP/1.1 client and server built on top of the TCP I/O loop.

- `http_request` -- parsed request: method, target, version, headers, body
- `http_response` -- status line, headers, body; supports chunked transfer
- `http_parser` -- incremental request/response parser fed from `io_buffer`
- `http_server` -- accepts connections via `tcp_listener`; dispatches parsed
  `http_request` objects to a handler callback; manages connection keep-alive
- `http_client` -- sends `http_request`, delivers `http_response` via callback
  or coroutine (TBD)
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
- Async model TBD (callbacks first, coroutines considered for a later pass).
- Linux is the target OS, but platform dependencies (POSIX primitives, `epoll`,
  `io_uring`) should be isolated to dedicated headers so that porting to
  Windows (IOCP, Winsock) or macOS (kqueue) is a matter of providing
  alternative implementations, not restructuring the library.
