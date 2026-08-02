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
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>
#include <string_view>
#include <string>
#include <atomic>

// We support both Windows and POSIX.
#ifdef _WIN32
// windows.h pulls in min/max macros, and its non-lean corners pollute further
// (see corvid/infra/log.h); keep them out.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace corvid { inline namespace concurrency {

#pragma region jthread_stoppable_sleep

// Interruptible deadline sleep for use with `std::jthread`.
//
// A thin, named veneer over the stop-token overload of
// `std::condition_variable_any::wait_until`: sleep until the deadline, wake
// immediately when a stop is requested.
//
// Also home to `set_thread_name`, a small cross-platform thread-naming
// helper used by the I/O loops.
//
// Usage:
//
//   jthread_stoppable_sleep sleep;
//
//   std::jthread worker{[&sleep](std::stop_token st) {
//     while (!sleep.until(st, next_deadline()))
//       do_work();
//   }};
//
//   // From any thread -- wakes the sleeping jthread immediately:
//   worker.request_stop();
//
class jthread_stoppable_sleep {
public:
#pragma region Operations

  // Sleep until `deadline`. Returns true if a stop was requested before the
  // deadline, false if the deadline elapsed normally.
  template<typename Clock, typename Duration>
  [[nodiscard]] bool until(std::stop_token st,
      const std::chrono::time_point<Clock, Duration>& deadline) {
    std::unique_lock lock(mutex_);
    return cv_.wait_until(lock, st, deadline, [&st] {
      return st.stop_requested();
    });
  }

  static void set_thread_name(std::string_view name) {
    static std::atomic_int thread_count;
    const auto n = ++thread_count;
    auto label = std::to_string(n);
    label += '-';
    label += name;
#ifdef _WIN32
    // `SetThreadDescription` takes a wide string; the name is ASCII in
    // practice, so widening character by character is enough.
    const std::wstring wide{label.begin(), label.end()};
    (void)::SetThreadDescription(::GetCurrentThread(), wide.c_str());
#else
    if (label.size() > 15) label.resize(15);
    (void)::pthread_setname_np(::pthread_self(), label.c_str());
#endif
  }

#pragma endregion
#pragma region Data members
private:
  std::mutex mutex_;
  std::condition_variable_any cv_;

#pragma endregion
};

#pragma endregion
}} // namespace corvid::concurrency
