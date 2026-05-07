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

#include <atomic>
#include <thread>

#include "../corvid/concurrency.h"
#include "minitest.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-derived-method-shadowing-base-method)

class OwnerThreadTestDispatcher: public owner_thread_dispatcher<> {
public:
  using parent = owner_thread_dispatcher<>;

  OwnerThreadTestDispatcher(size_t max_pending = 16) : parent(max_pending) {}

  [[nodiscard]] size_t execute_post_queue() {
    // Expose `execute_post_queue` for testing.
    return parent::execute_post_queue();
  }

  [[nodiscard]] const auto& wake_fd() const noexcept {
    return parent::wake_fd();
  }
};

// NOLINTEND(bugprone-derived-method-shadowing-base-method)

#pragma region IsLoopThread

void OwnerThreadDispatcher_IsLoopThread() {
  // Constructor thread is the loop thread; other threads are not.
  owner_thread_dispatcher<> dispatcher;
  EXPECT_TRUE(dispatcher.is_loop_thread());

  relaxed_atomic_bool other_is_loop{true};
  std::thread t{[&] { other_is_loop = dispatcher.is_loop_thread(); }};
  t.join();
  EXPECT_FALSE(other_is_loop);
}
#pragma endregion

#pragma region PostAndExecute

void OwnerThreadDispatcher_PostAndExecute() {
  // `post` queues callbacks; `execute_post_queue` drains and returns count.
  OwnerThreadTestDispatcher dispatcher;
  int count{0};
  EXPECT_TRUE(dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  }));
  EXPECT_TRUE(dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  }));
  EXPECT_TRUE(dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  }));
  auto executed = dispatcher.execute_post_queue();
  EXPECT_EQ(executed, 3U);
  EXPECT_EQ(count, 3);
}
#pragma endregion

#pragma region ExecutePostQueue_Empty

void OwnerThreadDispatcher_ExecutePostQueue_Empty() {
  // Empty queue returns 0 and does not crash.
  OwnerThreadTestDispatcher dispatcher;
  EXPECT_EQ(dispatcher.execute_post_queue(), 0U);
}
#pragma endregion

#pragma region ExecuteOrPost_OnLoopThread

void OwnerThreadDispatcher_ExecuteOrPost_OnLoopThread() {
  // On the loop thread `execute_or_post` runs inline without queuing.
  OwnerThreadTestDispatcher dispatcher;
  int count{0};
  auto ok = dispatcher.execute_or_post([&count]() -> bool {
    ++count;
    return true;
  });
  EXPECT_TRUE(ok);
  EXPECT_EQ(count, 1);
  EXPECT_EQ(dispatcher.execute_post_queue(), 0U);
}
#pragma endregion

#pragma region ExecuteOrPost_OffLoopThread

void OwnerThreadDispatcher_ExecuteOrPost_OffLoopThread() {
  // From a non-loop thread `execute_or_post` posts without executing inline.
  OwnerThreadTestDispatcher dispatcher;
  relaxed_atomic_int count{0};
  std::thread t{[&] {
    (void)dispatcher.execute_or_post([&count]() -> bool {
      ++count;
      return true;
    });
  }};
  t.join();
  EXPECT_EQ(count, 0); // Not yet executed.
  auto executed = dispatcher.execute_post_queue();
  EXPECT_EQ(executed, 1U);
  EXPECT_EQ(count, 1);
}
#pragma endregion

#pragma region PostAndWait_OnLoopThread

void OwnerThreadDispatcher_PostAndWait_OnLoopThread() {
  // On the loop thread `post_and_wait` executes the callback directly.
  OwnerThreadTestDispatcher dispatcher;
  int count{0};
  auto ok = dispatcher.post_and_wait([&count]() -> bool {
    ++count;
    return true;
  });
  EXPECT_TRUE(ok);
  EXPECT_EQ(count, 1);
  EXPECT_EQ(dispatcher.execute_post_queue(), 0U);
}
#pragma endregion

#pragma region PostAndWait_OffLoopThread

void OwnerThreadDispatcher_PostAndWait_OffLoopThread() {
  // From a non-loop thread `post_and_wait` blocks until the loop thread
  // drains the queue.
  OwnerThreadTestDispatcher dispatcher;
  bool result{false};
  std::atomic<int> count{0};

  std::thread t{[&] {
    result = dispatcher.post_and_wait([&count]() -> bool {
      ++count;
      return true;
    });
  }};

  // Spin until the posted item signals the eventfd.
  event_fd::counter_t val{};
  while (!dispatcher.wake_fd().read(val)) std::this_thread::yield();

  auto executed = dispatcher.execute_post_queue();
  t.join();

  EXPECT_EQ(executed, 1U);
  EXPECT_TRUE(result);
  EXPECT_EQ(count.load(), 1);
}
#pragma endregion

#pragma region QueueHighWatermark

void OwnerThreadDispatcher_QueueHighWatermark() {
  // `queue_high_watermark` reflects the maximum capacity seen.
  OwnerThreadTestDispatcher dispatcher{4};
  EXPECT_GE(dispatcher.queue_high_watermark(), 4U);
  for (int i{}; i < 8; ++i)
    EXPECT_TRUE(dispatcher.post([]() -> bool { return true; }));
  (void)dispatcher.execute_post_queue();
  EXPECT_GE(dispatcher.queue_high_watermark(), 8U);
}
#pragma endregion

