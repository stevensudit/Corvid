// Host-side tests for the SDF primitives and combinators: points inside, on,
// and outside each shape, checked against the exact distances.

#include <cuda_runtime.h>

#include "corvid/cuda/sdf.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

namespace {

constexpr auto eps = 1.0e-5F;

bool near(float a, float b) { return fabsf(a - b) < eps; }

} // namespace

#pragma region Primitives

TEST_CASE("sdf primitives", "[cuda][sdf]") {
  SECTION("sphere") {
    CHECK(near(sd_sphere({0.0F, 0.0F, 0.0F}, 1.0F), -1.0F));
    CHECK(near(sd_sphere({1.0F, 0.0F, 0.0F}, 1.0F), 0.0F));
    CHECK(near(sd_sphere({3.0F, 0.0F, 0.0F}, 1.0F), 2.0F));
  }

  SECTION("box") {
    const vec3 half{1.0F, 2.0F, 3.0F};
    CHECK(near(sd_box({0.0F, 0.0F, 0.0F}, half), -1.0F)); // nearest face x
    CHECK(near(sd_box({1.0F, 0.0F, 0.0F}, half), 0.0F));
    CHECK(near(sd_box({4.0F, 0.0F, 0.0F}, half), 3.0F));
    // Off a corner, the distance is Euclidean to the corner.
    CHECK(near(sd_box({4.0F, 6.0F, 3.0F}, half), 5.0F));
  }

  SECTION("capsule") {
    const vec3 a{0.0F, 0.0F, 0.0F};
    const vec3 b{2.0F, 0.0F, 0.0F};
    CHECK(near(sd_capsule({1.0F, 0.0F, 0.0F}, a, b, 0.5F), -0.5F)); // axis
    CHECK(near(sd_capsule({1.0F, 0.5F, 0.0F}, a, b, 0.5F), 0.0F));  // side
    // Past an end, the projection clamps to the endpoint (the cap).
    CHECK(near(sd_capsule({3.0F, 0.0F, 0.0F}, a, b, 0.5F), 0.5F));
    CHECK(near(sd_capsule({-2.0F, 0.0F, 0.0F}, a, b, 0.5F), 1.5F));
  }

  SECTION("plane") {
    const vec3 up{0.0F, 1.0F, 0.0F};
    // `h` is the signed origin-to-surface distance into the solid, so the
    // surface sits at y == -h.
    CHECK(near(sd_plane({0.0F, -1.0F, 0.0F}, up, 1.0F), 0.0F));
    CHECK(near(sd_plane({0.0F, 0.0F, 0.0F}, up, 1.0F), 1.0F));
    CHECK(near(sd_plane({0.0F, -3.0F, 0.0F}, up, 1.0F), -2.0F));
  }
}

#pragma endregion
#pragma region Combinators

TEST_CASE("sdf combinators", "[cuda][sdf]") {
  CHECK(near(op_union(1.0F, 2.0F), 1.0F));
  CHECK(near(op_intersect(1.0F, 2.0F), 2.0F));
  CHECK(near(op_subtract(1.0F, -2.0F), 2.0F));
  // Far apart, the smooth forms reduce to the sharp ones.
  CHECK(near(op_smooth_union(1.0F, 5.0F, 0.5F), 1.0F));
  CHECK(near(op_smooth_intersect(1.0F, 5.0F, 0.5F), 5.0F));
  // Equal inputs blend by the full k / 4.
  CHECK(near(op_smooth_union(1.0F, 1.0F, 0.5F), 1.0F - 0.125F));
  CHECK(near(op_smooth_intersect(1.0F, 1.0F, 0.5F), 1.0F + 0.125F));
}

#pragma endregion
