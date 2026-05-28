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
#include <chrono>
#include <format>
#include <iostream>
#include <mutex>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include "clocks.h"

namespace corvid { inline namespace infra {

#pragma region log_level

// Severity levels in ascending order.
// NOLINTNEXTLINE(performance-enum-size)
enum class log_level : int { trace, debug, info, warn, error };

#pragma endregion
#pragma region format_with_loc

// Pairs a `std::format_string<Args...>` with the `std::source_location` of the
// call site. The implicit constructor defaults the location to
// `std::source_location::current()`, evaluated at the caller, which sidesteps
// the "no default argument after parameter pack" restriction: the variadic
// pack belongs to the surrounding logging function, while the location
// default belongs to this constructor.
//
// Used as the first parameter of the level-named logging methods so call
// sites stay clean: `log::info("connected to {}", host)`.
template<typename... Args>
struct format_with_loc {
  std::format_string<Args...> fmt;
  std::source_location loc;

  template<typename T>
  consteval format_with_loc(const T& f,
      std::source_location l = std::source_location::current())
      : fmt{f}, loc{l} {}
};

#pragma endregion
#pragma region logger

// Owns a level threshold and a reference to an output stream, and serializes
// writes to that stream. Construct your own instance for a per-subsystem log
// (optionally pointed at a different stream), or use the singleton via the
// `log` static API below.
//
// The stream is held by reference, so the caller owns its lifetime; the
// default is `std::cerr`, whose lifetime spans the program.
//
// Output format: `YYYY-MM-DDTHH:MM:SS.sssZ [L file:line] message\n` where `L`
// is the level's first letter (`T`/`D`/`I`/`W`/`E`). The timestamp is ISO
// 8601 in UTC at millisecond precision so alphabetical and chronological
// sort match. Timestamps come from `system_now_clock`, so tests can
// install a deterministic fake.
class logger {
public:
#pragma region Construction
  logger() = default;
  explicit logger(log_level threshold) noexcept : threshold_{threshold} {}
  explicit logger(std::ostream& out,
      log_level threshold = log_level::info) noexcept
      : out_{&out}, threshold_{threshold} {}

  logger(const logger&) = delete;
  logger& operator=(const logger&) = delete;

#pragma endregion
#pragma region Accessors

  [[nodiscard]] log_level threshold() const noexcept { return threshold_; }
  void set_threshold(log_level lvl) noexcept { threshold_ = lvl; }

  [[nodiscard]] std::ostream& stream() const noexcept { return *out_; }
  void set_stream(std::ostream& out) noexcept { out_ = &out; }

  [[nodiscard]] bool enabled(log_level lvl) const noexcept {
    return lvl >= threshold_;
  }

#pragma endregion
#pragma region Emit

  template<typename... Args>
  void trace(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    emit(log_level::trace, msg, args...);
  }

  template<typename... Args>
  void debug(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    emit(log_level::debug, msg, args...);
  }

  template<typename... Args>
  void info(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    emit(log_level::info, msg, args...);
  }

  template<typename... Args>
  void warn(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    emit(log_level::warn, msg, args...);
  }

  template<typename... Args>
  void error(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    emit(log_level::error, msg, args...);
  }

#pragma endregion
#pragma region Helpers
private:
  template<typename... Args>
  void emit(log_level lvl, const format_with_loc<Args...>& msg, Args... args) {
    if (!enabled(lvl)) return;
    // `args` are lvalues in this body; `std::format`'s `Args&&` would deduce
    // them as `T&` and reject `msg.fmt` (typed without refs). Cast to xvalue
    // so deduction collapses to the value type.
    auto body = std::format(msg.fmt, std::move(args)...);
    write_line(lvl, msg.loc, body);
  }

  void write_line(log_level lvl, const std::source_location& loc,
      std::string_view body) {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        system_now_clock::now());
    // Single-letter level prefix, indexed by enum value. Update the string
    // when adding levels.
    constexpr std::string_view k_level_initials = "TDIWE";
    static_assert(
        k_level_initials.size() == static_cast<size_t>(log_level::error) + 1);
    std::scoped_lock lock{mutex_};
    (*out_) << std::format("{:%FT%T}", now) << "Z ["
            << k_level_initials[static_cast<size_t>(lvl)] << ' '
            << loc.file_name() << ':' << loc.line() << "] " << body << '\n';
  }

#pragma endregion
#pragma region Data members

  std::ostream* out_{&std::cerr};
  std::mutex mutex_;
  log_level threshold_{log_level::info};

#pragma endregion
};

#pragma endregion
#pragma region log

// Static-only facade whose methods forward to a process-wide singleton
// `logger`. Pair with a stack-local `logger` for subsystem-scoped logs.
class log final {
public:
  log() = delete;

#pragma region Singleton

  [[nodiscard]] static logger& singleton() noexcept {
    static logger instance;
    return instance;
  }

#pragma endregion
#pragma region Emit

  template<typename... Args>
  static void trace(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    singleton().trace(msg, args...);
  }

  template<typename... Args>
  static void debug(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    singleton().debug(msg, args...);
  }

  template<typename... Args>
  static void info(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    singleton().info(msg, args...);
  }

  template<typename... Args>
  static void warn(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    singleton().warn(msg, args...);
  }

  template<typename... Args>
  static void error(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    singleton().error(msg, args...);
  }

#pragma endregion
};

#pragma endregion

}} // namespace corvid::infra
