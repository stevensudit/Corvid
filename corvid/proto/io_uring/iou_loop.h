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
#include "../../meta/fixed_function.h"
#include "../../proto/net_endpoint.h"
#include "../../meta/concepts.h"
#include "../../filesys/os_file.h"
#include "../../concurrency/jthread_stoppable_sleep.h"
#include "../../concurrency/notifiable.h"
#include "../../concurrency/relaxed_atomic.h"
#include "../../concurrency/owner_thread_dispatcher.h"
#include "../../containers/scope_exit.h"
#include "../../containers/scoped_value.h"
#include "../../containers/object_pool.h"
#include "iou_buf_pool.h"
#include "iou_provided_buf_pool.h"
#include "iou_wrap.h"

namespace corvid { inline namespace proto { namespace iouring {
using namespace std::chrono_literals;

// NOLINTBEGIN(bugprone-move-forwarding-reference)
//
// This is a legitimate warning, but clang-tidy is applying it illegitimately,
// so it had to be disabled.
//
// It's complaining that a parameter that looks like an rvalue is actually a
// universal reference, so instead of being moved from, it should be forwarded.
// That's because a universal reference could bind to an lvalue.
//
// However, we use concepts to constrain the type to ensure that it's an
// rvalue, so forwarding is not necessary and the warning is wrong. See
// `MoveConsumable`. Since clang-tidy can't understand this, it
// spews warnings.

#pragma region Bindables

// Type-unsafe version of `completion_token`, necessary to avoid circular
// dependencies. Just construct a `completion_token` from it.
using completion_id = uint64_t;

// Because `io_uring` often takes parameters by address and requires these
// pointers to remain valid for an extended period, we need the ability to bind
// them into the `fixed_function`. This is a bit tricky, so we leverage
// `address_forwarder`.

struct bound_timeout: public address_forwarder<bound_timeout> {
  combined_timespec when;

  static combined_timespec* to_when(bound_timeout* bt) noexcept {
    return bt ? &bt->when : nullptr;
  }
};

struct bound_endpoint: public address_forwarder<bound_endpoint> {
  combined_endpoint sockaddr;
};

struct bound_endpoint_with_timeout
    : public address_forwarder<bound_endpoint_with_timeout> {
  combined_timespec when;
  combined_endpoint sockaddr;
};

struct bound_msghdr: public address_forwarder<bound_msghdr> {
  msghdr msg{};

  bound_msghdr() {
    msg.msg_namelen = static_cast<socklen_t>(net_endpoint::max_sockaddr_size);
  }
};

#pragma endregion
#pragma region Concepts

// For efficiency, we don't want to prematurely require `fixed_function`
// parameters for completion callbacks, instead accepting lambdas that can be
// captured into them. That's what the following concepts define.

// Concept for `iou_loop::buf_completion_fn` lambda in its stored form.
template<typename FN>
concept StoredBufCompletionInvocable = std::is_invocable_r_v<slot_retention,
    FN, completion_id, iou_buf_pool::buffer&>;

// Concept for `iou_loop::buf_completion_fn` lambda as a parameter.
template<typename FN>
concept BufCompletionInvocable =
    MoveConsumable<FN> && StoredBufCompletionInvocable<FN>;

// Concept for `iou_loop::endpoint_completion_fn` lambda in its stored form.
template<typename FN>
concept StoredEndpointCompletionInvocable =
    std::is_invocable_r_v<slot_retention, FN, completion_id, iou_res,
        iou_cqe_flags, combined_endpoint&>;

// Concept for `iou_loop::endpoint_completion_fn` lambda as a parameter.
template<typename FN>
concept EndpointCompletionInvocable =
    MoveConsumable<FN> && StoredEndpointCompletionInvocable<FN>;

// Concept for 'iou_loop::msghdr_completion_fn' lambda in its stored form.
template<typename FN>
concept StoredMsgHdrCompletionInvocable = std::is_invocable_r_v<slot_retention,
    FN, completion_id, iou_res, iou_cqe_flags, msghdr&>;

// Concept for `iou_loop::msghdr_completion_fn` lambda as a parameter.
template<typename FN>
concept MsgHdrCompletionInvocable =
    MoveConsumable<FN> && StoredMsgHdrCompletionInvocable<FN>;

// Concept for `iou_loop::completion_fn` lambda in its stored form.
template<typename FN>
concept StoredCompletionInvocable = std::is_invocable_r_v<slot_retention, FN,
    completion_id, iou_res, iou_cqe_flags>;

// Concept for `iou_loop::completion_fn` lambda as a parameter.
template<typename FN>
concept CompletionInvocable =
    MoveConsumable<FN> && StoredCompletionInvocable<FN>;

// Concept for any completion lambda. All of these become `completion_fn` in
// the end, but the "Buf" and "Endpoint" variants pass in parameters that were
// bound to the `fixed_function`.
template<typename FN>
concept AnyCompletionInvocable =
    CompletionInvocable<FN> || EndpointCompletionInvocable<FN> ||
    BufCompletionInvocable<FN> || MsgHdrCompletionInvocable<FN>;

// Concept for `iou_loop::posted_fn` lambda.
template<typename FN>
concept PostedInvocable =
    MoveConsumable<FN> && std::is_invocable_r_v<bool, FN>;

// Callback scheduled via `post` to run on the loop thread. These are used to
// force single-threading of ring access.
using posted_fn = fixed_function<default_fixed_function::capacity, bool()>;

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
// TODO: Allow `iou_buf_pool` size to be configured at compile time.
template<size_t RING_SIZE = 256, size_t SLOT_COUNT = 512>
class iou_basic_loop: public owner_thread_dispatcher<posted_fn> {
#pragma region Types
public:
  // Buffer pools.
  using buf_pool_t = iou_buf_pool;
  using provided_buf_pool_t = iou_provided_buf_pool;

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

  // Low-level callback invoked when an op completes. Moved to a slot borrowed
  // from the `completion_cb_pool_t`. This avoids the need for
  // `std::shared_ptr` for each `fixed_function` and also allows the use of
  // `completion_token` as user data for a SQE.
  //
  // If this calls a method on your class, you will likely want to bind in a
  // shared pointer to that instance.
  using completion_fn = fixed_function<default_fixed_function::capacity,
      slot_retention(completion_id, iou_res, iou_cqe_flags)>;

