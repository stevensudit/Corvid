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
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <vector>

#include "../../filesys/os_file.h"
#include "../../concurrency/jthread_stoppable_sleep.h"
#include "../../concurrency/notifiable.h"
#include "../../concurrency/relaxed_atomic.h"
#include "../../containers/scope_exit.h"
#include "../../containers/scoped_value.h"
#include "../../containers/object_pool.h"
#include "iou_buf_pool.h"
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
// `iou_basic_loop` is non-copyable and non-movable: the ring contains
// kernel-mapped memory with self-referential pointers.
//
// The `RING_SIZE` template parameter controls the number of SQE slots, where
// there are 2X CQE slots. The `SLOT_COUNT` controls the maximum number of
// in-flight operations, which must be less than or equal to the number of CQE
// slots.
template<size_t RING_SIZE = 256, size_t SLOT_COUNT = 512>
class iou_basic_loop
    : std::enable_shared_from_this<iou_basic_loop<RING_SIZE, SLOT_COUNT>> {
public:
  using duration_t = std::chrono::milliseconds;
  using block_size = iou_dma_buf_pool::block_size;

  static constexpr duration_t default_run_once_timeout{10};
  static constexpr duration_t default_post_and_wait_poll_interval{100};

  // Callback invoked when an op completes.
  using completion_fn = std::function<bool(iou_res)>;

  // Callback scheduled via `post` to run on the loop thread.
  using posted_fn_t = std::function<bool()>;

  using buf_pool_t = iou_dma_buf_pool;

  // RAII handle to a registered buffer slot. See `iou_dma_buf_pool::token`.
  using token = iou_dma_buf_pool::token;

  // Completion callback for `submit_recv_fixed`: receives the filled buffer
  // token and the byte count. The token is returned to the pool when the
  // callback's copy is destroyed.
  using recv_completion_fn = std::function<bool(token, iou_res)>;

private:
  enum class allow : bool { ctor };
  static_assert((RING_SIZE & (RING_SIZE - 1)) == 0,
      "RING_SIZE must be a power of two");
  static_assert(SLOT_COUNT <= RING_SIZE * 2,
      "SLOT_COUNT must be less than or equal to the number of CQE slots");
  using completion_cb_pool_t = object_pool<completion_fn, SLOT_COUNT>;

public:
  // Construct a loop with `ring_size` SQE slots (must be a power of two).
  // Throws `std::system_error` if the ring, wakeup `eventfd`, or buffer
  // registration fails.
  explicit iou_basic_loop(allow,
      duration_t post_and_wait_poll_interval =
          default_post_and_wait_poll_interval)
      : ring_{RING_SIZE}, wake_fd_{event_fd::create()},
        post_and_wait_poll_interval_{post_and_wait_poll_interval} {
    if (!buf_pool_.register_with(ring_))
      throw std::system_error(errno, std::system_category(),
          "io_uring_register_buffers");
  }

  ~iou_basic_loop() = default;

  iou_basic_loop(const iou_basic_loop&) = delete;
  iou_basic_loop& operator=(const iou_basic_loop&) = delete;
  iou_basic_loop(iou_basic_loop&&) = delete;
  iou_basic_loop& operator=(iou_basic_loop&&) = delete;

  // Create a heap-allocated `iou_basic_loop` managed by `std::shared_ptr`.
  [[nodiscard]] static std::shared_ptr<iou_basic_loop> make(
      duration_t post_and_wait_poll_interval =
          default_post_and_wait_poll_interval) {
    return std::make_shared<iou_basic_loop>(allow::ctor,
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
    return scoped_value<const iou_basic_loop*>{current_loop_, this};
  }

  // Schedule `fn` to run on the loop thread at the top of the next
  // `run_once`. Safe to call from any thread. Writes to the internal
  // `eventfd` to wake the loop if it is blocked waiting for CQEs.
  [[nodiscard]] bool post(posted_fn_t fn) {
    if (std::scoped_lock lock{post_mutex_}; true)
      (*active_queue_)->push_back(std::move(fn));
    return do_wake();
  }

  // Execute `fn` immediately if on the loop thread; otherwise `post` it.
  template<typename FN>
  [[nodiscard]] bool execute_or_post(FN&& fn) {
    if (is_loop_thread()) return fn();
    return post(std::forward<FN>(fn));
  }

  // Post `fn` to run on the loop thread and block the calling thread until
  // it completes. Returns false if the loop is not running or the post fails.
  // Executes inline and returns true if called from the loop thread.
  template<typename FN>
  [[nodiscard]] bool post_and_wait(FN&& fn) {
    if (!running_.get()) return false;
    if (is_loop_thread()) return fn();
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
    stop_.store(false, std::memory_order::relaxed);
    const auto scope = poll_thread_scope();
    running_.notify(true);
    scope_exit on_exit{[&] { running_.notify(false); }};
    while (!stop_.load(std::memory_order::acquire))
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
      if (cqe.get_data_ptr() == &wake_tag_)
        disarm_wake_poll();
      else if (do_dispatch(cqe))
        ++dispatched;
      return true;
    });
    (void)total;
    return dispatched;
  }

  // Signal the loop to exit after the current `run_once` returns. Writes to
  // the `eventfd` to interrupt a sleeping wait. Safe to call from any thread.
  void stop() noexcept {
    stop_.store(true, std::memory_order::release);
    (void)do_wake();
  }

  // Submit a no-op that completes immediately with `res` == 0. Useful for
  // verifying ring plumbing. Returns false if the SQ is full. May be called
  // from any thread.
  [[nodiscard]] bool submit_nop(completion_fn cb) {
    return execute_or_post([this, cb = std::move(cb)]() mutable -> bool {
      return do_submit([](iou_sqe sqe) { sqe.prep_nop(); }, std::move(cb));
    });
  }

  // Submit an async recv on `file` into `buf`. The completion callback
  // receives only the `iou_res`, so in order to know where to read from you
  // will need to bind `buf` and any other necessary state into the callback.
  // The buffer must remain valid until the callback fires. May be called from
  // any thread.
  [[nodiscard]] bool submit_recv(const os_file& file, std::span<std::byte> buf,
      completion_fn cb, int flags = 0) {
    return execute_or_post(
        [this, fd = file.handle(), data = buf.data(), len = buf.size(), flags,
            cb = std::move(cb)]() mutable -> bool {
          return do_submit(
              [fd, data, len, flags](iou_sqe sqe) {
                sqe.prep_recv(fd, data, len, flags);
              },
              std::move(cb));
        });
  }

  // Submit an async send of `buf` on `file`.  The completion callback receives
  // only the `iou_res`, so in order to know what was sent, you will need to
  // bind `buf` and any other necessary state into the callback. The buffer
  // must remain valid until the callback fires. May be called from any thread.
  [[nodiscard]] bool submit_send(const os_file& file,
      std::span<const std::byte> buf, completion_fn cb, int flags = 0) {
    return execute_or_post(
        [this, fd = file.handle(), data = buf.data(), len = buf.size(), flags,
            cb = std::move(cb)]() mutable -> bool {
          return do_submit(
              [fd, data, len, flags](iou_sqe sqe) {
                sqe.prep_send(fd, data, len, flags);
              },
              std::move(cb));
        });
  }

  // Acquire a registered buffer slot from the pool for the purpose of writing
  // to it. Returns an empty token if the pool is exhausted. Fill the token's
  // span, then pass it to `submit_send_fixed`. May be called from any thread.
  [[nodiscard]] token acquire_write_buffer(
      block_size sz = block_size::small) noexcept {
    return buf_pool_.alloc_write(sz);
  }

  // Submit a zero-copy recv into a registered buffer. The loop allocates the
  // buffer; the callback receives `(token)`. Check `tok.result` and read from
  // `tok.read_span`. Returns false if no buffer is available or the SQ is
  // full. May be called from any thread.
  [[nodiscard]] bool submit_recv_fixed(const os_file& file,
      recv_completion_fn cb, block_size sz = block_size::small) {
    auto tok = buf_pool_.alloc_read(sz);
    auto span = buf_pool_.access_full_span(tok);
    return execute_or_post(
        [this, fd = file.handle(), ptr = span.data(), len = span.size(),
            ndx = tok.buf_index(), tok = std::move(tok),
            cb = std::move(cb)]() mutable -> bool {
          return do_submit(
              [fd, ptr, len, ndx](iou_sqe sqe) {
                sqe.prep_read_fixed(fd, ptr, len, ndx);
              },
              [tok = std::move(tok), cb = std::move(cb)](iou_res res) mutable
                  -> bool { return cb(std::move(tok), res); });
        });
  }

  // Submit a zero-copy send from a pre-filled registered buffer. `tok` must
  // come from `alloc_buf()` and updated with the payload size.
  // Returns false if the SQ is full. May be called from any thread.
  [[nodiscard]] bool
  submit_send_fixed(const os_file& file, token tok, completion_fn cb) {
    auto span = tok.write_span();
    return execute_or_post(
        [this, fd = file.handle(), ptr = span.data(), len = span.size(),
            ndx = tok.buf_index(), tok = std::move(tok),
            cb = std::move(cb)]() mutable -> bool {
          return do_submit(
              [fd, ptr, len, ndx](iou_sqe sqe) {
                sqe.prep_write_fixed(fd, ptr, len, ndx);
              },
              [tok = std::move(tok), cb = std::move(cb)](
                  iou_res res) mutable -> bool {
                auto ok = cb(res);
                // TODO: No, we need to be able to retry, and that means
                // passing the whole token by ref.
                tok.reset();
                return ok;
              });
        });
  }

