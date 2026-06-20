// Unit test for corvid::cuda ray-march shading (corvid/cuda/raycast.cuh):
// launches kernels that run each device shading term against a unit-sphere
// scene and checks the results round-trip to the host. Covers the surface
// normal, soft shadow, ambient occlusion, the shade_ray pipeline (a hit vs a
// sky miss), and the gamma byte conversion.

#include <cmath>

#include <cuda_runtime.h>

#include "corvid/cuda/raycast.cuh"
#include "corvid/cuda/vec.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// A unit sphere centered at the origin, the scene field every test marches.
__device__ float sphere_sdf(pos3 p) { return length(p.v) - 1.0F; }

// A scene policy for shade_ray: the unit sphere, a red surface, a blue sky.
struct test_scene {
  static __device__ float sdf(pos3 p) { return sphere_sdf(p); }
  static __device__ vec3 albedo(pos3) { return {1.0F, 0.0F, 0.0F}; }
  static __device__ vec3 sky(vec3) { return {0.0F, 0.0F, 1.0F}; }
};

__global__ void normal_kernel(vec3* out) {
  *out = surface_normal<sphere_sdf>(pos3{vec3{1.0F, 0.0F, 0.0F}});
}

__global__ void shadow_kernel(float* out) {
  const pos3 p{vec3{3.0F, 0.0F, 0.0F}};
  // Light toward the sphere is blocked; light away from it is unobstructed.
  out[0] = soft_shadow<sphere_sdf>(p, vec3{-1.0F, 0.0F, 0.0F}, 16.0F);
  out[1] = soft_shadow<sphere_sdf>(p, vec3{1.0F, 0.0F, 0.0F}, 16.0F);
}

__global__ void ao_kernel(float* out) {
  // A point on the convex sphere, sampled along its outward normal.
  *out = ambient_occlusion<sphere_sdf>(pos3{vec3{1.0F, 0.0F, 0.0F}},
      vec3{1.0F, 0.0F, 0.0F});
}

__global__ void shade_kernel(vec3* out) {
  const vec3 light = normalize(vec3{1.0F, 1.0F, 1.0F});
  const pos3 eye{vec3{0.0F, 0.0F, 5.0F}};
  // A ray into the sphere is shaded with its albedo; a ray past it is sky.
  out[0] = shade_ray<test_scene>(eye, vec3{0.0F, 0.0F, -1.0F}, 0.001F, light);
  out[1] = shade_ray<test_scene>(eye, vec3{0.0F, 1.0F, 0.0F}, 0.001F, light);
}

__global__ void to_byte_kernel(unsigned char* out) {
  out[0] = to_byte(0.0F);
  out[1] = to_byte(1.0F);
  out[2] = to_byte(-1.0F); // clamps to 0
  out[3] = to_byte(2.0F);  // clamps to 255
  out[4] = to_byte(0.5F);  // gamma-encoded midtone
}

} // namespace

TEST_CASE("surface_normal points outward from the sphere", "[cuda][raycast]") {
  vec3 normal{};
  if (cuda_ptr<vec3> d_out; true) {
    REQUIRE(d_out.ok());
    normal_kernel<<<1, 1>>>(d_out.device_ptr());
    REQUIRE(d_out.store(normal));
  }
  REQUIRE(cuda_last_status{}.ok());
  CHECK(std::fabs(normal.x - 1.0F) < 1e-3F);
  CHECK(std::fabs(normal.y) < 1e-3F);
  CHECK(std::fabs(normal.z) < 1e-3F);
}

TEST_CASE("soft_shadow blocks light through the sphere", "[cuda][raycast]") {
  float shadow[2] = {-1.0F, -1.0F};
  if (cuda_ptr<float> d_out{2}; true) {
    REQUIRE(d_out.ok());
    shadow_kernel<<<1, 1>>>(d_out.device_ptr());
    REQUIRE(d_out.store(shadow));
  }
  REQUIRE(cuda_last_status{}.ok());
  CHECK(shadow[0] == 0.0F); // occluded
  CHECK(shadow[1] == 1.0F); // unobstructed
}

TEST_CASE("ambient_occlusion is open on a convex surface", "[cuda][raycast]") {
  float ao = -1.0F;
  if (cuda_ptr<float> d_out; true) {
    REQUIRE(d_out.ok());
    ao_kernel<<<1, 1>>>(d_out.device_ptr());
    REQUIRE(d_out.store(ao));
  }
  REQUIRE(cuda_last_status{}.ok());
  CHECK(ao > 0.99F);
  CHECK(ao <= 1.0F);
}

TEST_CASE("shade_ray shades a hit and returns sky on a miss",
    "[cuda][raycast]") {
  vec3 color[2] = {};
  if (cuda_ptr<vec3> d_out{2}; true) {
    REQUIRE(d_out.ok());
    shade_kernel<<<1, 1>>>(d_out.device_ptr());
    REQUIRE(d_out.store(color));
  }
  REQUIRE(cuda_last_status{}.ok());

  // The hit takes the red albedo: a positive red channel, no green or blue.
  CHECK(color[0].x > 0.0F);
  CHECK(color[0].y == 0.0F);
  CHECK(color[0].z == 0.0F);

  // The miss returns the blue sky, tonemapped (1 -> 0.5).
  CHECK(color[1].x == 0.0F);
  CHECK(color[1].y == 0.0F);
  CHECK(std::fabs(color[1].z - 0.5F) < 1e-4F);
}

TEST_CASE("to_byte gamma-encodes and clamps", "[cuda][raycast]") {
  unsigned char bytes[5] = {};
  if (cuda_ptr<unsigned char> d_out{5}; true) {
    REQUIRE(d_out.ok());
    to_byte_kernel<<<1, 1>>>(d_out.device_ptr());
    REQUIRE(d_out.store(bytes));
  }
  REQUIRE(cuda_last_status{}.ok());
  CHECK(bytes[0] == 0);
  CHECK(bytes[1] == 255);
  CHECK(bytes[2] == 0);   // negative clamps to 0
  CHECK(bytes[3] == 255); // above one clamps to 255
  CHECK(bytes[4] == 186); // 0.5 ^ (1/2.2) * 255, rounded
}

// NOLINTEND(readability-function-cognitive-complexity)
