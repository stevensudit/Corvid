// Unit test for corvid::win32::d3d::d3d11_bind_flag
// (corvid/cuda/windows/d3d11_swapchain.h): exercises the bitmask-enum
// registration wrapping `D3D11_BIND_FLAG`. Round-trips every named flag both
// ways and confirms the named flags are exactly the valid bits (so the unused
// bit 8 stays invalid), plus a combined value and an unknown name.

#include <string_view>

#include "corvid/cuda/windows/d3d11_swapchain.h"
#include "corvid/enums/enum_conversion.h"
#include "catch2_main.h"

using namespace corvid;
using corvid::win32::d3d::d3d11_bind_flag;

namespace {

struct flag_case {
  d3d11_bind_flag value;
  std::string_view name;
};

constexpr flag_case flag_cases[] = {
    {d3d11_bind_flag::vertex_buffer, "vertex_buffer"},
    {d3d11_bind_flag::index_buffer, "index_buffer"},
    {d3d11_bind_flag::constant_buffer, "constant_buffer"},
    {d3d11_bind_flag::shader_resource, "shader_resource"},
    {d3d11_bind_flag::stream_output, "stream_output"},
    {d3d11_bind_flag::render_target, "render_target"},
    {d3d11_bind_flag::depth_stencil, "depth_stencil"},
    {d3d11_bind_flag::unordered_access, "unordered_access"},
    {d3d11_bind_flag::decoder, "decoder"},
    {d3d11_bind_flag::video_encoder, "video_encoder"},
};

} // namespace

TEST_CASE("d3d11_bind_flag string round-trip and valid-bit set",
    "[d3d][enums]") {
  // Convert each named flag both ways, then confirm the named flags are
  // exactly the registration's valid bits. The union check is what catches a
  // misregistered bit: D3D11 has no flag at bit 8 (0x100), so naming it, or
  // dropping a real flag, makes the valid-bit set diverge from this union.
  using corvid::enums::bitmask::valid_bits_v;
  unsigned named{};
  for (const auto& c : flag_cases) {
    CAPTURE(c.name);
    CHECK(enum_as_string(c.value) == c.name); // enum -> string
    d3d11_bind_flag parsed{};
    CHECK(convert_enum(parsed, c.name)); // string -> enum
    CHECK(parsed == c.value);
    named |= *c.value;
  }
  CHECK(named == valid_bits_v<d3d11_bind_flag>);
}

TEST_CASE("d3d11_bind_flag combined and unknown handling", "[d3d][enums]") {
  // Combined flags print in registration order (high bit first), joined with
  // " + "; parsing is order-independent.
  const auto combo =
      d3d11_bind_flag::render_target | d3d11_bind_flag::shader_resource;
  CHECK(enum_as_string(combo) == "render_target + shader_resource");
  d3d11_bind_flag parsed{};
  CHECK(convert_enum(parsed, "shader_resource + render_target"));
  CHECK(parsed == combo);

  // An unknown name does not parse.
  d3d11_bind_flag unused{};
  CHECK_FALSE(convert_enum(unused, "not_a_flag"));
}
