// Tests for the 3D array wrappers: a kernel writes a known field through the
// surface, another reads it back through the texture (filtered) or the
// surface (exact), and the host checks the values.

#include <cstdint>

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_array_3d.cuh"
#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "corvid/cuda/cuda_surface.cuh"
#include "corvid/cuda/cuda_texture.cuh"
#include "corvid/cuda/cuda_volume.cuh"
#include "corvid/cuda/material_volume.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

namespace {

constexpr auto side = 4U;
constexpr auto voxels = side * side * side;

// The field value at voxel (x, y, z): distinct per voxel and linear in x, so
// a trilinear read between two x-neighbors lands exactly halfway.
__host__ __device__ float field_at(unsigned x, unsigned y, unsigned z) {
  return static_cast<float>(x) + (10.0F * static_cast<float>(y)) +
         (100.0F * static_cast<float>(z));
}

// One thread per voxel; the block is the whole 4x4x4 grid.
__global__ void fill_float(cudaSurfaceObject_t surface) {
  surf3Dwrite(field_at(threadIdx.x, threadIdx.y, threadIdx.z), surface,
      static_cast<int>(threadIdx.x * sizeof(float)), threadIdx.y, threadIdx.z);
}

// Read every voxel center through the texture, plus the midpoint between
// voxels (0, 0, 0) and (1, 0, 0) into `mid`.
__global__ void
read_float(cudaTextureObject_t texture, float* out, float* mid) {
  const auto x = threadIdx.x;
  const auto y = threadIdx.y;
  const auto z = threadIdx.z;
  out[x + (side * y) + (side * side * z)] = tex3D<float>(texture,
      static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F,
      static_cast<float>(z) + 0.5F);
  if (x == 0 && y == 0 && z == 0)
    *mid = tex3D<float>(texture, 1.0F, 0.5F, 0.5F);
}

__global__ void fill_u16(cudaSurfaceObject_t surface) {
  const auto value = static_cast<uint16_t>(
      threadIdx.x + (side * threadIdx.y) + (side * side * threadIdx.z));
  surf3Dwrite(value, surface, static_cast<int>(threadIdx.x * sizeof(uint16_t)),
      threadIdx.y, threadIdx.z);
}

__global__ void read_u16(cudaSurfaceObject_t surface, uint16_t* out) {
  uint16_t value{};
  surf3Dread(&value, surface, static_cast<int>(threadIdx.x * sizeof(uint16_t)),
      threadIdx.y, threadIdx.z);
  out[threadIdx.x + (side * threadIdx.y) + (side * side * threadIdx.z)] =
      value;
}

} // namespace

#pragma region cuda_volume

TEST_CASE(
    "cuda_volume writes through the surface and reads through the "
    "texture",
    "[cuda]") {
  const cudaExtent extent{side, side, side};
  cuda_volume<float> volume{extent};
  CHECK(volume.extent().width == side);

  const dim3 block{side, side, side};
  fill_float<<<1, block>>>(volume.surface());
  cuda_ptr<float> d_out{voxels};
  cuda_ptr<float> d_mid;
  read_float<<<1, block>>>(volume.texture(), d_out.get(), d_mid.get());
  REQUIRE(cuda_last_status{}.ok());

  float out[voxels]{};
  REQUIRE(d_out.store(out));
  for (auto z = 0U; z < side; ++z)
    for (auto y = 0U; y < side; ++y)
      for (auto x = 0U; x < side; ++x) {
        CAPTURE(x, y, z);
        CHECK(out[x + (side * y) + (side * side * z)] == field_at(x, y, z));
      }

  // Halfway between (0, 0, 0) and (1, 0, 0) the linear filter blends evenly.
  // Texture filtering uses 8-bit fixed-point weights, so allow a small error.
  float mid{};
  REQUIRE(d_mid.store(mid));
  CHECK(fabsf(mid - 0.5F) < 0.01F);
}

TEST_CASE("cuda_array_3d and its views fail politely", "[cuda]") {
  // An extent no device can allocate.
  const cudaExtent absurd{1U << 20, 1U << 20, 1U << 20};
  CHECK_FALSE(cuda_array_3d<float>::try_create(absurd).ok());
  CHECK_FALSE(cuda_last_status{}.ok()); // left for the caller; consumed
  CHECK_THROWS_AS(cuda_array_3d<float>{absurd}, std::runtime_error);
  CHECK(cuda_last_status{}.ok()); // the throw consumed it

  // Views over a null array cannot be created.
  CHECK_FALSE(cuda_surface::try_create(nullptr).ok());
  CHECK_FALSE(cuda_last_status{}.ok());
  CHECK_FALSE(cuda_texture::try_create(nullptr).ok());
  CHECK_FALSE(cuda_last_status{}.ok());
}

#pragma endregion
#pragma region material_volume

TEST_CASE("material_volume round-trips exact values through its surface",
    "[cuda]") {
  const cudaExtent extent{side, side, side};
  material_volume materials{extent};
  CHECK(materials.extent().depth == side);

  const dim3 block{side, side, side};
  fill_u16<<<1, block>>>(materials.surface());
  cuda_ptr<uint16_t> d_out{voxels};
  read_u16<<<1, block>>>(materials.surface(), d_out.get());
  REQUIRE(cuda_last_status{}.ok());

  uint16_t out[voxels]{};
  REQUIRE(d_out.store(out));
  for (auto i = 0U; i < voxels; ++i) {
    CAPTURE(i);
    CHECK(out[i] == i);
  }
}

#pragma endregion
