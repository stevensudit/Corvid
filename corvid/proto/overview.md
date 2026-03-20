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
is the state object (inheriting from `io_conn`) that holds the socket, buffers,
and async waiters. `stream_conn_ptr` is the move-only owning handle wrapping a
`shared_ptr<stream_conn>`; its destructor calls `hangup()` unless `close()` was
already called.

`stream_conn_ptr` provides three static factory methods:

- `adopt(loop, sock, remote, handlers, recv_buf_size)` -- adopts an
  already-connected non-blocking socket and registers it with the loop.
- `connect(loop, remote, handlers, local, recv_buf_size)` -- creates a
  non-blocking socket, optionally binds `local`, calls `connect(2)`, and
  registers with `EPOLLOUT`. When the kernel reports the outcome,
  `handle_connect()` transitions to connected state (firing `on_drain`) or
  closes the connection (firing `on_close`).
- `listen(loop, local, handlers, reuse_port)` -- creates a non-blocking
  listening socket, sets `SO_REUSEADDR`, binds, calls `listen(2)`, and
  registers with the loop. `EPOLLIN` events drain all pending connections via
  `accept4`; each accepted connection becomes a self-owning `stream_conn` with
  a copy of the listener's handlers.

Thread safety: `send()`, `close()`, `hangup()`, `shutdown_read()`,
`shutdown_write()`, and the destructor of `stream_conn_ptr` are safe to call
from any thread via `epoll_loop::execute_or_post()` or `post()`.

Send path: `send(string&&)` is thread-safe (posts to the loop). On the loop
thread, `enqueue_send` attempts an immediate `::write`; any unsent tail is
pushed onto a `deque<string>` and `EPOLLOUT` is armed. Subsequent `EPOLLOUT`
events drain the queue; when empty, `EPOLLOUT` is disarmed.

Receive path: `EPOLLIN` is armed while a one-shot read waiter is active or a
persistent `on_data` handler is installed. When it fires, `::read` fills a
buffer pre-sized with `resize_and_overwrite` (no zero-initialization), trimmed
to the actual byte count before delivery. `set_recv_buf_size(bytes)` sets the
per-connection receive-buffer target; `recv_buf_size()` reports it.

Half-close: `shutdown_read()` shuts down the local read side (notifying any
pending read waiter). `shutdown_write()` shuts down the local write side and
discards unsent data. `can_read()` and `can_write()` query each direction.
`local_endpoint()` and `remote_endpoint()` return the socket addresses.

Close: `close()` is graceful -- defers the socket close until the send queue
drains. `hangup()` discards pending outbound data and closes immediately.
The destructor of `stream_conn_ptr` calls `hangup()` unless `close()` was
already called.

Supports three async models:

- **Callback mode** (`stream_conn_handlers`): `on_data(conn, string&)` fires
  on each read; `on_drain(conn)` fires when a `send()` completes with no
  outbound bytes left pending (including immediate writes and `EPOLLOUT`-driven
  drains) and also when an async `connect()` succeeds; `on_close(conn)` fires
  once on peer EOF, I/O error, or failed async connect. For a normal peer EOF,
  writes may still be possible (half-close); call `close()` or `hangup()` to
  shut down fully.

- **Coroutine mode**: `co_await conn.async_read()` suspends until one batch
  of data arrives (or the connection closes, returning an empty string);
  `co_await conn.async_send(buf)` enqueues `buf` and suspends until the send
  queue drains. All coroutine resumptions are deferred through `loop_.post()`
  to avoid use-after-free when a write error triggers `do_close_now` from
  within `await_suspend`.

- **One-shot callback mode**: `async_cb_read(cb, exec)` registers a single
  callback for the next readable batch and invokes it inline on the loop thread
  with the internal `string&`. `async_cb_write(buf, cb, exec)` sends `buf` and
  invokes `cb(bool completed)` when the write reaches a terminal state:
  `completed == true` means the write fully drained; `completed == false` means
  the connection closed or failed before all bytes were sent, possibly after a
  partial write. The optional `exec` parameter (`execution::blocking` by
  default) controls whether the call blocks until registration is confirmed when
  invoked from off the loop thread.

Precedence is per direction: a pending one-shot waiter consumes the next read
or write-completion event, and the persistent handlers are used only when no
one-shot waiter is pending for that direction. Parallel `async_cb_*`
registrations fail cleanly by returning `false`; overlapping `async_read()` /
`async_send()` calls are programming errors (asserted in debug builds). If the
connection closes before a pending `async_cb_read()` receives data, the callback
is invoked with an empty string; code that retains access to the `stream_conn`
can use `is_open()` to detect that the empty buffer came from closure. Pending
write waiters always complete, reporting success or failure.

### `loop_task`

Fire-and-forget coroutine return type for `epoll_loop`-driven handlers. The
coroutine body starts eagerly on the call site (`initial_suspend` returns
`suspend_never`) and the frame self-destroys on completion (`final_suspend`
returns `suspend_never`). Unhandled exceptions call `std::terminate`.

```cpp
loop_task handle_conn(stream_conn_ptr conn) {
  while (conn->is_open()) {
    std::string data = co_await conn->async_read();
    if (data.empty()) break;
    co_await conn->async_send(make_reply(data));
  }
}
```

## What comes next

See `roadmap.md` for the full plan. Layer 2 is complete: listening and outbound
connection support are integrated into `stream_conn_ptr::listen()` and
`stream_conn_ptr::connect()`. Layer 3 adds HTTP/1.1; Layer 4 adds WebSockets.
If datagram support is needed later, it should arrive as a separate `dgram_conn`
abstraction built on `epoll_loop`.
