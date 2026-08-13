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

#include "corvid/filesys/os_event.h"

#include "catch2_main.h"

#include <chrono>
#include <utility>

using namespace corvid;
using namespace std::chrono_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("os_event portable surface") {
  // Default-constructed holds no event.
  if (true) {
    os_event e;
    CHECK_FALSE(e.is_open());
    CHECK(e.handle() == os_event::invalid_handle);
  }

  // Create, notify, drain.
  if (true) {
    auto e = os_event::create();
    REQUIRE(e.is_open());
    CHECK(e.notify());
    CHECK(e.drain());

    // Draining an already-clear event succeeds, and notifying works again
    // afterward.
    CHECK(e.drain());
    CHECK(e.notify());
  }

  // Move transfers ownership.
  if (true) {
    auto a = os_event::create();
    const auto h = a.handle();
    os_event b{std::move(a)};
    CHECK(b.is_open());
    CHECK(b.handle() == h);
  }

  // Timed wait: a clear event times out, a notified one is signaled, and
  // waiting does not consume the notification; `drain` does.
  if (true) {
    auto e = os_event::create();
    REQUIRE(e.is_open());
    CHECK(e.wait_for(0ms) == wait_result::timed_out);
    CHECK(e.notify());
    CHECK(e.wait_for(0ms) == wait_result::signaled);
    CHECK(e.wait_for(0ms) == wait_result::signaled);
    CHECK(e.drain());
    CHECK(e.wait_for(0ms) == wait_result::timed_out);
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
