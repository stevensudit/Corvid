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
#include <chrono>
#include <memory>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <thread>

#include "corvid/concurrency.h"
#include "corvid/meta/invoke/fixed_function.h"
#include "catch2_main.h"

using namespace corvid;
using namespace std::chrono_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region IsLoopThread

TEST_CASE("IsLoopThread", "[OwnerThreadDispatcher]") {
  // Constructor thread is the loop thread; other threads are not.
  owner_thread_dispatcher<> dispatcher;
  CHECK(dispatcher.is_loop_thread());

  relaxed_atomic_bool other_is_loop{true};
  std::thread t{[&] { other_is_loop = dispatcher.is_loop_thread(); }};
  t.join();
  CHECK_FALSE(other_is_loop);
}

#pragma endregion
#pragma region OneInstancePerThread

TEST_CASE("OneInstancePerThread", "[OwnerThreadDispatcher]") {
  // The thread is claimed by the dispatcher, not by the callback type, so a
  // second one is refused even when it stores its callbacks differently.
  using fixed_dispatcher = owner_thread_dispatcher<fixed_function<bool(), 64>>;
  owner_thread_dispatcher<> dispatcher;
  CHECK_THROWS_AS(owner_thread_dispatcher<>{}, std::logic_error);
  CHECK_THROWS_AS(fixed_dispatcher{}, std::logic_error);

  // The failed claims left the original one intact.
  CHECK(dispatcher.is_loop_thread());
}

#pragma endregion
#pragma region ThrowingConstructorLeavesThreadUnclaimed

TEST_CASE("ThrowingConstructorLeavesThreadUnclaimed",
    "[OwnerThreadDispatcher]") {
  // A constructor that throws never runs its destructor, so it must not have
  // claimed the thread yet. Reserving past `max_size` is the cheap way to make
  // it throw, since that is checked before anything is allocated.
  //
  // This runs on its own thread so that a regression fails here instead of
  // stranding the claim on the main thread, where every later case would
  // inherit the failure.
  using dispatcher_t = owner_thread_dispatcher<>;
  relaxed_atomic_bool refused{false};
  relaxed_atomic_bool stranded{false};
  relaxed_atomic_bool reclaimed{false};

  std::thread t{[&] {
    try {
      dispatcher_t doomed{dispatcher_t::npos};
    }
    catch (const std::length_error&) {
      refused = true;
    }

    // The thread is still available to the next dispatcher.
    try {
      dispatcher_t dispatcher;
      reclaimed = dispatcher.is_loop_thread();
    }
    catch (const std::logic_error&) {
      stranded = true;
    }
  }};
  t.join();

  CHECK(refused);
  CHECK_FALSE(stranded);
  CHECK(reclaimed);
}

#pragma endregion
#pragma region PostAndExecute

TEST_CASE("PostAndExecute", "[OwnerThreadDispatcher]") {
  // `post` queues callbacks; `execute_post_queue` drains and returns count.
  owner_thread_dispatcher<> dispatcher;
  int count{0};
  CHECK(dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  }));
  CHECK(dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  }));
  CHECK(dispatcher.post([&count]() mutable -> bool {
    ++count;
    return true;
  }));
  auto executed = dispatcher.execute_post_queue();
  CHECK(executed == 3U);
  CHECK(count == 3);
}

#pragma endregion
#pragma region MoveOnlyCallback

TEST_CASE("MoveOnlyCallback", "[OwnerThreadDispatcher]") {
  // A callback may own what it captures. The default callback type stores
  // move-only lambdas, which a `std::function` could not.
  owner_thread_dispatcher<> dispatcher;
  int value{0};

  // Posted outside the CHECK, which would move the capture inside a macro
  // that the analyzer reads as looping.
  auto owned = std::make_unique<int>(42);
  const auto posted = dispatcher.post([&value, owned = std::move(owned)] {
    value = *owned;
    return true;
  });
  CHECK(posted);

  CHECK(dispatcher.execute_post_queue() == 1U);
  CHECK(value == 42);
}

#pragma endregion
#pragma region ExecutePostQueue_Empty

TEST_CASE("ExecutePostQueue_Empty", "[OwnerThreadDispatcher]") {
  // Empty queue returns 0 and does not crash.
  owner_thread_dispatcher<> dispatcher;
  CHECK(dispatcher.execute_post_queue() == 0U);
}

#pragma endregion
#pragma region ExecuteOrPost_OnLoopThread

TEST_CASE("ExecuteOrPost_OnLoopThread", "[OwnerThreadDispatcher]") {
  // On the loop thread `execute_or_post` runs inline without queuing.
  owner_thread_dispatcher<> dispatcher;
  int count{0};
  auto ok = dispatcher.execute_or_post([&count]() -> bool {
    ++count;
    return true;
  });
  CHECK(ok);
  CHECK(count == 1);
  CHECK(dispatcher.execute_post_queue() == 0U);
}

