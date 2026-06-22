// Unit test for corvid::cuda::cuda_status (corvid/cuda/cuda_status.cuh):
// exercises the sparse sequence-enum registration. Checks that named values
// carry the
// right CUDA error codes, that enum<->string conversion round-trips, that an
// unnamed gap value stringifies to its number, and that an unknown name fails
// to parse.

#include <string_view>

#include <cuda_runtime.h>

#include "corvid/enums/enum_conversion.h"
#include "corvid/cuda/cuda_status.cuh"
#include "catch2_main.h"

using namespace corvid;
using namespace corvid::strings;
using corvid::cuda::cuda_status;

namespace {

struct status_case {
  cuda_status value;
  int code;
  std::string_view name;
};

// Spot checks across the sparse blocks, including the two values straddling a
// gap that were corrected during authoring (invalid_texture_binding = 19,
// invalid_channel_descriptor = 20) and both range endpoints.
constexpr status_case status_cases[] = {
    {cuda_status::success, 0, "success"},
    {cuda_status::invalid_value, 1, "invalid_value"},
    {cuda_status::version_translation, 10, "version_translation"},
    {cuda_status::invalid_pitch_value, 12, "invalid_pitch_value"},
    {cuda_status::invalid_texture, 18, "invalid_texture"},
    {cuda_status::invalid_texture_binding, 19, "invalid_texture_binding"},
    {cuda_status::invalid_channel_descriptor, 20,
        "invalid_channel_descriptor"},
    {cuda_status::launch_pending_count_exceeded, 69,
        "launch_pending_count_exceeded"},
    {cuda_status::assert, 710, "assert"},
    {cuda_status::invalid_resource_configuration, 915,
        "invalid_resource_configuration"},
    {cuda_status::unknown, 999, "unknown"},
    {cuda_status::api_failure_base, 10000, "api_failure_base"},
};

} // namespace

TEST_CASE("cuda_status values and string round-trip", "[cuda][enums]") {
  for (const auto& c : status_cases) {
    CAPTURE(c.name);
    const int underlying = *c.value; // underlying value
    CHECK(underlying == c.code);
    CHECK(enum_as_string(c.value) == c.name); // enum -> string
    cuda_status parsed{};
    CHECK(convert_enum(parsed, c.name)); // string -> enum
    CHECK(parsed == c.value);
  }
}

TEST_CASE("cuda_status wraps the matching CUDA constants", "[cuda][enums]") {
  // Guards the invalid_texture_binding / invalid_channel_descriptor
  // off-by-one.
  CHECK(static_cast<int>(cuda_status::invalid_texture_binding) ==
        cudaErrorInvalidTextureBinding);
  CHECK(static_cast<int>(cuda_status::invalid_channel_descriptor) ==
        cudaErrorInvalidChannelDescriptor);
}

TEST_CASE("cuda_status gap and unknown handling", "[cuda][enums]") {
  // 11 is a valid-but-unnamed value (the gap after version_translation); it
  // stringifies to its number rather than a name.
  CHECK(enum_as_string(cuda_status{11}) == "11");

  // An unknown name does not parse.
  cuda_status unused{};
  CHECK_FALSE(convert_enum(unused, "not_a_real_status"));
}
