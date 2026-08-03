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

#include <chrono>
#include <stop_token>
#include <thread>

#include "corvid/concurrency/jthread_stoppable_sleep.h"

#include "catch2_main.h"

using namespace std::chrono_literals;
using namespace corvid::concurrency;

#pragma region PreRequestedStop

TEST_CASE("PreRequestedStop", "[JthreadStoppableSleep]") {
  // A stop requested before the call returns true without sleeping.
  jthread_stoppable_sleep sleep;
  std::stop_source src;
  src.request_stop();
  CHECK(sleep.until(src.get_token(), std::chrono::steady_clock::now() + 1h));
}

#pragma endregion
#pragma region PastDeadline

TEST_CASE("PastDeadline", "[JthreadStoppableSleep]") {
  // An already-elapsed deadline returns false immediately when no stop was
  // requested.
  jthread_stoppable_sleep sleep;
  std::stop_source src;
  CHECK_FALSE(
      sleep.until(src.get_token(), std::chrono::steady_clock::now() - 1s));
}

#pragma endregion
#pragma region StopWakesSleeper

TEST_CASE("StopWakesSleeper", "[JthreadStoppableSleep]") {
  // A stop from another thread wakes the sleeper long before the deadline.
  // The healthy path finishes in microseconds; the far-off deadline and the
  // elapsed check exist so a lost wakeup fails loudly (and slowly) instead
  // of passing at the deadline, where the predicate would also be true.
  jthread_stoppable_sleep sleep;
  bool stopped_early = false;
  const auto begin = std::chrono::steady_clock::now();
  if (true) {
    std::jthread worker{[&](const std::stop_token& st) {
      stopped_early = sleep.until(st, begin + 10s);
    }};
    // The jthread destructor requests stop and joins.
  }
  CHECK(stopped_early);
  CHECK((std::chrono::steady_clock::now() - begin) < 5s);
}

#pragma endregion
#pragma region SetThreadName

TEST_CASE("SetThreadName", "[JthreadStoppableSleep]") {
  // Smoke: naming the current thread neither fails nor crashes, with a name
  // long enough to exercise the POSIX 15-character truncation.
  jthread_stoppable_sleep::set_thread_name("sleep-test-name-quite-long");
  SUCCEED();
}

#pragma endregion
