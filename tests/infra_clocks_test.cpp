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

#include "../corvid/infra/clocks.h"

#include "catch2_main.h"

#include <chrono>
#include <cstdint>

using namespace std::chrono_literals;
using namespace corvid;

namespace {
// File-scope counter for the captureless lambda passed to `set_now_fn`.
int custom_now_calls = 0;
} // namespace

TEST_CASE("set_now_fn installs a custom function", "[infra][clocks]") {
  custom_now_calls = 0;
  const auto sentinel =
      steady_now_clock::time_point_t{std::chrono::nanoseconds{42'000}};
  steady_now_clock::set_now_fn(
      +[]() noexcept -> steady_now_clock::time_point_t {
        ++custom_now_calls;
        return steady_now_clock::time_point_t{
            std::chrono::nanoseconds{42'000}};
      });

  CHECK(steady_now_clock::now() == sentinel);
  CHECK(steady_now_clock::now() == sentinel);
  CHECK(custom_now_calls == 2);
}

TEST_CASE("set_now_fn() with no argument installs the fake-clock callback",
    "[infra][clocks]") {
  steady_now_clock::set_now_fn();

  steady_now_clock::set_fake_now(steady_now_clock::time_point_t{1234ms});
  CHECK(steady_now_clock::now() == steady_now_clock::time_point_t{1234ms});

  steady_now_clock::set_fake_now(steady_now_clock::time_point_t{5678ms});
  CHECK(steady_now_clock::now() == steady_now_clock::time_point_t{5678ms});
}

TEST_CASE("steady_clock and system_clock keep independent fake state",
    "[infra][clocks]") {
  steady_now_clock::set_now_fn();
  system_now_clock::set_now_fn();

  steady_now_clock::set_fake_now(steady_now_clock::time_point_t{42ms});
  system_now_clock::set_fake_now(system_now_clock::time_point_t{99ms});

  CHECK(steady_now_clock::now() == steady_now_clock::time_point_t{42ms});
  CHECK(system_now_clock::now() == system_now_clock::time_point_t{99ms});

  // Swap the order to confirm neither write clobbered the other slot.
  system_now_clock::set_fake_now(system_now_clock::time_point_t{7ms});
  CHECK(steady_now_clock::now() == steady_now_clock::time_point_t{42ms});
  CHECK(system_now_clock::now() == system_now_clock::time_point_t{7ms});
}

TEST_CASE(
    "high_resolution_clock has its own state even when aliased to another "
    "clock",
    "[infra][clocks]") {
  // On most platforms `std::chrono::high_resolution_clock` is a typedef for
  // `steady_clock` or `system_clock`. The `size_t` template parameter on
  // `global_clock` gives this instantiation its own statics regardless.
  steady_now_clock::set_now_fn();
  high_resolution_now_clock::set_now_fn();

  steady_now_clock::set_fake_now(steady_now_clock::time_point_t{1ms});
  high_resolution_now_clock::set_fake_now(
      high_resolution_now_clock::time_point_t{2ms});

  CHECK(steady_now_clock::now() == steady_now_clock::time_point_t{1ms});
  CHECK(high_resolution_now_clock::now() ==
        high_resolution_now_clock::time_point_t{2ms});
}

TEST_CASE("as_nanoseconds and from_nanoseconds round-trip",
    "[infra][clocks]") {
  const auto tp = steady_now_clock::time_point_t{1234ms};
  CHECK(steady_now_clock::as_nanoseconds(tp) == 1'234'000'000U);
  CHECK(steady_now_clock::from_nanoseconds(uint64_t{1'234'000'000}) == tp);

  CHECK(steady_now_clock::as_nanoseconds(steady_now_clock::time_point_t{}) ==
        0U);
  CHECK(steady_now_clock::from_nanoseconds(uint64_t{0}) ==
        steady_now_clock::time_point_t{});
}

TEST_CASE("time_point_t::max maps to UINT64_MAX sentinel", "[infra][clocks]") {
  CHECK(steady_now_clock::as_nanoseconds(
            steady_now_clock::time_point_t::max()) == UINT64_MAX);
  CHECK(steady_now_clock::from_nanoseconds(UINT64_MAX) ==
        steady_now_clock::time_point_t::max());
}

TEST_CASE("from_nanoseconds(int64_t) treats negatives as max",
    "[infra][clocks]") {
  CHECK(steady_now_clock::from_nanoseconds(int64_t{-1}) ==
        steady_now_clock::time_point_t::max());
  CHECK(steady_now_clock::from_nanoseconds(int64_t{-9999}) ==
        steady_now_clock::time_point_t::max());
}

TEST_CASE("utc_clock supports fake injection", "[infra][clocks]") {
  utc_now_clock::set_now_fn();
  utc_now_clock::set_fake_now(utc_now_clock::time_point_t{42ms});
  CHECK(utc_now_clock::now() == utc_now_clock::time_point_t{42ms});
}

TEST_CASE("tai_clock supports fake injection", "[infra][clocks]") {
  tai_now_clock::set_now_fn();
  tai_now_clock::set_fake_now(tai_now_clock::time_point_t{42ms});
  CHECK(tai_now_clock::now() == tai_now_clock::time_point_t{42ms});
}

TEST_CASE("gps_clock supports fake injection", "[infra][clocks]") {
  gps_now_clock::set_now_fn();
  gps_now_clock::set_fake_now(gps_now_clock::time_point_t{42ms});
  CHECK(gps_now_clock::now() == gps_now_clock::time_point_t{42ms});
}

TEST_CASE("utc/tai/gps clocks keep independent fake state",
    "[infra][clocks]") {
  utc_now_clock::set_now_fn();
  tai_now_clock::set_now_fn();
  gps_now_clock::set_now_fn();

  utc_now_clock::set_fake_now(utc_now_clock::time_point_t{1ms});
  tai_now_clock::set_fake_now(tai_now_clock::time_point_t{2ms});
  gps_now_clock::set_fake_now(gps_now_clock::time_point_t{3ms});

  CHECK(utc_now_clock::now() == utc_now_clock::time_point_t{1ms});
  CHECK(tai_now_clock::now() == tai_now_clock::time_point_t{2ms});
  CHECK(gps_now_clock::now() == gps_now_clock::time_point_t{3ms});
}

TEST_CASE("libc++ experimental tzdb / utc_now_clock are available",
    "[infra][clocks]") {
  // Sanity check that `-fexperimental-library` + libc++experimental are
  // wired in: locate a well-known zone, build a `zoned_time`, and read
  // `utc_now_clock::now()`. None of these compile without the experimental
  // headers being unlocked.
  const auto& tzdb = std::chrono::get_tzdb();
  CHECK_FALSE(tzdb.version.empty());

  const auto* la = std::chrono::locate_zone("America/Los_Angeles");
  REQUIRE(la != nullptr);

  const std::chrono::zoned_time zt{la, std::chrono::system_clock::now()};
  CHECK(zt.get_time_zone() == la);

  const auto utc = std::chrono::utc_clock::now();
  CHECK(utc.time_since_epoch().count() > 0);
}
