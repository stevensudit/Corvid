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
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

#include "../../meta/forwarding_address.h"
#include "../../filesys/os_file.h"
#include "../../concurrency/jthread_stoppable_sleep.h"
#include "../../concurrency/notifiable.h"
#include "../../concurrency/relaxed_atomic.h"
#include "../../containers/scope_exit.h"
#include "../../containers/scoped_value.h"
#include "../../containers/object_pool.h"
#include "iou_buf_pool.h"
#include "iou_wrap.h"

namespace corvid { inline namespace proto { namespace iouring {
using namespace std::chrono_literals;

// This is a legitimate warning, but clang-tidy is applying it illegitimately.
// It's complaining that what looks like an rvalue, and is being moved from, is
// actually a universal reference. It could therefore become an lvalue, so we
// should instead forward it. However, we use concepts to lock down the type to
// ensure that it's an rvalue. See `ConsumableCallback`.
//
// NOLINTBEGIN(bugprone-move-forwarding-reference)

#pragma region Bindables

// Type-unsafe version of `completion_token`, necessary to avoid circular
// dependencies. Just construct a `completion_token` from it.
using completion_id = uint64_t;

// Bindable timeout.
struct flagged_timeout: public address_forwarder<flagged_timeout> {
  combined_timespec when;

  auto& forwarding_payload() { return when; }
};

// Bindable endpoint.
struct sockaddr_endpoint: public address_forwarder<sockaddr_endpoint> {
  combined_endpoint sockaddr;
};

// Bindable endpoint, with timeout and flags.
struct flagged_timeout_endpoint
    : public address_forwarder<flagged_timeout_endpoint> {
  combined_timespec when;
  combined_endpoint sockaddr;
};

#pragma endregion
#pragma region Concepts

// Concept for a callback that can be moved (not copied) into a
// `std::function`.
template<class FN>
concept ConsumableCallback =
    !std::is_lvalue_reference_v<FN> &&
    !std::is_const_v<std::remove_reference_t<FN>>;

// Concept for `iou_loop::buf_completion_fn` lambda.
template<typename FN>
concept BufCompletionInvocable =
    ConsumableCallback<FN> &&
    std::is_invocable_r_v<slot_retention, FN, completion_id,
        iou_buf_pool::buffer&>;

// Concept for `iou_loop::endpoint_completion_fn` lambda.
template<typename FN>
concept EndpointCompletionInvocable =
    ConsumableCallback<FN> &&
    std::is_invocable_r_v<slot_retention, FN, completion_id, iou_res,
        iou_cqe_flags, combined_endpoint&>;

// Concept for `iou_loop::completion_fn` lambda.
template<typename FN>
concept CompletionInvocable =
    ConsumableCallback<FN> &&
    std::is_invocable_r_v<slot_retention, FN, completion_id, iou_res,
        iou_cqe_flags>;

// Concept for `iou_loop::posted_fn` lambda.
template<typename FN>
concept PostedInvocable =
    ConsumableCallback<FN> && std::is_invocable_r_v<bool, FN>;

#pragma endregion
#pragma region iou_loop

// Completion-based I/O loop built on `io_uring`. This implements a Proactor
// pattern where we submit operations and then wait for their completion.
//
// Use an `iou_loop_runner` to drive this loop with `run` (blocking, on the
// loop thread) or `run_once` (one batch, useful for testing) in its own
// thread. Public methods are thread-safe unless otherwise indicated.
//
// Note that, unlike `epoll_loop`, this does not have a registry of
// connections. Instead, the only thing keeping a connection alive, if nobody
// else holds a shared pointer to it, is the presence of an in-flight operation
// whose completion callback has a shared pointer bound into it.
//
// Barring that external owner, so long as there is always a pending read or
// accept operation, the connection will be naturally kept alive until the peer
// closes the underlying socket or an error occurs. This means that if you need
// to apply backpressure by not scheduling new operations, you will need to
// keep the connection alive externally.
//
// Details:
//
// `post` is the cross-thread entry point: any thread can push an arbitrary
// callback onto the queue, and the loop will execute it at the top of the next
// `run_once`. The loop is woken by a multishot `IORING_OP_POLL_ADD` armed on
// an internal `eventfd`, so the wakeup signal travels through the same CQE
// path as every other completion. `post_and_wait` is a synchronous variant
// that blocks the caller until the callback completes: it has only niche uses.
//
// `execute_or_post` executes inline on the loop thread or posts otherwise. It
// is used under the hood to make public operations thread-safe without
// unnecessary posting overhead when already on the loop thread (which is the
// case for callbacks triggered by CQEs).
//
// `submit_*` methods enqueue I/O operations paired with a `completion_fn`
// (perhaps wrapping a more specific callback, such as `buf_completion_fn`) as
// their completion callback.
//
// `iou_basic_loop` itself is non-copyable and non-movable: the rings contain
// kernel-mapped memory with self-referential pointers, and these can only be
// accessed safely from a single thread.
//
// The `RING_SIZE` template parameter controls the number of SQE slots, where
// there are 2X CQE slots. The `SLOT_COUNT` controls the maximum number of
// in-flight operations, which must be less than or equal to the number of CQE
// slots.
//
// TODO:
// - Allow `iou_buf_pool` size to be configured, likely at compile time.
template<size_t RING_SIZE = 256, size_t SLOT_COUNT = 512>
class iou_basic_loop
    : public std::enable_shared_from_this<
          iou_basic_loop<RING_SIZE, SLOT_COUNT>> {
#pragma region Types
public:
  // Buffer pool.
  using buf_pool_t = iou_buf_pool;

  // Enum for block sizes in pool.
  using block_size = buf_pool_t::block_size;

  // RAII handle for a buffer borrowed from the pool.
  using buffer = buf_pool_t::buffer;

  // Mutable and const byte spans.
  using span_t = buffer::span_t;
  using const_span_t = buffer::const_span_t;

  // Timeouts.
  using duration_t = std::chrono::nanoseconds;
  static constexpr duration_t default_run_once_timeout{10ms};
  static constexpr duration_t default_post_and_wait_poll_interval{100ms};
  static constexpr size_t default_max_pending_sqes{RING_SIZE / 4};

  // Callback scheduled via `post` to run on the loop thread. These are used to
  // force single-threading of ring access.
  using posted_fn = std::function<bool()>;

  // Low-level callback invoked when an op completes. Moved to a slot borrowed
  // from the `completion_cb_pool_t`. This avoids the need for
  // `std::shared_ptr` for each `std::function` and also allows the use of
  // `completion_token` as user data for a SQE.
  //
  // If this calls a method on your class, you will likely want to bind in a
  // shared pointer to that instance.
  using completion_fn =
      std::function<slot_retention(completion_id, iou_res, iou_cqe_flags)>;

  // Completion callback for operations using a `buffer`, which includes the
  // `iou_res` and `iou_cqe_flags`. If the `buffer` is not moved from during
  // the callback, it is returned to the pool afterwards.
  //
  // If this calls a method on your class, you will likely want to bind in a
  // shared pointer to that instance.
  using buf_completion_fn =
      std::function<slot_retention(completion_id, buffer&)>;

  // Completion callback for operations that return an endpoint, like `accept`.
  //
  // If this calls a method on your class, you will likely want to bind in a
  // shared pointer to that instance.
  using endpoint_completion_fn = std::function<slot_retention(completion_id,
      iou_res, iou_cqe_flags, combined_endpoint&)>;

#pragma endregion
#pragma region Details
private:
  enum class allow : bool { ctor };
  static_assert((RING_SIZE & (RING_SIZE - 1)) == 0,
      "RING_SIZE must be a power of two");
  static_assert(SLOT_COUNT <= RING_SIZE * 2,
      "SLOT_COUNT must be less than or equal to the number of CQE slots");

  // Cleanup invokable for `completion_fn` objects in the pool.
  struct clear_function_cb {
    constexpr void operator()(auto& completion_fn) const noexcept {
      completion_fn = nullptr;
    }
  };

  // Object pool for `completion_fn` objects, which are used as callbacks for
  // CQE completions. This avoids the need for `std::shared_ptr` for each
  // `std::function` and allows us to refer to callbacks by
  // `completion_token`, which is generation-checking.
  using completion_cb_pool_t = object_pool<completion_fn, SLOT_COUNT * 2,
      generation_scheme::versioned, no_op_cb, clear_function_cb>;

  // RAII handle for a `completion_fn` borrowed from the pool. Can be created
  // from a `completion_token`, like a `std::shared_ptr` locked from a
  // `std::weak_ptr`.
  using borrowed_cb = completion_cb_pool_t::borrowed;

  // Type for the post queue, which holds `posted_fn`s scheduled to run on the
  // loop thread.
  using post_queue_t = std::vector<posted_fn>;

#pragma endregion
#pragma region Infrastructure
public:
  // Generation-checking token for a `completion_fn` in the pool. You can use
  // this to cancel an operation by its callback.
  using completion_token = completion_cb_pool_t::token;

  // Construct a loop with `ring_size` SQE slots (must be a power of two).
  // Throws `std::system_error` if the ring, wakeup `eventfd`, or
  // `buf_pool_t` registration fails.
  //
  // Note: The flags for the ring require that all SQEs be issued from the same
  // thread, and optimizes completions for the single-issuer case.
  explicit iou_basic_loop(allow,
      duration_t post_and_wait_poll_interval =
          default_post_and_wait_poll_interval,
      size_t max_pending_sqes = default_max_pending_sqes)
      : ring_{RING_SIZE,
            iou_setup_flags::setup_single_issuer |
                iou_setup_flags::setup_defer_taskrun |
                iou_setup_flags::setup_submit_all},
        wake_fd_{event_fd::create()}, max_pending_sqes_{max_pending_sqes},
        post_and_wait_poll_interval_{post_and_wait_poll_interval} {
    post_queues_[0].reserve(32);
    post_queues_[1].reserve(32);
    if (!buf_pool_.register_with(ring_))
      throw std::system_error{errno, std::system_category(),
          "io_uring_register_buffers"};
  }

  iou_basic_loop(const iou_basic_loop&) = delete;
  iou_basic_loop& operator=(const iou_basic_loop&) = delete;
  iou_basic_loop(iou_basic_loop&&) = delete;
  iou_basic_loop& operator=(iou_basic_loop&&) = delete;

  // Create a heap-allocated `iou_basic_loop` managed by `std::shared_ptr`.
  [[nodiscard]] static std::shared_ptr<iou_basic_loop>
  make(duration_t post_and_wait_poll_interval =
           default_post_and_wait_poll_interval,
      size_t max_pending_sqes = default_max_pending_sqes) {
    return std::make_shared<iou_basic_loop>(allow::ctor,
        post_and_wait_poll_interval, max_pending_sqes);
  }

  // Returns an RAII guard that designates the calling thread as the loop
  // thread for its lifetime. `run` does this internally; call it manually
  // before using `run_once` directly. For tests only.
  [[nodiscard]] auto poll_thread_scope() const {
    return scoped_value<const iou_basic_loop*>{current_loop_, this};
  }

  // Block until `run` is active. Returns false on timeout.
  [[nodiscard]] bool wait_until_running(duration_t timeout = 60s) {
    return running_.wait_for_value(timeout, true);
  }

  // Run the loop on the calling thread until `stop` is called. This is a
  // blocking call. Used by `iou_loop_runner`.
  size_t run() {
    stop_.store(false, std::memory_order::relaxed);
    const auto scope = poll_thread_scope();
    running_.notify(true);
    scope_exit on_exit{[&] { running_.notify(false); }};
    arm_wake_poll_multishot();

    iou_timespec timeout{default_run_once_timeout};

    size_t total{};
    while (!stop_.load(std::memory_order::acquire)) total = run_once(timeout);
    return total;
  }

  // Process one batch of completions, waiting up to `timeout`. Drains the post
  // queue first, then submits SQEs and waits for CQEs. One of those CQEs may
  // be a wakeup triggered by setting the `eventfd`. Returns the number of
  // completions dispatched, or 0 on timeout or signal. Must be called on the
  // loop thread.
  [[nodiscard]] size_t run_once(const iou_timespec& timeout) {
    assert(is_loop_thread());
    do_drain_post_queue();

    // Simultaneously submit SQEs and wait for CQEs.
    pending_sqe_count_ = 0;
    auto res = ring_.submit_and_wait_timeout(timeout);
    if (res.is_soft_error()) return 0;
    res.throw_if_error("submit_and_wait_timeout");

    // Dispatch snapshotted SQEs.
    size_t dispatched{};
    size_t total = ring_.for_each_snapshotted_cqe([&](iou_cqe cqe) {
      if (do_dispatch(cqe)) ++dispatched;
      return true;
    });

    (void)total;
    return dispatched;
  }

  // Signal the loop to exit after the current `run_once` returns. May be
  // called from any thread.
  void stop() noexcept {
    stop_.store(true, std::memory_order::release);
    (void)do_wake();
  }

  // Borrow a registered `buffer` from the pool for the purpose of reading
  // into it. Returns an invalid `buffer` if the pool is exhausted. Pass to
  // `submit_recv_buffer`.
  [[nodiscard]] buffer borrow_read_buffer(
      block_size sz = block_size::small) noexcept {
    return buf_pool_.borrow_reader(sz);
  }

  // Borrow a registered `buffer` from the pool for the purpose of writing to
  // it. Returns an invalid `buffer` if the pool is exhausted. Fill the
  // `buffer`'s payload, then pass it to `submit_send_buffer`.
  [[nodiscard]] buffer borrow_write_buffer(
      block_size sz = block_size::small) noexcept {
    return buf_pool_.borrow_writer(sz);
  }

#pragma endregion
#pragma region Post

  // The post queue allows callbacks to be scheduled on the loop thread. This
  // is necessary because the `io_uring` ring queues are designed to be
  // accessed from a single thread.
  //
  // For maximum efficiency, public methods executing on the loop thread bypass
  // the post queue. When executed on another thread, they fall back to
  // posting. This is implemented by `execute_or_post`.
  //
  // There are other use cases where you might want to explicitly post to the
  // loop thread, so `post` is public.

  // True if the calling thread is the active loop thread for this instance.
  [[nodiscard]] bool is_loop_thread() const noexcept {
    return current_loop_ == this;
  }

  // Schedule `cbpost` to run on the loop thread at the top of the next
  // `run_once`.
  //
  // You will often want to use `execute_or_post` instead, as it executes
  // inline if already on the loop thread, avoiding unnecessary posting
  // overhead.
  [[nodiscard]] bool post(posted_fn&& cbpost) {
    bool was_empty{};
    if (std::scoped_lock lock{post_mutex_}; true) {
      auto& active_queue = **active_queue_;
      was_empty = active_queue.empty();
      active_queue.emplace_back(std::move(cbpost));
    }
    // On transition from empty, signal the `eventfd` to wake the loop thread.
    if (!was_empty) return true;
    return do_wake();
  }

  // Execute `fn` immediately if on the loop thread; otherwise `post` it. Does
  // not retry on failure.
  [[nodiscard]] bool execute_or_post(PostedInvocable auto&& fn) {
    if (is_loop_thread()) return fn();
    return post(std::move(fn));
  }

  // Execute `fn` immediately if on the loop thread. If it fails, or if we
  // aren't on the loop thread, post it.
  //
  // When it fails while executing from the post queue, it will requeue itself.
  // This only makes sense if the failure is retryable, such as the SQE queue
  // being full. If we encounter an error that isn't retryable, we must return
  // `true` to end the loop.
  //
  // Also, if `fn` has a `completion_token`, then it should check whether it's
  // been released, and return `true` if it has. This allows the caller to
  // abort retries.
  //
  // TODO: Consider whether we should add a retry count that defaults to a
  // reasonable number and decrement each time we requeue. As much as an
  // infinite loop shouldn't happen, it's better if it can't, even in the worst
  // case.
  //
  // Clang-tidy doesn't understand that these lambdas are unconditionally
  // moved, even though this is technically a universal reference.
  [[nodiscard]] bool execute_or_post_with_retry(PostedInvocable auto&& fn) {
    if (!is_loop_thread() || !fn())
      (void)post([this, fn = std::move(fn)]() mutable -> bool {
        return execute_or_post_with_retry(std::move(fn));
      });
    return true;
  }

  // Run `fn` fully synchronously on the loop thread. When executed from
  // another thread, posts and block the calling thread until it completes.
  [[nodiscard]] bool post_and_wait(PostedInvocable auto&& fn) {
    if (!running_.get()) return false;
    if (is_loop_thread()) return fn();
    using fn_type = std::decay_t<decltype(fn)>;
    struct wait_state {
      notifiable<bool> done{false};
      fn_type fn;
      explicit wait_state(fn_type&& f) : fn(std::move(f)) {}
    };
    auto waiter = std::make_shared<wait_state>(std::move(fn));
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

#pragma endregion
#pragma region Completion tokens

  // CQE's are ultimately dispatched to user-provided callbacks of type
  // `completion_fn` (and, indirectly, `buf_completion_fn`, which is bound in;
  // in contrast, `posted_fn` is not involved in this scheme).
  //
  // Callbacks are first moved into the pool, where their slot is referenced by
  // `completion_token`s. These are cheaply-copied, generation-checking tokens
  // that fit in 64 bits. When passes in callbacks, these are watered down to
  // `completion_id`, which is a type-unsafe uint64_t.
  //
  // The `completion_token` provides a persistent identity that handles
  // lifespan issues cleanly. When it's time to invoke the callback, the token
  // is atomically converted into an owned callback, so we fail gracefully if
  // it has already been released for any reason.
  //
  // Simply invalidating the token effectively cancels the callback, but not
  // the underlying operation. The token can also be used to tell `io_uring` to
  // cancel all SQEs associated with that token, which is especially necessary
  // for multi-shot operations.
  //
  // By design, tokens are not owning; their lifecycle is managed by the
  // system. In the standard single-shot workflow, the token remains valid
  // until the CQE is processed, at which point the callback returns the
  // default `slot_retention::automatic`, which leads to it being released.

  // By returning `slot_retention::retain`, the callback can attach itself to
  // another SQE without going through the pool again. When using a
  // `buf_completion_fn`, you may also need to move the `buffer` to keep it
  // from being released.
  //
  // Multishot operations are a bit more complex. So long as the CQE has
  // `iou_cqe_flags::more` set, then `slot_retention::automatic` acts as
  // `slot_retention::retain`.
  //
  // However, `io_uring` can choose to clear `iou_cqe_flags::more` on any CQE,
  // even on a soft error or no error (such as when IORING_MAX_FIXED_FILES is
  // reached), so the callback must either accept this or resubmit the token.
  // In the latter case, it must explicitly return `slot_retention::retain`.
  //
  // All methods in this section are thread-safe.

  // Determines whether the `completion_fn` associated with `cbtoken` has
  // already been released. If it has, then `cbtoken` is invalidated, since it
  // could never be used again.
  //
  // Note that a `true` is reliable and permanent, but a `false` reflects only
  // the state at that moment.
  [[nodiscard]] bool is_released(completion_token& cbtoken) noexcept {
    if (cbtoken.get_ptr(completion_cb_pool_)) return false;
    cbtoken = {};
    return true;
  }

  // Convert a `completion_fn` into a `completion_token` by moving it into the
  // pool. On failure, the return value is invalid.
  [[nodiscard]] completion_token tokenize(completion_fn&& cb) {
    if (!cb) return {};
    return completion_token{borrow(std::move(cb))};
  }

  // Release the `completion_fn` associated with `cbtoken` back into the pool.
  // Returns whether it was successfully released. Note that if the callback
  // was already released or is currently borrowed, this will fail.
  [[nodiscard]] bool release(completion_token&& cbtoken) {
    const auto borrowed = cbtoken.borrow(completion_cb_pool_);
    if (!borrowed) return false;
    return true;
  }

  // Create completion callback that releases `cancelation_token` when invoked.
  [[nodiscard]] completion_fn make_release_fn(
      completion_token&& cancelation_token) {
    return [this, cancelation_token](completion_id, iou_res,
               iou_cqe_flags) mutable {
      (void)release(std::move(cancelation_token));
      return slot_retention::automatic;
    };
  }

  // Borrow a callback pool slot and move `cb` into it.
  // This has only niche uses.
  [[nodiscard]] borrowed_cb borrow(completion_fn&& cb) {
    auto borrowed_cb = completion_cb_pool_.borrow();
    if (borrowed_cb) *borrowed_cb = std::move(cb);
    return borrowed_cb;
  }

  // Borrow a callback pool slot from its `completion_token`.
  // This has only niche uses.
  [[nodiscard]] borrowed_cb borrow(completion_token token) {
    return token.borrow(completion_cb_pool_);
  }

  // Detach a borrowed callback, without freeing it.
  // This has only niche uses.
  void detach(borrowed_cb&& cb) {
    (void)completion_cb_pool_.detach(std::move(cb));
  }

#pragma endregion
#pragma region Callbacks

  // Accept the inner `buf_completion_fn`-shaped lambda and `buffer`, binding
  // them into a `completion_fn`. Does not work with Provided Buffers.
  completion_fn
  wrap_buf_completion_fn(BufCompletionInvocable auto&& bufcb, buffer&& buf) {
    return [bufcb = std::move(bufcb),
               buf = std::move(buf)](completion_id cbhandle, iou_res res,
               iou_cqe_flags flags) mutable -> slot_retention {
      return bufcb(cbhandle, buf.update(res, flags));
    };
  }

  // Wrap a `buf_completion_fn`-shaped lambda and `buffer` into a
  // `completion_token` and `buffer*`.
  std::pair<completion_token, buffer*> wrap_buf_completion_fn_and_ptr(
      BufCompletionInvocable auto&& bufcb, buffer&& buf) {
    buffer* buf_ptr{};
    buf.forwarding_address() = &buf_ptr;
    const auto cbtoken =
        tokenize(wrap_buf_completion_fn(std::move(bufcb), std::move(buf)));
    if (buf_ptr) buf_ptr->forwarding_address() = nullptr;
    return {cbtoken, buf_ptr};
  }

  // Accept the inner `endpoint_completion_fn`-shaped lambda and an endpoint
  // with a `sockaddr` member (e.g. `sockaddr_endpoint` or
  // `flagged_timeout_endpoint`), binding them into a `completion_fn`.
  template<typename EP>
    requires AddressForwarder<EP> && requires(EP ep) { ep.sockaddr; }
  completion_fn wrap_endpoint_completion_fn(
      EndpointCompletionInvocable auto&& epcb, EP&& endpoint) {
    return [epcb = std::move(epcb),
               endpoint = std::move(endpoint)](completion_id cbhandle,
               iou_res res, iou_cqe_flags flags) mutable -> slot_retention {
      endpoint.sockaddr.len = net_endpoint::max_sockaddr_size;
      return epcb(cbhandle, res, flags, endpoint.sockaddr);
    };
  }

  // Wrap an `endpoint_completion_fn`-shaped lambda and an endpoint with a
  // `sockaddr` member (e.g. `sockaddr_endpoint` or
  // `flagged_timeout_endpoint`) into a `completion_token` and endpoint
  // pointer.
  template<typename EP>
    requires AddressForwarder<EP> && requires(EP ep) { ep.sockaddr; }
  std::pair<completion_token, EP*>
  wrap_endpoint_completion_fn_and_ptr(EndpointCompletionInvocable auto&& cb,
      EP&& ep) {
    EP* ep_ptr{};
    ep.forwarding_address() = &ep_ptr;
    const auto cbtoken =
        tokenize(wrap_endpoint_completion_fn(std::move(cb), std::move(ep)));
    if (ep_ptr) ep_ptr->forwarding_address() = nullptr;
    return {cbtoken, ep_ptr};
  }

  // Wrap a `completion_fn`-shaped lambda and `AddressForwarder` into a
  // `completion_fn`.
  // TODO: Generalize to check for `payload`, and forward that as well if it
  // exists. This would require expanding CompletionInvocable to allow for that
  // extra parameter based on whether `payload` exists.
  completion_fn wrap_completion_fn_and_timeout(CompletionInvocable auto&& cb,
      AddressForwarder auto&& af) {
    return [wrapcb = std::move(cb), af = std::move(af)](completion_id cbhandle,
               iou_res res, iou_cqe_flags flags) mutable -> slot_retention {
      return wrapcb(cbhandle, res, flags);
    };
  }

  // Wrap a `completion_fn`-shaped lambda and `AddressForwarder` into a
  // `completion_token` and `AddressForwarder*`. If the `AddressForwarder` is
  // invalid, then it will not be wrapped, and the `AddressForwarder*` returned
  // will be nullptr.
  template<AddressForwarder AF>
  std::pair<completion_token, AF*>
  wrap_completion_fn_and_timeout_ptr(CompletionInvocable auto&& cb, AF&& af) {
    AF* ptr{};
    completion_token cbtoken;
    if (af.is_valid()) {
      af.forwarding_address() = &ptr;
      cbtoken = tokenize(
          wrap_completion_fn_and_timeout(std::move(cb), std::move(af)));
      ptr->forwarding_address() = nullptr;
    } else
      cbtoken = tokenize(std::move(cb));

    return {cbtoken, ptr};
  }

#pragma endregion
#pragma region Submit

  // Submit overview:
  //
  // Methods in the family of `submit_nop` acquire, prepare, and (eventually)
  // submit a SQE. These typically come in sets with three variants:
  // single-shot, with  `completion_fn` and `completion_token`; and multi-shot,
  // always with `completion_token`.
  //
  // These methods all use `execute_or_post_with_retry`, which not only
  // attempts to execute inline on the loop thread, but falls back to posting.
  // Moreover, the post is wrapped in a retry loop, that keeps reposting itself
  // until the transient error (usually SQE congestion) is resolved.
  //
  // Variants:
  //
  // For the `completion_fn` variants, the callback is moved into the pool and
  // associated with a `completion_token`. This token is returned on success,
  // and is invalid on immediate failure.
  //
  // For the `completion_token` variants, the caller must have already acquired
  // a token by calling `tokenize` on a `completion_fn`. On success, `true` is
  // returned. Depending on `on_fail`, the token is either released or retained
  // on failure.
  //
  // Other variants are similar to `completion_fn` except that they may also
  // add a payload parameter, such as a `buffer` or `net_endpoint`.
  //
  // Timeouts:
  //
  // When it makes sense for the operation, single-shot variants take a timeout
  // in the form of an `iou_timespec`. For multi-shot variants, timeouts would
  // apply to the SQE as a whole, not to individual CQEs, so you should instead
  // `submit_timeout` separately and check for a timestamp updated by the
  // `completion_callback`.

  // Submit immediately if there are any pending SQEs. Not generally necessary.
  [[nodiscard]] bool immediate_submit() {
    if (!is_loop_thread()) return false;
    if (!pending_sqe_count_) return true;
    pending_sqe_count_ = 0;
    auto res = ring_.submit();
    return (res.ok() || res.is_soft_error());
  }

#pragma region Nop

  // Per the name, nop does nothing except trigger a completion.

  // Submit an async nop.
  [[nodiscard]] completion_token submit_nop(completion_fn&& cb) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!submit_nop(cbtoken, slot_retention::automatic)) return {};
    return cbtoken;
  }

