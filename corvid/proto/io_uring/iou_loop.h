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
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../../concurrency/jthread_stoppable_sleep.h"
#include "../../concurrency/notifiable.h"
#include "../../containers/scope_exit.h"
#include "../../containers/scoped_value.h"
#include "iou_wrap.h"

namespace corvid { inline namespace proto { inline namespace iouring {
using namespace std::chrono_literals;

// Completion-based I/O loop built on io_uring.
//
// Drive the loop with `run()` (blocking, on the loop thread) or `run_once()`
// (one batch, useful for testing). Both drain the post queue before waiting.
//
// `post` is the cross-thread entry point: any thread can push a callback onto
// the queue, and the loop will execute it at the top of the next `run_once`.
// The loop is woken by a `IORING_OP_POLL_ADD` armed on an internal `eventfd`,
// so the wakeup signal travels through the same CQE path as every other
// completion, with no side channels. `post_and_wait` is a synchronous variant
// that blocks the caller until the callback completes.
//
// `execute_or_post` executes inline on the loop thread or posts otherwise.
//
// `submit_*` methods enqueue I/O operations paired with a `completion_fn`
// callback. They must only be called from the loop thread (directly or via
// `post`/`execute_or_post`), since the SQ ring is not thread-safe.
//
// `stop()` is safe to call from any thread.
//
// `iou_loop` is non-copyable and non-movable: the ring contains kernel-mapped
// memory with self-referential pointers.
class iou_loop: std::enable_shared_from_this<iou_loop> {
  enum class allow : bool { ctor };

public:
  using duration_t = std::chrono::milliseconds;

  static constexpr duration_t default_run_once_timeout{10};
  static constexpr duration_t default_post_and_wait_poll_interval{100};

  // Callback invoked when an op completes.
  using completion_fn = std::function<bool(iou_res)>;

  // Callback scheduled via `post` to run on the loop thread.
  using posted_fn_t = std::function<bool()>;

  // Construct a loop with `ring_size` SQE slots (must be a power of two).
  // Throws `std::system_error` if the ring or wakeup `eventfd` cannot be
  // created.
  explicit iou_loop(allow, unsigned ring_size = 256,
      duration_t post_and_wait_poll_interval =
          default_post_and_wait_poll_interval)
      : ring_{ring_size}, wake_fd_{event_fd::create()},
        post_and_wait_poll_interval_{post_and_wait_poll_interval} {}

  ~iou_loop() = default;

  iou_loop(const iou_loop&) = delete;
  iou_loop& operator=(const iou_loop&) = delete;
  iou_loop(iou_loop&&) = delete;
  iou_loop& operator=(iou_loop&&) = delete;

  // Create a heap-allocated `iou_loop` managed by `std::shared_ptr`.
  [[nodiscard]] static std::shared_ptr<iou_loop> make(unsigned ring_size = 256,
      duration_t post_and_wait_poll_interval =
          default_post_and_wait_poll_interval) {
    return std::make_shared<iou_loop>(allow::ctor, ring_size,
        post_and_wait_poll_interval);
  }

  // True if the calling thread is the active loop thread for this instance.
  [[nodiscard]] bool is_loop_thread() const noexcept {
    return current_loop_ == this;
  }

  // Returns a RAII guard that designates the calling thread as the loop thread
  // for its lifetime. `run()` does this internally; call it manually before
  // using `run_once()` directly (e.g., in tests).
  [[nodiscard]] auto poll_thread_scope() const {
    return scoped_value<const iou_loop*>{current_loop_, this};
  }

  // Schedule `fn` to run on the loop thread at the top of the next
  // `run_once`. Safe to call from any thread. Writes to the internal
  // `eventfd` to wake the loop if it is blocked waiting for CQEs.
  [[nodiscard]] bool post(posted_fn_t fn) {
    {
      std::scoped_lock lock{post_mutex_};
      active_queue_.load(std::memory_order_relaxed)->push_back(std::move(fn));
    }
    return do_wake();
  }

  // Execute `fn` immediately if on the loop thread; otherwise `post` it.
  template<typename FN>
  [[nodiscard]] bool execute_or_post(FN&& fn) {
    if (is_loop_thread()) {
      fn();
      return true;
    }
    return post(std::forward<FN>(fn));
  }