  // Completion callback for operations using a `buffer`, which includes the
  // `iou_res` and `iou_cqe_flags`. If the `buffer` is not moved from during
  // the callback, it is returned to the pool afterwards.
  //
  // If this calls a method on your class, you will likely want to bind in a
  // shared pointer to that instance.
  using buf_completion_fn = fixed_function<default_fixed_function::capacity,
      slot_retention(completion_id, buffer&)>;

  // Completion callback for operations that return an endpoint, like `accept`.
  //
  // If this calls a method on your class, you will likely want to bind in a
  // shared pointer to that instance.
  using endpoint_completion_fn =
      fixed_function<default_fixed_function::capacity,
          slot_retention(completion_id, iou_res, iou_cqe_flags,
              combined_endpoint&)>;

  // Completion callback for operations that return a `msghdr`, like
  // `recvmsg_multishot`.
  using msghdr_completion_fn = fixed_function<default_fixed_function::capacity,
      slot_retention(completion_id, iou_res, iou_cqe_flags, msghdr&)>;

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
  // `fixed_function` and allows us to refer to callbacks by
  // `completion_token`, which is generation-checking. This token is used for
  // the SQE user data.
  using completion_cb_pool_t = object_pool<completion_fn, SLOT_COUNT * 2,
      generation_scheme::versioned, no_op_cb, clear_function_cb>;

  // RAII handle for a `completion_fn` borrowed from the pool. Can be recreated
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
  // `udp_provided_size` controls the optional provided-buffer pool slab of 2k
  // buffers. Pass 0 to disable it; the default is one hugepage (2 MB).
  //
  // `tcp_provided_size` controls the optional provided-buffer pool slab for
  // multishot TCP reads. Pass 0 to disable it; the default is one hugepage (2
  // MB), split into `tcp_provided_buf_size` buffers (defaulting to 4k).
  //
  // Note: The flags for the ring require that all SQEs be issued from the same
  // thread, and optimizes completions for the single-issuer case.
  explicit iou_basic_loop(allow,
      duration_t post_and_wait_poll_interval =
          default_post_and_wait_poll_interval,
      size_t max_pending_sqes = default_max_pending_sqes,
      size_t udp_provided_size = buf_pool_t::hugepage_size,
      size_t tcp_provided_size = buf_pool_t::hugepage_size,
      block_size tcp_provided_buf_size = block_size::kb004)
      : ring_{RING_SIZE,
            iou_setup_flags::setup_single_issuer |
                iou_setup_flags::setup_defer_taskrun |
                iou_setup_flags::setup_submit_all},
        udp_buf_pool_{this, udp_provided_size, block_size::kb002, 1},
        tcp_provided_buf_pool_{this, tcp_provided_size, tcp_provided_buf_size,
            2},
        max_pending_sqes_{max_pending_sqes},
        post_and_wait_poll_interval_{post_and_wait_poll_interval} {
    if (!buf_pool_.register_with(ring_))
      throw std::system_error{errno, std::system_category(),
          "io_uring_register_buffers"};
    if (!udp_buf_pool_.register_with(ring_))
      throw std::system_error{errno, std::system_category(),
          "io_uring_register_buffers (udp_buf_pool)"};
    if (!tcp_provided_buf_pool_.register_with(ring_))
      throw std::system_error{errno, std::system_category(),
          "io_uring_register_buffers (tcp_provided_buf_pool)"};
  }

  iou_basic_loop(const iou_basic_loop&) = delete;
  iou_basic_loop& operator=(const iou_basic_loop&) = delete;
  iou_basic_loop(iou_basic_loop&&) = delete;
  iou_basic_loop& operator=(iou_basic_loop&&) = delete;

  // Create a heap-allocated `iou_basic_loop` managed by `std::shared_ptr`.
  [[nodiscard]] static std::shared_ptr<iou_basic_loop>
  make(duration_t post_and_wait_poll_interval =
           default_post_and_wait_poll_interval,
      size_t max_pending_sqes = default_max_pending_sqes,
      size_t udp_provided_size = buf_pool_t::hugepage_size,
      size_t tcp_provided_size = buf_pool_t::hugepage_size,
      block_size tcp_provided_buf_size = block_size::kb004) {
    return std::make_shared<iou_basic_loop>(allow::ctor,
        post_and_wait_poll_interval, max_pending_sqes, udp_provided_size,
        tcp_provided_size, tcp_provided_buf_size);
  }

  // Block until `run` is active. Returns false on timeout.
  [[nodiscard]] bool wait_until_running(duration_t timeout = 60s) {
    return running_.wait_for_value(timeout, true);
  }

  // Run the loop on the calling thread until `stop` is called. This is a
  // blocking call. Used by `iou_loop_runner`.
  size_t run() {
    stop_.store(false, std::memory_order::relaxed);
    running_.notify(true);
    scope_exit on_exit{[&] { running_.notify(false); }};
    arm_wake_poll_multishot();

    iou_timespec timeout{default_run_once_timeout};

    size_t total{};
    while (!stop_.load(std::memory_order::acquire)) total += run_once(timeout);
    return total;
  }

  // Process one batch of completions, waiting up to `timeout`. Drains the post
  // queue first, then submits SQEs and waits for CQEs. One of those CQEs may
  // be a wakeup triggered by setting the `eventfd`. Returns the number of
  // completions dispatched, or 0 on timeout or signal. Must be called on the
  // loop thread.
  [[nodiscard]] size_t run_once(const iou_timespec& timeout) {
    assert(is_loop_thread());
    (void)execute_post_queue();

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
    (void)wake_post_queue();
  }

#pragma endregion
#pragma region Post