#pragma endregion
#pragma region ExecuteOrPost_OffLoopThread

TEST_CASE("ExecuteOrPost_OffLoopThread", "[OwnerThreadDispatcher]") {
  // From a non-loop thread `execute_or_post` posts without executing inline.
  owner_thread_dispatcher<> dispatcher;
  relaxed_atomic_int count{0};
  std::thread t{[&] {
    (void)dispatcher.execute_or_post([&count]() -> bool {
      ++count;
      return true;
    });
  }};
  t.join();
  CHECK(count == 0); // Not yet executed.
  auto executed = dispatcher.execute_post_queue();
  CHECK(executed == 1U);
  CHECK(count == 1);
}

#pragma endregion
#pragma region PostAndWait_OnLoopThread

TEST_CASE("PostAndWait_OnLoopThread", "[OwnerThreadDispatcher]") {
  // On the loop thread `post_and_wait` executes the callback directly.
  owner_thread_dispatcher<> dispatcher;
  int count{0};
  auto ok = dispatcher.post_and_wait([&count]() -> bool {
    ++count;
    return true;
  });
  CHECK(ok);
  CHECK(count == 1);
  CHECK(dispatcher.execute_post_queue() == 0U);
}

#pragma endregion
#pragma region PostAndWait_OffLoopThread

TEST_CASE("PostAndWait_OffLoopThread", "[OwnerThreadDispatcher]") {
  // From a non-loop thread `post_and_wait` blocks until the loop thread
  // drains the queue.
  owner_thread_dispatcher<> dispatcher;
  bool result{false};
  std::atomic<int> count{0};

  std::thread t{[&] {
    result = dispatcher.post_and_wait([&count]() -> bool {
      ++count;
      return true;
    });
  }};

  // Spin until the posted item signals the eventfd.
  os_event::counter_t val{};
  while (!dispatcher.wake_event().read(val)) std::this_thread::yield();

  auto executed = dispatcher.execute_post_queue();
  t.join();

  CHECK(executed == 1U);
  CHECK(result);
  CHECK(count.load() == 1);
}

#pragma endregion
#pragma region PostAndWait_ShutdownReleasesWaiter

TEST_CASE("PostAndWait_ShutdownReleasesWaiter", "[OwnerThreadDispatcher]") {
  // A shutdown discards the queued callback without running it, and the
  // waiter is released anyway, reporting failure.
  //
  // A regression strands the waiter thread permanently, so the state that
  // thread reports through is heap-owned: the timeout below reports a failure
  // and abandons it, rather than hanging the suite on a join that can never
  // finish.
  struct waiter_state {
    relaxed_atomic_bool result{true};
    relaxed_atomic_int count{0};
    std::binary_semaphore finished{0};
  };
  auto state = std::make_unique<waiter_state>();
  owner_thread_dispatcher<> dispatcher;

  std::thread t{[&dispatcher, s = state.get()] {
    s->result = dispatcher.post_and_wait([s]() -> bool {
      ++s->count;
      return true;
    });
    s->finished.release();
  }};

  // Spin until the callback is queued, which the wake signal proves because
  // `post` raises it only after adding to the queue.
  os_event::counter_t val{};
  while (!dispatcher.wake_event().read(val)) std::this_thread::yield();

  CHECK(dispatcher.shutdown());

  // Wildly generous: the whole suite runs in well under a second.
  const auto released = state->finished.try_acquire_for(20s);
  CHECK(released);
  if (!released) {
    // A stranded waiter reads nothing but `state` on the way out, so leaking
    // that is enough. The dispatcher is left to die normally here, which
    // keeps this thread usable by the rest of the file.
    t.detach();
    [[maybe_unused]] auto* leaked = state.release();
    return;
  }

  t.join();
  CHECK_FALSE(state->result);
  CHECK(state->count == 0);
}

#pragma endregion
#pragma region QueueHighWatermark

TEST_CASE("QueueHighWatermark", "[OwnerThreadDispatcher]") {
  // `queue_high_watermark` reflects the maximum capacity seen.
  owner_thread_dispatcher<> dispatcher{4};
  CHECK(dispatcher.queue_high_watermark() >= 4U);
  for (auto ndx = 0; ndx < 8; ++ndx)
    CHECK(dispatcher.post([]() -> bool { return true; }));
  (void)dispatcher.execute_post_queue();
  CHECK(dispatcher.queue_high_watermark() >= 8U);
}

#pragma endregion
#pragma region DoubleBuffer

