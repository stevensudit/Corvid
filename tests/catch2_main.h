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
#include <print>
#include <string_view>
#include <vector>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#if defined(__has_feature) && __has_feature(memory_sanitizer)
#  include <cstdio>
#  include <cstring>
#  include <string>
#  include <unistd.h>
#  include <catch2/catch_assertion_result.hpp>
#  include <catch2/catch_test_case_info.hpp>
#  include <catch2/interfaces/catch_interfaces_reporter.hpp>
#  include <catch2/internal/catch_list.hpp>
#  include <catch2/internal/catch_test_run_info.hpp>
#  include <catch2/reporters/catch_reporter_registrars.hpp>
#  include <catch2/reporters/catch_reporter_streaming_base.hpp>

namespace corvid_msan {

// Default Catch2 reporters format their output through libc++ ostreams,
// whose integer formatting writes to a stack buffer with shadow gaps under
// MSAN-instrumented libc++. The fwrite interceptor then sees those bytes
// as uninitialized and reports before any test body runs. This reporter
// bypasses libc++ streams entirely: it formats with snprintf (whose output
// MSAN unpoisons via its interceptor) and writes via the write(2) syscall.
class MsanFriendlyReporter final : public Catch::StreamingReporterBase {
public:
  using StreamingReporterBase::StreamingReporterBase;

  static std::string getDescription() {
    return "MSAN-friendly reporter that bypasses libc++ streams";
  }

  // ReporterBase listing methods write through m_stream; replace with no-ops.
  void listReporters(std::vector<Catch::ReporterDescription> const&) override {}
  void listListeners(std::vector<Catch::ListenerDescription> const&) override {}
  void listTests(std::vector<Catch::TestCaseHandle> const&) override {}
  void listTags(std::vector<Catch::TagInfo> const&) override {}

  void testRunStarting(Catch::TestRunInfo const& info) override {
    StreamingReporterBase::testRunStarting(info);
    writef("[run] %.*s\n",
           static_cast<int>(info.name.size()), info.name.data());
  }

  void testRunEnded(Catch::TestRunStats const& stats) override {
    writef("[done] cases: passed=%llu failed=%llu skipped=%llu | "
           "assertions: passed=%llu failed=%llu\n",
           static_cast<unsigned long long>(stats.totals.testCases.passed),
           static_cast<unsigned long long>(stats.totals.testCases.failed),
           static_cast<unsigned long long>(stats.totals.testCases.skipped),
           static_cast<unsigned long long>(stats.totals.assertions.passed),
           static_cast<unsigned long long>(stats.totals.assertions.failed));
  }

  void assertionEnded(Catch::AssertionStats const& stats) override {
    auto const& r = stats.assertionResult;
    if (r.succeeded()) return;
    auto src = r.getSourceInfo();
    writef("[FAIL] %s:%zu\n", src.file, src.line);
    if (r.hasExpression()) {
      auto expr = r.getExpressionInMacro();
      auto expanded = r.getExpandedExpression();
      writef("  expr: %s\n", expr.c_str());
      if (expanded != expr) writef("  =>    %s\n", expanded.c_str());
    }
    if (r.hasMessage()) {
      auto m = r.getMessage();
      writef("  msg:  %.*s\n", static_cast<int>(m.size()), m.data());
    }
  }

  void skipTest(Catch::TestCaseInfo const& info) override {
    writef("[skip] %s\n", info.name.c_str());
  }

  void fatalErrorEncountered(Catch::StringRef error) override {
    writef("[fatal] %.*s\n",
           static_cast<int>(error.size()), error.data());
  }

private:
  template <class... Args>
  void writef(char const* fmt, Args... args) {
    char buf[2048];
    int n = std::snprintf(buf, sizeof(buf), fmt, args...);
    if (n <= 0) return;
    size_t len = (n < static_cast<int>(sizeof(buf)))
                     ? static_cast<size_t>(n)
                     : sizeof(buf) - 1;
    ssize_t w = ::write(STDOUT_FILENO, buf, len);
    (void)w;
  }
};

}  // namespace corvid_msan

CATCH_REGISTER_REPORTER("msan-friendly", corvid_msan::MsanFriendlyReporter)
#endif

