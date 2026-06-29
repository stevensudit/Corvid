// Unit test for the avatar's rigid-body physics
// (corvid/cuda/windows/game/avatar_body.cuh): keystone A of the physics
// correctness pass. The body is host math, so these run on the host directly,
// no kernels. Each case feeds a synthetic contact (a floor, a ramp, a wall)
// and checks a physical invariant: free fall, settling, a contact-normal
// jump's apex, the friction-angle slope threshold (replacing the hardcoded
// climb limit), and rolling without slipping.

#include <cmath>
#include <numbers>

#include "corvid/cuda/vec.cuh"
#include "corvid/cuda/windows/game/avatar_body.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// Round, non-unit constants so a stray factor of mass or radius would show.
body_params test_params() {
  return body_params{.radius = 1.0F,
      .mass = 2.0F,
      .gravity = 10.0F,
      .drive_force = 40.0F,
      .friction = 1.0F, // friction angle atan(1) = 45 degrees
      .drag = 0.0F,     // off, so the cases exercise the pure dynamics
      .jump_speed = 10.0F,
      .rolling_resistance = 0.0F};
}

constexpr float deg = std::numbers::pi_v<float> / 180.0F;
constexpr vec3 up{0.0F, 1.0F, 0.0F};

// An outward normal for a surface tilted `degrees` off level, leaning in +x.
vec3 slope_normal(float degrees) {
  return vec3{std::sin(degrees * deg), std::cos(degrees * deg), 0.0F};
}

} // namespace

TEST_CASE("avatar_body falls under gravity", "[cuda][physics][avatar_body]") {
  avatar_body b;
  b.params = test_params();
  b.center = pos3{vec3{0.0F, 100.0F, 0.0F}};
  const body_contact air{};
  const float dt = 0.001F;
  for (int step = 0; step < 1000; ++step) b.advance(air, vec3{}, false, dt);

  // After one second, v = -g t and the drop is about half g t squared.
  CHECK(std::fabs(b.velocity.y - (-10.0F)) < 1e-3F);
  CHECK(b.center.v.y < 95.1F);
  CHECK(b.center.v.y > 94.8F);
  CHECK_FALSE(b.grounded);
}

TEST_CASE("avatar_body settles to rest on a flat floor",
    "[cuda][physics][avatar_body]") {
  avatar_body b;
  b.params = test_params();
  b.center = pos3{vec3{0.0F, 1.0F, 0.0F}};
  const body_contact floor{.touching = true,
      .normal = up,
      .penetration = 0.0F};
  b.advance(floor, vec3{}, false, 0.01F);

  CHECK(b.grounded);
  CHECK(std::fabs(b.velocity.y) < 1e-4F); // gravity canceled by the contact
  CHECK(std::fabs(b.center.v.y - 1.0F) < 1e-4F);
}

TEST_CASE("avatar_body is pushed out of an overlap",
    "[cuda][physics][avatar_body]") {
  avatar_body b;
  b.params = test_params();
  b.center = pos3{vec3{0.0F, 0.7F, 0.0F}};
  const body_contact floor{.touching = true,
      .normal = up,
      .penetration = 0.3F};
  b.advance(floor, vec3{}, false, 0.01F);

  CHECK(b.center.v.y > 0.99F); // lifted ~0.3 back to resting
  CHECK(b.grounded);
}

TEST_CASE("avatar_body jump apex matches v^2 / 2g",
    "[cuda][physics][avatar_body]") {
  avatar_body b;
  b.params = test_params();
  b.center = pos3{vec3{0.0F, 1.0F, 0.0F}};
  const body_contact floor{.touching = true,
      .normal = up,
      .penetration = 0.0F};
  const float dt = 0.001F;
  const float start = b.center.v.y;

  b.advance(floor, vec3{}, true, dt); // launch
  CHECK_FALSE(b.grounded);            // rising, so no longer grounded

  const body_contact air{};
  float apex = b.center.v.y;
  for (int step = 0; step < 100000 && b.velocity.y > 0.0F; ++step) {
    b.advance(air, vec3{}, false, dt);
    apex = fmaxf(apex, b.center.v.y);
  }

  const float expected = (10.0F * 10.0F) / (2.0F * 10.0F); // jump^2 / 2g = 5
  CHECK(std::fabs((apex - start) - expected) < 0.1F);
}

