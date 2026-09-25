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
#include <concepts>
#include <functional>
#include <ratio>
#include <type_traits>
#include <utility>

#include "clocks.h"
#include "relaxed_atomic.h"

namespace corvid { inline namespace infra {

// Timing how long code takes.
//
// `stopwatch` starts when constructed and reports the time since then, so
// instrumentation can take split times in place and pause across work that
// should not count:
//
//   stopwatch watch;
//   parse(input);
//   const auto parse_time = watch.elapsed();
//   watch.pause();
//   log_progress();
//   watch.resume();
//
// `scope_timer` writes the elapsed time of its scope into a duration on
// destruction, or earlier on `stop`. It is always true, so it can open the
// scope it times:
//
//   fp_milliseconds ms;
//   if (const auto timer = scope_timer(ms)) {
//     run_kernel();
//   }
//
// `stopwatch::timed_call` wraps a callable so that every invocation writes
// its elapsed time into a bound duration:
//
//   auto timed_decode = stopwatch::timed_call(ms, decode);
//   const auto token = timed_decode(context);
//
// All of it reads the clock through `steady_now_clock`, so tests can drive
// it with a fake clock.

#pragma region Aliases

// Milliseconds with a floating-point count, for reporting timings in a way
// that's compatible with CUDA.
using fp_milliseconds = std::chrono::duration<double, std::milli>;

#pragma endregion
#pragma region stopwatch

// Elapsed-time meter over the steady clock.
//
// Construction starts it. Instances copy freely, so several can share a start
// and diverge in how they are paused. Pausing or resuming when already in that
// state does nothing.
class [[nodiscard]] stopwatch {
public:
#pragma region Types

  using clock_t = steady_now_clock;
  using time_point_t = clock_t::time_point_t;
  using duration_t = clock_t::duration_t;

#pragma endregion
#pragma region Construction

  stopwatch() noexcept : start_{clock_t::now()} {}

#pragma endregion
#pragma region Reading

  // Time since the start, excluding time spent paused. While paused, this is
  // the value at the moment of pausing.
  [[nodiscard]] duration_t elapsed() const noexcept {
    return (is_paused() ? paused_at_ : clock_t::now()) - start_;
  }

  [[nodiscard]] bool is_paused() const noexcept {
    return (paused_at_ != not_paused);
  }

#pragma endregion
#pragma region Control

  // Start over from now, unpaused.
  //
  // Returns the elapsed time up to the restart, so that repeated calls yield
  // lap times.
  duration_t restart() noexcept {
    const auto lap = elapsed();
    start_ = clock_t::now();
    paused_at_ = not_paused;
    return lap;
  }

  // Stop counting time until `resume`.
  void pause() noexcept {
    if (!is_paused()) paused_at_ = clock_t::now();
  }

  // Continue counting time.
  void resume() noexcept {
    if (!is_paused()) return;
    // Sliding the start forward by the length of the pause keeps `elapsed` a
    // single subtraction, with no paused total to carry.
    start_ += clock_t::now() - paused_at_;
    paused_at_ = not_paused;
  }

#pragma endregion
#pragma region Wrapping

  // Wrap `fn` so that each invocation is timed, writing the elapsed time into
  // `out`.
  //
  // The wrapper holds `fn` by value (moved from an rvalue and copied from an
  // lvalue) and a reference to `out`. It forwards its arguments and result,
  // and writes `out` even when `fn` throws. With a `relaxed_atomic` as `out`,
  // the wrapper can run on another thread.
  template<typename Duration, typename F>
  requires std::constructible_from<std::decay_t<F>, F>
  [[nodiscard]] static auto timed_call(Duration& out, F&& fn);

#pragma endregion
#pragma region Data members
private:
  // The `paused_at_` value while running. A pause in the far future is no
  // pause at all, whereas the epoch is where a fresh fake clock starts.
  static constexpr time_point_t not_paused = time_point_t::max();

  time_point_t start_;
  time_point_t paused_at_ = not_paused;

#pragma endregion
};

#pragma endregion
#pragma region timer_output

namespace details {

// The duration behind a timer's output, which is either a duration itself or
// a `relaxed_atomic` holding one.
template<typename T>
struct timer_output {
  using duration_t = T;
};

template<typename T>
struct timer_output<relaxed_atomic<T>> {
  using duration_t = T;
};

template<typename T>
using timer_duration_t = timer_output<T>::duration_t;

} // namespace details

#pragma endregion
#pragma region scope_timer

// RAII timer that writes the elapsed time of its scope into a duration.
//
// Construction zeroes `out` and starts timing. Destruction writes the elapsed
// time into it, converted to the output's duration. `stop` does the same
// early, after which the destructor does nothing, for a scope that goes on to
// do cleanup that should not count.
//
// `Output` is a duration, or a `relaxed_atomic` holding one for a result read
// from another thread.
template<typename Output = stopwatch::duration_t>
class [[nodiscard]] scope_timer {
public:
#pragma region Types

  using duration_t = details::timer_duration_t<Output>;

#pragma endregion
#pragma region Construction

  explicit scope_timer(Output& out) noexcept : out_{&out} {
    *out_ = duration_t{};
  }

  scope_timer(const scope_timer&) = delete;
  scope_timer& operator=(const scope_timer&) = delete;

  ~scope_timer() { stop(); }

#pragma endregion
#pragma region Control

  // Write the elapsed time into `out` now and disarm the destructor. A second
  // call does nothing.
  void stop() noexcept {
    if (!out_) return;
    *out_ = std::chrono::duration_cast<duration_t>(watch_.elapsed());
    out_ = nullptr;
  }

  // Always true, so that the timer can open the scope it times.
  [[nodiscard]] explicit operator bool() const noexcept { return true; }

#pragma endregion
#pragma region Accessors

  // The underlying stopwatch, for pausing across work that should not count.
  [[nodiscard]] stopwatch& watch() const noexcept { return watch_; }

#pragma endregion
#pragma region Data members
private:
  mutable stopwatch watch_;
  Output* out_;

#pragma endregion
};

#pragma endregion
#pragma region timed_call

template<typename Duration, typename F>
requires std::constructible_from<std::decay_t<F>, F>
auto stopwatch::timed_call(Duration& out, F&& fn) {
  // The constraint lets an invocability probe (such as `std::jthread` asking
  // whether the wrapper takes a `stop_token`) fail without instantiating the
  // body, which would be a hard error under the deduced return type.
  return [&out, fn = std::forward<F>(fn)]<typename... Args>(
             Args&&... args) mutable -> decltype(auto)
         requires std::invocable<std::decay_t<F>&, Args...>
  {
    scope_timer timer(out);
    return std::invoke(fn, std::forward<Args>(args)...);
  };
}

#pragma endregion

}} // namespace corvid::infra
