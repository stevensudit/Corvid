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
//  socket_enums - typed enums for the socket APIs: `address_family`,
//                `socket_type`, `protocol_type`, socket/TCP options, and
//                `shutdown_how`
//  sockaddr_view - non-owning `sockaddr` pointer + length pair with family
//                classification, accessors, comparison, and formatting
//  net_socket  - RAII network socket wrapping creation, bind/connect/accept,
//                synchronous I/O, and options
//  net_endpoint - IP address + port pair for IPv4 or IPv6, or a UDS/ANS path
//  iov_msghdr  - scatter/gather socket I/O via `msghdr` with
//                `sendmsg`/`recvmsg`
//  dns_resolver - thin wrapper around `getaddrinfo` returning a list of
//                `net_endpoint` values
//  epoll_loop  - single-threaded epoll-based I/O event loop
//  epoll_stream_conn - non-blocking stream connection with persistent-callback
//                async I/O
//  json_parser - strict header-only JSON parser and writer with non-owning
//                views
//  terminated_text_parser - sentinel-terminated text frame parser for
//                line-oriented protocols (HTTP, SMTP, POP3, etc.)
//  base-64     - Base64 encoding and decoding
//  utf8-checker - incremental UTF-8 validator
//  http_head_codec - HTTP/1.1 types: `http_version`/`http_method` enums,
//                `http_headers` ordered multimap, `request_head` and
//                `response_head` with extraction and serialization
//  sha-1       - SHA-1 digest for non-security-critical protocol work, such
//                as the WebSocket handshake
//  epoll_http_server - HTTP/1.1 server with keep-alive and pipelining
//  iou_loop    - io_uring-based I/O event loop, the counterpart to
//                `epoll_loop`
//  iou_stream_conn - io_uring-backed stream connection
#include "proto/ipv4_addr.h"
#include "proto/ipv6_addr.h"
#include "proto/socket_enums.h"
#include "proto/sockaddr_view.h"
#include "proto/net_socket.h"
#include "proto/net_endpoint.h"
#include "proto/iov_msghdr.h"
#include "proto/dns_resolver.h"
#include "proto/epoll/epoll_loop.h"
#include "proto/epoll/epoll_stream_conn.h"
#include "proto/misc/json_parser.h"
#include "proto/misc/terminated_text_parser.h"
#include "proto/misc/base-64.h"
#include "proto/misc/utf8-checker.h"
#include "proto/misc/http_head_codec.h"
#include "proto/misc/sha-1.h"
#include "proto/epoll/epoll_http_transaction.h"
#include "proto/epoll/epoll_http_websocket.h"
#include "proto/epoll/epoll_http_websocket_transaction.h"
#include "proto/epoll/epoll_http_server.h"
#include "proto/epoll/epoll_http_file_transaction.h"
#include "proto/io_uring/iou_loop.h"
#include "proto/io_uring/iou_stream_conn.h"
