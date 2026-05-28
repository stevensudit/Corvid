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
#include <concepts>
#include <exception>
#include <string_view>
#include <utility>

namespace corvid { inline namespace meta {

#pragma region try_or_log

// Run `fn` inside a try block as a noexcept firewall: returns `fn()` on
// no-throw, or `on_throw` if it threw. Canonical idiom for the body of a
// noexcept callback that wraps a C-ABI boundary (ngtcp2, nghttp3, etc.) and
// needs to recover from `std::bad_alloc` (or similar) by reporting failure to
// its caller instead of terminating the process.
//
// `on_throw` defaults to `false`, so the common bool-returning case ("session
// healthy?" / "callback succeeded?") works with no second argument. For other
// return types, pass a matching `on_throw` value: e.g., `try_or_log([&]{
// return when(); }, time_point_t{})` for a timer callback that wants to
// unschedule on throw.
//
// The lambda's return type and `on_throw`'s type must agree (or be unifiable);
// the function's return type is deduced from both. Logical-false and
// exceptional failure map to the same outer return when `on_throw` matches the
// "failure" sentinel, which is usually what we want at a firewall.
//
// On exception, a `reason` view is captured (`what()` for `std::exception`, a
// placeholder literal otherwise) and currently discarded. The two-arm shape
// matches the production pattern: the typed handler is preferred and yields a
// readable message; the catch-all is the fallback for non-std throws, where a
// useful message would require platform-specific sniffing (e.g., demangling
// the active exception's type via `abi::__cxa_demangle`).
//
// `reason` is held as a `string_view`, not a `string`, so the recovery path
// itself cannot trigger a second `bad_alloc` and terminate via this function's
// own `noexcept`. When real logging is wired in, the logger must consume
// `reason` within this scope; `e.what()`'s lifetime ends with the catch frame.
//
// TODO: route `reason` to a logging facility once Corvid has one.
template<std::invocable F, typename T = bool>
[[nodiscard]] auto try_or_log(F&& fn, T on_throw = false) noexcept {
  std::string_view reason;
  try {
    return std::forward<F>(fn)();
  }
  catch (const std::exception& e) {
    reason = e.what();
  }
  catch (...) {
    reason = "unknown exception";
  }
  (void)reason;
  return on_throw;
}

#pragma endregion

}} // namespace corvid::meta
