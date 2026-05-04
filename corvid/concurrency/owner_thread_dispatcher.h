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
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <exception>
#include <functional>
#include <latch>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "../meta/concepts.h"
#include "../filesys/os_file.h"
#include "../filesys/event_fd.h"
#include "../concurrency/relaxed_atomic.h"

namespace corvid { inline namespace concurrency {
inline namespace owner_thread_dispatcherns {

#pragma region owner_thread_dispatcher

// Concept for `owner_thread_dispatcher::posted_fn` lambda in its stored form.
// This is what the invocable in the post queue has to fit.
template<typename FN>
concept StoredPostedInvocable =
    MoveConsumable<FN> && std::is_invocable_r_v<bool, FN>;

// Concept for `owner_thread_dispatcher::posted_fn` lambda as a parameter. This
// is what the parameter to `post` has to fit: see `MoveConsumable` for
// details.
template<typename FN>
concept PostedInvocable = MoveConsumable<FN> && StoredPostedInvocable<FN>;

// Dispatches callbacks to execute only in the owning thread, by using a post
// queue.
//
// The instance imprints on the thread it was created by and only executes
// callbacks in that thread. If a callback is passed from another thread, it is
// instead posted to the queue so that it can be executed by the owning thread
// later. To synchronize, posting a callback signals an `eventfd`.
//
// The post queue may be useful even when running entirely within the loop
// thread, since you can use to defer execution.
//
// This class is designed to work equally well as a parent and as a member
// (whose methods may be re-published). It will also work with any sort of
// callback type, from raw function pointer to `fixed_function` (with the
// exception of `execute_or_post_with_retry` and `post_and_wait`, which require
// the ability to wrap the callback in a lambda and post that, and therefore
// won't work with a raw function  pointer).
//
// NOLINTBEGIN(bugprone-move-forwarding-reference)
template<typename CB = std::function<bool()>>
class owner_thread_dispatcher {
public:
  static_assert(StoredPostedInvocable<CB>,
      "CB must be a PostedInvocable lambda type");

  using posted_fn = CB;
  static constexpr size_t npos = -1;

  // Construct with initial sizes for post queues and default retry count. See
  // `queue_high_watermark` for tuning.
  explicit owner_thread_dispatcher(size_t post_queue_reserve = 32,
      size_t default_retry_count = 100)
      : default_retry_count_{default_retry_count} {
    current_loop_ = this;
    post_queues_[0].reserve(post_queue_reserve);
    post_queues_[1].reserve(post_queue_reserve);
  }

  ~owner_thread_dispatcher() = default;

  // Access `eventfd` to wait on for work in the post queue.
  const auto& wake_fd() const noexcept { return wake_fd_; }

  // True if the calling thread is the active loop thread for this instance.
  [[nodiscard]] bool is_loop_thread() const noexcept {
    return current_loop_ == this;
  }

  // Execute all pending callbacks in the post queue. Returns the number of
  // callbacks executed. There is no reason to call this until after `post`
  // signals the `eventfd`, and it must only be called from the owning thread.
  [[nodiscard]] size_t execute_post_queue() {
    assert(is_loop_thread());

    // Atomically swap between the double-buffered queues.
    post_queue_t* pending;
    if (std::scoped_lock lock{post_mutex_}; true) {
      pending = active_queue_;
      post_queue_t* other =
          (pending == &post_queues_[0]) ? &post_queues_[1] : &post_queues_[0];
      active_queue_ = other;
    }

    size_t count = pending->size();
    for (auto& fn : *pending) fn();
    pending->clear();
    return count;
  }

  // Schedule `cbpost` to run on the loop thread.
  //
  // You will often want to use `execute_or_post` instead, as it executes
  // inline if already on the loop thread, avoiding unnecessary posting
  // overhead.
  [[nodiscard]] bool post(posted_fn&& cbpost) {
    bool was_empty{};
    // In the steady state, this does not require reallocation.
    if (std::scoped_lock lock{post_mutex_}; true) {
      auto& active_queue = **active_queue_;
      was_empty = active_queue.empty();
      active_queue.emplace_back(std::move(cbpost));
    }
    // On transition from empty, signal the `eventfd` to wake the loop thread.
    if (was_empty) return do_wake();

    return true;
  }

  // Execute `fn` immediately if on the loop thread. If it fails, or if we
  // aren't on the loop thread, post it.
  //
  // When it fails while executing from the post queue, it will requeue itself.
  // This only makes sense if the failure is retryable. Even so, we have a
  // finite retry count, which you can configure the default for in the
  // constructor.
  [[nodiscard]] bool execute_or_post_with_retry(PostedInvocable auto&& fn,
      size_t retry_count = npos) {
    if (retry_count == npos) retry_count = default_retry_count_;
    if (is_loop_thread()) {
      if (fn()) return true;
      if (retry_count-- == 0) return false;
    }

    // This will always be executed in the loop thread, where it will run
    // once. It will only be queued again if it fails but there are more
    // retries.
    (void)post([this, retry_count, fn = std::move(fn)]() mutable -> bool {
      return execute_or_post_with_retry(std::move(fn), retry_count);
    });
    return true;
  }

  // Run `fn` fully synchronously on the loop thread. When executed from
  // another thread, posts before blocking the calling thread until it
  // completes.
  //
  // The use cases for this are very limited, but it's helpful for testing.
  [[nodiscard]] bool post_and_wait(PostedInvocable auto&& fn) {
    if (is_loop_thread()) return fn();
    bool result{};
    std::latch done{1};

    if (post([fn = std::move(fn), &result, &done]() -> bool {
          result = fn();
          done.count_down();
          return true;
        }))
      done.wait();

    return result;
  }

  // Returns the current high-watermark for the post queues, which can be used
  // to tune the constructor's `post_queue_reserve` parameter. In practice,
  // vectors grow by doubling and never shrink, so in the steady state, there
  // should be no allocations.
  [[nodiscard]] size_t queue_high_watermark() const noexcept {
    std::scoped_lock lock{post_mutex_};
    return std::max(post_queues_[0].capacity(), post_queues_[1].capacity());
  }

  // Execute `fn` immediately if on the loop thread; otherwise `post` it. Does
  // not retry on failure.
  [[nodiscard]] bool execute_or_post(PostedInvocable auto&& fn) {
    if (is_loop_thread()) return fn();
    return post(std::move(fn));
  }

private:
  using post_queue_t = std::vector<posted_fn>;

  [[nodiscard]] bool do_wake() noexcept { return wake_fd_.notify(); }

private:
  event_fd wake_fd_{0};
  inline static thread_local const owner_thread_dispatcher* current_loop_{};
  mutable std::mutex post_mutex_;
  post_queue_t post_queues_[2];
  relaxed_atomic<post_queue_t*> active_queue_{&post_queues_[0]};
  relaxed_atomic_size_t default_retry_count_{3};
};

// NOLINTEND(bugprone-move-forwarding-reference)

#pragma endregion

}}} // namespace corvid::concurrency::owner_thread_dispatcherns