  // Post `fn` to run on the loop thread and block the calling thread until
  // it completes. Returns false if the loop is not running or the post fails.
  // Executes inline and returns true if called from the loop thread.
  template<typename FN>
  [[nodiscard]] bool post_and_wait(FN&& fn) {
    if (is_loop_thread()) {
      fn();
      return true;
    }
    if (!running_.get()) return false;

    using fn_type = std::decay_t<FN>;
    struct wait_state {
      notifiable<bool> done{false};
      fn_type fn;
      explicit wait_state(fn_type&& f) : fn(std::move(f)) {}
    };
    auto waiter = std::make_shared<wait_state>(std::forward<FN>(fn));
    if (!post([waiter] {
          waiter->fn();
          waiter->done.notify_one(true);
          return true;
        }))
      return false;
    while (true) {
      if (waiter->done.wait_for_value(post_and_wait_poll_interval_, true))
        return true;
      if (!running_.get()) return false;
    }
  }

  // Block until `run()` is active. Returns false on timeout.
  // Pass -1 (the default) to wait up to 60 seconds.
  [[nodiscard]] bool wait_until_running(int timeout_ms = -1) {
    if (timeout_ms < 0) timeout_ms = 60000;
    return running_.wait_for_value(duration_t{timeout_ms}, true);
  }

  // Run the loop on the calling thread until `stop()` is called.
  void run() {
    stop_.store(false, std::memory_order_relaxed);
    const auto scope = poll_thread_scope();
    running_.notify(true);
    scope_exit on_exit{[&] { running_.notify(false); }};
    while (!stop_.load(std::memory_order_acquire))
      (void)run_once(default_run_once_timeout);
  }

  // Process one batch of completions, waiting up to `timeout`. Drains the post
  // queue first, then arms the wake poll if needed. Returns the number of user
  // completions dispatched (internal wakeups excluded). Returns 0 on timeout
  // or signal. Must be called on the loop thread.
  [[nodiscard]] size_t run_once(duration_t timeout = {}) {
    assert(is_loop_thread());
    do_drain_post_queue();
    ensure_wake_poll_armed();

    auto res = ring_.wait_cqe_timeout(timeout);
    if (res.is_soft_error()) return 0;
    res.throw_if_error("wait_cqe_timeout");

    size_t dispatched{};
    size_t total = ring_.for_each_cqe([&](iou_cqe cqe) {
      if (cqe.get_data() == &wake_tag_) {
        (void)wake_fd_.read();
        wake_poll_armed_ = false;
      } else {
        do_dispatch(cqe);
        ++dispatched;
      }
      return true;
    });
    return dispatched;
  }

  // Signal the loop to exit after the current `run_once` returns. Writes to
  // the `eventfd` to interrupt a sleeping wait. Safe to call from any thread.
  void stop() noexcept {
    stop_.store(true, std::memory_order_release);
    (void)do_wake();
  }

  // Submit a no-op that completes immediately with `res` == 0. Useful for
  // verifying ring plumbing. Returns false if the SQ is full.
  [[nodiscard]] bool submit_nop(completion_fn cb) {
    return do_submit([](iou_sqe sqe) { sqe.prep_nop(); }, std::move(cb));
  }

private:
  // Get an SQE, prepare it via `prep`, attach `cb` as user data, and submit.
  // Returns false if the SQ is full.
  template<typename Prep>
  [[nodiscard]] bool do_submit(Prep prep, completion_fn cb) {
    auto sqe = ring_.get_sqe();
    if (!sqe) return false;
    prep(sqe);
    sqe.set_data(new completion_fn(std::move(cb)));
    return ring_.submit().ok(1);
  }

  static bool do_dispatch(iou_cqe cqe) noexcept {
    bool ok{};
    auto* cb = cqe.get_data<completion_fn>();
    if (cb) {
      ok = (*cb)(iou_res{cqe.res()});
      delete cb;
    }
    return ok;
  }

  // Atomically swap out the active post queue, then execute and clear it
  // without holding the lock. Callbacks posted from within a drained callback
  // land in the newly active queue and run on the next iteration.
  void do_drain_post_queue() {
    assert(is_loop_thread());
    post_queue_t* pending;
    {
      std::scoped_lock lock{post_mutex_};
      pending = active_queue_.load(std::memory_order_relaxed);
      post_queue_t* other =
          (pending == &post_queues_[0]) ? &post_queues_[1] : &post_queues_[0];
      active_queue_.store(other, std::memory_order_relaxed);
    }
    for (auto& fn : *pending) fn();
    pending->clear();
  }

