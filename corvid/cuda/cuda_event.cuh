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
#include <stdexcept>
#include <utility>

#include <cuda_runtime.h>

#include "./cuda_status.cuh"

namespace corvid::cuda {

#pragma region cuda_event

// CUDA event (timing-enabled) wrapper.
//
// You should probably just use `cuda_timer`.
class cuda_event {
public:
#pragma region Construction

  cuda_event() {
    if (!cuda_last_status{cudaEventCreate(&event_)}) event_ = nullptr;
  }

  cuda_event(const cuda_event&) = delete;
  cuda_event& operator=(const cuda_event&) = delete;

  cuda_event(cuda_event&& other) noexcept
      : event_{std::exchange(other.event_, nullptr)} {}
  cuda_event& operator=(cuda_event&& other) noexcept {
    if (this != &other) {
      destroy();
      event_ = std::exchange(other.event_, nullptr);
    }
    return *this;
  }
  ~cuda_event() { destroy(); }

#pragma endregion Construction
#pragma region Status

  [[nodiscard]] bool ok() const { return event_; }
  [[nodiscard]] explicit operator bool() const { return ok(); }
  [[nodiscard]] bool operator!() const { return !ok(); }
  void operator*() const {
    if (!event_) throw std::runtime_error{"dereferencing null cuda_event"};
  }

#pragma endregion Status
#pragma region Operations

  // Record this event into `stream` (default stream).
  [[nodiscard]] cuda_last_status record(cudaStream_t stream = nullptr) {
    return cudaEventRecord(event_, stream);
  }
  // Block the host until this event completes.
  [[nodiscard]] cuda_last_status synchronize() const {
    return cudaEventSynchronize(event_);
  }

#pragma endregion Operations
#pragma region Accessors

  [[nodiscard]] cudaEvent_t get() const { return event_; }
  [[nodiscard]] operator cudaEvent_t() const { return event_; }

#pragma endregion Accessors
#pragma region Timing

  // Milliseconds between two recorded events; `stop` must have completed.
  [[nodiscard]] static cuda_last_status
  elapsed_ms(const cuda_event& start, const cuda_event& stop, float& ms) {
    return cudaEventElapsedTime(&ms, start, stop);
  }

#pragma endregion Timing
#pragma region Helpers
private:
  void destroy() {
    if (event_) cudaEventDestroy(event_);
  }

#pragma endregion Helpers
#pragma region Data members

  cudaEvent_t event_{};

#pragma endregion Data members
};

#pragma endregion cuda_event
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

#pragma endregion Construction
#pragma region Operations

  // Synchronize the device and host, blocking until all preceding work on the
  // device has completed. This is a common thing to do in benchmarks.
  [[nodiscard]] static cuda_last_status synchronize() {
    return cudaDeviceSynchronize();
  }

#pragma endregion Operations
#pragma region Data members
private:
  cuda_event start_;
  cuda_event stop_;
  float& ms_;

#pragma endregion Data members
};

#pragma endregion cuda_timer

} // namespace corvid::cuda
