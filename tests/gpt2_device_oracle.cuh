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

#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "corvid/containers/utils/matrix_view.h"
#include "corvid/cuda/cuda_buffer.cuh"
#include "corvid/cuda/cuda_matrix.cuh"
#include "corvid/cuda/llm/gpt2_engine.cuh"
#include "corvid/llm/gpt2.h"
#include "gpt2_oracle.h"

// The device side of the GPT-2 oracle.
//
// It downloads a device matrix and compares it against a dumped one, and
// holds the dumps and the engines over their weights for a whole run.
namespace corvid::tests::gpt2 {

#pragma region Fixtures

// The oracle dumps and the fp32 model over their weights.
//
// The members are built in order, and the weights move into the model, so
// `oracle` keeps the dumps the tests compare against.
struct shared_dumps {
  oracle_dumps oracle;
  const corvid::llm::gpt2_model<float> model;

  shared_dumps()
      : oracle(oracle_dumps::load()),
        model(
            corvid::llm::gpt2_model<float>::load(std::move(oracle.weights))) {}
};

// The dumps, loaded once for the whole run.
inline const shared_dumps& dumps() {
  static const shared_dumps shared;
  return shared;
}

// The engine over the shared model as `T`, uploaded once for the whole run,
// with its cache cleared so that each case starts from a fresh one.
template<typename T>
const corvid::cuda::llm::gpt2_engine<T>& fresh_engine() {
  static corvid::cuda::llm::gpt2_engine<T> engine(dumps().model);
  engine.clear_cache();
  return engine;
}

#pragma endregion
#pragma region Comparison

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

// Download `in` into a host vector of `float`, converted from its element
// type.
template<typename T>
std::vector<float> download(const corvid::cuda::cuda_buffer<T>& in) {
  std::vector<T> storage(in.size());
  REQUIRE(in.store(storage));
  return {storage.begin(), storage.end()};
}

// Download `device` and check that it is close to `expected`.
inline void check_device_close(corvid::cuda::cuda_matrix_view<float> device,
    float_matrix_view expected, float atol, float rtol) {
  const auto storage = download(device);
  check_close(float_matrix_view(storage, device.extent()), expected, atol,
      rtol);
}

#pragma endregion

} // namespace corvid::tests::gpt2
