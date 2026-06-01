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

// `try_or_log` and `try_or_terminate` are exception firewalls: they run a
// callable inside a try block so a throw cannot cross a boundary that must not
// let one through.
//
// Reach for `try_or_log` when the boundary is a function that must not throw
// but has a meaningful way to report failure through its return value. The
// canonical case is a callback invoked across a C ABI, where an exception
// escaping into a C frame is undefined behavior; the firewall logs and
// substitutes `on_throw` so the caller sees an ordinary failure value instead.
//
// Reach for `try_or_terminate` when the boundary has no way to report failure,
// the destructor being the motivating case. Throwing out of a destructor
// (implicitly `noexcept`) already calls `std::terminate`, so the firewall does
// not make an otherwise-fine destructor fatal; what it adds is a log entry
// naming the exception that forced the exit, rather than the bare
// `std::terminate` the runtime would call on its own. It also makes the
// no-throw guarantee true in fact, which resolves the legitimate
// `bugprone-exception-escape` warning clang-tidy would otherwise emit for a
// destructor whose body can throw.
//
// The `rethrow_policy::attempt` variants are for the rare boundary that is
// deliberately declared `noexcept(false)` and whose failure is a normal,
// recoverable error the immediate caller should handle: a commit- or
// flush-on-close object, say. There the firewall logs and rethrows when it is
// safe (no outer exception is unwinding), degrading to a logged terminate
// only mid-unwind.

#pragma region rethrow_policy

// Policy for whether a logging firewall should rethrow the caught exception
// when it can do so safely.
enum class rethrow_policy : bool { never = false, attempt = true };

#pragma endregion
#pragma region Details

namespace details {

// Log an exception via `log::error`.
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
#ifndef EXCEPTION_FIREWALLS_NO_ASSERT
  assert("Logged exception" && false);
#endif
  if constexpr (policy == rethrow_policy::attempt)
    if (std::uncaught_exceptions() == 0) throw;
}

} // namespace details

#pragma endregion

#pragma region try_or_log

// Run `fn` inside a try block as a noexcept firewall: returns `fn()` on
// no-throw, or `on_throw` (defaulting to `false`) if it threw.
// TODO: Use https://github.com/jeremy-rifkin/cpptrace for richer traces.
// TODO: Consider catch `const char*`.
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
template<rethrow_policy policy = rethrow_policy::never, std::invocable F>
void try_or_terminate(F&& fn,
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept(policy == rethrow_policy::never) {
  if (!try_or_log<policy>(std::forward<F>(fn), false, msg)) log::terminate();
}

#pragma endregion

}} // namespace corvid::infra
