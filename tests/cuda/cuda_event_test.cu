// Tests for cuda_event and cuda_timer: creation, recording and timing on the
// default stream, and the RAII timer around a trivial kernel.

#include <cuda_runtime.h>

#include "corvid/cuda/cuda_event.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

namespace {

__global__ void noop_kernel() {}

} // namespace

#pragma region cuda_event

TEST_CASE("cuda_event records and times on the default stream", "[cuda]") {
  cuda_event start;
  cuda_event stop;
  REQUIRE(start.ok());
  REQUIRE(stop.ok());
  CHECK(cuda_event::try_create().ok());

  REQUIRE(start.record());
  noop_kernel<<<1, 1>>>();
  REQUIRE(stop.record());
  REQUIRE(stop.synchronize());

  float ms = -1.0F;
  REQUIRE(cuda_event::elapsed_ms(start, stop, ms));
  CHECK(ms >= 0.0F);
  CHECK(cuda_last_status{}.ok());
}

#pragma endregion
#pragma region cuda_timer

TEST_CASE("cuda_timer sets its milliseconds on destruction", "[cuda]") {
  float ms = -1.0F;
  {
    cuda_timer timer{ms};
    CHECK(ms == 0.0F); // zeroed at construction
    noop_kernel<<<1, 1>>>();
  }
  CHECK(ms >= 0.0F);
  CHECK(cuda_timer::synchronize());
  CHECK(cuda_last_status{}.ok());
}

#pragma endregion