  // The post queue allows callbacks to be scheduled on the loop thread. This
  // is necessary because the `io_uring` ring queues are designed to be
  // accessed from a single thread.
  //
  // For efficiency, public methods executing on the loop thread bypass the
  // post queue. When executed on another thread, they fall back to posting.
  // This is implemented by `execute_or_post`.
  //
  // There are other use cases where you might want to explicitly post to the
  // loop thread, so `post` is public.
  //
  // See `owner_thread_dispatcher` for more details on the threading model and
  // the post queue.

#pragma endregion
#pragma region Buffers

  //
  // These methods are used to interact with the `iou_buf_pool`, not the
  // `iou_provided_buf_pool`.
  //

  // Borrow a registered `buffer` from the pool for the purpose of reading
  // into it. Returns an invalid `buffer` if the pool is exhausted. Pass to
  // `submit_recv_buffer`.
  [[nodiscard]] buffer borrow_read_buffer(
      block_size sz = block_size::kb004) noexcept {
    return buf_pool_.borrow_reader(sz);
  }

  // Borrow a registered `buffer` from the pool for the purpose of writing to
  // it. Returns an invalid `buffer` if the pool is exhausted. Fill the
  // `buffer`'s payload, then pass it to `submit_send_buffer`.
  [[nodiscard]] buffer borrow_write_buffer(
      block_size sz = block_size::kb004) noexcept {
    return buf_pool_.borrow_writer(sz);
  }

  // Snapshot of number of free buffers in the TCP Provided Buffer pool.
  [[nodiscard]] size_t free_tcp_block_count() const noexcept {
    return tcp_provided_buf_pool_.free_block_count();
  }

  // Snapshot of number of free buffers in the UDP Provided Buffer pool.
  [[nodiscard]] size_t free_udp_block_count() const noexcept {
    return udp_buf_pool_.free_block_count();
  }

#pragma endregion
#pragma region Completion tokens

  //
  // CQE's are ultimately dispatched to user-provided callbacks of type
  // `completion_fn`.
  //
  // Callbacks are first moved into the pool, where their slot is referenced by
  // `completion_token`s. These are cheaply-copied, generation-checking tokens
  // that fit in 64 bits. When stored as `user_data_` or passed as a parameter
  // when the callback is invoked, these are watered down to `completion_id`,
  // which is a type-unsafe `uint64_t` that breaks the dependency loop.
  //
  // The `completion_token` provides a persistent identity that handles
  // lifespan issues cleanly. When it's time to invoke the callback, the token
  // is atomically converted into an owned callback, so we fail gracefully if
  // it has already been released for any reason.
  //
  // Simply invalidating the token effectively cancels the callback, but not
  // the underlying operation. The token can also be used to tell the kernel to
  // cancel all SQEs associated with that token, which is especially necessary
  // for multi-shot operations.
  //
  // By design, tokens are not owning; their lifecycle is managed by the
  // system. In the single-shot workflow, the token remains valid until the
  // CQE is processed, at which point the callback returns the default
  // `slot_retention::automatic`, which leads to it being released.
  //
  // By returning `slot_retention::retain`, the callback can attach itself to
  // another SQE without going through the pool again. When using a
  // `buf_completion_fn`, you may also need to move the `buffer` to keep it
  // from being released.
  //
  // Multishot operations are different. So long as the CQE has
  // `iou_cqe_flags::more` set, then `slot_retention::automatic` acts as
  // `slot_retention::retain`.
  //
  // However, `io_uring` can choose to clear `iou_cqe_flags::more` on any CQE,
  // even on a soft error or no error (such as when `IORING_MAX_FIXED_FILES` is
  // reached), so the callback must either accept this and fail, or resubmit
  // the token. In the latter case, it must explicitly return
  // `slot_retention::retain`.
  //
  // All methods in this section are thread-safe.
  //

  // Determines whether the `completion_fn` associated with `cbtoken` has
  // already been released. If it has, then `cbtoken` is invalidated, since it
  // could never be used again.
  //
  // Note that a `true` is reliable and permanent, but a `false` reflects only
  // the state at that moment. If true, `cbtoken` is cleared.
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

  // Bind an `AnyCompletionInvocable`-shaped lambda and `AddressForwarder`
  // into a `completion_fn`. Behavior depends on `CB`.
  completion_fn wrap_completion_fn(AnyCompletionInvocable auto&& cb,
      AddressForwarder auto&& af) {
    return [cb = std::move(cb), af = std::move(af)](completion_id cbhandle,
               iou_res res, iou_cqe_flags flags) mutable -> slot_retention {
      if constexpr (StoredBufCompletionInvocable<decltype(cb)>) {
        return cb(cbhandle, af.update(res, flags));
      } else if constexpr (StoredEndpointCompletionInvocable<decltype(cb)>) {
        af.sockaddr.len = net_endpoint::max_sockaddr_size;
        return cb(cbhandle, res, flags, af.sockaddr);
      } else if constexpr (StoredMsgHdrCompletionInvocable<decltype(cb)>) {
        af.msg.msg_namelen = net_endpoint::max_sockaddr_size;
        return cb(cbhandle, res, flags, af.msg);
      } else {
        return cb(cbhandle, res, flags);
      }
    };
  }

  // Bind any `AnyCompletionInvocable`-shaped lambda and `AddressForwarder`
  // into a `completion_token` and `AddressForwarder*`.
  auto wrap_completion_fn_and_ptr(AnyCompletionInvocable auto&& cb,
      AddressForwarder auto&& af)
      -> std::pair<completion_token, std::remove_reference_t<decltype(af)>*> {
    using af_t = std::remove_cvref_t<decltype(af)>;
    // No need to bind in an empty timeout.
    if constexpr (std::is_same_v<af_t, bound_timeout>) {
      if (!af.when.ts.is_valid())
        return {tokenize(completion_fn{std::move(cb)}), nullptr};
    }
    af_t* af_ptr{};
    af.forwarding_address() = &af_ptr;
    const auto cbtoken =
        tokenize(wrap_completion_fn(std::move(cb), std::move(af)));
    if (af_ptr) af_ptr->forwarding_address() = nullptr;
    return {cbtoken, af_ptr};
  }

#pragma endregion
#pragma region Submit

