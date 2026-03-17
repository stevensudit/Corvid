// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2026 Steven Sudit
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once
// Umbrella header for the Corvid proto module.
//
// Includes:
//  ipv4_addr   - IPv4 address type with construction, classification, and
//                formatting
//  ipv6_addr   - IPv6 address type with construction, classification, and
//                formatting
//  ip_endpoint - IP address + port pair for IPv4 or IPv6
//  dns_resolve - thin wrapper around `getaddrinfo` returning a list of
//                `ip_endpoint` values
//  io_loop     - single-threaded epoll-based I/O event loop
//  tcp_conn    - non-blocking TCP connection with callback-based async I/O
//  loop_task   - fire-and-forget coroutine return type for `io_loop` handlers
#include "proto/ipv4_addr.h"
#include "proto/ipv6_addr.h"
#include "proto/ip_endpoint.h"
#include "proto/dns_resolve.h"
#include "proto/io_loop.h"
#include "proto/tcp_conn.h"
#include "proto/loop_task.h"
