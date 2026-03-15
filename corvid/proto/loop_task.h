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
#include <coroutine>
#include <exception>

namespace corvid { inline namespace proto {

// Fire-and-forget coroutine return type for use with `io_loop`-driven code.
//
// `loop_task` is the minimal coroutine machinery needed to write async I/O
// handlers using `co_await`. The returned `loop_task` value is intentionally
// discarded by the caller; the coroutine frame is self-destroying.
//
// `initial_suspend` returns `std::suspend_never`, so the coroutine body
// begins executing immediately on the call site (synchronously) up to its
// first `co_await`. `final_suspend` returns `std::suspend_never`, so the
// frame is destroyed automatically when the body exits.
//
// Exceptions escaping the coroutine body call `std::terminate`. An
// unhandled exception in an async handler indicates a programming error
// with no safe recovery path.
//
// Usage:
//
//   loop_task handle_conn(tcp_conn conn) {
//     while (conn.is_open()) {
//       std::string data = co_await conn.async_read();
//       if (data.empty()) break;                // connection closed
//       co_await conn.async_send(make_reply(data));
//     }
//   }
//
//   // Spawn from within a loop callback or `post()`'d function so that
//   // subsequent coroutine resumptions stay on the loop thread:
//   on_accept = [](tcp_conn conn) { handle_conn(std::move(conn)); };
//
// NOLINTBEGIN(readability-convert-member-functions-to-static)
struct loop_task {
  struct promise_type {
    loop_task get_return_object() noexcept { return {}; }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() noexcept { std::terminate(); }
  };
};
// NOLINTEND(readability-convert-member-functions-to-static)

}} // namespace corvid::proto
