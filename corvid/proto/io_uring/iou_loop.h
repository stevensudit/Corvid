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
#include <exception>
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

// Completion-based I/O loop built on io_uring. This implements a Proactor
// pattern, where we submit operations and then wait for their completion.
//
// Use an `iou_loop_runner` to drive this loop with `run` (blocking, on the
// loop thread) or `run_once` (one batch, useful for testing) in its own
// thread. Public methods are labeled with their thread safety.
//
// Note that, unlike `epoll_loop`, this does not have a registry of
// connections. Instead, the only thing keeping a connection alive is the
// presence of an in-flight operation whose completion callback holds a shared
// pointer to it.
//
// So long as there is always a pending read or accept operation, it will be
// naturally kept alive until the peer closes the connection or an error
// occurs. If you need to apply backpressure, you need to either submit a
// timeout operation or simply store a shared pointer to the connection
// somewhere, such as in a protocol server.
//
// Details:
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
// callback. They use `execute_or_post`, since the SQ ring is not thread-safe.
//
// `iou_basic_loop` is non-copyable and non-movable: the ring contains
// kernel-mapped memory with self-referential pointers.
//
// The `RING_SIZE` template parameter controls the number of SQE slots, where
// there are 2X CQE slots. The `SLOT_COUNT` controls the maximum number of
// in-flight operations, which must be less than or equal to the number of CQE
// slots.
//
// Plans:
// - Allow `iou_buf_pool` size to be configured.
template<size_t RING_SIZE = 256, size_t SLOT_COUNT = 512>
class iou_basic_loop
    : public std::enable_shared_from_this<
          iou_basic_loop<RING_SIZE, SLOT_COUNT>> {
public:
  // Buffer pool.
  using buf_pool_t = iou_buf_pool;
  using block_size = buf_pool_t::block_size;
  using buffer = buf_pool_t::buffer;
  using span_t = buffer::span_t;
  using const_span_t = buffer::const_span_t;

  // Timeouts.
  using duration_t = std::chrono::milliseconds;
  static constexpr duration_t default_run_once_timeout{10ms};
  static constexpr duration_t default_post_and_wait_poll_interval{100ms};

  // Callback scheduled via `post` to run on the loop thread.
  using posted_fn_t = std::function<bool()>;

  // Low-level callback invoked when an op completes. Pooled in
  // `completion_cb_pool_t` to avoid the need for `std::shared_ptr` for each
  // `std::function`.
  using completion_fn = std::function<bool(iou_res, iou_cqe_flags)>;

  // Completion callback for `submit_recv_buffer` and `submit_send_buffer`:
  // receives the `buffer`, which includes an `iou_res` and `iou_cqe_flags`. If
  // the `buffer` is not moved from during the callback, it is returned to the
  // pool afterwards.
  using buf_completion_fn = std::function<bool(buffer&)>;

private:
  struct clear_function_cb {
    constexpr void operator()(auto& completion_fn) const noexcept {
      completion_fn = nullptr;
    }
  };

  using completion_cb_pool_t = object_pool<completion_fn, SLOT_COUNT,
      generation_scheme::versioned, no_op_cb, clear_function_cb>;

  using borrowed_cb = typename completion_cb_pool_t::borrowed;

public:
  using cancelation_token = typename completion_cb_pool_t::handle;

#pragma region Details
private:
  enum class allow : bool { ctor };
  static_assert((RING_SIZE & (RING_SIZE - 1)) == 0,
      "RING_SIZE must be a power of two");
  static_assert(SLOT_COUNT <= RING_SIZE * 2,
      "SLOT_COUNT must be less than or equal to the number of CQE slots");

  using post_queue_t = std::vector<posted_fn_t>;
#pragma endregion
#pragma region Infrastructure: construction, thread scope, and main loop
public:
  // Construct a loop with `ring_size` SQE slots (must be a power of two).
  // Throws `std::system_error` if the ring, wakeup `eventfd`, or
  // `buf_pool_t` registration fails.
  //
  // Note: The flags for the ring require that all SQEs be issued from the same
  // thread, and optimizes completions for the single-issuer case.
  explicit iou_basic_loop(allow,
      duration_t post_and_wait_poll_interval =
          default_post_and_wait_poll_interval)
      : ring_{RING_SIZE,
            IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN},
        wake_fd_{event_fd::create()},
        post_and_wait_poll_interval_{post_and_wait_poll_interval} {
    if (!buf_pool_.register_with(ring_))
      throw std::system_error(errno, std::system_category(),
          "io_uring_register_buffers");
  }

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
  // for its lifetime. `run` does this internally; call it manually before
  // using `run_once` directly (e.g., in tests).
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

  // Block until `run` is active. Returns false on timeout.
  // Pass -1 (the default) to wait up to 60 seconds.
  [[nodiscard]] bool wait_until_running(int timeout_ms = -1) {
    if (timeout_ms < 0) timeout_ms = 60000;
    return running_.wait_for_value(duration_t{timeout_ms}, true);
  }

  // Run the loop on the calling thread until `stop` is called.
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
  // the `eventfd` to interrupt a sleeping wait. May be called from any thread.
  void stop() noexcept {
    stop_.store(true, std::memory_order::release);
    (void)do_wake();
  }

#pragma endregion
#pragma region Submit and buffer APIs

  // Submit an async accept on `file`. May be called from any thread.
  [[nodiscard]] cancelation_token submit_accept(const os_file& file,
      completion_fn&& cb, int flags = SOCK_NONBLOCK | SOCK_CLOEXEC) {
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.handle(), flags,
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit(
              [fd, flags](iou_sqe sqe) { sqe.prep_accept(fd, flags); },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Submit a multishot async accept on `file`. The callback fires for every
  // accepted connection without re-arming; it is released only when the
  // operation terminates (error or cancel). May be called from any thread.
  //
  // As with any multishot operation, the persisted callback takes up a slot in
  // the `completion_cb_pool_t` for the duration.
  [[nodiscard]] cancelation_token submit_accept_multishot(const os_file& file,
      completion_fn&& cb, int flags = SOCK_NONBLOCK | SOCK_CLOEXEC) {
    if (!file) return {};
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.handle(), flags,
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit(
              [fd, flags](iou_sqe sqe) {
                sqe.prep_accept_multishot(fd, flags);
              },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Submit an async connect on `file` to `endpoint`. `endpoint` must remain
  // valid until the CQE fires. May be called from any thread.
  //
  // Note that the `endpoint` must remain valid until submit, so you may need
  // to call `submit_now`.
  [[nodiscard]] cancelation_token submit_connect(const os_file& file,
      const net_endpoint& endpoint, completion_fn&& cb) {
    if (!file) return {};
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.handle(), &endpoint,
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit(
              [fd, &endpoint](iou_sqe sqe) { sqe.prep_connect(fd, endpoint); },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Submit an async recvmsg on `file`. `msg` must remain valid until the
  // CQE fires. May be called from any thread.
  [[nodiscard]] cancelation_token submit_recvmsg(const os_file& file,
      msghdr* msg, completion_fn&& cb, int flags = 0) {
    // TODO: Revisit this.
    if (!file) return {};
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.handle(), msg, flags,
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit(
              [fd, msg, flags](iou_sqe sqe) {
                sqe.prep_recvmsg(fd, msg, flags);
              },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Submit an async sendmsg on `file`. `msg` must remain valid until the
  // CQE fires. May be called from any thread.
  [[nodiscard]] cancelation_token submit_sendmsg(const os_file& file,
      const msghdr* msg, completion_fn&& cb, int flags = MSG_NOSIGNAL) {
    if (!file) return {};
    // TODO: Revisit this.
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.handle(), msg, flags,
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit(
              [fd, msg, flags](iou_sqe sqe) {
                sqe.prep_sendmsg(fd, msg, flags);
              },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Submit a no-op that completes immediately with `res` == 0. Useful for
  // verifying ring plumbing. May be called from any thread.
  [[nodiscard]] cancelation_token submit_nop(completion_fn&& cb) {
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post(
            [this, cb = std::move(borrowed_cb)]() mutable -> bool {
              return do_submit([](iou_sqe sqe) { sqe.prep_nop(); },
                  std::move(cb))
                  .valid();
            }))
      return {};
    return token;
  }

  // Close `file` via `IORING_OP_CLOSE`. May be called from any thread.
  [[nodiscard]] cancelation_token
  submit_close(os_file&& file, completion_fn&& cb) {
    if (!file) return {};
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.release(),
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit([fd](iou_sqe sqe) { sqe.prep_close(fd); },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Cancel all in-flight operations targeting `file` via
  // `IORING_OP_ASYNC_CANCEL`. May be called from any thread.
  [[nodiscard]] cancelation_token
  submit_cancel_fd(const os_file& file, completion_fn&& cb) {
    if (!file) return {};
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.handle(),
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit([fd](iou_sqe sqe) { sqe.prep_cancel_fd(fd); },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Cancel all in-flight operations with `target_token` via
  // `IORING_OP_ASYNC_CANCEL`. May be called from any thread.
  [[nodiscard]] cancelation_token
  submit_cancel_token(cancelation_token target_token, completion_fn&& cb) {
    if (!target_token) return {};
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, target_token,
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit(
              [target_token](iou_sqe sqe) {
                sqe.prep_cancel_user_data(target_token.as_int());
              },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Submit a standalone timeout.  May be called from any thread.
  //
  // Note that the `duration` must remain valid until submit, so you may need
  // to call `submit_now`.
  [[nodiscard]] cancelation_token
  submit_timeout(__kernel_timespec* duration, completion_fn&& cb) {
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post(
            [this, duration, cb = std::move(borrowed_cb)]() mutable -> bool {
              return do_submit(
                  [duration](iou_sqe sqe) { sqe.prep_timeout(duration); },
                  std::move(cb))
                  .valid();
            }))
      return {};
    return token;
  }

  // TODO: Add recurring timeout as well as io_uring_prep_poll_multishot.

  // Low-level method to submit an async recv on `file` into bytes at `buf`.
  // The completion callback receives only the `iou_res` and `iou_cqe_flags` so
  // in order to know where to read from, you will need to bind `span` and any
  // other necessary state into the callback. This buffer span must remain
  // valid until the callback fires. May be called from any thread.
  //
  // Prefer `submit_recv_buffer` over this.
  [[nodiscard]] cancelation_token submit_recv_bytes(const os_file& file,
      span_t span, completion_fn&& cb, int flags = 0) {
    if (!file) return {};
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.handle(), span, flags,
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit(
              [fd, span, flags](iou_sqe sqe) {
                sqe.prep_recv(fd, span, flags);
              },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Low-level method to submit an async send of bytes at `buf` on `file`. The
  // completion callback receives only the `iou_res`, so in order to know what
  // was sent, you will need to bind `buf` and any other necessary state into
  // the callback. The buffer span must remain valid until the callback fires.
  // May be called from any thread.
  //
  // Prefer `submit_send_buffer` over this.
  [[nodiscard]] cancelation_token submit_send_bytes(const os_file& file,
      const_span_t span, completion_fn&& cb, int flags = 0) {
    if (!file) return {};
    auto borrowed_cb = borrow(std::move(cb));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post([this, fd = file.handle(), span, flags,
                             cb = std::move(borrowed_cb)]() mutable -> bool {
          return do_submit(
              [fd, span, flags](iou_sqe sqe) {
                sqe.prep_send(fd, span, flags);
              },
              std::move(cb))
              .valid();
        }))
      return {};
    return token;
  }

  // Borrow a registered `buffer` from the pool for the purpose of reading into
  // it. Returns an invalid `buffer` if the pool is exhausted. May be called
  // from any thread.
  [[nodiscard]] buffer borrow_read_buffer(
      block_size sz = block_size::small) noexcept {
    return buf_pool_.borrow_reader(sz);
  }

  // Borrow a registered `buffer` from the pool for the purpose of writing to
  // it. Returns an invalid `buffer` if the pool is exhausted. Fill the
  // `buffer`'s payload, then pass it to `submit_send_buffer`. May be called
  // from any thread.
  [[nodiscard]] buffer borrow_write_buffer(
      block_size sz = block_size::small) noexcept {
    return buf_pool_.borrow_writer(sz);
  }

  // Submit a zero-copy recv into a registered `buffer`. The loop allocates the
  // `buffer`; the callback receives it so that it can check `buf.result` and
  // read from `buf.payload`. It may then wish to move the buffer and reuse it.
  // If the callback does not move the `buffer` out, it is returned to the
  // pool.
  //
  // Note that the `timeout` must remain valid until submit, so you may need
  // to call `submit_now`.
  //
  // Returns cancelation token, which will be invalid if we fail early. May be
  // called from any thread. See overload for description of `iou_res` values.
  //
  // Prefer this over manually calling `borrow_read_buffer` and
  // `submit_recv_bytes`. However, this method is ideal for reusing an existing
  // read buffer, or a converted write buffer.
  [[nodiscard]] cancelation_token submit_recv_buffer(const os_file& file,
      buf_completion_fn cb, block_size sz = block_size::small,
      __kernel_timespec* timeout = nullptr) {
    return submit_recv_buffer(file, borrow_read_buffer(sz), std::move(cb),
        timeout);
  }

  // Submit a zero-copy recv into a registered `buffer`. The callback receives
  // the `buffer` so that it can check `buf.result` and read from
  // `buf.payload`. It may then wish to move the buffer and reuse it. If the
  // callback does not move the `buffer` out, it is returned to the pool.
  //
  // Note that the `timeout` must remain valid until submit, so you may need to
  // call `submit_now`.
  //
  // Returns cancelation token, which will be invalid if the `buffer` is
  // invalid. May be called from any thread.
  //
  // On success, the `buffer.result().value()` will be set a positive integer,
  // which is the number of bytes read. If there's room in the buffer and you
  // expect more data, you may move the buffer by calling this method.
  //
  // On EOF, the `buffer.result().value()` will be 0. The buffer payload is
  // unchanged and should be inspected for any remaining data. You should then
  // close the socket.
  //
  // On soft error, `buffer.result().is_soft_error()` will be true. The buffer
  // payload is unchanged and should be inspected for any remaining data. You
  // may wish to retry, much as you would to accumulate more data.
  //
  // Prefer this over `submit_recv_bytes`.
  [[nodiscard]] cancelation_token submit_recv_buffer(const os_file& file,
      buffer&& buf, buf_completion_fn cb,
      __kernel_timespec* timeout = nullptr) {
    if (!file || !buf) return {};
    buf.reset_result();
    const auto span = buf.active_span();
    const auto buf_index = buf.buf_index();
    completion_fn inner =
        [buf = std::move(buf), cb = std::move(cb)](iou_res res,
            iou_cqe_flags flags) mutable -> bool {
      return cb(buf.update(res, flags));
    };
    auto borrowed_cb = borrow(std::move(inner));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post(
            [this, fd = file.handle(), cb = std::move(borrowed_cb), span,
                buf_index, timeout]() mutable -> bool {
              auto prep = [fd, span, buf_index](iou_sqe sqe) {
                sqe.prep_read_fixed(fd, span, buf_index);
              };
              return do_submit(std::move(prep), std::move(cb), timeout)
                  .valid();
            }))
      return {};
    return token;
  }

  // Submit a zero-copy send from a pre-filled registered buffer. `buf` must
  // come from `borrow_write_buffer` and be filled via `append` or
  // `update_payload`. The callback receives the `buffer` so that it can check
  // `buf.result` and perhaps read from `buf.payload`. It may then wish to move
  // the buffer and reuse it. If the callback does not move the `buffer` out,
  // it is returned to the pool.
  //
  // Returns false if the `buffer` is invalid. May be called from any thread.
  //
  // On success, the `buffer.result().value()` will be set a positive integer,
  // which is the number of bytes written. This may be less than the
  // `payload_span`, which means you may wish to retry by moving the buffer and
  // calling this method again to write the remaining data. Note that a 0 is
  // just the extreme case of a partial write, not an indication of EOF or
  // error.
  //
  // On EOF, the `buffer.result().err()` will be `EPIPE` or even `ECONNRESET`.
  // You should then close the socket.
  //
  // On soft error, `buffer.result().is_soft_error()` will be true. You
  // may wish to retry, much as you would on any other partial write.
  //
  // Prefer this over `submit_send_bytes`.
  [[nodiscard]] cancelation_token submit_send_buffer(const os_file& file,
      buffer&& buf, buf_completion_fn cb,
      __kernel_timespec* timeout = nullptr) {
    if (!file || !buf) return {};
    buf.reset_result();
    const auto span = buf.active_span();
    const auto buf_index = buf.buf_index();
    completion_fn inner =
        [buf = std::move(buf), cb = std::move(cb)](iou_res res,
            iou_cqe_flags flags) mutable -> bool {
      return cb(buf.update(res, flags));
    };
    auto borrowed_cb = borrow(std::move(inner));
    if (!borrowed_cb) return {};
    auto token = cancelation_token{borrowed_cb};
    if (!execute_or_post(
            [this, fd = file.handle(), cb = std::move(borrowed_cb), span,
                buf_index, timeout]() mutable -> bool {
              auto prep = [fd, span, buf_index](iou_sqe sqe) {
                sqe.prep_write_fixed(fd, span, buf_index);
              };
              return do_submit(std::move(prep), std::move(cb), timeout)
                  .valid();
            }))
      return {};
    return token;
  }
#pragma endregion
#pragma region Helpers.
private:
  // Borrow a callback pool slot and move `cb` into it.
  [[nodiscard]] borrowed_cb borrow(completion_fn&& cb) {
    auto borrowed_cb = completion_cb_pool_.borrow();
    if (borrowed_cb) *borrowed_cb = std::move(cb);
    return borrowed_cb;
  }

  // Borrow a callback pool slot from its handle.
  [[nodiscard]] borrowed_cb borrow(cancelation_token token) {
    return token.borrow(completion_cb_pool_);
  }

  // Detach a borrowed callback, without freeing it.
  void detach(borrowed_cb&& cb) {
    (void)completion_cb_pool_.detach(std::move(cb));
  }

  // Get an SQE, prepare it via `prep`, attach `cb` as user data, and
  // potentially submit. If `timeout` specified, links a timeout SQE to it.
  // Note that, if `timeout` points to a local variable, the caller must call
  // `submit_now` before it leaves scope.
  [[nodiscard]] cancelation_token
  do_submit(std::invocable<iou_sqe> auto&& prep, borrowed_cb&& cb,
      __kernel_timespec* timeout = nullptr) {
    auto sqe = ring_.next_sqe();
    if (!sqe) return {};

    // If timeout, follow it up with a linked timeout SQE.
    iou_sqe sqe_to;
    if (timeout) {
      sqe_to = ring_.next_sqe();
      if (!sqe_to) {
        sqe.prep_nop();
        return {};
      };
      sqe_to.prep_link_timeout(timeout);
      sqe_to.set_data_pointer(nullptr);
    }

    // Prep the operation, and if there's a timeout, link it.
    std::forward<decltype(prep)>(prep)(sqe);
    if (timeout) sqe.set_sqe_flags(iou_sqe_flags::io_link);

    // Store as token, "leaking" it.
    cancelation_token token{std::move(cb)};
    sqe.set_data_int(token.as_int());
    if (!maybe_submit_pending()) return {};
    return token;
  }

  // Submit pending SQEs, although this could be delayed. The `sqe_count` is
  // added to the pending value, and if it exceeds the configured limit, the
  // submit is triggered immediately. Even if it doesn't exceed this limit,
  // we also check how long it's been since the last submit, and if it
  // exceeds that configured limit, we submit immediately.
  [[nodiscard]] bool maybe_submit_pending(size_t sqe_count = 1) {
    (void)sqe_count;
    // TODO: Make the above true. For now, we just submit immediately.
    return submit_now();
  }

  // Submit immediately if there are any pending SQEs.
  [[nodiscard]] bool submit_now() {
    // TODO: Check whether there are any pending SQEs at all, and if not,
    // don't bother submitting.
    auto res = ring_.submit();
    if (res.ok() || res.is_soft_error()) return true;
    // TODO: Don't return false, just shut down the loop.
    return false;
  }

  // Fire-and-forget submit: no callback, null user data. `do_dispatch`
  // ignores null-data CQEs, so no pool slot is needed.
  [[nodiscard]] bool do_submit_void(std::invocable<iou_sqe> auto&& prep) {
    auto sqe = ring_.next_sqe();
    if (!sqe) return false;
    std::forward<decltype(prep)>(prep)(sqe);
    sqe.set_data_pointer(nullptr);
    return submit_now();
  }

  // Dispatch a CQE to its callback. For multishot operations,
  // `IORING_CQE_F_MORE` indicates more CQEs will follow; the callback is
  // retained rather than released. On the final CQE (flag absent), the
  // callback is released as usual.
  //
  // TOOD: We want the callback to return an enum for:
  //   - Default behavior (release depending on `IORING_CQE_F_MORE`).
  //   - Always release. (probably not needed evere, but nice to have.)
  //   - Always retain. (Suitable for reusing the callback in a new SQE.)
  bool do_dispatch(iou_cqe cqe) {
    // Call through raw pointer.
    cancelation_token token{cqe.get_data_int()};

    // If it doesn't have a cancelation token, no problem.
    if (!token) return true;

    // Get ownership of the callback. This will quietly fail if the generation
    // has been invalidated.
    auto borrowed_cb = token.borrow(completion_cb_pool_);
    if (!borrowed_cb || !*borrowed_cb) return false;

    // Invoke the callback with the result and flags.
    bool result = borrowed_cb(cqe.res(), cqe.flags());

    // If no more, release the callback.
    if (!bitmask::has(cqe.flags(), iou_cqe_flags::more)) return result;

    // Steal ownership without freeing it.
    (void)cancelation_token{std::move(borrowed_cb)};
    return result;
  }

  // Atomically swap out the active post queue, then execute and clear it
  // without holding the lock. Callbacks posted from within a drained
  // callback land in the newly active queue and run on the next iteration.
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
  // false; the 10ms fallback timeout in `run` covers this case, and the
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

#pragma endregion
#pragma region Data members
private:
  // Rings and buffers.
  iou_ring ring_;
  buf_pool_t buf_pool_;

  // Poll wake system.
  event_fd wake_fd_;
  char wake_tag_{};        // SQE user-data sentinel.
  bool wake_poll_armed_{}; // Current armed state.

  // Completion callback pool, used to avoid `std::shared_ptr`.
  completion_cb_pool_t completion_cb_pool_;

  // Stop system.
  std::atomic_bool stop_;
  notifiable<std::atomic_bool> running_;

  // Post queue system.
  inline static thread_local const iou_basic_loop* current_loop_{};
  std::mutex post_mutex_;
  post_queue_t post_queues_[2];
  relaxed_atomic<post_queue_t*> active_queue_{&post_queues_[0]};

  const std::chrono::milliseconds post_and_wait_poll_interval_;
#pragma endregion
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
      : post_and_wait_poll_interval_{post_and_wait_poll_interval},
        thread_{[this](const std::stop_token& st) { run(st); }} {
    if (!started_.wait_for_value(std::chrono::milliseconds{1000}, true)) {
      thread_.request_stop();
      throw std::runtime_error("iou_loop_runner failed to start");
    }
    std::shared_ptr<loop_t> loop;
    std::exception_ptr startup_error;
    if (std::scoped_lock lock{startup_mutex_}; true) {
      loop = loop_;
      startup_error = startup_error_;
    }
    if (startup_error) {
      thread_.join();
      std::rethrow_exception(startup_error);
    }
    if (!loop || !loop->wait_until_running(1000)) {
      thread_.request_stop();
      if (thread_.joinable()) thread_.join();
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

  // Signal the loop thread to exit. Idempotent. Also called by the
  // destructor.
  void stop() { thread_.request_stop(); }

  [[nodiscard]] const std::shared_ptr<loop_t>& loop() noexcept {
    return loop_;
  }
  [[nodiscard]] operator loop_t&() noexcept { return *loop_; }
  [[nodiscard]] loop_t* operator->() noexcept { return loop_.get(); }

private:
  void run(const std::stop_token& st) {
    jthread_stoppable_sleep::set_thread_name("iouring");
    try {
      auto loop = loop_t::make(post_and_wait_poll_interval_);
      if (std::scoped_lock lock{startup_mutex_}; true) loop_ = loop;
      started_.notify(true);
      // Hold a local shared_ptr so the loop outlives this function even if
      // the runner's shared_ptr is dropped by a callback running on this
      // thread.
      std::stop_callback on_stop{st, [loop] { loop->stop(); }};
      loop->run();
    }
    catch (...) {
      if (std::scoped_lock lock{startup_mutex_}; true)
        startup_error_ = std::current_exception();
      started_.notify(true);
    }
  }

  const std::chrono::milliseconds post_and_wait_poll_interval_;
  std::mutex startup_mutex_;
  std::exception_ptr startup_error_;
  notifiable<std::atomic_bool> started_{false};
  std::shared_ptr<loop_t> loop_;
  std::jthread thread_;
};

// Alias for the common case of default template parameters.
using iou_loop_runner = iou_basic_loop_runner<>;
}}} // namespace corvid::proto::iouring