  // Submit a one-shot `IORING_OP_POLL_ADD` for the wakeup `eventfd`. When
  // `post` or `stop` writes to the `eventfd`, this poll fires as a CQE and
  // interrupts `io_uring_wait_cqe_timeout`. The poll is one-shot and is
  // rearmed after each wakeup. If the SQ is full, `wake_poll_armed_` stays
  // false; the 10ms fallback timeout in `run()` covers this case, and the
  // arm is retried on the next `run_once` iteration.
  void ensure_wake_poll_armed() {
    if (wake_poll_armed_) return;
    auto sqe = ring_.get_sqe();
    if (!sqe) return;
    sqe.prep_poll_oneshot(wake_fd_.handle(), POLLIN);
    sqe.set_data(&wake_tag_);
    if (ring_.submit().ok(1)) wake_poll_armed_ = true;
  }

  [[nodiscard]] bool do_wake() noexcept { return wake_fd_.notify(); }

  // Members are declared in initialization order.

  iou_ring ring_;
  event_fd wake_fd_;

  // `&wake_tag_` is the SQE user-data sentinel for the wake-poll CQE,
  // distinct from any heap-allocated `completion_fn*`.
  char wake_tag_{};
  bool wake_poll_armed_{false};

  inline static thread_local const iou_loop* current_loop_{nullptr};

  std::atomic_bool stop_{false};
  notifiable<std::atomic_bool> running_{false};

  std::mutex post_mutex_;
  using post_queue_t = std::vector<posted_fn_t>;
  post_queue_t post_queues_[2];
  std::atomic<post_queue_t*> active_queue_{&post_queues_[0]};

  const std::chrono::milliseconds post_and_wait_poll_interval_;
};

// Runs an `iou_loop` in its own background thread.
//
// Shutdown ordering: call `stop` (or destroy the runner) before destroying
// any object that a pending `post` callback might reference.
class iou_loop_runner {
public:
  explicit iou_loop_runner(unsigned ring_size = 256,
      std::chrono::milliseconds post_and_wait_poll_interval =
          iou_loop::default_post_and_wait_poll_interval)
      : loop_{iou_loop::make(ring_size, post_and_wait_poll_interval)},
        thread_{[this](const std::stop_token& st) { run(st); }} {
    if (!loop_->wait_until_running(1000)) {
      thread_.request_stop();
      throw std::runtime_error("iou_loop_runner failed to start");
    }
  }

  ~iou_loop_runner() {
    thread_.request_stop();
    if (!thread_.joinable()) return;
    // If the last shared_ptr owner is a callback on the loop thread, the
    // runner's destructor may run on that thread. Detach to avoid self-join.
    if (thread_.get_id() == std::this_thread::get_id())
      thread_.detach();
    else
      thread_.join();
  }

  iou_loop_runner(const iou_loop_runner&) = delete;
  iou_loop_runner& operator=(const iou_loop_runner&) = delete;
  iou_loop_runner(iou_loop_runner&&) = delete;
  iou_loop_runner& operator=(iou_loop_runner&&) = delete;

  // Signal the loop thread to exit. Idempotent. Also called by the destructor.
  void stop() { thread_.request_stop(); }

  [[nodiscard]] const std::shared_ptr<iou_loop>& loop() noexcept {
    return loop_;
  }
  [[nodiscard]] operator iou_loop&() noexcept { return *loop_; }
  [[nodiscard]] iou_loop* operator->() noexcept { return loop_.get(); }

private:
  void run(const std::stop_token& st) {
    jthread_stoppable_sleep::set_thread_name("iouring");
    // Hold a local shared_ptr so the loop outlives this function even if the
    // runner's shared_ptr is dropped by a callback running on this thread.
    auto loop = loop_;
    std::stop_callback on_stop{st, [loop] { loop->stop(); }};
    loop->run();
  }

  std::shared_ptr<iou_loop> loop_;
  std::jthread thread_;
};

}}} // namespace corvid::proto::iouring