#pragma region DoubleBuffer

void OwnerThreadDispatcher_DoubleBuffer() {
  // Callbacks posted during `execute_post_queue` go into the inactive buffer
  // and are deferred to the next drain.
  OwnerThreadTestDispatcher dispatcher;
  int first{0};
  int second{0};

  EXPECT_TRUE(dispatcher.post([&]() mutable -> bool {
    ++first;
    (void)dispatcher.post([&]() mutable -> bool {
      ++second;
      return true;
    });
    return true;
  }));

  auto count1 = dispatcher.execute_post_queue();
  EXPECT_EQ(count1, 1U);
  EXPECT_EQ(first, 1);
  EXPECT_EQ(second, 0); // Deferred to next drain.

  auto count2 = dispatcher.execute_post_queue();
  EXPECT_EQ(count2, 1U);
  EXPECT_EQ(second, 1);
}
#pragma endregion

#pragma region WakeFd

void OwnerThreadDispatcher_WakeFd() {
  // `wake_fd` is signaled exactly once when the queue transitions from empty.
  OwnerThreadTestDispatcher dispatcher;

  // No signal before any post.
  EXPECT_FALSE(dispatcher.wake_fd().read().has_value());

  // First post to empty queue signals the fd.
  EXPECT_TRUE(dispatcher.post([]() -> bool { return true; }));
  EXPECT_TRUE(dispatcher.wake_fd().read().has_value());

  // Second post to a non-empty queue does not re-signal.
  EXPECT_TRUE(dispatcher.post([]() -> bool { return true; }));
  EXPECT_FALSE(dispatcher.wake_fd().read().has_value());

  (void)dispatcher.execute_post_queue();
}
#pragma endregion

#pragma region ExecuteOrPostWithRetry_Success

void OwnerThreadDispatcher_ExecuteOrPostWithRetry_Success() {
  // On the loop thread, fn succeeds immediately; returns true without posting.
  OwnerThreadTestDispatcher dispatcher;
  int calls{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&calls]() mutable -> bool {
        ++calls;
        return true;
      },
      2);
  EXPECT_TRUE(ok);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(dispatcher.execute_post_queue(), 0U);
}
#pragma endregion

#pragma region ExecuteOrPostWithRetry_ExhaustedRetry

void OwnerThreadDispatcher_ExecuteOrPostWithRetry_ExhaustedRetry() {
  // With retry_count=0 and a fn that always fails, returns false immediately.
  OwnerThreadTestDispatcher dispatcher;
  int calls{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&calls]() mutable -> bool {
        ++calls;
        return false;
      },
      0);
  EXPECT_FALSE(ok);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(dispatcher.execute_post_queue(), 0U);
}
#pragma endregion

#pragma region ExecuteOrPostWithRetry_Retry

void OwnerThreadDispatcher_ExecuteOrPostWithRetry_Retry() {
  // fn fails the first time; the retry posted to the queue succeeds.
  OwnerThreadTestDispatcher dispatcher;
  int attempts{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&attempts]() mutable -> bool {
        ++attempts;
        return attempts >= 2;
      },
      2);
  EXPECT_TRUE(ok);
  EXPECT_EQ(attempts, 1); // First call failed; retry was posted.

  (void)dispatcher.execute_post_queue();
  EXPECT_EQ(attempts, 2); // Retry succeeded.
}
#pragma endregion

#pragma region ExecuteOrPostWithRetry_OffLoopThread

void OwnerThreadDispatcher_ExecuteOrPostWithRetry_OffLoopThread() {
  // From a non-loop thread, fn is never called inline; it is always posted.
  OwnerThreadTestDispatcher dispatcher;
  relaxed_atomic_int calls{0};
  std::thread t{[&] {
    (void)dispatcher.execute_or_post_with_retry([&calls]() -> bool {
      ++calls;
      return true;
    });
  }};
  t.join();
  EXPECT_EQ(calls, 0); // Not yet executed.
  (void)dispatcher.execute_post_queue();
  EXPECT_EQ(calls, 1);
}
#pragma endregion

MAKE_TEST_LIST(OwnerThreadDispatcher_IsLoopThread,
    OwnerThreadDispatcher_PostAndExecute,
    OwnerThreadDispatcher_ExecutePostQueue_Empty,
    OwnerThreadDispatcher_ExecuteOrPost_OnLoopThread,
    OwnerThreadDispatcher_ExecuteOrPost_OffLoopThread,
    OwnerThreadDispatcher_PostAndWait_OnLoopThread,
    OwnerThreadDispatcher_PostAndWait_OffLoopThread,
    OwnerThreadDispatcher_QueueHighWatermark,
    OwnerThreadDispatcher_DoubleBuffer, OwnerThreadDispatcher_WakeFd,
    OwnerThreadDispatcher_ExecuteOrPostWithRetry_Success,
    OwnerThreadDispatcher_ExecuteOrPostWithRetry_ExhaustedRetry,
    OwnerThreadDispatcher_ExecuteOrPostWithRetry_Retry,
    OwnerThreadDispatcher_ExecuteOrPostWithRetry_OffLoopThread);

// NOLINTEND(readability-function-cognitive-complexity)