  // Submit overview:
  //
  // Methods in the family of `submit_nop` acquire, prepare, and (eventually)
  // submit a SQE. These typically come in sets with three variants:
  // single-shot with `completion_fn`; single-shot with `completion_token`; and
  // multi-shot, with just `completion_token`.
  //
  // These methods all use `execute_or_post_with_retry`, which not only
  // attempts to execute inline on the loop thread, but falls back to posting.
  // Moreover, the post is wrapped in a retry loop, that keeps reposting itself
  // until the transient error (usually SQE congestion) is resolved.
  //
  // Variants:
  //
  // For the `completion_fn` variants, the callback is moved into the pool
  // and associated with a `completion_token`. This token is returned on
  // success, but is invalid on immediate failure.
  //
  // For the `completion_token` variants, the caller must have already acquired
  // a token by calling `tokenize` on a `completion_fn`. On success, `true` is
  // returned. Depending on `on_fail`, the token is either released or retained
  // on failure.
  //
  // Other variants are similar to `completion_fn` except that they may also
  // wrap the lambda passed to reshape it into a `completion_fn` and bind
  // parameters to it, such as a `buffer` or `net_endpoint`.
  //
  // Timeouts:
  //
  // When it makes sense for the operation, single-shot variants take a timeout
  // in the form of a `bound_timeout`, or `bound_endpoint_with_timeout`.
  //
  // For multi-shot variants, we can't use hard-linked timeouts, so you should
  // instead `submit_timeout` separately and check for a timestamp updated by
  // the completion callback.

  // Submit immediately if there are any pending SQEs. Not generally necessary.
  [[nodiscard]] bool immediate_submit() {
    if (!is_loop_thread()) return false;
    if (!pending_sqe_count_) return true;
    pending_sqe_count_ = 0;
    auto res = ring_.submit();
    return (res.ok() || res.is_soft_error());
  }

#pragma region Nop

  //
  // Per the name, nop does nothing except trigger a completion.
  //

  // Submit an async nop.
  [[nodiscard]] completion_token submit_nop(CompletionInvocable auto&& cb) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!submit_nop(cbtoken, slot_retention::automatic)) return {};
    return cbtoken;
  }

