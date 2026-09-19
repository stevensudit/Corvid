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
#include <vector>

#include "corvid/cuda/cuda_block.cuh"
#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

namespace {

// Each thread contributes its index and then its index times ten, so the two
// sums in a row exercise the shared-memory reuse, and every thread records
// both so the broadcast to all threads is checked too.
__global__ void block_kernel(int* first, int* second) {
  const auto thread = cuda_kernel::x_thread();
  first[thread] = cuda_block::sum(thread);
  second[thread] = cuda_block::sum(thread * 10);
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region cuda_block

TEST_CASE("cuda_block sums across one warp and across eight", "[cuda]") {
  for (const auto threads : {32U, 256U}) {
    DYNAMIC_SECTION(threads << " threads") {
      cuda_buffer<int> d_first{threads};
      cuda_buffer<int> d_second{threads};
      block_kernel<<<1, threads>>>(d_first.get(), d_second.get());
      std::vector<int> first(threads);
      std::vector<int> second(threads);
      REQUIRE(d_first.store(first));
      REQUIRE(d_second.store(second));
      REQUIRE(cuda_last_status{}.ok());

      // 0 + 1 + ... + (threads - 1)
      const auto expected = static_cast<int>(threads * (threads - 1) / 2);
      for (auto thread = 0U; thread < threads; ++thread) {
        CAPTURE(thread);
        CHECK(first[thread] == expected);
        CHECK(second[thread] == expected * 10);
      }
    }
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
