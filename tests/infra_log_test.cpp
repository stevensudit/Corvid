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

#include "../corvid/infra.h"
#include "catch2_main.h"

#include <sstream>

using corvid::infra::log;
using corvid::infra::log_level;
using corvid::infra::logger;

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

  // Install a deterministic time: 2026-05-28T12:34:56.789Z.
  sysclk::set_now_fn();
  const sysclk::time_point_t when =
      sys_days{2026y / May / 28} + 12h + 34min + 56s + 789ms;
  sysclk::set_fake_now(when);

  std::stringstream sink;
  logger lg{sink};
  lg.info("hello");

  CHECK(sink.str().starts_with("2026-05-28T12:34:56.789Z [I "));

  // Restore the real clock so later tests see real time.
  sysclk::set_now_fn(&std::chrono::system_clock::now);
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

// NOLINTEND(readability-function-cognitive-complexity)
