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

namespace corvid { inline namespace concurrency {

// Interruptible deadline sleep for use with `std::jthread`.
//
// Wraps the workaround needed because libc++ does not yet implement the
// stop-token overload of `std::condition_variable_any::wait_until`. The
// pattern -- register a `std::stop_callback` that wakes the cv, then use
// a regular `wait_until` with a stop-check predicate -- is encapsulated
// here so callers don't have to repeat it.
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
  // Sleep until `deadline`. Returns true if a stop was requested before the
  // deadline, false if the deadline elapsed normally.
  template<typename Clock, typename Duration>
  [[nodiscard]] bool until(std::stop_token st,
      const std::chrono::time_point<Clock, Duration>& deadline) {
    std::stop_callback on_stop{st, [this] { cv_.notify_all(); }};
    std::unique_lock lock{mutex_};
    return cv_.wait_until(
        lock, deadline, [&st] { return st.stop_requested(); });
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
};

}} // namespace corvid::concurrency
