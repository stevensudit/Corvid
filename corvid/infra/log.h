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
#ifndef _WIN32
#include <pthread.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <exception>
#include <format>
#include <iostream>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

#include "clocks.h"
#include "relaxed_atomic.h"

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
// Output format: `YYYY-MM-DDTHH:MM:SS.sssZ [name(tid)] [L file:line]
// message\n` where `name(tid)` is the calling thread's name and ID, and `L`
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

  [[nodiscard]] std::ostream& stream() const noexcept {
    std::scoped_lock lock{mutex_};
    return *out_;
  }
  void set_stream(std::ostream& out) noexcept {
    std::scoped_lock lock{mutex_};
    out_ = &out;
  }

  [[nodiscard]] bool enabled(log_level lvl) const noexcept {
    return lvl >= threshold_;
  }

#pragma endregion
#pragma region Emit

  // As a convenience, all of these methods return `false`.

  template<typename... Args>
  bool trace(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return emit(log_level::trace, msg, args...);
  }

  template<typename... Args>
  bool debug(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return emit(log_level::debug, msg, args...);
  }

  template<typename... Args>
  bool info(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return emit(log_level::info, msg, args...);
  }

  template<typename... Args>
  bool warn(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return emit(log_level::warn, msg, args...);
  }

  template<typename... Args>
  bool error(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return emit(log_level::error, msg, args...);
  }

  template<typename... Args>
  [[noreturn]] void
  fatal(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    emit(log_level::error, msg, args...);
    terminate();
  }

  // NOLINTNEXTLINE(bugprone-exception-escape): a throw only reaches terminate
  [[noreturn]] void terminate() noexcept {
    std::scoped_lock lock{mutex_};
    (*out_) << "Terminating due to previous fatal log message.\n"
            << std::flush;
    std::terminate();
  }

#pragma endregion
#pragma region Helpers
private:
  template<typename... Args>
  bool emit(log_level lvl, const format_with_loc<Args...>& msg, Args... args) {
    if (!enabled(lvl)) return false;
    // `args` are lvalues in this body; `std::format`'s `Args&&` would deduce
    // them as `T&` and reject `msg.fmt` (typed without refs). Cast to xvalue
    // so deduction collapses to the value type.
    auto body = std::format(msg.fmt, std::move(args)...);
    return write_line(lvl, msg.loc, body) && false;
  }

  // Returns the calling thread's `name(tid)` label, computed once per thread
  // and cached in thread-local storage. The name falls back to "thread" when
  // unnamed; it is at most 16 bytes including the null terminator.
  static const std::string& thread_label() {
    thread_local const std::string label = [] {
#ifdef _WIN32
      return std::format("thread({})", std::this_thread::get_id());
#else
      std::array<char, 16> name{};
      const char* thread_name =
          (pthread_getname_np(pthread_self(), name.data(), name.size()) == 0 &&
              name[0] != '\0')
              ? name.data()
              : "thread";
      return std::string{thread_name} + '(' +
             std::to_string(syscall(SYS_gettid)) + ')';
#endif
    }();
    return label;
  }

  // Sample output:
  // 2026-05-29T22:26:27Z [wheel(42)] [I file.cpp:42] message
  bool write_line(log_level lvl, const std::source_location& loc,
      std::string_view body) {
    static constexpr std::string_view k_level_initials = "TDIWE";
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
        system_now_clock::now());
    // Build everything but the body into a stack buffer before locking, so the
    // lock only spans the stream writes (no heap-allocating timestamp format
    // inside it). 256 bytes covers the fixed-width timestamp, the <=15-char
    // thread name and tid, the level, and a full file path with line.
    // `format_to_n` stops at the buffer end, so a long path truncates the
    // prefix rather than overflowing.
    std::array<char, 256> prefix;
    auto res = std::format_to_n(prefix.data(), prefix.size(),
        "{:%FT%T}Z [{}] [{} {}:{}] ", now, thread_label(),
        k_level_initials[static_cast<size_t>(lvl)], loc.file_name(),
        loc.line());
    const auto prefix_len = res.out - prefix.data();
    std::scoped_lock lock{mutex_};
    out_->write(prefix.data(), static_cast<std::streamsize>(prefix_len));
    out_->write(body.data(), static_cast<std::streamsize>(body.size()));
    out_->put('\n');
    return true;
  }

#pragma endregion
#pragma region Data members

  std::ostream* out_{&std::cerr};
  mutable std::mutex mutex_;
  relaxed_atomic<log_level> threshold_{log_level::info};

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

  // As a convenience, all of these methods return `false`.

  template<typename... Args>
  static bool trace(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return singleton().trace(msg, args...);
  }

  template<typename... Args>
  static bool debug(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return singleton().debug(msg, args...);
  }

  template<typename... Args>
  static bool info(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return singleton().info(msg, args...);
  }

  template<typename... Args>
  static bool warn(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return singleton().warn(msg, args...);
  }

  template<typename... Args>
  static bool error(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    return singleton().error(msg, args...);
  }

  template<typename... Args>
  [[noreturn]] static void
  fatal(const format_with_loc<std::type_identity_t<Args>...>& msg,
      Args... args) {
    singleton().fatal(msg, args...);
  }

  [[noreturn]] static void terminate() { singleton().terminate(); }

#pragma endregion
};

#pragma endregion

}} // namespace corvid::infra
