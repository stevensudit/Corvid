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
#include "../corvid/proto/io_uring/iou_loop.h"

#include <atomic>
#include <chrono>
#include <thread>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;
using namespace std::chrono_literals;

namespace {

bool WaitFor(const auto& pred, std::chrono::milliseconds timeout = 500ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return pred();
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)
void IouLoop_NopCompletion() {
  // Verify that a submitted NOP fires its completion callback on the runner.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool fired{false};
    std::atomic<int32_t> result{-999};

    const bool submitted = runner->post_and_wait([&] {
      return runner->submit_nop([&](iou_res res) {
        result.store(res.value(), std::memory_order::relaxed);
        fired.store(true, std::memory_order::release);
        return true;
      });
    });
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(
        WaitFor([&] { return fired.load(std::memory_order::acquire); }));
    EXPECT_EQ(result.load(), 0);
  }
}

void IouLoop_MultipleNops() {
  // Submit several NOPs and confirm all complete on the runner thread.
  if (true) {
    iou_loop_runner runner;
    std::atomic<int> count{0};

    const bool submitted = runner->post_and_wait([&] {
      for (int i = 0; i < 4; ++i) {
        const bool ok = runner->submit_nop([&](iou_res) {
          count.fetch_add(1, std::memory_order::relaxed);
          return true;
        });
        if (!ok) return false;
      }
      return true;
    });
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(
        WaitFor([&] { return count.load(std::memory_order::acquire) == 4; }));
    EXPECT_EQ(count.load(), 4);
  }
}

void IouLoop_StopFromThread() {
  // Stop the runner from another thread and let destruction join cleanly.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool stopped{false};

    std::thread t{[&] {
      runner.stop();
      stopped.store(true, std::memory_order::release);
    }};
    std::this_thread::sleep_for(20ms);
    t.join();
    EXPECT_TRUE(stopped.load(std::memory_order::acquire));
  }
}

void IouLoop_PostFromThread() {
  // Post a callback from an external thread; verify the loop executes it.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool fired{false};

    const bool ok = runner->post([&] {
      fired.store(true, std::memory_order::release);
      return true;
    });
    EXPECT_TRUE(ok);

    EXPECT_TRUE(
        WaitFor([&] { return fired.load(std::memory_order::acquire); }));
  }
}

void IouLoop_PostAndWait() {
  // post_and_wait blocks until the callback runs, then returns.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool ran{false};

    const bool ok = runner->post_and_wait([&] {
      ran.store(true, std::memory_order::relaxed);
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(ran.load());
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
MAKE_TEST_LIST(IouLoop_NopCompletion, IouLoop_MultipleNops,
    IouLoop_StopFromThread, IouLoop_PostFromThread, IouLoop_PostAndWait)
