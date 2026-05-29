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
#include <typeinfo>
#include <utility>

#include "log.h"

namespace corvid { inline namespace infra {

#pragma region Details

namespace details {

// Log an exception via `log::error`. If the rich path throws (e.g.
// `bad_alloc` inside `std::format`), fall back to a minimal raw line written
// directly to the singleton's stream, using only `operator<<` on builtin
// types so it doesn't itself reach for the allocator.
//
// `noexcept` keeps this safe to call from a noexcept frame; the fallback can
// only throw if the caller has enabled exceptions on the stream, which is not
// the default.
inline void
do_log_exception(const format_with_loc<const char*, const char*>& msg,
    const char* type_name, const char* what) noexcept {
  try {
    log::error(msg, type_name, what);
  }
  catch (...) {
    log::terminate();
  }
}

} // namespace details

#pragma endregion

#pragma region try_or_log

// Run `fn` inside a try block as a noexcept firewall: returns `fn()` on
// no-throw, or `on_throw` if it threw. Canonical idiom for the body of a
// noexcept callback that wraps a C-ABI boundary (ngtcp2, nghttp3, etc.) and
// needs to recover from `std::bad_alloc` (or similar) by reporting failure to
// its caller instead of terminating the process.
//
// On throw, logs at `error` via the `log` singleton using `msg` as the format
// string with two arguments: the exception's type name (from
// `typeid(e).name()`, ABI-mangled on most platforms) and its `what()` text.
// Non-`std::exception` throws log placeholder strings, since extracting the
// type without a typed reference would require platform sniffing (e.g.
// `abi::__cxa_demangle`).
//
// `on_throw` defaults to `false` so the common bool-returning case works with
// no extra argument. For other return types, pass a matching value: e.g.,
// `try_or_log([&]{ return when(); }, time_point_t{})` for a timer callback
// that wants to unschedule on throw. The lambda's return type and
// `on_throw`'s type must agree (or be unifiable); the outer return type is
// deduced from both.
//
// `msg` defaults to "exception {}: {}", capturing the caller's
// `source_location` so the emitted log line points to the `try_or_log` site.
// Override the format string for context-specific text; the two `{}` slots
// are filled with the type name and `what()` text, respectively.
//
// Logging itself is routed through `details::do_log_exception`, which keeps
// the outer `noexcept` contract by swallowing any throw from the formatter
// and falling back to a minimal raw line written directly to the stream.
//
// TODO: Use https://github.com/jeremy-rifkin/cpptrace for richer traces.
template<std::invocable F, typename T = bool>
[[nodiscard]] auto try_or_log(F&& fn, T on_throw = false,
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept {
  try {
    return std::forward<F>(fn)();
  }
  catch (const std::exception& e) {
    details::do_log_exception(msg, typeid(e).name(), e.what());
  }
  catch (...) {
    details::do_log_exception(msg, "<unknown>", "unknown exception");
  }
  return on_throw;
}

// Like `try_or_log`, but terminates the process on throw instead of returning
// a value. This is ideal for destructors.
//
// The lambda must return `true` on success.
template<std::invocable F>
void try_or_terminate(F&& fn,
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept {
  if (!try_or_log(std::forward<F>(fn), false, msg)) log::terminate();
}

#pragma endregion

}} // namespace corvid::infra
