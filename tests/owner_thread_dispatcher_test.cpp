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
#include "catch2_main.h"

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

TEST_CASE("OwnerThreadDispatcher_IsLoopThread", "[OwnerThreadDispatcher]") {
  // Constructor thread is the loop thread; other threads are not.
  owner_thread_dispatcher<> dispatcher;
  CHECK((dispatcher.is_loop_thread()));

  relaxed_atomic_bool other_is_loop{true};
  std::thread t{[&] { other_is_loop = dispatcher.is_loop_thread(); }};
  t.join();
  CHECK_FALSE((other_is_loop));
}
#pragma endregion

#pragma region PostAndExecute

TEST_CASE("OwnerThreadDispatcher_PostAndExecute", "[OwnerThreadDispatcher]") {
  // `post` queues callbacks; `execute_post_queue` drains and returns count.
  OwnerThreadTestDispatcher dispatcher;
  int count{0};
  CHECK((dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  })));
  CHECK((dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  })));
  CHECK((dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  })));
  auto executed = dispatcher.execute_post_queue();
  CHECK((executed) == (3U));
  CHECK((count) == (3));
}
#pragma endregion

#pragma region ExecutePostQueue_Empty

TEST_CASE("OwnerThreadDispatcher_ExecutePostQueue_Empty",
    "[OwnerThreadDispatcher]") {
  // Empty queue returns 0 and does not crash.
  OwnerThreadTestDispatcher dispatcher;
  CHECK((dispatcher.execute_post_queue()) == (0U));
}
#pragma endregion

#pragma region ExecuteOrPost_OnLoopThread

TEST_CASE("OwnerThreadDispatcher_ExecuteOrPost_OnLoopThread",
    "[OwnerThreadDispatcher]") {
  // On the loop thread `execute_or_post` runs inline without queuing.
  OwnerThreadTestDispatcher dispatcher;
  int count{0};
  auto ok = dispatcher.execute_or_post([&count]() -> bool {
    ++count;
    return true;
  });
  CHECK((ok));
  CHECK((count) == (1));
  CHECK((dispatcher.execute_post_queue()) == (0U));
}
#pragma endregion

#pragma region ExecuteOrPost_OffLoopThread

TEST_CASE("OwnerThreadDispatcher_ExecuteOrPost_OffLoopThread",
    "[OwnerThreadDispatcher]") {
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
  CHECK((count) == (0)); // Not yet executed.
  auto executed = dispatcher.execute_post_queue();
  CHECK((executed) == (1U));
  CHECK((count) == (1));
}
#pragma endregion

#pragma region PostAndWait_OnLoopThread

TEST_CASE("OwnerThreadDispatcher_PostAndWait_OnLoopThread",
    "[OwnerThreadDispatcher]") {
  // On the loop thread `post_and_wait` executes the callback directly.
  OwnerThreadTestDispatcher dispatcher;
  int count{0};
  auto ok = dispatcher.post_and_wait([&count]() -> bool {
    ++count;
    return true;
  });
  CHECK((ok));
  CHECK((count) == (1));
  CHECK((dispatcher.execute_post_queue()) == (0U));
}
#pragma endregion

#pragma region PostAndWait_OffLoopThread

TEST_CASE("OwnerThreadDispatcher_PostAndWait_OffLoopThread",
    "[OwnerThreadDispatcher]") {
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

  CHECK((executed) == (1U));
  CHECK((result));
  CHECK((count.load()) == (1));
}
#pragma endregion

#pragma region QueueHighWatermark

TEST_CASE("OwnerThreadDispatcher_QueueHighWatermark",
    "[OwnerThreadDispatcher]") {
  // `queue_high_watermark` reflects the maximum capacity seen.
  OwnerThreadTestDispatcher dispatcher{4};
  CHECK((dispatcher.queue_high_watermark()) >= (4U));
  for (int i{}; i < 8; ++i)
    CHECK((dispatcher.post([]() -> bool { return true; })));
  (void)dispatcher.execute_post_queue();
  CHECK((dispatcher.queue_high_watermark()) >= (8U));
}
#pragma endregion

