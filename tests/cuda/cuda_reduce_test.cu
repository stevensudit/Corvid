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
#include <ranges>
#include <vector>

#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_kernel.cuh"
#include "corvid/cuda/cuda_reduce.cuh"
#include "corvid/cuda/cuda_status.cuh"
#include "catch2_main.h"

using namespace corvid::cuda;

namespace {

constexpr auto lanes = 32U;

// Each thread contributes its index to a warp sum and a block sum, then its
// index times ten to a second block sum, so the two block sums in a row
// exercise the shared-memory reuse. It then contributes its index less 1000
// to a warp max and a block max, all negative, so a lane that wrongly brought
// zero would win. Every thread records all five, so the broadcast to every
// thread is checked too.
__global__ void reduce_kernel(int* warp, int* first, int* second,
    int* warp_top, int* block_top) {
  const auto thread = cuda_kernel::x_thread();
  warp[thread] = cuda_reduce::warp_sum(thread);
  first[thread] = cuda_reduce::block_sum(thread);
  second[thread] = cuda_reduce::block_sum(thread * 10);
  warp_top[thread] = cuda_reduce::warp_max(thread - 1000);
  block_top[thread] = cuda_reduce::block_max(thread - 1000);
}

// Each thread contributes `values[thread]` at its own index to a block max
// element, and every thread records the winning index, so the broadcast to
// every thread is checked too.
__global__ void max_element_kernel(const float* values, size_t* winner) {
  const auto thread = cuda_kernel::x_thread<size_t>();
  const auto best = cuda_reduce::block_max(
      cuda_reduce::element<float>{values[thread], thread});
  winner[thread] = best.index;
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region cuda_reduce

TEST_CASE("cuda_reduce sums and maxes across one warp and across eight",
    "[cuda]") {
  for (const auto threads : {lanes, 8 * lanes}) {
    DYNAMIC_SECTION(threads << " threads") {
      cuda_buffer<int> d_warp{threads};
      cuda_buffer<int> d_first{threads};
      cuda_buffer<int> d_second{threads};
      cuda_buffer<int> d_warp_top{threads};
      cuda_buffer<int> d_block_top{threads};
      reduce_kernel<<<1, threads>>>(d_warp.get(), d_first.get(),
          d_second.get(), d_warp_top.get(), d_block_top.get());
      std::vector<int> warp(threads);
      std::vector<int> first(threads);
      std::vector<int> second(threads);
      std::vector<int> warp_top(threads);
      std::vector<int> block_top(threads);
      REQUIRE(d_warp.store(warp));
      REQUIRE(d_first.store(first));
      REQUIRE(d_second.store(second));
      REQUIRE(d_warp_top.store(warp_top));
      REQUIRE(d_block_top.store(block_top));
      REQUIRE(cuda_last_status{}.ok());

      // 0 + 1 + ... + (threads - 1)
      const auto block_total = static_cast<int>(threads * (threads - 1) / 2);
      for (auto thread = 0U; thread < threads; ++thread) {
        CAPTURE(thread);
        // The thread's warp holds the 32 indexes from `32 * w` on, whose sum
        // is 32 * 32 * w plus 0 + 1 + ... + 31.
        const auto w = thread / lanes;
        const auto warp_total =
            static_cast<int>((lanes * lanes * w) + (lanes * (lanes - 1) / 2));
        CHECK(warp[thread] == warp_total);
        CHECK(first[thread] == block_total);
        CHECK(second[thread] == block_total * 10);
        // The warp's largest index is its last lane's.
        CHECK(
            warp_top[thread] == static_cast<int>(lanes * (w + 1)) - 1 - 1000);
        CHECK(block_top[thread] == static_cast<int>(threads) - 1 - 1000);
      }
    }
  }
}

TEST_CASE(
    "cuda_reduce finds the first largest element across one warp and "
    "across eight",
    "[cuda]") {
  for (const auto threads : {lanes, 8 * lanes}) {
    DYNAMIC_SECTION(threads << " threads") {
      // The values fall with the index, so the largest sits at index 0 until
      // a peak is planted: at the last thread, in the middle of the block, or
      // at two threads for a tie, which the lower index wins.
      struct peak_case {
        std::vector<size_t> peaks;
        size_t expected;
      };
      const std::vector<peak_case> cases{
          {{}, 0},
          {{threads - 1}, threads - 1},
          {{threads / 2 + 1}, threads / 2 + 1},
          {{threads / 4, 3 * threads / 4}, threads / 4},
      };
      for (const auto& [peaks, expected] : cases) {
        CAPTURE(expected);
        std::vector<float> values(threads);
        for (const auto [thread, value] : std::views::enumerate(values))
          value = -static_cast<float>(thread);
        for (const auto peak : peaks) values[peak] = 1.0F;

        const cuda_buffer<float> d_values(values);
        cuda_buffer<size_t> d_winner{threads};
        max_element_kernel<<<1, threads>>>(d_values.get(), d_winner.get());
        std::vector<size_t> winner(threads);
        REQUIRE(d_winner.store(winner));
        REQUIRE(cuda_last_status{}.ok());

        for (const auto [thread, index] : std::views::enumerate(winner)) {
          CAPTURE(thread);
          CHECK(index == expected);
        }
      }
    }
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
