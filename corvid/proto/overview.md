# Proto Module Overview

The `corvid::proto` module provides TCP/IP networking primitives, currently
at the POSIX/Linux wrapper layer.

## What exists now

`ipv4_addr` and `ipv6_addr` cover the essentials: construction, parsing,
formatting, comparison, and classification predicates (loopback, multicast,
private, etc.). On POSIX platforms they interop with `in_addr` / `in6_addr`.

`ip_endpoint` pairs an IPv4 or IPv6 address with a port number. It
default-constructs to an invalid state (checkable via `is_valid()`). It parses
`1.2.3.4:80` and `[2001:db8::1]:80` notation, formats back to the same, and
on POSIX interops with `sockaddr_in`, `sockaddr_in6`, and `sockaddr_storage`.
Named factories `any_v4()` and `any_v6()` produce wildcard bind addresses.

`os_file` is a RAII wrapper around a platform file descriptor. It is movable
and non-copyable, and provides `control()` (a variadic `fcntl` wrapper),
`get_flags()`, and `set_nonblocking()`.

`ip_socket` owns an `os_file` and adds socket-specific operations:
`set_option`/`get_option` wrap `setsockopt`/`getsockopt`, and named helpers
cover the most common options (`set_reuse_addr`, `set_reuse_port`,
`set_nodelay`, `set_keepalive`, `set_recv_buffer_size`, `set_send_buffer_size`).
Fd-level operations are delegated to the underlying `os_file`. It is intended
as a base class for future `tcp_socket` and `udp_socket` types.

`dns_resolver` resolves hostnames to `ip_endpoint` values via `getaddrinfo`.
`find_all()` returns a vector of all matching endpoints, with optional `family`
filter (`AF_UNSPEC`, `AF_INET`, or `AF_INET6`) and `max_results` cap.
`find_one()` returns the first result, or a default-constructed (invalid)
`ip_endpoint` on failure.

## What comes next

See `roadmap.md` for the full plan. The immediate next layer adds an
epoll-based I/O loop (`io_loop`), TCP listener/connection types
(`tcp_listener`, `tcp_conn`), and outbound connection support (`tcp_client`).