#pragma region DoubleBuffer

TEST_CASE("OwnerThreadDispatcher_DoubleBuffer", "[OwnerThreadDispatcher]") {
  // Callbacks posted during `execute_post_queue` go into the inactive buffer
  // and are deferred to the next drain.
  OwnerThreadTestDispatcher dispatcher;
  int first{0};
  int second{0};

  CHECK((dispatcher.post([&]() mutable -> bool {
    ++first;
    (void)dispatcher.post([&]() mutable -> bool {
      ++second;
      return true;
    });
    return true;
  })));

  auto count1 = dispatcher.execute_post_queue();
  CHECK((count1) == (1U));
  CHECK((first) == (1));
  CHECK((second) == (0)); // Deferred to next drain.

  auto count2 = dispatcher.execute_post_queue();
  CHECK((count2) == (1U));
  CHECK((second) == (1));
}
#pragma endregion

#pragma region WakeFd

TEST_CASE("OwnerThreadDispatcher_WakeFd", "[OwnerThreadDispatcher]") {
  // `wake_fd` is signaled exactly once when the queue transitions from empty.
  OwnerThreadTestDispatcher dispatcher;

  // No signal before any post.
  CHECK_FALSE((dispatcher.wake_fd().read().has_value()));

  // First post to empty queue signals the fd.
  CHECK((dispatcher.post([]() -> bool { return true; })));
  CHECK((dispatcher.wake_fd().read().has_value()));

  // Second post to a non-empty queue does not re-signal.
  CHECK((dispatcher.post([]() -> bool { return true; })));
  CHECK_FALSE((dispatcher.wake_fd().read().has_value()));

  (void)dispatcher.execute_post_queue();
}
#pragma endregion

#pragma region ExecuteOrPostWithRetry_Success

TEST_CASE("OwnerThreadDispatcher_ExecuteOrPostWithRetry_Success",
    "[OwnerThreadDispatcher]") {
  // On the loop thread, fn succeeds immediately; returns true without posting.
  OwnerThreadTestDispatcher dispatcher;
  int calls{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&calls]() mutable -> bool {
        ++calls;
        return true;
      },
      2);
  CHECK((ok));
  CHECK((calls) == (1));
  CHECK((dispatcher.execute_post_queue()) == (0U));
}
#pragma endregion

#pragma region ExecuteOrPostWithRetry_ExhaustedRetry

TEST_CASE("OwnerThreadDispatcher_ExecuteOrPostWithRetry_ExhaustedRetry",
    "[OwnerThreadDispatcher]") {
  // With retry_count=0 and a fn that always fails, returns false immediately.
  OwnerThreadTestDispatcher dispatcher;
  int calls{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&calls]() mutable -> bool {
        ++calls;
        return false;
      },
      0);
  CHECK_FALSE((ok));
  CHECK((calls) == (1));
  CHECK((dispatcher.execute_post_queue()) == (0U));
}
#pragma endregion

#pragma region ExecuteOrPostWithRetry_Retry

TEST_CASE("OwnerThreadDispatcher_ExecuteOrPostWithRetry_Retry",
    "[OwnerThreadDispatcher]") {
  // fn fails the first time; the retry posted to the queue succeeds.
  OwnerThreadTestDispatcher dispatcher;
  int attempts{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&attempts]() mutable -> bool {
        ++attempts;
        return attempts >= 2;
      },
      2);
  CHECK((ok));
  CHECK((attempts) == (1)); // First call failed; retry was posted.

  (void)dispatcher.execute_post_queue();
  CHECK((attempts) == (2)); // Retry succeeded.
}
#pragma endregion

#pragma region ExecuteOrPostWithRetry_OffLoopThread

TEST_CASE("OwnerThreadDispatcher_ExecuteOrPostWithRetry_OffLoopThread",
    "[OwnerThreadDispatcher]") {
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
  CHECK((calls) == (0)); // Not yet executed.
  (void)dispatcher.execute_post_queue();
  CHECK((calls) == (1));
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