TEST_CASE("DoubleBuffer", "[OwnerThreadDispatcher]") {
  // Callbacks posted during `execute_post_queue` go into the inactive buffer
  // and are deferred to the next drain.
  owner_thread_dispatcher<> dispatcher;
  int first{0};
  int second{0};

  CHECK(dispatcher.post([&]() mutable -> bool {
    ++first;
    (void)dispatcher.post([&]() mutable -> bool {
      ++second;
      return true;
    });
    return true;
  }));

  auto count1 = dispatcher.execute_post_queue();
  CHECK(count1 == 1U);
  CHECK(first == 1);
  CHECK(second == 0); // Deferred to next drain.

  auto count2 = dispatcher.execute_post_queue();
  CHECK(count2 == 1U);
  CHECK(second == 1);
}

#pragma endregion
#pragma region ShutdownFailureModes

TEST_CASE("ShutdownFailureModes", "[OwnerThreadDispatcher]") {
  // Losing the race to whoever shut it down first is a false return. Calling
  // from off the loop thread is a caller bug, and says so.
  owner_thread_dispatcher<> dispatcher;
  CHECK(dispatcher.shutdown());
  CHECK_FALSE(dispatcher.shutdown());

  relaxed_atomic_bool refused{false};
  std::thread t{[&] {
    try {
      (void)dispatcher.shutdown();
    }
    catch (const std::logic_error&) {
      refused = true;
    }
  }};
  t.join();
  CHECK(refused);
}

#pragma endregion
#pragma region Retire

TEST_CASE("Retire", "[OwnerThreadDispatcher]") {
  // Retiring on the loop thread shuts down, releases the thread claim, and
  // waives the wrong-thread destruction guard.
  auto dispatcher = std::make_unique<owner_thread_dispatcher<>>();
  CHECK(dispatcher->is_loop_thread());

  // A foreign thread cannot retire a live dispatcher: the attempt throws
  // (the `shutdown` contract) and leaves the claim and queue intact.
  relaxed_atomic_bool foreign_threw{false};
  std::thread rejected{[&] {
    try {
      (void)dispatcher->retire();
    }
    catch (const std::logic_error&) {
      foreign_threw = true;
    }
  }};
  rejected.join();
  CHECK(foreign_threw);
  CHECK(dispatcher->is_loop_thread());
  CHECK(dispatcher->post([] { return true; }));
  CHECK(dispatcher->execute_post_queue() == 1U);

  CHECK(dispatcher->retire());
  CHECK_FALSE(dispatcher->retire()); // idempotent
  CHECK_FALSE(dispatcher->is_loop_thread());
  CHECK_FALSE(dispatcher->post([] { return true; }));

  // The thread is unclaimed again, so a new dispatcher can bind here while
  // the retired one is still alive.
  owner_thread_dispatcher<> replacement;
  CHECK(replacement.is_loop_thread());

  // After retirement the idempotent early-out wins from any thread: a
  // foreign `retire` returns false without throwing. The last owner may then
  // destroy the retired dispatcher from any thread.
  relaxed_atomic_bool foreign_after_retire{false};
  std::thread t{[&, dispatcher = std::move(dispatcher)]() mutable {
    foreign_after_retire = !dispatcher->retire();
    dispatcher.reset();
  }};
  t.join();
  CHECK(foreign_after_retire);
}

#pragma endregion
#pragma region ShutdownFromCallback

TEST_CASE("ShutdownFromCallback", "[OwnerThreadDispatcher]") {
  // A callback may shut the dispatcher down. The drain running it stops
  // there, so the rest of the batch neither runs nor counts, and the queue it
  // was walking survives long enough for that callback to return.
  owner_thread_dispatcher<> dispatcher;
  int before{0};
  int after{0};
  std::string tag;

  CHECK(dispatcher.post([&before]() -> bool {
    ++before;
    return true;
  }));

  // The owned string is read back after the shutdown, so that a shutdown
  // which destroyed this callback mid-flight would be a use-after-free rather
  // than something that happens to work. It is long enough to be heap
  // allocated rather than held inline.
  CHECK(dispatcher.post(
      [&dispatcher, &tag, owned = std::string(64, 'x')]() -> bool {
        const auto ok = dispatcher.shutdown();
        tag = owned;
        return ok;
      }));

  CHECK(dispatcher.post([&after]() -> bool {
    ++after;
    return true;
  }));

  CHECK(dispatcher.execute_post_queue() == 2U);
  CHECK(before == 1);
  CHECK(after == 0);
  CHECK(tag == std::string(64, 'x'));

  // Shutdown is complete: nothing more can be queued or drained.
  CHECK_FALSE(dispatcher.post([]() -> bool { return true; }));
  CHECK(dispatcher.execute_post_queue() == 0U);
}