  // Submit an async nop.
  [[nodiscard]] bool submit_nop(completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    auto fn = [this, cbtoken, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [](iou_sqe sqe) { sqe.prep_nop(); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Poll

  // Submit an async poll on `file`. Completes when `file` is ready for the
  // events specified by `poll_mask`.
  [[nodiscard]] completion_token submit_poll(const os_file& file,
      CompletionInvocable auto&& cb, poll_flags poll_mask = poll_flags::in,
      flagged_timeout timeout = {}) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_timeout_ptr(std::move(cb), std::move(timeout));
    if (!submit_poll(file, timeout_ptr, cbtoken, poll_mask,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async poll on `file`. Note that `timeout` must either be
  // nullptr, point inside the callback, or remain valid until cancelation.
  [[nodiscard]] bool submit_poll(const os_file& file, flagged_timeout* timeout,
      completion_token cbtoken, poll_flags poll_mask = poll_flags::in,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    auto fn = [this, cbtoken, fd = *file, timeout = timeout, poll_mask,
                  on_fail]() mutable {
      return do_submit_timeout(cbtoken, timeout, on_fail,
          [fd, poll_mask](iou_sqe sqe) { sqe.prep_poll(fd, poll_mask); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

  // Submit an async multishot poll on `file`.
  [[nodiscard]] completion_token submit_poll_multishot(const os_file& file,
      completion_fn&& cb, poll_flags poll_mask = poll_flags::in) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!submit_poll_multishot(file, cbtoken, poll_mask,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async multishot poll on `file`.
  [[nodiscard]] bool submit_poll_multishot(const os_file& file,
      completion_token cbtoken, poll_flags poll_mask = poll_flags::in,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    auto fn = [this, cbtoken, fd = *file, poll_mask, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [fd, poll_mask](iou_sqe sqe) {
        sqe.prep_poll_multishot(fd, poll_mask);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Timeout

  // Allows creating, removing, and updating stand-alone timeouts.
  //
  // These are not linked to existing operations: that functionality is
  // provided through the `timeout` parameter on those methods.

  //
  // Create
  //

  // Submit an async timeout. For multishot, set the
  // `io_timeout_flags::multishot` flag. The meaning of `cqe_count` depends on
  // the mode.
  [[nodiscard]] completion_token submit_timeout(flagged_timeout&& timeout,
      CompletionInvocable auto&& cb, size_t cqe_count = 0) {
    if (!timeout.is_valid()) return {};
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_timeout_ptr(std::move(cb), std::move(timeout));
    if (!submit_timeout(*timeout_ptr, cbtoken, cqe_count,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async timeout. For multishot, set the
  // `io_timeout_flags::multishot` flag. The meaning of `cqe_count` depends on
  // the mode. Note that `timeout` must either point inside the callback or
  // remain valid until cancelation.
  [[nodiscard]] bool submit_timeout(flagged_timeout& timeout,
      completion_token cbtoken, size_t cqe_count = 0,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    auto fn = [this, &timeout, cbtoken, cqe_count, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [&timeout, cqe_count](iou_sqe sqe) {
        sqe.prep_timeout(&timeout.when.ts, timeout.when.flags, cqe_count);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

  //
  // Remove
  //

  // Submit an async timeout removal.
  [[nodiscard]] completion_token submit_timeout_remove(
      completion_token cancelation_token) {
    const auto cbtoken =
        tokenize(make_release_fn(std::move(cancelation_token)));
    if (!submit_timeout_remove(cancelation_token, cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async timeout removal.
  [[nodiscard]] bool submit_timeout_remove(completion_token cancelation_token,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    auto fn = [this, cancelation_token, cbtoken, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [cancelation_token](iou_sqe sqe) {
        sqe.prep_timeout_remove(cancelation_token.as_int());
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

  //
  // Update.
  //

  // Submit an async timeout update. The meaning of `new_timeout` depends on
  // the mode. The `timeout` must remain valid until cancelation.
  [[nodiscard]] bool submit_timeout_update(completion_token update_token,
      flagged_timeout& timeout,
      slot_retention on_fail = slot_retention::retain) {
    if (!update_token.is_valid()) return false;
    auto fn = [this, update_token, &timeout, on_fail]() mutable {
      return do_submit(update_token, on_fail,
          [update_token, &timeout](iou_sqe sqe) {
            sqe.prep_timeout_update(&timeout.when.ts, update_token,
                timeout.when.flags);
          });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Close

  // Submit an async close on `file`. Invalidates `file`.
  [[nodiscard]] completion_token submit_close(os_file&& file,
      CompletionInvocable auto&& cb, flagged_timeout timeout = {}) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_timeout_ptr(std::move(cb), std::move(timeout));
    if (!submit_close(std::move(file), cbtoken, timeout_ptr,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async close on `file`. Invalidates `file`. Note that `timeout`
  // must either be nullptr, point inside the callback, or remain valid until
  // cancelation.
  [[nodiscard]] bool submit_close(os_file&& file, completion_token cbtoken,
      flagged_timeout* timeout = nullptr,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    if (!file) return fail_and_maybe_release(on_fail, cbtoken);
    combined_timespec* tsf = timeout ? &timeout->when : nullptr;
    auto fn = [this, fd = file.release(), cbtoken, tsf, on_fail]() mutable {
      return do_submit_timeout(cbtoken, tsf, on_fail, [fd](iou_sqe sqe) {
        sqe.prep_close(fd);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Shutdown

  // Submit an async shutdown on `file`.
  [[nodiscard]] completion_token
  submit_shutdown(const os_file& file, shutdown_how how, completion_fn&& cb) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!submit_shutdown(std::move(file), how, cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async shutdown on `file`.
  [[nodiscard]] bool submit_shutdown(const os_file& file, shutdown_how how,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    if (!file) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = file.handle(), how, cbtoken, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [fd, how](iou_sqe sqe) {
        sqe.prep_shutdown(fd, how);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Accept

  // Submit an async accept on `socket`.
  [[nodiscard]] completion_token submit_accept(const os_file& socket,
      EndpointCompletionInvocable auto&& cb,
      flagged_timeout_endpoint endpoint = {}) {
    auto [cbtoken, endpoint_ptr] = wrap_endpoint_completion_fn_and_ptr(
        std::move(cb), std::move(endpoint));
    if (!submit_accept(socket, *endpoint_ptr, cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async accept on `socket`. Note that `endpoint` must either point
  // inside the callback or remain valid until cancelation.
  [[nodiscard]] bool submit_accept(const os_file& socket,
      flagged_timeout_endpoint& endpoint, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    if (!socket) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, &endpoint, cbtoken, on_fail]() mutable {
      return do_submit_timeout(cbtoken, &endpoint.when, on_fail,
          [fd, &endpoint](iou_sqe sqe) {
            sqe.prep_accept(fd, &endpoint.sockaddr);
          });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

  // Submit a multishot async accept on `socket`.
  [[nodiscard]] bool submit_accept_multishot(const os_file& socket,
      combined_endpoint& endpoint, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    if (!socket) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, &endpoint, cbtoken, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [fd, &endpoint](iou_sqe sqe) {
        sqe.prep_accept_multishot(fd, &endpoint);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }
#pragma endregion
#pragma region Connect

  // Submit an async connect on `socket` to `endpoint`.
  [[nodiscard]] completion_token submit_connect(const os_file& socket,
      flagged_timeout_endpoint&& remote, completion_fn&& cb) {
    auto [cbtoken, endpoint_ptr] =
        wrap_completion_fn_and_timeout_ptr(std::move(cb), std::move(remote));
    if (!submit_connect(socket, *endpoint_ptr, cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async connect on `socket` to `remote`. `remote` must point
  // inside the callback or remain valid until cancelation.
  [[nodiscard]] bool submit_connect(const os_file& socket,
      flagged_timeout_endpoint& remote, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    if (!socket) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, &remote, cbtoken, on_fail]() mutable {
      return do_submit_timeout(cbtoken, &remote.when, on_fail,
          [fd, &remote](iou_sqe sqe) {
            sqe.prep_connect(fd, &remote.sockaddr.sockaddr);
          });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region CancelFile

  // Submit an async cancel on `file`. Cancels all ops for the file.
  [[nodiscard]] completion_token
  submit_cancel(const os_file& file, completion_fn&& cb) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!submit_cancel(file, cbtoken, slot_retention::automatic)) return {};
    return cbtoken;
  }

  // Submit an async cancel on `file`. Note that `timeout` must either point
  // inside the callback or remain valid until cancelation.
  [[nodiscard]] bool submit_cancel(const os_file& file,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    if (!file) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *file, cbtoken, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [fd](iou_sqe sqe) {
        sqe.prep_cancel_fd(fd);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region CancelToken

  // Submit an async cancel on `cancelation_token`.
  [[nodiscard]] completion_token
  submit_cancel(completion_token cancelation_token, completion_fn&& cb) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!submit_cancel(cancelation_token, cbtoken, slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async cancel on `cancelation_token`.
  [[nodiscard]] bool submit_cancel(completion_token cancelation_token,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    if (!cancelation_token) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, cancelation_token, cbtoken, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [cancelation_token](iou_sqe sqe) {
        sqe.prep_cancel_user_data(cancelation_token.as_int());
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region RecvBytes

  // Low-level method to submit an async recv on `socket` into bytes at `span`.
  //
  // Prefer `submit_recv_buffer` over this.
  [[nodiscard]] completion_token submit_recv_bytes(const os_file& socket,
      span_t span, completion_fn&& cb, flagged_timeout timeout = {}) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_timeout_ptr(std::move(cb), std::move(timeout));
    if (!submit_recv_bytes(socket, span, cbtoken, &timeout_ptr->when,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async recvmsg on `socket`. Note that `timeout` must either be
  // null, inside the callback, or remain valid until cancelation.
  [[nodiscard]] bool submit_recv_bytes(const os_file& socket, span_t span,
      completion_token cbtoken, combined_timespec* timeout = nullptr,
      slot_retention on_fail = slot_retention::retain, msg_flags flags = {}) {
    if (!cbtoken.is_valid()) return false;
    if (!socket || span.empty())
      return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, flags, cbtoken, span, timeout,
                  on_fail]() mutable {
      return do_submit_timeout(cbtoken, timeout, on_fail,
          [fd, flags, span](iou_sqe sqe) { sqe.prep_recv(fd, span, flags); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region SendBytes

  // Low-level method to submit an async send on `socket` from bytes at `span`.
  //
  // Prefer `submit_send_buffer` over this.
  [[nodiscard]] completion_token submit_send_bytes(const os_file& socket,
      const_span_t span, completion_fn&& cb, flagged_timeout timeout = {}) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_timeout_ptr(std::move(cb), std::move(timeout));
    if (!submit_send_bytes(socket, span, cbtoken, &timeout_ptr->when,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async send on `socket`. Note that `timeout` must either be null,
  // inside the callback, or remain valid until cancelation.
  [[nodiscard]] bool submit_send_bytes(const os_file& socket,
      const_span_t span, completion_token cbtoken,
      combined_timespec* timeout = nullptr,
      slot_retention on_fail = slot_retention::retain, msg_flags flags = {}) {
    if (!cbtoken.is_valid()) return false;
    if (!socket || span.empty())
      return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, flags, cbtoken, span, timeout,
                  on_fail]() mutable {
      return do_submit_timeout(cbtoken, timeout, on_fail,
          [fd, flags, span](iou_sqe sqe) { sqe.prep_send(fd, span, flags); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region RecvMsg

  // TODO: We need a version that takes  a buffer, and likely wrap the msg.
  // Also, are the flags correct? We might want a multishot.

  // Submit an async recvmsg on `socket`.
  [[nodiscard]] completion_token submit_recvmsg(const os_file& socket,
      msghdr* msg, completion_fn&& cb, flagged_timeout timeout = {},
      msg_flags flags = {}) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_timeout_ptr(std::move(cb), std::move(timeout));
    if (!submit_recvmsg(socket, msg, cbtoken, &timeout_ptr->when,
            slot_retention::automatic, flags))
      return {};
    return cbtoken;
  }

  // Submit an async recvmsg on `socket`.
  [[nodiscard]] bool submit_recvmsg(const os_file& socket, msghdr* msg,
      completion_token cbtoken, combined_timespec* timeout = nullptr,
      slot_retention on_fail = slot_retention::retain, msg_flags flags = {}) {
    if (!cbtoken.is_valid()) return false;
    if (!socket || !msg) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, flags, cbtoken, msg, timeout,
                  on_fail]() mutable {
      return do_submit_timeout(cbtoken, timeout, on_fail,
          [fd, flags, msg](iou_sqe sqe) { sqe.prep_recvmsg(fd, msg, flags); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region SendMsg

  // TODO: This needs to take a buffer, and likely wrap the msg. Also, are the
  // flags correct?

  // Submit an async sendmsg on `socket`.
  [[nodiscard]] completion_token submit_sendmsg(const os_file& socket,
      const msghdr* msg, completion_fn&& cb, flagged_timeout timeout = {},
      msg_flags flags = msg_flags::nosignal) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_timeout_ptr(std::move(cb), std::move(timeout));
    if (!submit_sendmsg(socket, msg, cbtoken, &timeout_ptr->when,
            slot_retention::automatic, flags))
      return {};
    return cbtoken;
  }

  // Submit an async sendmsg on `socket`.
  [[nodiscard]] bool submit_sendmsg(const os_file& socket, const msghdr* msg,
      completion_token cbtoken, combined_timespec* timeout = nullptr,
      slot_retention on_fail = slot_retention::retain,
      msg_flags flags = msg_flags::nosignal) {
    if (!cbtoken.is_valid()) return false;
    if (!socket || !msg) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, flags, cbtoken, msg, timeout,
                  on_fail]() mutable {
      return do_submit_timeout(cbtoken, timeout, on_fail,
          [fd, flags, msg](iou_sqe sqe) { sqe.prep_sendmsg(fd, msg, flags); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region ReadBuffer

  //
  // Read into buffers from `os_file`. Works with sockets or files, where the
  // latter interacts with `buffer::file_offset`.
  //

  // Submit an async read_fixed on `file` into a borrowed buffer.
  template<BufCompletionInvocable FN>
  [[nodiscard]] completion_token submit_read_buffer(const os_file& file,
      FN&& bufcb, block_size sz = block_size::small,
      const combined_timespec& timeout = {}) {
    buffer buf = borrow_read_buffer(sz);
    buf.timeout() = timeout;
    return submit_read_buffer(file, std::move(buf), std::forward<FN>(bufcb));
  }

  // Submit an async read_fixed on `file` into `buf`.
  template<BufCompletionInvocable FN>
  [[nodiscard]] completion_token
  submit_read_buffer(const os_file& file, buffer&& buf, FN&& bufcb) {
    const auto [cbtoken, buf_ptr] = wrap_buf_completion_fn_and_ptr(
        std::forward<FN>(bufcb), std::move(buf));
    if (!submit_read_buffer(file, *buf_ptr, cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async read_fixed on `file`. Note that `buf` must point inside
  // the callback.
  [[nodiscard]] bool submit_read_buffer(const os_file& file, buffer& buf,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    auto [span, buf_index, file_offset] = buf.prepare();
    if (!file || span.empty()) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *file, cbtoken, span, buf_index, file_offset,
                  &timeout = buf.timeout(), on_fail]() mutable {
      return do_submit_timeout(cbtoken, &timeout, on_fail,
          [fd, span, buf_index, file_offset](iou_sqe sqe) {
            sqe.prep_read_fixed(fd, span, buf_index, file_offset);
          });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region WriteBuffer

  //
  // Write from buffers to `os_file`.
  //
  // Works with sockets or files. Files interact with `buffer::file_offset` For
  // sockets, there's no way to pass `msg_flags::nosignal` so you need to
  // disable SIGPIPE process-wide. Prefer `submit_send_buffer` over this, if
  // you can justify the added complexity.
  //
  // The reason for this recommendation is that this uses fixed buffers with
  // `io_uring_prep_write_fixed`, which is not socket-specific or zero-copy.
  // However, it only generates a single CQE, which is  simpler to deal with
  // and is likely to be faster for packets under 10k. Contrast with
  // `submit_send_buffer`, which uses `io_uring_prep_send_zc_fixed`.

  // Submit an async write_fixed on `file` from `buf`.
  template<BufCompletionInvocable FN>
  [[nodiscard]] completion_token
  submit_write_buffer(const os_file& file, buffer&& buf, FN&& bufcb) {
    const auto [cbtoken, buf_ptr] = wrap_buf_completion_fn_and_ptr(
        std::forward<FN>(bufcb), std::move(buf));
    if (!submit_write_buffer(file, *buf_ptr, cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async write_fixed on `file`. Note that `buf` must point inside
  // the callback.
  [[nodiscard]] bool submit_write_buffer(const os_file& file, buffer& buf,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken.is_valid()) return false;
    auto [span, buf_index, file_offset] = buf.prepare();
    if (!file || span.empty()) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *file, cbtoken, span, buf_index, file_offset,
                  &timeout = buf.timeout(), on_fail]() mutable {
      return do_submit_timeout(cbtoken, &timeout, on_fail,
          [fd, span, buf_index, file_offset](iou_sqe sqe) {
            sqe.prep_write_fixed(fd, span, buf_index, file_offset);
          });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region RecvBufferMulti

  // TODO: `submit_recv_buffer_multishot` is the most complicate way to read
  // from a socket, requiring a whole new type of buffer pool, for Provided
  // Buffers.

#pragma endregion
#pragma region SendBufferZeroCopy

  // TODO: `submit_send_buffer_zero_copy` is the most complicated way to write
  // to a socket, requiring special handling of completions to avoid
  // prematurely releasing buffers.

#pragma endregion
// Next lines closes "Submit".
#pragma endregion
#pragma region Helpers.
private:
  // Get an SQE, prepare it via `prep`, attach `cbtoken` as user data, and
  // potentially submit. Note that `cbtoken` is allowed to be `!is_valid` but
  // not released.
  //
  // If `timeout` specified, links a timeout SQE to it. See warnings about
  // `combined_timespec` lifetime in class documentation.
  [[nodiscard]] bool do_submit_timeout(completion_token cbtoken,
      combined_timespec* timeout, slot_retention on_fail,
      std::invocable<iou_sqe> auto&& prep) {
    assert(on_fail != slot_retention::release); // Would be dumb.
    auto do_submit = [&]() {
      // Must return true to end retries.
      if (cbtoken.is_valid() && is_released(cbtoken)) return true;

      // Check availability up front and only assert on each.
      bool use_timeout = timeout && timeout->ts.is_valid();
      size_t sqe_needed = use_timeout ? 2 : 1;
      if (!ring_.enough_sqe_available(sqe_needed)) return false;

      // Queue the operation SQE.
      auto sqe_op = ring_.next_sqe();
      assert(sqe_op);

      // If timeout, follow it up with a linked timeout SQE.
      iou_sqe sqe_to;

      if (use_timeout) sqe_to = ring_.next_sqe();

      // Prep the operation.
      std::forward<decltype(prep)>(prep)(sqe_op);

      // If there's a timeout, link it.
      if (use_timeout) {
        assert(sqe_to);
        sqe_op.set_sqe_flags(iou_sqe_flags::io_link);
        sqe_to.prep_link_timeout(&timeout->ts, timeout->flags);
        // On timeout, `cbtoken` is invoked with `iou_res.err() ==
        // E_::canceled`. `sqe_to` will also generate a CQE (with
        // `iou_res.err() == E_::time`), but this will be swallowed.
        // When the operation fails, then it's the timeout CQE that gets
        // the `E_canceled`.
        sqe_to.set_data_pointer(nullptr);
      }

      // Store as token, "leaking" it.
      sqe_op.set_data_int(cbtoken.as_int());
      if (!maybe_submit_pending(sqe_needed)) return false;
      return true;
    };

    if (!do_submit()) return fail_and_maybe_release(on_fail, cbtoken);
    return true;
  }

  // Get an SQE, prepare it via `prep`, attach `cbtoken` as user data, and
  // potentially submit.  Note that `cbtoken` is allowed to be `!is_valid`
  // but not released.
  //
  // If `timeout` specified, links a timeout SQE to it. See warnings about
  // `iou_timespec` lifetime in class documentation.
  [[nodiscard]] bool do_submit(completion_token cbtoken,
      slot_retention on_fail, std::invocable<iou_sqe> auto&& prep) {
    assert(is_loop_thread());
    auto do_submit = [&]() {
      if (cbtoken.is_valid() && is_released(cbtoken)) return true;

      // Queue the operation SQE.
      auto sqe_op = ring_.next_sqe();
      if (!sqe_op) return false;

      // Prep the operation.
      std::forward<decltype(prep)>(prep)(sqe_op);

      // Store as token, "leaking" it.
      sqe_op.set_data_int(cbtoken.as_int());
      if (!maybe_submit_pending(1)) return false;
      return true;
    };

    if (!do_submit()) return fail_and_maybe_release(on_fail, cbtoken);
    return true;
  }

  // Error-handling helper. Always returns false, and optionally performs
  // cleanup.
  [[nodiscard]] bool
  fail_and_maybe_release(slot_retention on_fail, completion_token& cbtoken) {
    assert(on_fail != slot_retention::release);
    if (on_fail != slot_retention::retain) (void)release(std::move(cbtoken));
    return false;
  }

  // Submit pending SQEs, although this could be delayed.
  //
  // The `sqe_count` is added to the pending value, and if it exceeds the
  // configured limit, the submit is triggered immediately.
  [[nodiscard]] bool maybe_submit_pending(size_t sqe_count = 1) {
    assert(is_loop_thread());
    pending_sqe_count_ += sqe_count;
    if (pending_sqe_count_ >= max_pending_sqes_) return immediate_submit();
    return true;
  }

  // Fire-and-forget submit: no callback, null user data. `do_dispatch`
  // ignores null-data CQEs, so no pool slot is needed.
  [[nodiscard]] bool do_submit_void(std::invocable<iou_sqe> auto&& prep) {
    assert(is_loop_thread());
    auto sqe = ring_.next_sqe();
    if (!sqe) return false;
    std::forward<decltype(prep)>(prep)(sqe);
    sqe.set_data_pointer(nullptr);
    maybe_submit_pending(1);
    return immediate_submit();
  }

  // Dispatch a CQE to its registered callback. The callback returns a
  // `dispatch_result` that controls slot retention:
  //   - `automatic`: retain iff `IORING_CQE_F_MORE` is set (multishot
  //   default).
  //   - `release`: always free the slot.
  //   - `retain`: always keep the slot (e.g., callback resubmits a new SQE).
  // Returns false if no valid callback was found (canceled or no-data CQE).
  bool do_dispatch(iou_cqe cqe) {
    completion_token token{cqe.get_data_int()};
    if (!token) return true;

    // Take ownership of the callback. Quietly fails if the generation has
    // been invalidated.
    auto borrowed_cb = token.borrow(completion_cb_pool_);
    if (!borrowed_cb || !*borrowed_cb) return false;

    const auto retention = borrowed_cb(token.as_int(), cqe.res(), cqe.flags());
    const bool keep =
        retention == slot_retention::retain ||
        (retention == slot_retention::automatic &&
            bitmask::has(cqe.flags(), iou_cqe_flags::more));

    // To keep, steal ownership away without freeing.
    if (keep) detach(std::move(borrowed_cb));
    return true;
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

  // Submit a multishot `IORING_OP_POLL_ADD` for the wakeup `eventfd`. Each
  // time `post` or `stop` writes to the `eventfd`, the poll fires as a CQE
  // and interrupts `io_uring_wait_cqe_timeout`. Because the operation is
  // multishot, the kernel keeps it alive as long as `IORING_CQE_F_MORE` is
  // set; the callback drains the `eventfd` on each firing. If the kernel
  // ends the multishot (flag absent), the callback resubmits.
  bool arm_wake_poll_multishot() {
    if (wake_poll_token_.is_valid()) return true;

    // Set up a callback that resubmits itself, and use it too bootstrap the
    // initial submission.
    auto raw_cb =
        [this](completion_id cbhandle, iou_res, iou_cqe_flags flags) {
          (void)wake_fd_.read();
          if (bitmask::has(flags, iou_cqe_flags::more))
            return slot_retention::automatic;
          if (!submit_poll_multishot(wake_fd_, completion_token{cbhandle},
                  poll_flags::in, slot_retention::retain))
            throw std::runtime_error{"failed to resubmit wake poll multishot"};
          return slot_retention::retain;
        };

    // Make sure it doesn't just lock up at the start.
    (void)do_wake();

    // Pretend a CQE arrived.
    wake_poll_token_ = tokenize(std::move(raw_cb));
    auto borrowed = borrow(wake_poll_token_);

    // Get it to submit itself.
    (void)borrowed(wake_poll_token_.as_int(), iou_res{}, iou_cqe_flags{});

    detach(std::move(borrowed));
    return true;
  }

  [[nodiscard]] bool do_wake() noexcept { return wake_fd_.notify(); }

#pragma endregion
#pragma region Data members
private:
  // Rings and buffers. (Order is important.)
  buf_pool_t buf_pool_;
  iou_ring ring_;

  // Poll wake system.
  event_fd wake_fd_;
  completion_token wake_poll_token_;

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

  size_t max_pending_sqes_{};
  size_t pending_sqe_count_{};
  const duration_t post_and_wait_poll_interval_;
#pragma endregion
};

// Alias for the common case of default template parameters.
using iou_loop = iou_basic_loop<>;

#pragma endregion
#pragma region Loop Runner

// Runs an `iou_basic_loop` in its own background thread.
//
// Shutdown ordering: call `stop` (or destroy the runner) before destroying
// any object that a pending `post` callback might reference.
template<size_t RING_SIZE = 256, size_t SLOT_COUNT = 512>
class iou_basic_loop_runner {
public:
  using loop_t = iou_basic_loop<RING_SIZE, SLOT_COUNT>;

  explicit iou_basic_loop_runner(
      std::chrono::nanoseconds post_and_wait_poll_interval =
          loop_t::default_post_and_wait_poll_interval)
      : post_and_wait_poll_interval_{post_and_wait_poll_interval},
        thread_{[this](const std::stop_token& st) { run(st); }} {
    if (!started_.wait_for_value(std::chrono::milliseconds{1000}, true)) {
      thread_.request_stop();
      throw std::runtime_error{"iou_loop_runner failed to start"};
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
    if (!loop || !loop->wait_until_running(1000ms)) {
      thread_.request_stop();
      if (thread_.joinable()) thread_.join();
      throw std::runtime_error{"iou_loop_runner failed to start"};
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

  const std::chrono::nanoseconds post_and_wait_poll_interval_;
  std::mutex startup_mutex_;
  std::exception_ptr startup_error_;
  notifiable<std::atomic_bool> started_{false};
  std::shared_ptr<loop_t> loop_;
  std::jthread thread_;
};

// Alias for the common case of default template parameters.
using iou_loop_runner = iou_basic_loop_runner<>;

#pragma endregion

// NOLINTEND(bugprone-move-forwarding-reference)

}}} // namespace corvid::proto::iouring
