# Proto Module Overview

The `corvid::proto` module provides TCP/IP networking primitives and an
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

### `ip_endpoint`

Address + port pair in a tagged union of `sockaddr_in` and `sockaddr_in6`.
Default-constructs to an invalid state (checkable via `is_valid()`). Parses
`1.2.3.4:80` and `[2001:db8::1]:80` notation and formats back to the same.
Named factories `any_v4()` and `any_v6()` produce wildcard bind addresses.
On POSIX, interops with `sockaddr_in`, `sockaddr_in6`, and
`sockaddr_storage`.

### `ip_socket`

RAII socket handle owning an `os_file`; movable, non-copyable; fd-level
operations delegate to the underlying `os_file` via `file()`. Type-safe
`set_option<T>(level, optname, value)` / `get_option<T>(level, optname)`
wrap `setsockopt` / `getsockopt`. Named helpers cover the most common
options: `set_reuse_addr`, `set_reuse_port`, `set_nodelay`, `set_keepalive`,
`set_recv_buffer_size`, `set_send_buffer_size`. Intended as a base class for
`tcp_socket` and `udp_socket`; a protected constructor and `make_ip_socket`
static are provided for that purpose.

### `dns_resolver`

Resolves hostnames to `ip_endpoint` values via `getaddrinfo`. `find_all()`
returns a `std::vector<ip_endpoint>` with an optional address-family filter
(`AF_UNSPEC`, `AF_INET`, or `AF_INET6`) and `max_results` cap. `find_one()`
returns the first result as an `ip_endpoint`, or a default-constructed
(invalid) endpoint on failure.

## Layer 2: TCP I/O Loop

### `io_loop`

`epoll`-based I/O event loop. `register_socket(sock, shared_ptr<io_conn>)`
accepts a pre-built `io_conn` object (used by `tcp_conn` to eliminate a
separate allocation). `set_writable(sock, bool)` toggles `EPOLLOUT` without
disturbing stored handlers -- call it only while the send buffer is
non-empty. `post(fn)` is thread-safe: it locks a queue, pushes the callback,
then writes to an internal `eventfd` to interrupt a sleeping `epoll_wait`.
`run()` / `run_once(timeout_ms)` / `stop()` drive the dispatch loop.

`io_conn` is an abstract base with virtual `on_readable`, `on_writable`, and
`on_error`; higher-level types inherit from it directly to avoid a separate
handler-lambda allocation per connection.

### `tcp_conn`

Non-blocking TCP connection driven by an `io_loop`. Implemented as a movable
handle owning a `shared_ptr<state>`, where `state` inherits from `io_conn` --
one heap allocation per connection.

Send path: `send(string&&)` is thread-safe (posts to the loop). On the loop
thread, `enqueue_send` attempts an immediate `::write`; any unsent tail is
pushed onto a `deque<string>` and `EPOLLOUT` is armed. Subsequent `EPOLLOUT`
events drain the queue; when empty, `EPOLLOUT` is disarmed.

Receive path: `EPOLLIN` triggers `::read` into a buffer pre-sized with
`resize_and_overwrite` (no zero-initialization), trimmed to the actual byte
count before delivery. `set_recv_buf_size(bytes)` changes the per-connection
read size used for future reads, and `recv_buf_size()` reports the current
configured size.

Graceful close: `close()` defers the socket close until the send queue
drains; the destructor has the same semantics.

Supports three async models:

- **Callback mode** (`tcp_conn_handlers`): `on_data(string&)` fires on each
  read, `on_drain()` fires whenever a `send()` completes with no outbound
  bytes left pending (including immediate writes), `on_close()` fires on EOF
  or error.

- **Coroutine mode**: `co_await conn.async_read()` suspends until one batch
  of data arrives (or the connection closes, returning an empty string);
  `co_await conn.async_send(buf)` enqueues `buf` and suspends until the send
  queue drains. All coroutine resumptions are deferred through `loop_.post()`
  to avoid use-after-free when a write error triggers `do_close_now` from
  within `await_suspend`.

- **One-shot callback mode**: `async_cb_read(cb)` registers a single callback
  for the next readable batch and invokes it inline on the loop thread with
  the internal `string&`, preserving the same move-or-borrow semantics as
  `on_data`. `async_cb_write(buf, cb)` sends `buf` and invokes
  `cb(bool completed)` when the write reaches a terminal state:
  `completed == true` means the write fully drained; `completed == false`
  means the connection closed or failed before all bytes were sent, possibly
  after a partial write. For each direction, at most one one-shot waiter may
  be outstanding at a time, regardless of whether it came from the coroutine
  or callback API.

Precedence is per direction: a pending one-shot waiter consumes the next read
or write-completion event, and the persistent handlers are used only when no
one-shot waiter is pending for that direction. Parallel `async_cb_*`
registrations fail cleanly by returning `false`; overlapping `async_read()` /
`async_send()` calls are still programming errors. If the connection closes
before a pending `async_cb_read()` receives data, the callback is invoked with
an empty string; code that retains access to the `tcp_conn` can use `is_open()`
to detect that the empty buffer came from closure. Pending write waiters
always complete, reporting success or failure.

### `loop_task`

Fire-and-forget coroutine return type for `io_loop`-driven handlers. The
coroutine body starts eagerly on the call site (`initial_suspend` returns
`suspend_never`) and the frame self-destroys on completion (`final_suspend`
returns `suspend_never`). Unhandled exceptions call `std::terminate`.

```cpp
loop_task handle_conn(tcp_conn conn) {
  while (conn.is_open()) {
    std::string data = co_await conn.async_read();
    if (data.empty()) break;
    co_await conn.async_send(make_reply(data));
  }
}
```

## What comes next

See `roadmap.md` for the full plan. The immediate next items in Layer 2 are
`tcp_listener` (accept loop producing `tcp_conn` instances) and `tcp_client`
(outbound non-blocking `connect()`). Layer 3 adds HTTP/1.1; Layer 4 adds
WebSockets.
