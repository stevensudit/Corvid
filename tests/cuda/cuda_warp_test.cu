// Tests for cuda_warp: one warp of lanes exercises the lane and mask queries
// and each shuffle, and the host checks the pattern each lane recorded.

#include <cstdint>

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_ptr.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "corvid/cuda/cuda_warp.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

namespace {

constexpr auto lanes = 32U;

struct lane_record {
  unsigned lane_id;
  unsigned warp_id;
  uint32_t active_mask;
  int shuffle;      // lane 5's value
  int shuffle_up;   // the lane below, or own value at lane 0
  int shuffle_down; // the lane above, or own value at lane 31
  int shuffle_xor;  // the partner lane
  int reduced;      // sum over the warp
};

// Each lane's value is its index times ten, so every shuffle result names the
// lane it came from.
__global__ void warp_kernel(lane_record* out) {
  const auto lane = cuda_warp::lane_id();
  const auto value = static_cast<int>(lane) * 10;
  auto& rec = out[lane];
  rec.lane_id = lane;
  rec.warp_id = cuda_warp::warp_id();
  rec.active_mask = cuda_warp::active_mask();
  rec.shuffle = cuda_warp::shuffle(value, 5);
  rec.shuffle_up = cuda_warp::shuffle_up(value, 1U);
  rec.shuffle_down = cuda_warp::shuffle_down(value, 1U);
  rec.shuffle_xor = cuda_warp::shuffle_xor(value, 1U);
  auto sum = value;
  for (auto offset = lanes / 2; offset > 0; offset /= 2)
    sum += cuda_warp::shuffle_down(sum, offset, cuda_warp::all_mask);
  cuda_warp::sync();
  rec.reduced = cuda_warp::shuffle(sum, 0, cuda_warp::all_mask);
}

} // namespace

#pragma region cuda_warp

TEST_CASE("cuda_warp lane queries and shuffles", "[cuda]") {
  cuda_ptr<lane_record> d_out{lanes};
  REQUIRE(d_out.ok());
  warp_kernel<<<1, lanes>>>(d_out.get());
  lane_record out[lanes]{};
  REQUIRE(d_out.store(out));
  REQUIRE(cuda_last_status{}.ok());

  // 10 * (0 + 1 + ... + 31)
  constexpr auto warp_sum = 10 * (lanes * (lanes - 1) / 2);
  for (auto lane = 0U; lane < lanes; ++lane) {
    CAPTURE(lane);
    const auto& rec = out[lane];
    CHECK(rec.lane_id == lane);
    CHECK(rec.warp_id == 0);
    CHECK(rec.active_mask == cuda_warp::all_mask);
    CHECK(rec.shuffle == 50);
    CHECK(rec.shuffle_up == static_cast<int>(lane == 0 ? 0 : lane - 1) * 10);
    CHECK(rec.shuffle_down ==
          static_cast<int>(lane == lanes - 1 ? lane : lane + 1) * 10);
    CHECK(rec.shuffle_xor == static_cast<int>(lane ^ 1U) * 10);
    CHECK(rec.reduced == static_cast<int>(warp_sum));
  }
}

#pragma endregion
