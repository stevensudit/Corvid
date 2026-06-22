// Unit test for corvid::cuda::cuda_graphics_register_flags
// (corvid/cuda/windows/cuda_d3d11_interop.cuh): exercises the bitmask-enum
// registration wrapping `cudaGraphicsRegisterFlags`. Round-trips every named
// flag, plus a combined value and an unknown name. Host-only: it launches no
// kernel and touches no device memory.

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