private:
  // Get an SQE, prepare it via `prep`, attach `cb` as user data, and submit.
  // Returns false if the SQ is full.
  [[nodiscard]] bool
  do_submit(std::invocable<iou_sqe> auto&& prep, completion_fn&& cb) {
    auto sqe = ring_.next_sqe();
    if (!sqe) return false;
    std::forward<decltype(prep)>(prep)(sqe);
    // Use a pool to persist `cb`. If we submit successfully, we detach to take
    // full ownership.
    auto borrowed_cb = completion_cb_pool_.borrow();
    if (!borrowed_cb) return sqe.prep_nop() && false;
    *borrowed_cb = std::move(cb);
    sqe.set_data_pointer(borrowed_cb.get());
    if (!ring_.submit().ok(1)) return sqe.prep_nop() && false;
    return completion_cb_pool_.detach(std::move(borrowed_cb)) || true;
  }

  bool do_dispatch(iou_cqe cqe) noexcept {
    auto* raw_cb = cqe.get_data_ptr<completion_fn>();
    if (!raw_cb) return false;
    auto cb = completion_cb_pool_.reattach(std::move(raw_cb));
    if (!cb) return false;
    return (cb.value())(iou_res{cqe.res()});
  }

  // Atomically swap out the active post queue, then execute and clear it
  // without holding the lock. Callbacks posted from within a drained callback
  // land in the newly active queue and run on the next iteration.
  void do_drain_post_queue() {
    assert(is_loop_thread());
    post_queue_t* pending;
    if (std::scoped_lock lock{post_mutex_}; true) {
      pending = active_queue_;
      post_queue_t* other =
          (pending == &post_queues_[0]) ? &post_queues_[1] : &post_queues_[0];
      active_queue_ = other;
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
  bool ensure_wake_poll_armed() {
    if (wake_poll_armed_) return true;
    auto sqe = ring_.next_sqe();
    if (!sqe) return false;
    sqe.prep_poll_oneshot(wake_fd_.handle(), POLLIN);
    sqe.set_data_pointer(&wake_tag_);
    if (ring_.submit().ok(1)) wake_poll_armed_ = true;
    return true;
  }

  // Consume the wakeup signal and disarm the wake poll.
  bool disarm_wake_poll() {
    (void)wake_fd_.read();
    wake_poll_armed_ = false;
    return true;
  }

  [[nodiscard]] bool do_wake() noexcept { return wake_fd_.notify(); }

  using post_queue_t = std::vector<posted_fn_t>;

  // Members are declared in initialization order.

  iou_ring ring_;
  buf_pool_t buf_pool_;

  // `&wake_tag_` is the SQE user-data sentinel for the wake-poll CQE,
  // distinct from any `completion_fn*`.
  event_fd wake_fd_;
  char wake_tag_{};
  bool wake_poll_armed_{};

  completion_cb_pool_t completion_cb_pool_;

  inline static thread_local const iou_basic_loop* current_loop_{};

  std::atomic_bool stop_;
  notifiable<std::atomic_bool> running_;

  std::mutex post_mutex_;
  post_queue_t post_queues_[2];
  relaxed_atomic<post_queue_t*> active_queue_{&post_queues_[0]};

  const std::chrono::milliseconds post_and_wait_poll_interval_;
};

// Alias for the common case of default template parameters.
using iou_loop = iou_basic_loop<>;

// Runs an `iou_basic_loop` in its own background thread.
//
// Shutdown ordering: call `stop` (or destroy the runner) before destroying
// any object that a pending `post` callback might reference.
template<size_t RING_SIZE = 256, size_t SLOT_COUNT = 512>
class iou_basic_loop_runner {
public:
  using loop_t = iou_basic_loop<RING_SIZE, SLOT_COUNT>;

  explicit iou_basic_loop_runner(
      std::chrono::milliseconds post_and_wait_poll_interval =
          loop_t::default_post_and_wait_poll_interval)
      : loop_{loop_t::make(post_and_wait_poll_interval)},
        thread_{[this](const std::stop_token& st) { run(st); }} {
    if (!loop_->wait_until_running(1000)) {
      thread_.request_stop();
      throw std::runtime_error("iou_loop_runner failed to start");
    }
  }

  ~iou_basic_loop_runner() {
    thread_.request_stop();
    if (!thread_.joinable()) return;
    // If the last shared_ptr owner is a callback on the loop thread, the
    // runner's destructor may run on that thread. Detach to avoid self-join.
    if (thread_.get_id() == std::this_thread::get_id())
      thread_.detach();
    else
      thread_.join();
  }

  iou_basic_loop_runner(const iou_basic_loop_runner&) = delete;
  iou_basic_loop_runner& operator=(const iou_basic_loop_runner&) = delete;
  iou_basic_loop_runner(iou_basic_loop_runner&&) = delete;
  iou_basic_loop_runner& operator=(iou_basic_loop_runner&&) = delete;

  // Signal the loop thread to exit. Idempotent. Also called by the destructor.
  void stop() { thread_.request_stop(); }

  [[nodiscard]] const std::shared_ptr<loop_t>& loop() noexcept {
    return loop_;
  }
  [[nodiscard]] operator loop_t&() noexcept { return *loop_; }
  [[nodiscard]] loop_t* operator->() noexcept { return loop_.get(); }

private:
  void run(const std::stop_token& st) {
    jthread_stoppable_sleep::set_thread_name("iouring");
    // Hold a local shared_ptr so the loop outlives this function even if the
    // runner's shared_ptr is dropped by a callback running on this thread.
    auto loop = loop_;
    std::stop_callback on_stop{st, [loop] { loop->stop(); }};
    loop->run();
  }

  std::shared_ptr<loop_t> loop_;
  std::jthread thread_;
};

// Alias for the common case of default template parameters.
using iou_loop_runner = iou_basic_loop_runner<>;

}}} // namespace corvid::proto::iouring
