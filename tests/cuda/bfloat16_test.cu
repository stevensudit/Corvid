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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "corvid/cuda/bfloat16.cuh"
#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_reduce.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "corvid/meta/concepts.h"
#include "corvid/containers/utils/interval.h"
#include "catch2_main.h"

using corvid::Floating;
using corvid::iota;
using corvid::cuda::bfloat16_t;
using corvid::cuda::cuda_buffer;
using corvid::cuda::cuda_kernel;
using corvid::cuda::cuda_last_status;
using corvid::cuda::cuda_reduce;
using corvid::cuda::DeviceFloating;

namespace {

constexpr auto threads = 64U;

// Each thread narrows its float and widens it back, and every thread records
// the block's largest, so the identity and the shuffle of a two-byte type are
// exercised too.
__global__ void round_trip_kernel(const float* in, bfloat16_t* narrowed,
    float* widened, bfloat16_t* largest) {
  const auto thread = cuda_kernel::x_thread<size_t>();
  const bfloat16_t value{in[thread]};
  narrowed[thread] = value;
  widened[thread] = static_cast<float>(value);
  largest[thread] = cuda_reduce::block_max(value);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region bfloat16_t

static_assert(sizeof(bfloat16_t) == 2);
static_assert(std::is_trivially_copyable_v<bfloat16_t>);
static_assert(std::is_trivially_default_constructible_v<bfloat16_t>);
static_assert(DeviceFloating<bfloat16_t>);
static_assert(DeviceFloating<float>);
static_assert(DeviceFloating<double>);
static_assert(!DeviceFloating<int>);
static_assert(!Floating<bfloat16_t>);
static_assert(::cuda::std::numeric_limits<bfloat16_t>::is_specialized);
static_assert(::cuda::std::numeric_limits<bfloat16_t>::digits == 8);

TEST_CASE("bfloat16_t is the top half of a float", "[cuda]") {
  struct pattern {
    float value;
    uint16_t bits;
  };
  // Exactly representable values keep their top half, and the rest round to
  // nearest, ties to even: 1 + 2^-8 sits between 0x3F80 and 0x3F81 and takes
  // the even one, 1 + 3 * 2^-8 sits between 0x3F81 and 0x3F82 and takes
  // 0x3F82.
  const std::vector<pattern> patterns{
      {0.0F, 0x0000},
      {1.0F, 0x3F80},
      {2.0F, 0x4000},
      {-1.5F, 0xBFC0},
      {1.0078125F, 0x3F81},
      {1.00390625F, 0x3F80},
      {1.01171875F, 0x3F82},
      {1.005F, 0x3F81},
  };
  for (const auto& [value, bits] : patterns) {
    CAPTURE(value);
    CHECK(bfloat16_t{value}.bits() == bits);
    CHECK(static_cast<float>(bfloat16_t::from_bits(bits)) ==
          static_cast<float>(bfloat16_t{value}));
  }

  // The exactly representable ones survive the round trip untouched.
  for (const auto value : {0.0F, 1.0F, 2.0F, -1.5F, 1.0078125F})
    CHECK(static_cast<float>(bfloat16_t{value}) == value);
}

TEST_CASE("bfloat16_t compares as its float value", "[cuda]") {
  const bfloat16_t one{1.0F};
  const bfloat16_t two{2.0F};
  CHECK(one == bfloat16_t::from_bits(0x3F80));
  CHECK(one < two);
  CHECK(!(two < one));
  CHECK(!(one < one));
}

TEST_CASE("bfloat16_t limits are the float limits with the low half cut",
    "[cuda]") {
  using limits = ::cuda::std::numeric_limits<bfloat16_t>;
  CHECK(limits::max().bits() == 0x7F7F);
  CHECK(limits::lowest().bits() == 0xFF7F);
  CHECK(limits::min().bits() == 0x0080);
  CHECK(limits::infinity().bits() == 0x7F80);
  CHECK(limits::epsilon() == bfloat16_t{0.0078125F});

  // The extremes are finite and mirror each other, and they are the widest
  // pattern the mantissa holds: 2 - 2^-7, times 2^127.
  const auto max = static_cast<float>(limits::max());
  CHECK(std::isfinite(max));
  CHECK(max == (2.0F - 0.0078125F) * 0x1p127F);
  CHECK(static_cast<float>(limits::lowest()) == -max);
  CHECK(std::isinf(static_cast<float>(limits::infinity())));
  CHECK(std::isnan(static_cast<float>(limits::quiet_NaN())));
}

TEST_CASE("bfloat16_t converts on the device and reduces through block_max",
    "[cuda]") {
  // Half steps from -10, all exact in 8 significant bits, so the round trip
  // is exact and the largest is the last thread's 21.5.
  std::vector<float> in(threads);
  for (const auto thread : iota(threads))
    in[thread] = (static_cast<float>(thread) * 0.5F) - 10.0F;

  const cuda_buffer<float> d_in(in);
  cuda_buffer<bfloat16_t> d_narrowed{threads};
  cuda_buffer<float> d_widened{threads};
  cuda_buffer<bfloat16_t> d_largest{threads};
  round_trip_kernel<<<1, threads>>>(d_in.get(), d_narrowed.get(),
      d_widened.get(), d_largest.get());
  std::vector<bfloat16_t> narrowed(threads);
  std::vector<float> widened(threads);
  std::vector<bfloat16_t> largest(threads);
  REQUIRE(d_narrowed.store(narrowed));
  REQUIRE(d_widened.store(widened));
  REQUIRE(d_largest.store(largest));
  REQUIRE(cuda_last_status{}.ok());

  for (const auto thread : iota(threads)) {
    CAPTURE(thread);
    CHECK(narrowed[thread].bits() == bfloat16_t{in[thread]}.bits());
    CHECK(widened[thread] == in[thread]);
    CHECK(largest[thread] == bfloat16_t{21.5F});
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
