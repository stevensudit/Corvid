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

#pragma region rethrow_policy

// Policy for whether a logging firewall should rethrow the caught exception
// when it can do so safely.
//
// `never` (the default) always swallows: the firewall reports failure through
// its return value and never lets an exception escape. This is the right
// choice at a C-ABI boundary, where an escaping C++ exception would be
// undefined behavior. It's also a safe, reasonable choice in general.
//
// `attempt` rethrows the caught exception, but only when
// `std::uncaught_exceptions` is zero at the catch point, meaning no other
// exception is currently propagating. If one is (we are running inside the
// unwinding of an outer exception, e.g. a destructor invoked mid-unwind),
// rethrowing would cause a double-exception `terminate`, so the firewall
// swallows instead. The name reflects that the rethrow is best-effort,
// conditioned on safety.
//
// `attempt` only makes sense for a caller that is itself declared
// `noexcept(false)`. Destructors are implicitly `noexcept(true)`, so a
// destructor using `try_or_terminate<rethrow_policy::attempt>` must be
// declared `noexcept(false)` explicitly, or the rethrow will `terminate` at
// the destructor's own boundary, and without logging.
enum class rethrow_policy : bool { never = false, attempt = true };

#pragma endregion
#pragma region Details

namespace details {

// Log an exception via `log::error`. If the rich path throws (e.g.
// `bad_alloc` inside `std::format`), fall back to a minimal raw line written
// directly to the singleton's stream, using only `operator<<` on builtin
// types so it doesn't itself reach for the allocator.
//
// With the default `rethrow_policy::never` this is `noexcept`; the fallback
// can only throw if the caller has enabled exceptions on the stream, which is
// not the default.
//
// With `rethrow_policy::attempt`, after logging it rethrows the exception
// currently being handled, but only when `std::uncaught_exceptions()` is zero
// (no outer exception is unwinding). The bare `throw;` rethrows the exception
// active in the calling firewall's catch block, which is why this lives here
// rather than being duplicated across that block's catch arms. This policy
// makes the function `noexcept(false)`.
template<rethrow_policy policy = rethrow_policy::never>
void do_log_exception(const format_with_loc<const char*, const char*>& msg,
    const char* type_name,
    const char* what) noexcept(policy == rethrow_policy::never) {
  try {
    log::error(msg, type_name, what);
  }
  catch (...) {
    log::terminate();
  }
  if constexpr (policy == rethrow_policy::attempt)
    if (std::uncaught_exceptions() == 0) throw;
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
// `policy` defaults to `rethrow_policy::never`, which is the firewall
// behavior described above. With `rethrow_policy::attempt` the function logs
// and then rethrows when it is safe to do so, returning `on_throw` only in
// the unwinding case; see `rethrow_policy`. Selecting `attempt` makes the
// function `noexcept(false)`, so it is only appropriate inside a caller that
// is itself `noexcept(false)`.
//
// TODO: Use https://github.com/jeremy-rifkin/cpptrace for richer traces.
template<rethrow_policy policy = rethrow_policy::never, std::invocable F,
    typename T = bool>
[[nodiscard]] auto try_or_log(F&& fn, T on_throw = false,
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept(policy == rethrow_policy::never) {
  try {
    return std::forward<F>(fn)();
  }
  catch (const std::exception& e) {
    details::do_log_exception<policy>(msg, typeid(e).name(), e.what());
  }
  catch (...) {
    details::do_log_exception<policy>(msg, "<unknown>", "unknown exception");
  }
  return on_throw;
}

// Like `try_or_log`, but terminates the process on throw instead of returning
// a value. This is ideal for destructors.
//
// The lambda must return `true` on success.
//
// With `rethrow_policy::attempt`, a throw is rethrown when no outer exception
// is unwinding and only falls back to `log::terminate` mid-unwind; see
// `rethrow_policy`. A destructor opting into this must be declared
// `noexcept(false)`, since the rethrow would otherwise `terminate` at the
// destructor's own implicit `noexcept` boundary.
//
// Note that throwing in a destructor is uncommon and works badly when the
// object is an element of a container.
template<rethrow_policy policy = rethrow_policy::never, std::invocable F>
void try_or_terminate(F&& fn,
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept(policy == rethrow_policy::never) {
  if (!try_or_log<policy>(std::forward<F>(fn), false, msg)) log::terminate();
}

#pragma endregion

}} // namespace corvid::infra
