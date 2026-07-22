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

#include "corvid/infra.h"
#include "catch2_main.h"

#include "corvid/concurrency/jthread_stoppable_sleep.h"

#include <sstream>
#include <stdexcept>
#include <thread>

using corvid::infra::log;
using corvid::infra::log_level;
using corvid::infra::log_policy;
using corvid::infra::logger;
using corvid::infra::rethrow_policy;
using corvid::infra::try_or_log;
using corvid::infra::try_or_terminate;

// Move-only formattable payload, pinning that log arguments are forwarded
// rather than copied.
struct move_only_arg {
  std::string text;
  explicit move_only_arg(std::string text) noexcept : text{std::move(text)} {}
  move_only_arg(move_only_arg&&) = default;
  move_only_arg(const move_only_arg&) = delete;
};

template<>
struct std::formatter<move_only_arg>: std::formatter<std::string_view> {
  auto format(const move_only_arg& m, std::format_context& ctx) const {
    return std::formatter<std::string_view>::format(m.text, ctx);
  }
};

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

TEST_CASE("log forwards arguments without copying", "[infra][log]") {
  std::stringstream sink;
  logger lg{sink};
  lg.info("payload {}", move_only_arg{"zap"});
  CHECK(sink.str().contains("payload zap"));
}

TEST_CASE("logger thread label reflects the thread name", "[infra][log]") {
  // Run on a fresh thread, since the label caches per thread and this one is
  // already labeled. `set_thread_name` prefixes a serial number, so match on
  // the suffix.
  std::stringstream sink;
  logger lg{sink};
  std::thread t{[&] {
    corvid::concurrency::jthread_stoppable_sleep::set_thread_name("wheel");
    lg.info("named");
  }};
  t.join();
  CHECK(sink.str().contains("-wheel("));

  // A long name truncates to 15 characters on both platforms.
  std::stringstream sink2;
  logger lg2{sink2};
  std::thread t2{[&] {
    corvid::concurrency::jthread_stoppable_sleep::set_thread_name(
        "abcdefghijklmnopqrstuvwxyz");
    lg2.info("named long");
  }};
  t2.join();
  CHECK(sink2.str().contains("-abcdefghij"));
  CHECK_FALSE(sink2.str().contains("abcdefghijklmnopqrstuvwxyz"));
}

TEST_CASE("loggers sharing a stream do not interleave lines", "[infra][log]") {
  // Two independent loggers, one shared stream: `osyncstream` keys atomicity
  // on the stream's buffer, so every line must arrive whole.
  std::stringstream sink;
  logger lg_a{sink};
  logger lg_b{sink};
  const std::string aa(120, 'a');
  const std::string bb(120, 'b');
  constexpr int n_lines = 200;
  std::thread ta{[&] {
    for (int ndx = 0; ndx < n_lines; ++ndx) lg_a.info("{}", aa);
  }};
  std::thread tb{[&] {
    for (int ndx = 0; ndx < n_lines; ++ndx) lg_b.info("{}", bb);
  }};
  ta.join();
  tb.join();

  int cnt = 0;
  std::string line;
  while (std::getline(sink, line)) {
    ++cnt;
    CHECK((line.ends_with(aa) || line.ends_with(bb)));
  }
  CHECK(cnt == 2 * n_lines);
}

TEST_CASE("try_or_log swallows and substitutes failure_value",
    "[infra][exception]") {
  std::stringstream sink;
  log::singleton().set_stream(sink);

  CHECK(
      try_or_log([]() -> bool { throw std::runtime_error("boom"); }) == false);
  CHECK(
      try_or_log([]() -> int { throw std::runtime_error("boom"); }, 42) == 42);
  CHECK(try_or_log([] { return 7; }, 0) == 7);

  // A void lambda maps success and throw onto `success_value` and
  // `failure_value`, which default to `true` and `false`.
  CHECK(try_or_log([] {}) == true);
  CHECK(
      try_or_log([]() -> void { throw std::runtime_error("boom"); }) == false);
  CHECK(try_or_log([] {}, 5, -1) == 5);
  CHECK(try_or_log([]() -> void { throw std::runtime_error("boom"); }, 5,
            -1) == -1);

  // A thrown C string is caught and its text logged.
  CHECK(try_or_log([]() -> bool { throw "c-string boom"; }) == false);

  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().contains("boom"));
  CHECK(sink.str().contains("c-string boom"));
}

