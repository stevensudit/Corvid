// Unit tests for corvid/cuda/windows/cuda_d3d11_interop.cuh. Two parts: the
// `cuda_graphics_register_flags` bitmask-enum registration wrapping
// `cudaGraphicsRegisterFlags` (round-tripping every named flag, plus a
// combined value and an unknown name), and `cuda_interop_adapter`, which picks
// the DXGI adapter that shares a GPU with CUDA. Neither launches a kernel or
// touches device memory; the adapter test does require a CUDA-capable GPU.

#include <cstring>
#include <string_view>

#include "corvid/cuda/windows/cuda_d3d11_interop.cuh"
#include "corvid/enums/enum_conversion.h"
#include "catch2_main.h"

using namespace corvid;
using corvid::cuda::cuda_graphics_register_flags;

namespace {

struct flag_case {
  cuda_graphics_register_flags value;
  std::string_view name;
};

constexpr flag_case flag_cases[] = {
    {cuda_graphics_register_flags::read_only, "read_only"},
    {cuda_graphics_register_flags::write_discard, "write_discard"},
    {cuda_graphics_register_flags::surface_load_store, "surface_load_store"},
    {cuda_graphics_register_flags::texture_gather, "texture_gather"},
};

} // namespace

TEST_CASE("cuda_graphics_register_flags single-flag round-trip",
    "[cuda][enums]") {
  for (const auto& c : flag_cases) {
    CAPTURE(c.name);
    CHECK(enum_as_string(c.value) == c.name); // enum -> string
    cuda_graphics_register_flags parsed{};
    CHECK(convert_enum(parsed, c.name)); // string -> enum
    CHECK(parsed == c.value);
  }
}

TEST_CASE("cuda_graphics_register_flags combined and unknown",
    "[cuda][enums]") {
  // Combined flags print in registration order (high bit first), joined with
  // " + "; parsing is order-independent.
  const auto combo =
      cuda_graphics_register_flags::surface_load_store |
      cuda_graphics_register_flags::read_only;
  CHECK(enum_as_string(combo) == "surface_load_store + read_only");
  cuda_graphics_register_flags parsed{};
  CHECK(convert_enum(parsed, "read_only + surface_load_store"));
  CHECK(parsed == combo);

  // An unknown name does not parse.
  cuda_graphics_register_flags unused{};
  CHECK_FALSE(convert_enum(unused, "not_a_flag"));
}

TEST_CASE("cuda_interop_adapter picks the CUDA device's adapter",
    "[cuda][interop]") {
  // These CUDA tests already target a discrete GPU, so an adapter must be
  // found; a null result would mean no CUDA-capable adapter at all.
  const auto adapter = corvid::cuda::cuda_interop_adapter();
  REQUIRE(adapter);

  // The call also made that GPU CUDA's current device. Its LUID must match the
  // adapter's, which is exactly the invariant interop needs: D3D and CUDA on
  // one physical GPU. CUDA reports the LUID in the same byte layout as DXGI.
  int device = 0;
  REQUIRE(cudaGetDevice(&device) == cudaSuccess);
  cudaDeviceProp prop{};
  REQUIRE(cudaGetDeviceProperties(&prop, device) == cudaSuccess);

  DXGI_ADAPTER_DESC desc{};
  REQUIRE(SUCCEEDED(adapter->GetDesc(&desc)));
  CHECK(std::memcmp(&desc.AdapterLuid, prop.luid, sizeof(desc.AdapterLuid)) ==
        0);
}
