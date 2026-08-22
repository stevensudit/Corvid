// Unit test for corvid::sdl::frame_stats (corvid/sdl/frame_stats.h): the
// rolling one-second window yields nothing until it fills, then the frame
// rate and min/average/max frame time, and resets for the next window. Frame
// times are dyadic so the window sums exactly.

#include "corvid/sdl/frame_stats.h"
#include "catch2_main.h"

using corvid::sdl::frame_stats;

#pragma region frame_stats

TEST_CASE("frame_stats summarizes once a second and resets", "[sdl]") {
  frame_stats stats;

  // Short of a second: nothing yet.
  CHECK_FALSE(stats.record(0.25F));
  CHECK_FALSE(stats.record(0.5F));

  // The frame that completes the second yields the window.
  const auto first = stats.record(0.25F);
  REQUIRE(first);
  CHECK(first->fps == 3.0F);
  CHECK(first->min_ms == 250.0F);
  CHECK(first->max_ms == 500.0F);
  CHECK(first->avg_ms == 1000.0F / 3.0F);

  // The next window starts fresh: its first frame seeds min and max, and the
  // previous window's figures do not leak in.
  CHECK_FALSE(stats.record(0.75F));
  const auto second = stats.record(0.25F);
  REQUIRE(second);
  CHECK(second->fps == 2.0F);
  CHECK(second->min_ms == 250.0F);
  CHECK(second->max_ms == 750.0F);
  CHECK(second->avg_ms == 500.0F);
}

TEST_CASE("frame_stats reports a single long frame", "[sdl]") {
  frame_stats stats;
  const auto only = stats.record(2.0F);
  REQUIRE(only);
  CHECK(only->fps == 0.5F);
  CHECK(only->min_ms == 2000.0F);
  CHECK(only->max_ms == 2000.0F);
  CHECK(only->avg_ms == 2000.0F);
}

#pragma endregion