// Draining from inside a callback asserts, so it cannot be probed here. The
// call would be:
//
//   (void)dispatcher.post(
//       [&dispatcher]() -> bool { return dispatcher.execute_post_queue(); });

#pragma endregion
#pragma region WakeEvent

TEST_CASE("WakeEvent", "[OwnerThreadDispatcher]") {
  // `wake_event` is signaled exactly once when the queue transitions from
  // empty.
  owner_thread_dispatcher<> dispatcher;

  // No signal before any post.
  CHECK_FALSE(dispatcher.wake_event().read().has_value());

  // First post to empty queue signals the event.
  CHECK(dispatcher.post([]() -> bool { return true; }));
  CHECK(dispatcher.wake_event().read().has_value());

  // Second post to a non-empty queue does not re-signal.
  CHECK(dispatcher.post([]() -> bool { return true; }));
  CHECK_FALSE(dispatcher.wake_event().read().has_value());

  (void)dispatcher.execute_post_queue();
}

#pragma endregion
#pragma region ExecuteOrPostWithRetry_Success

TEST_CASE("ExecuteOrPostWithRetry_Success", "[OwnerThreadDispatcher]") {
  // On the loop thread, fn succeeds immediately; returns true without posting.
  owner_thread_dispatcher<> dispatcher;
  int calls{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&calls]() mutable -> bool {
        ++calls;
        return true;
      },
      2);
  CHECK(ok);
  CHECK(calls == 1);
  CHECK(dispatcher.execute_post_queue() == 0U);
}

#pragma endregion
#pragma region ExecuteOrPostWithRetry_ExhaustedRetry

TEST_CASE("ExecuteOrPostWithRetry_ExhaustedRetry", "[OwnerThreadDispatcher]") {
  // With retry_count=0 and a fn that always fails, returns false immediately.
  owner_thread_dispatcher<> dispatcher;
  int calls{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&calls]() mutable -> bool {
        ++calls;
        return false;
      },
      0);
  CHECK_FALSE(ok);
  CHECK(calls == 1);
  CHECK(dispatcher.execute_post_queue() == 0U);
}

#pragma endregion
#pragma region ExecuteOrPostWithRetry_Retry

TEST_CASE("ExecuteOrPostWithRetry_Retry", "[OwnerThreadDispatcher]") {
  // fn fails the first time; the retry posted to the queue succeeds.
  owner_thread_dispatcher<> dispatcher;
  int attempts{0};
  auto ok = dispatcher.execute_or_post_with_retry(
      [&attempts]() mutable -> bool {
        ++attempts;
        return attempts >= 2;
      },
      2);
  CHECK(ok);
  CHECK(attempts == 1); // First call failed; retry was posted.

  (void)dispatcher.execute_post_queue();
  CHECK(attempts == 2); // Retry succeeded.
}

#pragma endregion
#pragma region ExecuteOrPostWithRetry_OffLoopThread

TEST_CASE("ExecuteOrPostWithRetry_OffLoopThread", "[OwnerThreadDispatcher]") {
  // From a non-loop thread, fn is never called inline; it is always posted.
  owner_thread_dispatcher<> dispatcher;
  relaxed_atomic_int calls{0};
  std::thread t{[&] {
    (void)dispatcher.execute_or_post_with_retry([&calls]() -> bool {
      ++calls;
      return true;
    });
  }};
  t.join();
  CHECK(calls == 0); // Not yet executed.
  (void)dispatcher.execute_post_queue();
  CHECK(calls == 1);
}

#pragma endregion
#pragma region ExecuteOrPostWithRetry_AfterShutdown

TEST_CASE("ExecuteOrPostWithRetry_AfterShutdown", "[OwnerThreadDispatcher]") {
  // After shutdown, nothing can be queued, so a callback that needs the queue
  // fails instead of reporting a success that never happens.
  owner_thread_dispatcher<> dispatcher;
  CHECK(dispatcher.shutdown());

  // Inline execution still works, since it doesn't touch the queue.
  int calls{0};
  CHECK(dispatcher.execute_or_post_with_retry([&calls] {
    ++calls;
    return true;
  }));
  CHECK(calls == 1);

  // The retry has nowhere to go, even though retries remain.
  calls = 0;
  CHECK_FALSE(dispatcher.execute_or_post_with_retry(
      [&calls] {
        ++calls;
        return false;
      },
      2));
  CHECK(calls == 1);

  // Off the loop thread, there is nothing but the queue.
  relaxed_atomic_int off_thread_calls{0};
  relaxed_atomic_bool ok{true};
  std::thread t{[&] {
    ok = dispatcher.execute_or_post_with_retry([&off_thread_calls] {
      ++off_thread_calls;
      return true;
    });
  }};
  t.join();
  CHECK_FALSE(ok);
  CHECK(off_thread_calls == 0);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
