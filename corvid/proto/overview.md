# Proto Module Overview

The `corvid::proto` module is just getting started. Right now it provides
basic IP address types for IPv4 and IPv6.

`ipv4_addr` and `ipv6_addr` cover the essentials: construction, parsing,
formatting, comparison, and a few common classification checks. On Unix-like
platforms, they also offer straightforward interop with the corresponding POSIX
socket address types.

Higher-level networking pieces such as endpoints, sockets, DNS resolution, and
transport protocols are not part of the module yet.
