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
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <latch>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "../meta/concepts.h"
#include "../meta/fixed_function.h"
#include "../infra/exception_firewalls.h"
#include "../infra/scope_exit.h"
#include "../filesys/os_event.h"
#include "../infra/relaxed_atomic.h"

namespace corvid { inline namespace concurrency {
inline namespace owner_thread_dispatcherns {

#pragma region owner_thread_dispatcher

// Inline capacity of the `fixed_function` that callbacks are stored in, both
// here and in the loops built on this class. Large enough for the captures
// these callbacks carry in practice, which are a handle or two and a small
// payload.
namespace default_fixed_function {
inline constexpr auto capacity = 384UZ;
} // namespace default_fixed_function

// Concept for `owner_thread_dispatcher::posted_fn` lambda in its stored form.
// This is what the invocable in the post queue has to fit.
template<typename FN>
concept StoredPostedInvocable = std::is_invocable_r_v<bool, FN>;

// Concept for `owner_thread_dispatcher::posted_fn` lambda as a parameter. This
// is what a callback handed to `execute_or_post` and its relatives has to fit,
// adding to the stored form only the requirement that it can be moved from:
// see `MoveConsumable` for details.
template<typename FN>
concept PostedInvocable = MoveConsumable<FN> && StoredPostedInvocable<FN>;

// Records which dispatcher, if any, has claimed the current thread.
//
// The claim lives outside the template so that all callback types compete for
// it. As a member of `owner_thread_dispatcher`, it would be a distinct
// `thread_local` per instantiation, letting dispatchers with different
// callback types each claim the same thread. It also carries the
// `enable_shared_from_this` hookup, so a dispatcher needs only this one base.
class owner_thread_claim
    : public std::enable_shared_from_this<owner_thread_claim> {
protected:
  ~owner_thread_claim() = default;

  inline static thread_local const owner_thread_claim* current_loop_{};
};

// Dispatches callbacks to execute only in the owning thread (aka the loop
// thread) by queuing them when they're posted from another thread.
//
// The goal is to provide an inter-thread message-passing primitive. In this
// system, the callback is the message and posting it delivers it to the loop
// thread that this dispatcher is bound to, where it runs in the loop thread's
// context. Posts are non-blocking: the poster never waits for the callback's
// result, so threads stay decoupled.
//
// Within the loop thread, code is effectively single-threaded because only one
// callback runs at a time. As a result, you don't need mutexes to protect
// state owned by that thread.
//
// Concurrency is reintroduced by composition: a callback can fan out the work
// by handing it off to another thread's dispatcher, or placing it in a shared
// work queue, or however else it prefers. Some state should use atomics, not
// so much for correctness as to allow visibility from other threads.
//
// This mechanism is essential for safe and efficient I/O loops, which is the
// initial use case that the class targets.
//
// Requires strict ownership semantics because the class instance is bound to
// the thread that it dispatches into. The expectation is that the thread runs
// a loop that consumes the post queue in a timely manner by executing the
// callbacks.
//
// An instance must be created on the thread it will run in and destructed in
// that same thread. Only one such instance can live on a thread, and the post
// queue must be consumed only from within that thread.
//
// When lambdas are passed to `execute_or_post`, they are only executed
// immediately if already on the loop thread. Otherwise, they are posted to the
// queue, as though `post` had been called, so that they can later be executed
// on that thread.
//
// The post queue may be useful even when running entirely within the loop
// thread, since you can use it to defer execution and break up recursive call
// stacks.
//
// This class is designed to work equally well as a parent and as a member
// (whose methods may be wrapped and re-published). It will also work with any
// sort of callback type, from raw function pointer to `fixed_function`. The
// exception is that if you choose to use `execute_or_post_with_retry` and
// `post_and_wait`, they require the ability to wrap the callback in a lambda
// and therefore won't work with a raw function pointer.
//
// NOLINTBEGIN(bugprone-move-forwarding-reference)
template<typename CB =
             fixed_function<default_fixed_function::capacity, bool()>>
class owner_thread_dispatcher: public owner_thread_claim {
public:
#pragma region Infrastructure

  static_assert(StoredPostedInvocable<CB>,
      "CB must be a StoredPostedInvocable callback type");

  using posted_fn = CB;
  static constexpr size_t npos = -1;

  // Construct with initial sizes for post queues and default retry count.
  //
  // See `queue_high_watermark` for tuning. The constructing thread becomes the
  // loop thread. Throws `std::logic_error` when that thread already has a
  // dispatcher, whatever its callback type, and `std::runtime_error` when the
  // wake event could not be created. A construction that throws for any
  // reason leaves the thread unclaimed.
  explicit owner_thread_dispatcher(size_t post_queue_reserve = 32UZ,
      size_t default_retry_count = npos) {
    if (current_loop_)
      throw std::logic_error{
          "another owner_thread_dispatcher already exists on this thread"};

    if (!wake_event_)
      throw std::runtime_error{
          "owner_thread_dispatcher could not create its wake event"};

    if (default_retry_count != npos)
      default_retry_count_ = default_retry_count;

    post_queues_[0].reserve(post_queue_reserve);
    post_queues_[1].reserve(post_queue_reserve);

    // Don't claim until fully constructed.
    current_loop_ = this;
  }

  // Force shutdown of resources.
  //
  // Idempotent, returning false when the shutdown already happened. May be
  // called from a posted callback, in which case the drain that is running it
  // stops after it returns, discarding the rest of that batch.
  //
  // Throws when called from any thread but the loop thread, which is a caller
  // bug rather than a lost race.
  [[nodiscard]] bool shutdown() {
    if (current_loop_ != this)
      throw std::logic_error{"Wrong owner_thread_dispatcher shut down thread"};

    std::scoped_lock lock(post_mutex_);
    if (!active_queue_.exchange(nullptr)) return false;

    // A drain in progress owns the queue it is walking, so leave that one to
    // it. Clearing it here would destroy the callback that is executing.
    for (auto& queue : post_queues_)
      if (&queue != draining_queue_) queue.clear();

    return true;
  }

  ~owner_thread_dispatcher() {
    try_or_terminate([&] {
      if (current_loop_ != this)
        throw std::logic_error{
            "owner_thread_dispatcher destructed on wrong thread"};
      (void)shutdown();
      current_loop_ = nullptr;
    });
  }

#pragma endregion
#pragma region Accessors

  // True if the calling thread is the active loop thread for this instance.
  [[nodiscard]] bool is_loop_thread() const noexcept {
    return current_loop_ == this;
  }

#pragma endregion
#pragma region Post

  // Schedule `cbpost` to run on the loop thread.
  //
  // You will often want to use `execute_or_post` instead, as it executes
  // inline if already on the loop thread, avoiding unnecessary posting
  // overhead.
  [[nodiscard]] bool post(posted_fn&& cbpost) {
    bool was_empty{};
    // In the steady state, this does not require reallocation.
    if (std::scoped_lock lock(post_mutex_); true) {
      auto active_queue_ptr = *active_queue_;
      if (!active_queue_ptr) return false;
      auto& active_queue = *active_queue_ptr;
      was_empty = active_queue.empty();
      active_queue.emplace_back(std::move(cbpost));
    }
    // On transition from empty, signal the wake event to wake the loop
    // thread.
    if (was_empty) (void)wake_post_queue();

    return true;
  }

#pragma endregion
#pragma region Execute or post

  // Execute `fn` immediately if on the loop thread; otherwise `post` it. Does
  // not retry on failure.
  [[nodiscard]] bool execute_or_post(PostedInvocable auto&& fn) {
    if (is_loop_thread()) return fn();
    return post(std::move(fn));
  }

#pragma endregion
#pragma region Execute or post with retry

  // Execute `fn` immediately if on the loop thread. If it fails, or if we
  // aren't on the loop thread, post it.
  //
  // When it fails while executing from the post queue, it will requeue itself.
  // This only makes sense if the failure is retryable. Even so, we have a
  // finite retry count, which you can configure the default for in the
  // constructor.
  //
  // Returns true when `fn` succeeded inline or was queued for a later
  // attempt, and false when it failed inline with no retries left or the post
  // was refused, as it is after shutdown.
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
    return post([this, retry_count, fn = std::move(fn)]() mutable -> bool {
      return execute_or_post_with_retry(std::move(fn), retry_count);
    });
  }

#pragma endregion
#pragma region Post and wait

  // Run `fn` fully synchronously on the loop thread. When executed from
  // another thread, posts before blocking the calling thread until it
  // completes.
  //
  // Returns false without running `fn` when the post cannot be queued, and
  // also when a shutdown discards the post before the loop thread reaches it.
  // Otherwise the wait is unbounded, so the loop thread has to be consuming
  // its post queue.
  //
  // The use cases for this are very limited, but it's helpful for testing.
  [[nodiscard]] bool post_and_wait(PostedInvocable auto&& fn) {
    if (is_loop_thread()) return fn();
    bool result{};
    std::latch done{1};

    // The guard rides along in the callback, ending the wait when the
    // callback is destroyed. That covers being discarded by a shutdown as
    // well as having run, at the cost of waking no earlier than the end of
    // the drain that ran it.
    //
    // Copying the guard throws, so the queue must never copy the callback.
    // Growth uses `move_if_noexcept`, which reaches for the copy only when
    // the move can throw.
    static_assert(std::is_nothrow_move_constructible_v<posted_fn> ||
                      !std::is_copy_constructible_v<posted_fn>,
        "posted_fn must be nothrow-movable, or the post queue would copy "
        "callbacks when it grows");

    if (post([fn = std::move(fn), &result, guard = scope_exit{[&done] {
          done.count_down();
        }}]() mutable -> bool {
          result = fn();
          return true;
        }))
      done.wait();

    return result;
  }

#pragma endregion

  // Returns the current high-watermark for the post queues, which can be used
  // to tune the constructor's `post_queue_reserve` parameter
  //
  // In practice, vectors grow by doubling and never shrink, so in the steady
  // state, there should be no allocations.
  [[nodiscard]] size_t queue_high_watermark() const noexcept {
    std::scoped_lock lock(post_mutex_);
    return std::max(post_queues_[0].capacity(), post_queues_[1].capacity());
  }

#pragma region Loop integration

  // Access the wake event to wait on for work in the post queue.
  const auto& wake_event() const noexcept { return wake_event_; }

  // Signal the wake event to wake the loop thread.
  [[nodiscard]] bool wake_post_queue() noexcept {
    return wake_event_.notify();
  }

  // Execute all pending callbacks in the post queue.
  //
  // Returns the number of callbacks executed. There is no reason to call this
  // until after `post` signals the wake event, and it must only be called
  // from the owning thread.
  //
  // A callback must not call this again: a nested drain would take over the
  // queue this one is walking. Doing so asserts, and returns zero without
  // draining anything.
  //
  // A callback may call `shutdown`, which ends the drain after that callback
  // returns. The rest of the batch is discarded and counts as unexecuted. This
  // is intentional: we want to fail on an exception in a callback function.
  //
  // NOLINTNEXTLINE(bugprone-exception-escape)
  [[nodiscard]] size_t execute_post_queue() noexcept {
    assert(is_loop_thread());
    assert(!draining_queue_);
    if (draining_queue_) return 0;

    // Atomically swap between the double-buffered queues.
    post_queue_t* pending{};
    if (std::scoped_lock lock(post_mutex_); true) {
      pending = active_queue_;
      if (!pending) return 0; // shutdown has been called.
      auto* other =
          (pending == &post_queues_[0]) ? &post_queues_[1] : &post_queues_[0];
      active_queue_ = other;
      draining_queue_ = pending;
    }

    // Note that this method is marked `noexcept`, so if any callback throws,
    // we crash. This is because we have no reasonable alternative.
    size_t count{};
    for (auto& fn : *pending) {
      if (!active_queue_) break; // A callback shut us down.
      fn();
      ++count;
    }

    // Clear before releasing the claim, so that a destructor reaching
    // `shutdown` still finds this queue spoken for.
    pending->clear();
    draining_queue_ = nullptr;
    return count;
  }

#pragma endregion
private:
  using post_queue_t = std::vector<posted_fn>;

#pragma region Data members

  // Summary:
  //
  // `wake_event_` is used to signal that `active_queue_` has work to do.
  //
  // `post_mutex_` protects the `post_queues_`.
  //
  // `active_queue_` is the queue that is currently being filled by `post`.
  //
  // `draining_queue_` is the queue that is currently being drained by
  // `execute_post_queue`, only while it's being drained.
  //
  // `default_retry_count_` is the default number of times
  // `execute_or_post_with_retry` will retry a failed callback.

  os_event wake_event_{os_event::create()};
  mutable std::mutex post_mutex_;
  post_queue_t post_queues_[2];
  relaxed_atomic<post_queue_t*> active_queue_{&post_queues_[0]};
  post_queue_t* draining_queue_{};
  relaxed_atomic_size_t default_retry_count_{3};

#pragma endregion
};

// NOLINTEND(bugprone-move-forwarding-reference)

#pragma endregion

}}} // namespace corvid::concurrency::owner_thread_dispatcherns
