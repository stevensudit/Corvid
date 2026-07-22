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
#include <cstdint>
#include <exception>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include "../meta/crossplatform.h"
#include "log.h"

namespace corvid { inline namespace infra {

// `try_or_log` and `try_or_terminate` are exception firewalls.
//
// They run a callable (typically a lambda) inside a try block so that a throw
// cannot cross a boundary that must not let one through. They also log the
// exception contents so that they're not lost.
//
// Reach for `try_or_log` when a throw at the boundary is survivable.
//
// The canonical case for `try_or_log` is a callback invoked across a C ABI,
// where an exception escaping into a C frame is undefined behavior. Instead,
// the firewall catches it there and hands the caller an ordinary value.
//
// A callable with a `void` result maps its outcome onto explicit
// `success_value` and `failure_value` results, based entirely on whether it
// threw. A value-returning callable passes its result through and substitutes
// `failure_value` only for a throw.
//
// Reach for `try_or_terminate` when the boundary has no way to report failure.
//
// The motivating case is the destructor, which is implicitly `noexcept` and
// therefore already calls `std::terminate` if a throw escapes it. The
// firewall ensures that the exception is logged.
//
// It also makes the no-throw guarantee true in fact, which resolves the
// legitimate `bugprone-exception-escape` warning clang-tidy would otherwise
// emit for a destructor whose body can throw.
//
// When a value-returning callable returns `failure_value`, this also leads to
// logging and termination, allowing you to escalate a failed cleanup to a
// fatality.
//
// There are two settings that are passed in as defaulted template parameters.
//
// The `log_policy` template parameter determines which failure modes get
// logged: a thrown exception, a returned `failure_value`, either, or neither.
//
// The `rethrow_policy` template parameter determines whether it will attempt
// to rethrow the exception after logging it.
//
// This `rethrow_policy::attempt` option is for the rare boundary that is
// deliberately declared `noexcept(false)` and whose failure is a normal,
// recoverable error the immediate caller should handle: a commit- or
// flush-on-close object, say. There the firewall logs and rethrows when it is
// safe to do so, meaning no outer exception is unwinding. Mid-unwind,
// `try_or_log` swallows the exception and returns `failure_value`, while
// `try_or_terminate` degrades to a logged terminate.

#pragma region log_policy

// Policy for which firewall failure modes get logged: a thrown exception, a
// returned `failure_value`, either, or neither.
//
// Note: This is a bitmask, but infra sits too low in the layering to register
// it as one, so the combined value is spelled as a named enumerator and
// membership is tested manually.
enum class log_policy : std::uint8_t {
  never = 0,
  on_throw = 1 << 0,
  on_failure_value = 1 << 1,
  on_either_error = on_throw | on_failure_value,
};

#pragma endregion
#pragma region rethrow_policy

// Policy for whether a firewall should rethrow the caught exception when it
// can do so safely.
enum class rethrow_policy : bool { never = false, attempt = true };

#pragma endregion
#pragma region Details

namespace details {

// Test whether `policy` includes `bit`.
consteval bool has_logs_bit(log_policy policy, log_policy bit) {
  return (std::to_underlying(policy) & std::to_underlying(bit)) != 0;
}

// Log a firewalled failure via `log::error`, terminating if logging itself
// throws.
//
// TODO: Use https://github.com/jeremy-rifkin/cpptrace for richer traces and to
// replace the mangled `typeid` exception names with demangled ones.
inline void do_log_error(const format_with_loc<const char*, const char*>& msg,
    const char* type_name, const char* what) noexcept {
  try {
    // Pass prvalue copies so `Args` deduce as the value types `msg` was
    // declared with.
    log::error(msg, auto{type_name}, auto{what});
  }
  catch (...) {
    log::terminate();
  }
}

// Handle an exception caught by a firewall: log it when `logging` includes
// `on_throw`, and rethrow it when `rethrow` is `attempt` and no outer
// exception is unwinding.
template<log_policy logging, rethrow_policy rethrow>
void do_caught(const format_with_loc<const char*, const char*>& msg,
    const char* type_name,
    const char* what) noexcept(rethrow == rethrow_policy::never) {
  if constexpr (has_logs_bit(logging, log_policy::on_throw))
    do_log_error(msg, type_name, what);
  if constexpr (rethrow == rethrow_policy::attempt)
    if (std::uncaught_exceptions() == 0) throw;
}

// Shared try/catch skeleton for the firewalls, containing the catch blocks.
template<log_policy logging, rethrow_policy rethrow, std::invocable Body,
    typename R>
auto do_firewall(Body&& body, R failure_value,
    const format_with_loc<const char*, const char*>&
        msg) noexcept(rethrow == rethrow_policy::never) {
  try {
    return std::forward<Body>(body)();
  }
  catch (const std::exception& e) {
    do_caught<logging, rethrow>(msg, typeid(e).name(), e.what());
  }
  catch (const char* s) {
    do_caught<logging, rethrow>(msg, "const char*", s);
  }
  catch (...) {
    do_caught<logging, rethrow>(msg, "<unknown>", "unknown exception");
  }
  return failure_value;
}

} // namespace details

#pragma endregion
#pragma region try_or_log

// Run a `void` callable inside a try block as a noexcept firewall.
//
// Returns `success_value` if it didn't throw, or `failure_value` if it did.
PRAGMA_DIAG(push)
PRAGMA_MSVC_IGNORED(4702)
template<log_policy logging = log_policy::on_throw,
    rethrow_policy rethrow = rethrow_policy::never, std::invocable F,
    typename R = bool>
requires std::is_void_v<std::invoke_result_t<F>>
[[nodiscard]] R
try_or_log(F&& fn, R success_value = true, R failure_value = false,
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept(rethrow == rethrow_policy::never) {
  return details::do_firewall<logging, rethrow>(
      [&]() -> R {
        std::forward<F>(fn)();
        return success_value;
      },
      failure_value, msg);
}

// Run a value-returning callable inside a try block as a noexcept firewall.
//
// Returns the result of `fn()` if it didn't throw, or `failure_value` if it
// did.
//
// When `logging` includes `on_failure_value`, a result equal to
// `failure_value` is also logged. Only that policy requires the result type
// to be equality comparable, and the constraint enforces exactly that.
template<log_policy logging = log_policy::on_throw,
    rethrow_policy rethrow = rethrow_policy::never, std::invocable F>
requires(
    !std::is_void_v<std::invoke_result_t<F>> &&
    (!details::has_logs_bit(logging, log_policy::on_failure_value) ||
        std::equality_comparable<std::decay_t<std::invoke_result_t<F>>>))
[[nodiscard]] auto
// NOLINTNEXTLINE(bugprone-exception-escape): never policy swallows all throws
try_or_log(F&& fn, std::decay_t<std::invoke_result_t<F>> failure_value = {},
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept(rethrow == rethrow_policy::never) {
  return details::do_firewall<logging, rethrow>(
      [&] {
        auto result = std::forward<F>(fn)();
        if constexpr (details::has_logs_bit(logging,
                          log_policy::on_failure_value))
          if (result == failure_value)
            details::do_log_error(msg, "<none>", "returned failure value");
        return result;
      },
      failure_value, msg);
}
PRAGMA_DIAG(pop)

#pragma endregion
#pragma region try_or_terminate

// Like `try_or_log`, but terminates the process if the `void` callable throws,
// instead of returning a value. This is ideal for destructors.
template<log_policy logging = log_policy::on_throw,
    rethrow_policy rethrow = rethrow_policy::never, std::invocable F>
requires std::is_void_v<std::invoke_result_t<F>>
void try_or_terminate(F&& fn,
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept(rethrow == rethrow_policy::never) {
  if (!try_or_log<logging, rethrow>(std::forward<F>(fn), true, false, msg))
    log::terminate();
}

// Like the value-returning `try_or_log`, but terminates the process instead of
// returning `failure_value`, whether the callable threw or returned it. On
// success, returns the result.
//
// `logging` defaults to `on_either_error` so that a log entry precedes the
// terminate no matter which failure mode triggered it. The terminate decision
// compares against `failure_value`, so the result type must be equality
// comparable.
template<log_policy logging = log_policy::on_either_error,
    rethrow_policy rethrow = rethrow_policy::never, std::invocable F>
requires(!std::is_void_v<std::invoke_result_t<F>> &&
         std::equality_comparable<std::decay_t<std::invoke_result_t<F>>>)
// NOLINTNEXTLINE(bugprone-exception-escape): never policy swallows all throws
[[nodiscard]] auto try_or_terminate(F&& fn,
    const std::decay_t<std::invoke_result_t<F>>& failure_value = {},
    format_with_loc<const char*, const char*> msg =
        "exception {}: {}") noexcept(rethrow == rethrow_policy::never) {
  auto result =
      try_or_log<logging, rethrow>(std::forward<F>(fn), failure_value, msg);
  if (result == failure_value) log::terminate();
  return result;
}

#pragma endregion

}} // namespace corvid::infra
