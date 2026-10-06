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
#pragma once

#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "corvid/containers/utils/matrix_view.h"
#include "corvid/cuda/cuda_matrix.cuh"
#include "gpt2_oracle.h"

// The device side of the GPT-2 oracle, which downloads a device matrix and
// compares it against a dumped one.
namespace corvid::tests::gpt2 {

// Download `in` into a host vector of `float`, in row-major order, converted
// from its element type.
template<corvid::cuda::DeviceMatrixViewable In>
std::vector<float> download(const In& in) {
  using T = corvid::cuda::device_value_t<In>;
  const corvid::cuda::cuda_matrix_view<T> device = in;
  std::vector<T> storage(device.size());
  REQUIRE(device.store(matrix_lens<T>(storage, device.extent())));
  return {storage.begin(), storage.end()};
}

// Download `device` and check that it is close to `expected`.
inline void check_device_close(corvid::cuda::cuda_matrix_view<float> device,
    float_matrix_view expected, float atol, float rtol) {
  const auto storage = download(device);
  check_close(float_matrix_view(storage, device.extent()), expected, atol,
      rtol);
}

} // namespace corvid::tests::gpt2
