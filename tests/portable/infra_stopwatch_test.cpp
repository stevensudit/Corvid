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

#include "corvid/infra/stopwatch.h"

#include "catch2_main.h"

#include <chrono>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
using namespace corvid;

namespace {
// Set the fake steady clock to `since_epoch`.
void set_now(std::chrono::milliseconds since_epoch) {
  steady_now_clock::set_fake_now(stopwatch::time_point_t{since_epoch});
}

// A callable that can be moved but not copied.
struct move_only_fn {
  std::unique_ptr<int> value = std::make_unique<int>(41);
  int operator()() const { return *value + 1; }
};
} // namespace

// Whether `timed_call` accepts a callable passed as `F`.
template<typename F>
concept Wrappable = requires(stopwatch::duration_t& took, F&& fn) {
  stopwatch::timed_call(took, std::forward<F>(fn));
};

// The wrapper takes its callable by value, so a move-only one must be passed
// as an rvalue.
static_assert(Wrappable<move_only_fn>);
static_assert(!Wrappable<move_only_fn&>);

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region stopwatch

TEST_CASE("stopwatch elapsed follows the clock", "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  set_now(100ms);
  const stopwatch watch;
  CHECK(watch.elapsed() == 0ms);
  set_now(150ms);
  CHECK(watch.elapsed() == 50ms);
  CHECK_FALSE(watch.is_paused());
}

TEST_CASE("stopwatch pause freezes elapsed and resume excludes the pause",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  stopwatch watch;
  set_now(10ms);
  watch.pause();
  CHECK(watch.is_paused());
  set_now(30ms);
  CHECK(watch.elapsed() == 10ms);
  watch.resume();
  CHECK_FALSE(watch.is_paused());
  CHECK(watch.elapsed() == 10ms);
  set_now(45ms);
  CHECK(watch.elapsed() == 25ms);
}

TEST_CASE("stopwatch pause and resume ignore a repeat", "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  stopwatch watch;
  set_now(10ms);
  watch.pause();
  set_now(20ms);
  watch.pause();
  CHECK(watch.elapsed() == 10ms);
  watch.resume();
  set_now(25ms);
  watch.resume();
  CHECK(watch.elapsed() == 15ms);
}

TEST_CASE("stopwatch restart returns the lap and starts over unpaused",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  stopwatch watch;
  set_now(10ms);
  CHECK(watch.restart() == 10ms);
  set_now(15ms);
  CHECK(watch.elapsed() == 5ms);
  watch.pause();
  set_now(20ms);
  CHECK(watch.restart() == 5ms);
  CHECK_FALSE(watch.is_paused());
  set_now(22ms);
  CHECK(watch.elapsed() == 2ms);
}

TEST_CASE("stopwatch copies diverge independently", "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  const stopwatch running;
  auto paused = running;
  set_now(10ms);
  paused.pause();
  set_now(30ms);
  CHECK(running.elapsed() == 30ms);
  CHECK(paused.elapsed() == 10ms);
}

TEST_CASE("stopwatch reads the real clock without a fake",
    "[infra][stopwatch]") {
  const stopwatch watch;
  const auto first = watch.elapsed();
  CHECK(first >= 0ns);
  CHECK(watch.elapsed() >= first);
}

#pragma endregion
#pragma region scope_timer

TEST_CASE("scope_timer writes its duration on destruction",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  fp_milliseconds ms{-1.0};
  if (const auto timer = scope_timer(ms)) {
    CHECK(ms == 0ms);
    set_now(7ms);
  }
  CHECK(ms == 7ms);
}

TEST_CASE("scope_timer stop writes early and disarms the destructor",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  stopwatch::duration_t took{};
  {
    scope_timer timer(took);
    set_now(3ms);
    timer.stop();
    set_now(9ms);
    timer.stop();
    CHECK(took == 3ms);
  }
  CHECK(took == 3ms);
}

TEST_CASE("scope_timer pauses through its stopwatch even when const",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  stopwatch::duration_t took{};
  if (const auto timer = scope_timer(took)) {
    set_now(2ms);
    timer.watch().pause();
    set_now(10ms);
    timer.watch().resume();
    set_now(13ms);
  }
  CHECK(took == 5ms);
}

TEST_CASE("scope_timer writes through a relaxed_atomic",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  relaxed_atomic<stopwatch::duration_t> took{1ms};
  {
    const scope_timer timer(took);
    CHECK(*took == 0ms);
    set_now(6ms);
  }
  CHECK(*took == 6ms);
}

#pragma endregion
#pragma region timed_call

TEST_CASE("timed_call forwards arguments and result and times each call",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  stopwatch::duration_t took{};
  auto timed_add =
      stopwatch::timed_call(took, [](int lhs, std::chrono::milliseconds cost) {
        set_now(cost);
        return lhs + 1;
      });
  set_now(0ms);
  CHECK(timed_add(1, 5ms) == 2);
  CHECK(took == 5ms);
  CHECK(timed_add(2, 12ms) == 3);
  CHECK(took == 7ms);
}

TEST_CASE("timed_call writes the duration when the callable throws",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  fp_milliseconds ms{};
  auto timed_throw = stopwatch::timed_call(ms, [] {
    set_now(4ms);
    throw std::runtime_error{"boom"};
  });
  CHECK_THROWS_AS(timed_throw(), std::runtime_error);
  CHECK(ms == 4ms);
}

TEST_CASE("timed_call moves a move-only callable in", "[infra][stopwatch]") {
  stopwatch::duration_t took{};
  auto timed = stopwatch::timed_call(took, move_only_fn{});
  CHECK(timed() == 42);
}

TEST_CASE("timed_call into a relaxed_atomic runs on another thread",
    "[infra][stopwatch]") {
  const auto guard = steady_now_clock::fake_now_scope();
  relaxed_atomic<stopwatch::duration_t> took;
  auto timed_advance = stopwatch::timed_call(took, [] { set_now(8ms); });
  std::jthread worker{std::move(timed_advance)};
  worker.join();
  CHECK(*took == 8ms);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
