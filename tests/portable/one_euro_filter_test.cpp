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

#include "corvid/math/one_euro_filter.h"
#include "catch2_main.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {
// A representative frame interval: one 60 Hz step, in seconds.
constexpr float dt = 1.0F / 60.0F;
} // namespace

TEST_CASE("OneEuroFirstSamplePassesThrough", "[OneEuroFilter]") {
  // The first sample has no history to smooth against, so it seeds the state
  // and is returned unchanged.
  one_euro_filter filter{50.0F, 1.0F};
  float dx = 5.0F;
  float dy = -3.0F;
  filter.smooth(dt, dx, dy);
  CHECK(dx == 5.0F);
  CHECK(dy == -3.0F);
}

TEST_CASE("OneEuroConstantInputIsFixed", "[OneEuroFilter]") {
  // A steady input has nothing to converge toward: once seeded, the same value
  // passes through untouched (lerp toward an equal target is a no-op).
  one_euro_filter filter{50.0F, 1.0F};
  float dx = 2.0F;
  float dy = 2.0F;
  filter.smooth(dt, dx, dy); // seed
  dx = 2.0F;
  dy = 2.0F;
  filter.smooth(dt, dx, dy);
  CHECK(dx == 2.0F);
  CHECK(dy == 2.0F);
}

TEST_CASE("OneEuroNonPositiveDtIsNoOp", "[OneEuroFilter]") {
  // A zero or negative elapsed time cannot define a speed, so the sample is
  // left untouched and the state stays unseeded.
  one_euro_filter filter{50.0F, 1.0F};
  float dx = 9.0F;
  float dy = 9.0F;
  filter.smooth(0.0F, dx, dy);
  CHECK(dx == 9.0F);
  CHECK(dy == 9.0F);

  // Still unseeded, so the next real sample passes through as the first.
  dx = 4.0F;
  dy = 4.0F;
  filter.smooth(dt, dx, dy);
  CHECK(dx == 4.0F);
  CHECK(dy == 4.0F);
}

TEST_CASE("OneEuroResetForgetsState", "[OneEuroFilter]") {
  // After reset, the filter behaves as if freshly constructed: the next sample
  // seeds and passes through rather than smoothing from the old state.
  one_euro_filter filter{50.0F, 1.0F};
  float dx = 1.0F;
  float dy = 0.0F;
  filter.smooth(dt, dx, dy); // seed at 1
  dx = 0.0F;
  dy = 0.0F;
  filter.smooth(dt, dx, dy); // smooths toward 0, landing between
  CHECK(dx > 0.0F);
  CHECK(dx < 1.0F);

  filter.reset();
  dx = 7.0F;
  dy = 0.0F;
  filter.smooth(dt, dx, dy);
  CHECK(dx == 7.0F);
}

TEST_CASE("OneEuroStepResponseConverges", "[OneEuroFilter]") {
  // Seeded at zero, a constant unit input is approached from below without
  // reversing and never overshoots, converging toward the target (it plateaus
  // at the largest float below one, so the approach is non-decreasing rather
  // than strictly increasing).
  one_euro_filter filter{50.0F, 0.0F};
  float dx = 0.0F;
  float dy = 0.0F;
  filter.smooth(dt, dx, dy); // seed at 0

  float previous = 0.0F;
  for (int step = 0; step < 60; ++step) {
    dx = 1.0F;
    dy = 0.0F;
    filter.smooth(dt, dx, dy);
    CHECK(dx >= previous);
    CHECK(dx < 1.0F);
    previous = dx;
  }
  CHECK(previous > 0.99F);
}

TEST_CASE("OneEuroSpeedRelaxesSmoothing", "[OneEuroFilter]") {
  // The adaptive cutoff is the point of the filter: for the same input, a
  // positive beta raises the cutoff with speed and so smooths less (tracks the
  // input more closely) than the plain low-pass with beta zero.
  one_euro_filter plain{50.0F, 0.0F};
  one_euro_filter adaptive{50.0F, 2.0F};

  float px = 0.0F;
  float py = 0.0F;
  plain.smooth(dt, px, py); // seed at 0
  float ax = 0.0F;
  float ay = 0.0F;
  adaptive.smooth(dt, ax, ay); // seed at 0

  px = 1.0F;
  py = 0.0F;
  plain.smooth(dt, px, py);
  ax = 1.0F;
  ay = 0.0F;
  adaptive.smooth(dt, ax, ay);

  CHECK(ax > px);
}

// NOLINTEND(readability-function-cognitive-complexity)
