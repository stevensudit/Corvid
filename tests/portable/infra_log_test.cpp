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

#define EXCEPTION_FIREWALLS_NO_ASSERT 1

#include "corvid/infra.h"
#include "catch2_main.h"

#include <sstream>
#include <stdexcept>

using corvid::infra::log;
using corvid::infra::log_level;
using corvid::infra::logger;
using corvid::infra::rethrow_policy;
using corvid::infra::try_or_log;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("logger threshold gates output", "[infra][log]") {
  logger lg;
  CHECK(lg.threshold() == log_level::info);
  CHECK_FALSE(lg.enabled(log_level::trace));
  CHECK_FALSE(lg.enabled(log_level::debug));
  CHECK(lg.enabled(log_level::info));
  CHECK(lg.enabled(log_level::warn));
  CHECK(lg.enabled(log_level::error));

  lg.set_threshold(log_level::warn);
  CHECK_FALSE(lg.enabled(log_level::info));
  CHECK(lg.enabled(log_level::warn));
}

TEST_CASE("logger emits to cerr with [L file:line] prefix", "[infra][log]") {
  // Redirect cerr to a stringstream so we can inspect the output.
  std::stringstream captured;
  auto* old_buf = std::cerr.rdbuf(captured.rdbuf());

  logger lg;
  lg.info("hello {}", 42);

  std::cerr.rdbuf(old_buf);
  auto out = captured.str();
  CHECK(out.contains("[I "));
  CHECK(out.contains("infra_log_test.cpp:"));
  CHECK(out.contains("hello 42"));
  CHECK(out.back() == '\n');
}

TEST_CASE("logger suppresses below-threshold writes", "[infra][log]") {
  std::stringstream captured;
  auto* old_buf = std::cerr.rdbuf(captured.rdbuf());

  logger lg{log_level::warn};
  lg.info("should not appear");
  lg.debug("nor this");
  lg.warn("but this should");
  lg.error("and this");

  std::cerr.rdbuf(old_buf);
  auto out = captured.str();
  CHECK_FALSE(out.contains("should not appear"));
  CHECK_FALSE(out.contains("nor this"));
  CHECK(out.contains("but this should"));
  CHECK(out.contains("and this"));
}

TEST_CASE("logger writes to an injected ostream", "[infra][log]") {
  std::stringstream sink;
  logger lg{sink};
  lg.info("routed to {}", "sink");
  auto out = sink.str();
  CHECK(out.contains("[I "));
  CHECK(out.contains("routed to sink"));
}

TEST_CASE("logger ostream ctor accepts a threshold", "[infra][log]") {
  std::stringstream sink;
  logger lg{sink, log_level::error};
  lg.warn("suppressed");
  lg.error("kept");
  auto out = sink.str();
  CHECK_FALSE(out.contains("suppressed"));
  CHECK(out.contains("kept"));
}

TEST_CASE("logger rebinds its stream via set_stream", "[infra][log]") {
  std::stringstream first;
  std::stringstream second;
  logger lg{first};
  lg.info("to first");
  lg.set_stream(second);
  lg.info("to second");
  CHECK(first.str().contains("to first"));
  CHECK_FALSE(first.str().contains("to second"));
  CHECK(second.str().contains("to second"));
  CHECK_FALSE(second.str().contains("to first"));
}

TEST_CASE("logger prefixes output with a UTC ISO-8601 timestamp",
    "[infra][log]") {
  using sysclk = corvid::system_now_clock;
  using namespace std::chrono;

  // Install a deterministic time: 2026-05-28T12:34:56.789Z. The scope guard
  // restores the real clock on exit so later tests see real time.
  auto clock_guard = sysclk::fake_now_scope();
  const sysclk::time_point_t when =
      sys_days{2026y / May / 28} + 12h + 34min + 56s + 789ms;
  sysclk::set_fake_now(when);

  std::stringstream sink;
  logger lg{sink};
  lg.info("hello");

  // The thread name/ID segment sits between the timestamp and the level.
  auto out = sink.str();
  CHECK(out.starts_with("2026-05-28T12:34:56.789Z ["));
  CHECK(out.contains("] [I "));
}

TEST_CASE("log singleton can be redirected via set_stream", "[infra][log]") {
  std::stringstream sink;
  log::singleton().set_stream(sink);
  log::info("singleton routed to {}", "sink");
  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().contains("singleton routed to sink"));
}

TEST_CASE("log static facade forwards to its singleton", "[infra][log]") {
  std::stringstream captured;
  auto* old_buf = std::cerr.rdbuf(captured.rdbuf());

  // Drop the singleton threshold so the trace call below is observable, then
  // restore it so we don't leak state into later tests.
  auto saved = log::singleton().threshold();
  log::singleton().set_threshold(log_level::trace);
  log::trace("static trace x={}", 7);
  log::singleton().set_threshold(saved);

  std::cerr.rdbuf(old_buf);
  auto out = captured.str();
  CHECK(out.contains("[T "));
  CHECK(out.contains("static trace x=7"));
}

TEST_CASE("try_or_log swallows and returns on_throw by default",
    "[infra][exception]") {
  std::stringstream sink;
  log::singleton().set_stream(sink);

  CHECK(
      try_or_log([]() -> bool { throw std::runtime_error("boom"); }) == false);
  CHECK(
      try_or_log([]() -> int { throw std::runtime_error("boom"); }, 42) == 42);
  CHECK(try_or_log([] { return 7; }, 0) == 7);

  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().contains("boom"));
}

TEST_CASE("try_or_log with attempt rethrows when not unwinding",
    "[infra][exception]") {
  std::stringstream sink;
  log::singleton().set_stream(sink);

  CHECK_THROWS_AS(try_or_log<rethrow_policy::attempt>([]() -> bool {
    throw std::runtime_error("rethrown");
  }),
      std::runtime_error);

  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().contains("rethrown"));
}

TEST_CASE("try_or_log with attempt swallows mid-unwind",
    "[infra][exception]") {
  std::stringstream sink;
  log::singleton().set_stream(sink);

  // A destructor invoked while `outer` unwinds calls `try_or_log<attempt>`,
  // whose `fn` throws. Because `std::uncaught_exceptions` is nonzero, it
  // must not rethrow (that would terminate); it logs and returns `on_throw`,
  // so the destructor completes and `outer` keeps propagating.
  bool swallowed = false;
  struct guard {
    bool& swallowed;
    ~guard() noexcept(false) {
      swallowed = try_or_log<rethrow_policy::attempt>(
          []() -> bool { throw std::runtime_error("inner"); }, true);
    }
  };

  CHECK_THROWS_AS(
      [&] {
        guard g{swallowed};
        throw std::logic_error("outer");
      }(),
      std::logic_error);
  CHECK(swallowed);

  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().contains("inner"));
}

// NOLINTEND(readability-function-cognitive-complexity)