TEST_CASE("try_or_log logs a returned failure value only when asked",
    "[infra][exception]") {
  // Under the default `on_throw` policy, a callable returning its
  // `failure_value` is passed through silently.
  std::stringstream sink;
  log::singleton().set_stream(sink);
  CHECK(try_or_log([] { return 0; }) == 0);
  CHECK(sink.str().empty());

  // Under `on_failure_value` (or `on_either_error`), the same return logs.
  CHECK(try_or_log<log_policy::on_failure_value>([] { return 0; }) == 0);
  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().contains("returned failure value"));
}

TEST_CASE("try_or_log under log_policy::never swallows silently",
    "[infra][exception]") {
  std::stringstream sink;
  log::singleton().set_stream(sink);
  CHECK(try_or_log<log_policy::never>([]() -> int {
    throw std::runtime_error("quiet");
  }) == 0);
  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().empty());
}

TEST_CASE("try_or_log throw-only policy needs no equality operator",
    "[infra][exception]") {
  // The `failure_value` comparison only happens when the policy logs it, so a
  // result type without `==` still works under the default policy.
  struct no_eq {
    int val;
  };
  CHECK(try_or_log([] { return no_eq{7}; }, no_eq{0}).val == 7);
  CHECK(try_or_log([]() -> no_eq { throw std::runtime_error("boom"); },
            no_eq{-1})
            .val == -1);
}

TEST_CASE("try_or_terminate returns the non-failure result",
    "[infra][exception]") {
  // Only the success path is testable in-process; the terminate paths would
  // end the test run.
  std::stringstream sink;
  log::singleton().set_stream(sink);
  CHECK(try_or_terminate([] { return 7; }) == 7);
  CHECK(try_or_terminate([] { return std::string{"ok"}; }) == "ok");
  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().empty());
}

// AddressSanitizer on Windows (clang-cl plus the VCRUNTIME exception runtime)
// access-violates when an in-flight exception is rethrown: the initial throw
// and catch work, but the rethrow reads a null per-thread exception record
// inside VCRUNTIME140 and faults. It is a toolchain limitation, not a Corvid
// bug. The same code passes on Linux and on Windows without the sanitizer, and
// only a live rethrow is affected; a plain throw or a swallowed mid-unwind
// throw (the cases above and below) is fine. This is the only test that
// performs a live rethrow, so skip just it under that one configuration.
#if defined(_WIN32) && defined(__has_feature)
#if __has_feature(address_sanitizer)
#define CORVID_WIN_ASAN_NO_RETHROW 1
#endif
#endif
#ifndef CORVID_WIN_ASAN_NO_RETHROW
TEST_CASE("try_or_log with attempt rethrows when not unwinding",
    "[infra][exception]") {
  std::stringstream sink;
  log::singleton().set_stream(sink);

  CHECK_THROWS_AS(
      (try_or_log<log_policy::on_throw, rethrow_policy::attempt>([]() -> bool {
        throw std::runtime_error("rethrown");
      })),
      std::runtime_error);

  log::singleton().set_stream(std::cerr);
  CHECK(sink.str().contains("rethrown"));
}
#endif

TEST_CASE("try_or_log with attempt swallows mid-unwind",
    "[infra][exception]") {
  std::stringstream sink;
  log::singleton().set_stream(sink);

  // A destructor invoked while `outer` unwinds calls `try_or_log<attempt>`,
  // whose `fn` throws. Because `std::uncaught_exceptions` is nonzero, it must
  // not rethrow (that would terminate); it logs and returns `failure_value`,
  // so the destructor completes and `outer` keeps propagating.
  bool swallowed = false;
  struct guard {
    bool& swallowed;
    ~guard() noexcept(false) {
      swallowed = try_or_log<log_policy::on_throw, rethrow_policy::attempt>(
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