// Mirrors the `MAKE_TEST_LIST` role from `minitest.h`: include in exactly one
// TU per Catch2 test executable to pull in the test macros (`TEST_CASE`,
// `SECTION`, `CHECK`, ...) and provide `main`.
//
// Default behavior:
//   - Highlights slow tests by injecting `--min-duration 0.1` when the user
//     hasn't specified duration flags themselves. Tests running under 100ms
//     stay silent; anything above appears in the output for attention.
//   - Prints total elapsed wall-clock time at the end, matching minitest's
//     `[TIME] Total: ...` line.
//
// Define `CATCH2_SHOW_TIMERS == 1` (mirroring `MINITEST_SHOW_TIMERS`) to get
// per-test timing for every test case, not just the slow ones.
int main(int argc, char* argv[]) {
  std::vector<char*> args(argv, argv + argc);
  bool have_durations{false};
  bool have_min_duration{false};
#if defined(__has_feature) && __has_feature(memory_sanitizer)
  bool have_rng_seed{false};
  bool have_reporter{false};
#endif
  for (const std::string_view a : args) {
    if (a == "-d" || a == "--durations") have_durations = true;
    if (a == "--min-duration") have_min_duration = true;
#if defined(__has_feature) && __has_feature(memory_sanitizer)
    if (a == "--rng-seed") have_rng_seed = true;
    if (a == "-r" || a == "--reporter") have_reporter = true;
#endif
  }
  if (!have_durations && !have_min_duration) {
#if defined(CATCH2_SHOW_TIMERS) && CATCH2_SHOW_TIMERS == 1
    args.push_back(const_cast<char*>("--durations"));
    args.push_back(const_cast<char*>("yes"));
#elif defined(__has_feature) && __has_feature(memory_sanitizer)
    // Under MSAN, skip the injection. Catch2's `--min-duration` parser uses
    // `std::istream >> double`, and libc++'s locale-based float parsing trips
    // a known MSAN false positive in `__constexpr_memchr`. Without this skip,
    // every test exits with an MSAN report before its body runs.
#else
    args.push_back(const_cast<char*>("--min-duration"));
    args.push_back(const_cast<char*>("0.1"));
#endif
  }
#if defined(__has_feature) && __has_feature(memory_sanitizer)
  if (!have_rng_seed) {
    // Catch2's default seed path calls `std::random_device`, whose output
    // bytes are not reliably unpoisoned under MSAN. When the seed is later
    // printed by the reporter, the fwrite interceptor flags the formatted
    // bytes as uninitialized. Pin a constant seed to avoid the path.
    args.push_back(const_cast<char*>("--rng-seed"));
    args.push_back(const_cast<char*>("0x12345678"));
  }
  if (!have_reporter) {
    // Use the snprintf+write(2) reporter defined above. The default Catch2
    // ConsoleReporter formats integers through libc++ ostreams whose stack
    // buffers have shadow gaps under MSAN-instrumented libc++.
    args.push_back(const_cast<char*>("--reporter"));
    args.push_back(const_cast<char*>("msan-friendly"));
  }
#endif
  const auto start = std::chrono::steady_clock::now();
  const int rc =
      Catch::Session().run(static_cast<int>(args.size()), args.data());
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
#if defined(__has_feature) && __has_feature(memory_sanitizer)
  // `std::println` formats via libc++'s `__output_nonunicode`, whose
  // ofstream-equivalent write path trips the MSAN false positives the
  // `msan-friendly` reporter exists to avoid. Mirror it here with
  // snprintf+write(2) under MSAN so the end-of-main timing line is clean.
  {
    char buf[64];
    int n = std::snprintf(buf, sizeof(buf), "[TIME] Total: %.3f ms\n", ms);
    if (n > 0) {
      ssize_t w = ::write(STDOUT_FILENO, buf,
                          n < static_cast<int>(sizeof(buf))
                              ? static_cast<size_t>(n)
                              : sizeof(buf) - 1);
      (void)w;
    }
  }
#else
  std::println("[TIME] Total: {:.3f} ms", ms);
#endif
  return rc;
}
