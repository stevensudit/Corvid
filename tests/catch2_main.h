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
#include <csignal>
#include <cstdio>
#include <string_view>
#include <vector>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

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
  // Ignore SIGPIPE so socket-using tests don't die when a peer closes
  // mid-write. The library's `submit_write_buffer` path can't pass
  // MSG_NOSIGNAL; the close-then-write race surfaces intermittently
  // (especially under MSAN). Server programs do the same at startup. Windows
  // has no SIGPIPE (and its socket tests live in the linux bucket anyway).
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
#endif

  std::vector<char*> args(argv, argv + argc);
  bool have_durations{false};
  bool have_min_duration{false};
  for (const std::string_view a : args) {
    if (a == "-d" || a == "--durations") have_durations = true;
    if (a == "--min-duration") have_min_duration = true;
  }
  if (!have_durations && !have_min_duration) {
#if defined(CATCH2_SHOW_TIMERS) && CATCH2_SHOW_TIMERS == 1
    static char durations_flag[] = "--durations";
    static char durations_value[] = "yes";
    args.push_back(durations_flag);
    args.push_back(durations_value);
#else
    static char min_duration_flag[] = "--min-duration";
    static char min_duration_value[] = "0.1";
    args.push_back(min_duration_flag);
    args.push_back(min_duration_value);
#endif
  }
  const auto start = std::chrono::steady_clock::now();
  const int rc =
      Catch::Session().run(static_cast<int>(args.size()), args.data());
  const auto elapsed = std::chrono::steady_clock::now() - start;
  const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
  // Use `printf` rather than `std::println`: some libstdc++ versions we
  // build against don't ship `<print>` yet.
  // NOLINTNEXTLINE(modernize-use-std-print)
  std::printf("[TIME] Total: %.3f ms\n", ms);
  return rc;
}
