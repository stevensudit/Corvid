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

#include <cuda_runtime.h>

#include "./cuda_handle.cuh"
#include "./cuda_status.cuh"

namespace corvid::cuda {

#pragma region cuda_event

// CUDA event (timing-enabled) wrapper.
//
// You should probably just use `cuda_timer`.
class cuda_event: public cuda_handle<cudaEvent_t, cudaEventDestroy> {
public:
#pragma region Construction

  cuda_event() : cuda_handle{create()} {}

#pragma endregion
#pragma region Operations

  // Record this event into `stream` (default stream).
  [[nodiscard]] cuda_last_status record(cudaStream_t stream = nullptr) {
    return cudaEventRecord(get(), stream);
  }
  // Block the host until this event completes.
  [[nodiscard]] cuda_last_status synchronize() const {
    return cudaEventSynchronize(get());
  }

#pragma endregion
#pragma region Timing

  // Milliseconds between two recorded events; `stop` must have completed.
  [[nodiscard]] static cuda_last_status
  elapsed_ms(const cuda_event& start, const cuda_event& stop, float& ms) {
    return cudaEventElapsedTime(&ms, start, stop);
  }

#pragma endregion
#pragma region Helpers
private:
  static cudaEvent_t create() noexcept {
    cudaEvent_t event{};
    if (!cuda_last_status{cudaEventCreate(&event)}) event = nullptr;
    return event;
  }

#pragma endregion
};

#pragma endregion
#pragma region cuda_timer

// RAII timer for CUDA code. Imprints on `ms` during construction and sets it
// on destruction. On failure, either throws or `ms` remains 0.
class cuda_timer {
public:
#pragma region Construction

  explicit cuda_timer(float& ms) : ms_{ms} {
    *start_;
    *stop_;
    ms_ = 0.F;
    *start_.record();
  }

  cuda_timer(const cuda_timer&) = delete;
  cuda_timer& operator=(const cuda_timer&) = delete;

  ~cuda_timer() {
    if (!stop_.record() || !stop_.synchronize()) return;
    (void)cuda_event::elapsed_ms(start_, stop_, ms_);
  }

#pragma endregion
#pragma region Operations

  // Synchronize the device and host, blocking until all preceding work on the
  // device has completed. This is a common thing to do in benchmarks.
  [[nodiscard]] static cuda_last_status synchronize() {
    return cudaDeviceSynchronize();
  }

#pragma endregion
#pragma region Data members
private:
  cuda_event start_;
  cuda_event stop_;
  float& ms_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