TEST_CASE("avatar_body holds on a slope below the friction angle",
    "[cuda][physics][avatar_body]") {
  avatar_body b;
  b.params = test_params();
  b.center = pos3{vec3{0.0F, 1.0F, 0.0F}};
  const body_contact slope{.touching = true,
      .normal = slope_normal(30.0F),
      .penetration = 0.0F};
  for (int step = 0; step < 200; ++step)
    b.advance(slope, vec3{}, false, 0.01F);

  // 30 degrees is below the 45 degree friction angle, so static friction holds
  // the ball: it does not slide.
  CHECK(length(b.velocity) < 0.05F);
}

TEST_CASE("avatar_body slides down a slope above the friction angle",
    "[cuda][physics][avatar_body]") {
  avatar_body b;
  b.params = test_params();
  b.center = pos3{vec3{0.0F, 1.0F, 0.0F}};
  const vec3 normal = slope_normal(60.0F);
  const body_contact slope{.touching = true,
      .normal = normal,
      .penetration = 0.0F};
  for (int step = 0; step < 200; ++step)
    b.advance(slope, vec3{}, false, 0.01F);

  // 60 degrees exceeds the friction angle, so the ball accelerates downhill.
  CHECK(length(b.velocity) > 1.0F);
  const vec3 gravity = up * -10.0F;
  const vec3 downhill = normalize(gravity - (normal * dot(gravity, normal)));
  CHECK(dot(normalize(b.velocity), downhill) > 0.9F);
}

TEST_CASE("avatar_body rolls without slipping on a floor",
    "[cuda][physics][avatar_body]") {
  avatar_body b;
  b.params = test_params();
  b.center = pos3{vec3{0.0F, 1.0F, 0.0F}};
  b.velocity = vec3{3.0F, 0.0F, 0.0F};
  const body_contact floor{.touching = true,
      .normal = up,
      .penetration = 0.0F};
  b.advance(floor, vec3{}, false, 0.01F);

  // omega * r equals the surface speed, with the spin axis perpendicular to
  // both the travel and the contact normal (the rolling-without-slipping
  // constraint).
  const float r = b.params.radius;
  const vec3 vt = b.velocity - (up * dot(b.velocity, up));
  CHECK(std::fabs((length(b.angular_velocity) * r) - length(vt)) < 1e-3F);
  CHECK(std::fabs(dot(b.angular_velocity, b.velocity)) < 1e-3F);
  CHECK(std::fabs(dot(b.angular_velocity, up)) < 1e-3F);
}

TEST_CASE("avatar_body reaches a drag-limited terminal speed",
    "[cuda][physics][avatar_body]") {
  avatar_body b;
  b.params = test_params();
  b.params.drag = 2.0F;      // quadratic: terminal = sqrt(drive accel / drag)
  b.params.friction = 10.0F; // high, so traction never clamps this drive
  b.center = pos3{vec3{0.0F, 1.0F, 0.0F}};
  const body_contact floor{.touching = true,
      .normal = up,
      .penetration = 0.0F};
  const vec3 drive{1.0F, 0.0F, 0.0F}; // full forward command
  for (int step = 0; step < 1000; ++step)
    b.advance(floor, drive, false, 0.01F);

  // drive accel = drive_force / mass = 40 / 2 = 20; quadratic drag balances at
  // sqrt(20 / 2) = sqrt(10) ~= 3.16.
  const float terminal =
      std::sqrt((b.params.drive_force / b.params.mass) / b.params.drag);
  const vec3 vt = b.velocity - (up * dot(b.velocity, up));
  CHECK(std::fabs(length(vt) - terminal) < 0.25F);
}

// NOLINTEND(readability-function-cognitive-complexity)