  // Submit an async nop.
  [[nodiscard]] bool submit_nop(completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken) return false;
    auto fn = [this, cbtoken, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [](iou_sqe sqe) { sqe.prep_nop(); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Poll

  //
  // Poll for a signal on an `os_file`, such as readability or writability.
  //
  // This could be used to emulate `epoll`-style edge-triggered notifications,
  // but that would be a bad idea. It's still useful for generating a CQE when
  // an `eventfd` is signaled, for example.
  //

  // Submit an async poll on `file`. Completes when `file` is ready for the
  // events specified by `poll_mask`.
  [[nodiscard]] completion_token submit_poll(const os_file& file,
      CompletionInvocable auto&& cb, poll_flags poll_mask = poll_flags::in,
      bound_timeout timeout = {}) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_ptr(std::move(cb), std::move(timeout));
    if (!submit_poll(file, timeout_ptr, cbtoken, poll_mask,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async poll on `file`. Note that `timeout` must either be
  // nullptr, point inside the callback, or remain valid until cancelation.
  [[nodiscard]] bool submit_poll(const os_file& file, bound_timeout* timeout,
      completion_token cbtoken, poll_flags poll_mask = poll_flags::in,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken) return false;
    if (!file) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, cbtoken, fd = *file, timeout = timeout, poll_mask,
                  on_fail]() mutable {
      return do_submit_timeout(cbtoken, bound_timeout::to_when(timeout),
          on_fail,
          [fd, poll_mask](iou_sqe sqe) { sqe.prep_poll(fd, poll_mask); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

  // Submit an async multishot poll on `file`.
  [[nodiscard]] completion_token submit_poll_multishot(const os_file& file,
      CompletionInvocable auto&& cb, poll_flags poll_mask = poll_flags::in) {
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
    if (!cbtoken) return false;
    if (!file) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, cbtoken, fd = *file, poll_mask, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [fd, poll_mask](iou_sqe sqe) {
        sqe.prep_poll_multishot(fd, poll_mask);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Timeout

  //
  // Allows creating, removing, and updating stand-alone timeouts.
  //
  // These are not linked to existing operations: that functionality is
  // provided through the `timeout` parameter on those methods.
  //

  //
  // Create
  //

  // Submit an async timeout. For multishot, set the
  // `io_timeout_flags::multishot` flag. The meaning of `cqe_count` depends on
  // the mode.
  [[nodiscard]] completion_token submit_timeout(bound_timeout&& timeout,
      CompletionInvocable auto&& cb, size_t cqe_count = 0) {
    if (!timeout.is_valid()) return {};
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_ptr(std::move(cb), std::move(timeout));
    if (!cbtoken) return {};
    if (!submit_timeout(*timeout_ptr, cbtoken, cqe_count,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async timeout. For multishot, set the
  // `io_timeout_flags::multishot` flag. The meaning of `cqe_count` depends on
  // the mode. Note that `timeout` must either point inside the callback or
  // remain valid until cancelation.
  [[nodiscard]] bool submit_timeout(bound_timeout& timeout,
      completion_token cbtoken, size_t cqe_count = 0,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken) return false;
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
  [[nodiscard]] bool submit_timeout_remove(
      completion_token&& cancelation_token) {
    if (!submit_timeout_remove(std::move(cancelation_token), {},
            slot_retention::automatic))
      return false;
    return true;
  }

  // Submit an async timeout removal.
  [[nodiscard]] bool submit_timeout_remove(
      completion_token&& cancelation_token, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cancelation_token) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, cancelation_token = std::move(cancelation_token), cbtoken,
                  on_fail]() mutable {
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
      bound_timeout& timeout,
      slot_retention on_fail = slot_retention::retain) {
    if (!update_token) return false;
    auto fn = [this, update_token, &timeout, on_fail]() mutable {
      return do_submit(update_token, on_fail,
          [update_token, &timeout](iou_sqe sqe) {
            sqe.prep_timeout_update(&timeout.when.ts, update_token.as_int(),
                timeout.when.flags);
          });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Close

  //
  // Close a file handle after canceling all associated operations.
  //

  // Submit a cancel-then-close as a hard-linked pair. The cancel SQE runs
  // first; its CQE is swallowed regardless of outcome. The close SQE starts
  // only after the cancel completes. whether it succeeds or fails. This
  // sequencing prevents the possibility of the cancel being processed after
  // the close, which would lead canceling on a reused FD.
  // Invalidates `file`.
  [[nodiscard]] completion_token
  submit_close(os_file&& file, CompletionInvocable auto&& cb) {
    if (!file) return {};
    const auto cbtoken = tokenize(std::move(cb));
    if (!cbtoken) return {};
    auto fn = [this, fd = file.release(), cbtoken,
                  on_fail = slot_retention::automatic]() mutable {
      return do_submit_hardlinked(
          cbtoken, on_fail, [fd](iou_sqe sqe) { sqe.prep_cancel_fd(fd); },
          [fd](iou_sqe sqe) { sqe.prep_close(fd); });
    };
    if (!execute_or_post_with_retry(std::move(fn))) return {};
    return cbtoken;
  }

#pragma endregion
#pragma region Shutdown

  //
  // Shut down the read or write sides of a socket.
  //

  // Submit an async shutdown on `socket`.
  [[nodiscard]] completion_token submit_shutdown(const net_socket& socket,
      shutdown_how how, CompletionInvocable auto&& cb) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!cbtoken) return {};
    if (!submit_shutdown(socket, how, cbtoken, slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async shutdown on `socket`.
  [[nodiscard]] bool submit_shutdown(const net_socket& socket,
      shutdown_how how, completion_token cbtoken = {},
      slot_retention on_fail = slot_retention::retain) {
    if (!socket) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, how, cbtoken, on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [fd, how](iou_sqe sqe) {
        sqe.prep_shutdown(fd, how);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region Accept

  //
  // Accept a new connection on a listening socket.
  //

  // Submit an async accept on `socket`.
  [[nodiscard]] completion_token submit_accept(const net_socket& socket,
      EndpointCompletionInvocable auto&& cb,
      bound_endpoint_with_timeout endpoint = {}) {
    auto [cbtoken, endpoint_ptr] =
        wrap_completion_fn_and_ptr(std::move(cb), std::move(endpoint));
    if (!cbtoken) return {};
    if (!submit_accept(socket, *endpoint_ptr, cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async accept on `socket`. Note that `endpoint` must either point
  // inside the callback or remain valid until cancelation.
  [[nodiscard]] bool submit_accept(const net_socket& socket,
      bound_endpoint_with_timeout& endpoint, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken) return false;
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
  [[nodiscard]] bool submit_accept_multishot(const net_socket& socket,
      combined_endpoint& endpoint, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken) return false;
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

  //
  // Connect a socket.
  //

  // Submit an async connect on `socket` to `endpoint`.
  [[nodiscard]] completion_token submit_connect(const net_socket& socket,
      bound_endpoint_with_timeout&& remote, CompletionInvocable auto&& cb) {
    auto [cbtoken, endpoint_ptr] =
        wrap_completion_fn_and_ptr(std::move(cb), std::move(remote));
    if (!cbtoken) return {};
    if (!submit_connect(socket, *endpoint_ptr, cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async connect on `socket` to `remote`. `remote` must point
  // inside the callback or remain valid until cancelation.
  [[nodiscard]] bool submit_connect(const net_socket& socket,
      bound_endpoint_with_timeout& remote, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken) return false;
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

  //
  // Cancel all pending operations for a file handle. Usually done as part of
  // closing but can also be used on its own.
  //

  // Submit an async cancel on `file`. Cancels all ops for the file.
  [[nodiscard]] completion_token
  submit_cancel(const os_file& file, CompletionInvocable auto&& cb) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!submit_cancel(file, cbtoken, slot_retention::automatic)) return {};
    return cbtoken;
  }

  // Submit an async cancel on `file`. Note that `timeout` must either point
  // inside the callback or remain valid until cancelation.
  [[nodiscard]] bool submit_cancel(const os_file& file,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cbtoken) return false;
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

  //
  // Cancel operations based on a `completion_token`.
  //

  // Submit an async cancel on `cancelation_token`.
  [[nodiscard]] bool submit_cancel(completion_token&& cancelation_token) {
    if (!submit_cancel(std::move(cancelation_token), {},
            slot_retention::automatic))
      return false;
    return true;
  }

  // Submit an async cancel on `cancelation_token`.
  [[nodiscard]] completion_token submit_cancel(
      completion_token&& cancelation_token, CompletionInvocable auto&& cb) {
    const auto cbtoken = tokenize(std::move(cb));
    if (!cbtoken) return {};
    if (!submit_cancel(std::move(cancelation_token), cbtoken,
            slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async cancel on `cancelation_token`.
  [[nodiscard]] bool submit_cancel(completion_token&& cancelation_token,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain) {
    if (!cancelation_token) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, cancelation_token = std::move(cancelation_token), cbtoken,
                  on_fail]() mutable {
      return do_submit(cbtoken, on_fail, [cancelation_token](iou_sqe sqe) {
        sqe.prep_cancel_user_data(cancelation_token.as_int());
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region RecvBytes

  //
  // Receive bytes from a socket into an arbitrary buffer, not a fixed or
  // provided one.
  //

  // Low-level method to submit an async recv on `socket` into bytes at `span`.
  //
  // Prefer `submit_recv_buffer` over this.
  [[nodiscard]] completion_token submit_recv_bytes(const net_socket& socket,
      span_t span, CompletionInvocable auto&& cb, bound_timeout timeout = {}) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_ptr(std::move(cb), std::move(timeout));
    if (!submit_recv_bytes(socket, span, cbtoken,
            bound_timeout::to_when(timeout_ptr), slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async recvmsg on `socket`. Note that `timeout` must either be
  // null, inside the callback, or remain valid until cancelation.
  [[nodiscard]] bool submit_recv_bytes(const net_socket& socket, span_t span,
      completion_token cbtoken, combined_timespec* timeout = nullptr,
      slot_retention on_fail = slot_retention::retain, msg_flags flags = {}) {
    if (!cbtoken) return false;
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

  //
  // Send bytes from an arbitrary buffer, not a fixed or provided one, and not
  // zero-copy.
  //

  // Low-level method to submit an async send on `socket` from bytes at `span`.
  //
  // Prefer `submit_send_buffer` over this.
  [[nodiscard]] completion_token submit_send_bytes(const net_socket& socket,
      const_span_t span, CompletionInvocable auto&& cb,
      bound_timeout timeout = {}) {
    auto [cbtoken, timeout_ptr] =
        wrap_completion_fn_and_ptr(std::move(cb), std::move(timeout));
    if (!submit_send_bytes(socket, span, cbtoken,
            bound_timeout::to_when(timeout_ptr), slot_retention::automatic))
      return {};
    return cbtoken;
  }

  // Submit an async send on `socket`. Note that `timeout` must either be null,
  // inside the callback, or remain valid until cancelation.
  [[nodiscard]] bool submit_send_bytes(const net_socket& socket,
      const_span_t span, completion_token cbtoken,
      combined_timespec* timeout = nullptr,
      slot_retention on_fail = slot_retention::retain, msg_flags flags = {}) {
    if (!cbtoken) return false;
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
#pragma region RecvMsgBuffer

  //
  // Receive a message from a datagram socket into a fixed buffer (however, it
  // is not taking full advantage of the fact that it's fixed, and it's not
  // provided or zero-copy). The socket is unbound, so the sender address is
  // placed into the `buffer::peer_addr`.
  //

  // Submit an async recvmsg on `socket` into `buf`. On completion, the
  // `buffer` is updated and forwarded to `bufcb`. The sender address is
  // available via `buf.peer_addr` in the callback.
  [[nodiscard]] completion_token
  submit_recvmsg_buffer(const net_socket& socket, buffer&& buf,
      BufCompletionInvocable auto&& bufcb, msg_flags flags = {}) {
    const auto [cbtoken, buf_ptr] =
        wrap_completion_fn_and_ptr(std::move(bufcb), std::move(buf));
    if (!cbtoken) return {};
    if (!submit_recvmsg_buffer(socket, *buf_ptr, cbtoken,
            slot_retention::automatic, flags))
      return {};
    return cbtoken;
  }

  // Submit an async recvmsg on `socket`. Note that `buf` must point inside
  // the callback, or remain valid until completion.
  [[nodiscard]] bool submit_recvmsg_buffer(const net_socket& socket,
      buffer& buf, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain, msg_flags flags = {}) {
    if (!cbtoken) return false;
    auto* msg = buf.prepare_recvmsg();
    if (!socket) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, flags, cbtoken, msg,
                  &timeout = buf.timeout(), on_fail]() mutable {
      return do_submit_timeout(cbtoken, &timeout, on_fail,
          [fd, flags, msg](iou_sqe sqe) { sqe.prep_recvmsg(fd, msg, flags); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region SendMsgBuffer

  //
  // Send a message from a datagram socket from a fixed buffer (however, it is
  // not taking full advantage of the fact that it's fixed, and it's not
  // provided or zero-copy). The socket is unbound, so the destination address
  // is taken from the `buffer::peer_addr`.
  //

  // Submit an async sendmsg on `socket` from `buf` to its `peer_addr`. On
  // completion, the buffer is updated and forwarded to `bufcb`.
  [[nodiscard]] completion_token
  submit_sendmsg_buffer(const net_socket& socket, buffer&& buf,
      BufCompletionInvocable auto&& bufcb,
      msg_flags flags = msg_flags::nosignal) {
    const auto [cbtoken, buf_ptr] =
        wrap_completion_fn_and_ptr(std::move(bufcb), std::move(buf));
    if (!cbtoken) return {};
    if (!submit_sendmsg_buffer(socket, *buf_ptr, cbtoken,
            slot_retention::automatic, flags))
      return {};
    return cbtoken;
  }

  // Submit an async sendmsg on `socket`. Note that `buf` must point inside
  // the callback, or remain valid until completion.
  [[nodiscard]] bool submit_sendmsg_buffer(const net_socket& socket,
      buffer& buf, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain,
      msg_flags flags = msg_flags::nosignal) {
    if (!cbtoken) return false;
    auto* msg = buf.prepare_sendmsg();
    if (!socket) return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, flags, cbtoken, msg,
                  &timeout = buf.timeout(), on_fail]() mutable {
      return do_submit_timeout(cbtoken, &timeout, on_fail,
          [fd, flags, msg](iou_sqe sqe) { sqe.prep_sendmsg(fd, msg, flags); });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region ReadBuffer

  //
  // Read into fixed `buffer` from `os_file`. Works with sockets or files,
  // where the latter interacts with `buffer::file_offset`.
  //
  // Uses `io_uring_prep_read_fixed`, which provides full `read` semantics, but
  // is not socket-specific or zero-copy.
  //

  // Submit an async read_fixed on `file` into a borrowed buffer.
  [[nodiscard]] completion_token submit_read_buffer(const os_file& file,
      BufCompletionInvocable auto&& bufcb, block_size sz = block_size::kb004,
      const combined_timespec& timeout = {}) {
    buffer buf = borrow_read_buffer(sz);
    buf.timeout() = timeout;
    return submit_read_buffer(file, std::move(buf), std::move(bufcb));
  }

  // Submit an async read_fixed on `file` into `buf`.
  [[nodiscard]] completion_token submit_read_buffer(const os_file& file,
      buffer&& buf, BufCompletionInvocable auto&& bufcb) {
    const auto [cbtoken, buf_ptr] =
        wrap_completion_fn_and_ptr(std::move(bufcb), std::move(buf));
    if (!cbtoken) return {};
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
    if (!cbtoken) return false;
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
  // Write from fixed `buffer` to `os_file`.
  //
  // Uses `io_uring_prep_write_fixed`, which provides full `write` semantics,
  // but is not socket-specific or zero-copy.
  //
  // Works with sockets or files. Files interact with `buffer::file_offset` For
  // sockets, there's no way to pass `msg_flags::nosignal` so you need to
  // disable SIGPIPE process-wide. Prefer `submit_send_buffer` over this, if
  // you can justify the added complexity and you're not using UDS.
  //
  // The reason for this recommendation is that this uses fixed buffers with
  // `io_uring_prep_write_fixed`. However, it only generates a single CQE,
  // which is  simpler to deal with and is likely to be faster for packets
  // under 10k. Contrast with `submit_send_buffer`, which uses
  // `io_uring_prep_send_zc_fixed`.

  // Submit an async write_fixed on `file` from `buf`.
  [[nodiscard]] completion_token submit_write_buffer(const os_file& file,
      buffer&& buf, BufCompletionInvocable auto&& bufcb) {
    const auto [cbtoken, buf_ptr] =
        wrap_completion_fn_and_ptr(std::move(bufcb), std::move(buf));
    if (!cbtoken) return {};
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
    if (!cbtoken) return false;
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

  //
  // Receive from a socket into a provided `buffer` repeatedly, using
  // `tcp_provided_buf_pool_`.
  //
  // Uses `io_uring_prep_recv_multishot`, which is socket-specific.
  //

  // Submit an async multishot recv on `socket`. The callback receives a
  // `buffer` borrowed from the provided pool on each completion. Use
  // `slot_retention::automatic` (the default on completion) to keep the
  // operation alive as long as `iou_cqe_flags::more` is set.
  [[nodiscard]] completion_token
  submit_recv_buffer_multi(const net_socket& socket,
      BufCompletionInvocable auto&& bufcb, msg_flags flags = {}) {
    auto cb =
        [this, bufcb = std::move(bufcb)](completion_id cbhandle, iou_res res,
            iou_cqe_flags cqe_flags) mutable -> slot_retention {
      auto buf = tcp_provided_buf_pool_.borrow(res, cqe_flags);
      return bufcb(cbhandle, buf);
    };
    const auto cbtoken = tokenize(std::move(cb));
    if (!cbtoken) return {};
    if (!submit_recv_buffer_multi(socket, cbtoken, slot_retention::automatic,
            flags))
      return {};
    return cbtoken;
  }

  // Submit an async multishot recv on `socket` using provided buffers.
  [[nodiscard]] bool submit_recv_buffer_multi(const net_socket& socket,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain, msg_flags flags = {}) {
    if (!cbtoken) return false;
    if (!socket || !tcp_provided_buf_pool_)
      return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, flags, cbtoken, on_fail]() mutable {
      const auto bgid = tcp_provided_buf_pool_.bgid();
      return do_submit(cbtoken, on_fail, [fd, flags, bgid](iou_sqe sqe) {
        sqe.prep_recv_multishot(fd, flags, bgid);
      });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region RecvMsgBufferMulti

  //
  // Receive messages from a datagram socket into provided buffers repeatedly,
  // using `udp_buf_pool_`.
  //
  // Uses `io_uring_prep_recvmsg_multishot`, which is socket-specific. Each
  // Provided Buffer holds an `io_uring_recvmsg_out` header followed by peer
  // address, control data, and payload. The `buffer` has `payload_span`
  // pointing to the payload, with `peer_addr` and `msghdr_flags` filled in.
  //

  // Submit an async multishot recvmsg on `socket` using `udp_buf_pool_`. The
  // callback receives a `buffer` borrowed from the provided pool on each
  // completion.
  [[nodiscard]] completion_token submit_recvmsg_buffer_multi(
      const net_socket& socket, BufCompletionInvocable auto&& bufcb,
      msg_flags flags = msg_flags::trunc) {
    auto cb =
        [this, bufcb = std::move(bufcb)](completion_id cbhandle, iou_res res,
            iou_cqe_flags cqe_flags, msghdr& msgh) mutable -> slot_retention {
      auto buf = udp_buf_pool_.borrow(res, cqe_flags, &msgh);
      return bufcb(cbhandle, buf);
    };
    const auto [cbtoken, hdr_ptr] =
        wrap_completion_fn_and_ptr(std::move(cb), std::move(bound_msghdr{}));
    if (!cbtoken) return {};
    if (!submit_recvmsg_buffer_multi(socket, hdr_ptr->msg, cbtoken,
            slot_retention::automatic, flags))
      return {};
    return cbtoken;
  }

  // Submit an async multishot recvmsg on `socket` using provided buffers.
  // Note that `msg` must point inside the callback or remain valid until
  // cancelation. Only `msg_namelen` is used by the kernel; `msg_iov` and
  // `msg_control` are ignored.
  [[nodiscard]] bool submit_recvmsg_buffer_multi(const net_socket& socket,
      msghdr& msg, completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain,
      msg_flags flags = msg_flags::trunc) {
    if (!cbtoken) return false;
    if (!socket || !udp_buf_pool_)
      return fail_and_maybe_release(on_fail, cbtoken);
    const auto bgid = udp_buf_pool_.bgid();
    auto* msg_ptr = &msg;
    auto fn = [this, fd = *socket, flags, cbtoken, msg_ptr, bgid,
                  on_fail]() mutable {
      return do_submit(cbtoken, on_fail,
          [fd, flags, msg_ptr, bgid](iou_sqe sqe) {
            sqe.prep_recvmsg_multishot(fd, msg_ptr, flags, bgid);
          });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
#pragma region SendBuffer

  //
  // Send to a socket from a fixed `buffer`, with zero-copy.
  //
  // This variant works for TCP/IP and UDP/IP sockets, but not UDS or files.
  //

  // Send from fixed `buffer` to `net_socket`.
  //
  // Uses `io_uring_prep_send_zc_fixed`, which provides full `send` semantics,
  // including `msg_flags::nosignal`, but potentially generates two CQEs per
  // send (one for the send and one for the release of the buffer).
  [[nodiscard]] completion_token submit_send_buffer(const net_socket& socket,
      buffer&& buf, BufCompletionInvocable auto&& bufcb,
      msg_flags flags = msg_flags::nosignal) {
    const auto [cbtoken, buf_ptr] =
        wrap_completion_fn_and_ptr(std::move(bufcb), std::move(buf));
    if (!cbtoken) return {};
    if (!submit_send_buffer(socket, *buf_ptr, cbtoken,
            slot_retention::automatic, flags))
      return {};
    return cbtoken;
  }

  // Send from fixed `buffer` to `net_socket`. Note that `buf` must point
  // inside the callback.
  [[nodiscard]] bool submit_send_buffer(const net_socket& socket, buffer& buf,
      completion_token cbtoken,
      slot_retention on_fail = slot_retention::retain,
      msg_flags flags = msg_flags::nosignal) {
    if (!cbtoken) return false;
    auto [span, buf_index, _] = buf.prepare();
    if (!socket || span.empty())
      return fail_and_maybe_release(on_fail, cbtoken);
    auto fn = [this, fd = *socket, flags, cbtoken, span, buf_index,
                  &timeout = buf.timeout(), on_fail]() mutable {
      return do_submit_timeout(cbtoken, &timeout, on_fail,
          [fd, flags, span, buf_index](iou_sqe sqe) {
            sqe.prep_send_zc_fixed(fd, span, flags, buf_index);
          });
    };
    return execute_or_post_with_retry(std::move(fn));
  }

#pragma endregion
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
    assert(is_loop_thread());
    assert(on_fail != slot_retention::release); // Would be dumb.
    auto do_submit = [&]() {
      // Must return true to end retries.
      if (cbtoken && is_released(cbtoken)) return true;

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

  // Submit two hard-linked SQEs atomically, so that they run in sequence but
  // the second executes regardless of the first's result. `prep_first` runs
  // with `io_hardlink` set and a null user-data (its CQE is swallowed).
  // `prep_second` carries `cbtoken`.
  [[nodiscard]] bool do_submit_hardlinked(completion_token cbtoken,
      slot_retention on_fail, std::invocable<iou_sqe> auto&& prep_first,
      std::invocable<iou_sqe> auto&& prep_second) {
    assert(is_loop_thread());
    auto do_submit = [&]() {
      if (cbtoken && is_released(cbtoken)) return true;
      if (!ring_.enough_sqe_available(2)) return false;

      auto sqe_first = ring_.next_sqe();
      auto sqe_second = ring_.next_sqe();
      assert(sqe_first && sqe_second);

      std::forward<decltype(prep_first)>(prep_first)(sqe_first);
      sqe_first.set_sqe_flags(iou_sqe_flags::io_hardlink);
      sqe_first.set_data_pointer(nullptr); // CQE swallowed.

      std::forward<decltype(prep_second)>(prep_second)(sqe_second);
      sqe_second.set_data_int(cbtoken.as_int());

      if (!maybe_submit_pending(2)) return false;
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
      if (cbtoken && is_released(cbtoken)) return true;

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
    (void)maybe_submit_pending(1);
    return immediate_submit();
  }

  // Dispatch a CQE to its registered callback. The callback returns a
  // `slot_retention` result:
  //   - `automatic`: retain iff `IORING_CQE_F_MORE` is set (multishot
  //   default).
  //   - `release`: always free the slot.
  //   - `retain`: always keep the slot (e.g., callback resubmits a new SQE).
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

  // Submit a multishot `IORING_OP_POLL_ADD` for the wakeup `eventfd`. Each
  // time `post` or `stop` writes to the `eventfd`, the poll fires as a CQE
  // and interrupts `io_uring_wait_cqe_timeout`. Because the operation is
  // multishot, the kernel keeps it alive as long as `IORING_CQE_F_MORE` is
  // set; the callback drains the `eventfd` on each firing. If the kernel
  // ends the multishot (flag absent), the callback resubmits.
  bool arm_wake_poll_multishot() {
    if (wake_poll_token_) return true;

    // Set up a callback that resubmits itself, and use it to bootstrap the
    // initial submission.
    auto raw_cb =
        [this](completion_id cbhandle, iou_res, iou_cqe_flags flags) {
          (void)wake_fd().read();
          if (bitmask::has(flags, iou_cqe_flags::more))
            return slot_retention::automatic;
          if (!submit_poll_multishot(wake_fd(), completion_token{cbhandle},
                  poll_flags::in, slot_retention::retain))
            throw std::runtime_error{"failed to resubmit wake poll multishot"};
          return slot_retention::retain;
        };

    // Make sure it doesn't just lock up at the start.
    (void)wake_post_queue();

    // Pretend a CQE arrived.
    wake_poll_token_ = tokenize(std::move(raw_cb));
    auto borrowed = borrow(wake_poll_token_);

    // Get it to submit itself.
    (void)borrowed(wake_poll_token_.as_int(), iou_res{}, iou_cqe_flags{});

    detach(std::move(borrowed));
    return true;
  }

#pragma endregion
#pragma region Data members
private:
  // Rings and buffers. (Order is important.)
  buf_pool_t buf_pool_;
  iou_ring ring_;
  provided_buf_pool_t udp_buf_pool_;
  provided_buf_pool_t tcp_provided_buf_pool_;

  // Completion callback pool, used to avoid `std::shared_ptr`.
  completion_cb_pool_t completion_cb_pool_;

  // Stop system.
  std::atomic_bool stop_;
  notifiable<std::atomic_bool> running_;
  completion_token wake_poll_token_;

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
          loop_t::default_post_and_wait_poll_interval,
      size_t pbuf_slab_size = loop_t::buf_pool_t::hugepage_size,
      size_t tcp_provided_size = loop_t::buf_pool_t::hugepage_size,
      block_size tcp_provided_buf_size = block_size::kb004)
      : post_and_wait_poll_interval_{post_and_wait_poll_interval},
        pbuf_slab_size_{pbuf_slab_size}, tcp_provided_size_{tcp_provided_size},
        tcp_provided_buf_size_{tcp_provided_buf_size},
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
      auto loop = loop_t::make(post_and_wait_poll_interval_,
          loop_t::default_max_pending_sqes, pbuf_slab_size_,
          tcp_provided_size_, tcp_provided_buf_size_);
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
  const size_t pbuf_slab_size_;
  const size_t tcp_provided_size_;
  const block_size tcp_provided_buf_size_;
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
