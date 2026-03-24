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
#include <chrono>
#include <cerrno>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>

#include "../concurrency/notifiable.h"
#include "../concurrency/tombstone.h"
#include "../containers/scoped_value.h"
#include "../containers/scope_exit.h"
#include "../containers/opt_find.h"
#include "../filesys/io_uring_ring.h"
#include "../filesys/event_fd.h"
#include "../filesys/net_socket.h"

// `<linux/io_uring.h>` already included via `io_uring_ring.h`.

namespace corvid { inline namespace proto {

using namespace corvid::container::value_scoping;

// Abstract base for objects registered with `iouring_loop`. Identical to the
// `io_conn` used by `epoll_loop` -- higher layers (e.g., `iou_stream_conn`)
// inherit from this without modification.
//
// `events` stores an `epoll`-compatible interest mask (`EPOLLIN`, `EPOLLOUT`,
// `EPOLLERR`, `EPOLLHUP`, `EPOLLRDHUP`). On Linux, these values are equal to
// the POSIX `POLLIN`/`POLLOUT`/etc. constants, so they can be passed directly
// to `io_uring`'s `IORING_OP_POLL_ADD` as the `poll32_events` field.
//
// Same lifetime guarantees as with `epoll_loop`: the loop holds a
// `shared_ptr<iou_io_conn>` per registration, keeping the object alive
// across any in-progress dispatch.
struct iou_io_conn: std::enable_shared_from_this<iou_io_conn> {
  explicit iou_io_conn(net_socket&& sock) : sock_(std::move(sock)) {}
  net_socket& sock() noexcept { return sock_; }
  const net_socket& sock() const noexcept { return sock_; }

  virtual bool on_readable() { return false; }
  virtual bool on_writable() { return false; }
  virtual bool on_error() { return on_readable(); }
  virtual ~iou_io_conn() = default;

  // Current epoll-compatible interest mask. Written only by `iouring_loop`
  // while on the loop thread.
  uint32_t events{0};

private:
  net_socket sock_;
};

// `io_uring`-based I/O event loop, API-compatible with `epoll_loop`.
//
// Uses `IORING_OP_POLL_ADD` with `IORING_POLL_ADD_MULTI` to emulate
// level-triggered epoll readiness notification: each fd receives a
// persistent multi-shot poll subscription that fires a CQE whenever the
// requested events become ready, without consuming the subscription.
//
// When the interest mask for a registered fd changes (e.g., `EPOLLOUT` is
// added or removed), the existing multi-shot subscription is cancelled and a
// new one with the updated mask is submitted. A per-registration `generation`
// counter tags every subscription's `user_data`; CQEs with a stale generation
// are silently discarded, eliminating any race between cancel and re-arm.
//
// Thread-wakeup (for `post` and `stop`) uses an `eventfd` with a single-shot
// `IORING_OP_POLL_ADD` that is re-armed after each wakeup CQE.
//
// The public API is identical to `epoll_loop`. Higher-layer code that holds
// a loop reference should be templated on the loop type to work with either.
class iouring_loop {
public:
  static constexpr size_t max_events = 64;
  static constexpr std::chrono::milliseconds
      default_post_and_wait_poll_interval{100};

  // Create the `io_uring` ring and internal `eventfd` wakeup handle.
  // Throws `std::system_error` on failure.
  explicit iouring_loop(
      std::chrono::milliseconds post_and_wait_poll_interval =
          default_post_and_wait_poll_interval)
      : ring_{create_ring()}, wake_fd_{create_eventfd()},
        post_and_wait_poll_interval_{post_and_wait_poll_interval} {
    // Register the eventfd with a single-shot POLL_ADD. Subsequent wakeups
    // re-arm it after each CQE is consumed.
    if (!submit_wake_poll())
      throw std::system_error(errno, std::generic_category(),
          "iouring_loop: submit initial wake poll");
    if (ring_.submit() < 0)
      throw std::system_error(errno, std::generic_category(),
          "iouring_loop: initial submit");
  }

  iouring_loop(const iouring_loop&) = delete;
  iouring_loop& operator=(const iouring_loop&) = delete;
  iouring_loop(iouring_loop&&) = delete;
  iouring_loop& operator=(iouring_loop&&) = delete;

  ~iouring_loop() = default;

  // Register `conn`. The initial event mask always includes
  // `EPOLLERR | EPOLLHUP | EPOLLRDHUP`; read and write readiness are
  // controlled by `readable` and `writable`. Returns false if the SQ is full.
  // If executed outside of loop thread, promotes to a `post` and returns true.
  [[nodiscard]] bool register_socket(std::shared_ptr<iou_io_conn> conn,
      bool readable = true, bool writable = false) {
    return execute_or_post(
        [this, conn = std::move(conn), readable, writable]() mutable {
          return do_register_socket(std::move(conn), readable, writable);
        });
  }

  // Add or remove `EPOLLIN` from the interest mask for `conn`. If executed
  // outside of loop thread, promotes to a `post` and returns true.
  [[nodiscard]] bool enable_reads(iou_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      return do_enable_interest(*sp, EPOLLIN, on);
    });
  }

  // Add or remove `EPOLLRDHUP` from the interest mask for `conn`. If executed
  // outside of loop thread, promotes to a `post` and returns true.
  [[nodiscard]] bool enable_rdhup(iou_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      return do_enable_interest(*sp, EPOLLRDHUP, on);
    });
  }

  // Add or remove `EPOLLOUT` from the interest mask for `conn`. If executed
  // outside of loop thread, promotes to a `post` and returns true.
  [[nodiscard]] bool enable_writes(iou_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      return do_enable_interest(*sp, EPOLLOUT, on);
    });
  }

  // Unregister `conn`. If executed outside of loop thread, promotes to a
  // `post` and returns true.
  [[nodiscard]] bool unregister_socket(iou_io_conn& conn) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp)] {
      return do_unregister_socket(*sp);
    });
  }

  // Unregister `sock`. If executed outside of loop thread, promotes to a
  // `post` and returns true.
  [[nodiscard]] bool unregister_socket(const net_socket& sock) {
    const auto fd = sock.handle();
    return execute_or_post([this, fd] {
      auto found = find_opt(registrations_, fd);
      if (!found) return false;
      return do_unregister_socket(*found->conn);
    });
  }

  // Look up `fd` in the registration table and return the owning
  // `shared_ptr<iou_io_conn>`, or null if not found. Loop-thread only.
  [[nodiscard]] std::shared_ptr<iou_io_conn> find_fd(
      os_file::file_handle_t fd) const {
    assert(is_loop_thread());
    auto found = find_opt(registrations_, fd);
    if (!found) return nullptr;
    return found->conn;
  }

  // Schedule `fn` to run at the top of the next `run_once` iteration.
  // Safe to call from any thread.
  [[nodiscard]] bool post(std::function<bool()> fn) {
    {
      std::scoped_lock lock{post_mutex_};
      post_queue_.push_back(std::move(fn));
    }
    return wake();
  }

  // Run `fn` on the loop thread and block until it returns. If already on
  // the loop thread, executes inline. Returns false if the loop is not
  // running.
  template<typename FN>
  [[nodiscard]] bool post_and_wait(FN&& fn) {
    if (is_loop_thread()) return fn();
    if (!running_.get()) return false;

    using fn_type = std::decay_t<FN>;
    struct wait_state {
      notifiable<bool> done{false};
      bool result{};
      fn_type fn;
      explicit wait_state(fn_type&& f) : fn(std::move(f)) {}
    };

    auto waiter = std::make_shared<wait_state>(std::forward<FN>(fn));
    if (!post([waiter] {
          waiter->result = waiter->fn();
          waiter->done.notify_one(true);
          return true;
        }))
      return false;

    while (true) {
      if (waiter->done.wait_for_value(post_and_wait_poll_interval_, true))
        return waiter->result;
      if (!running_.get()) return false;
    }
  }

  // Run `fn` immediately if on the loop thread, otherwise `post` it.
  template<typename FN>
  [[nodiscard]] bool execute_or_post(FN&& fn) {
    if (is_loop_thread()) return fn();
    return post(std::forward<FN>(fn));
  }

  // Signal the loop to exit. Safe to call from any thread.
  [[nodiscard]] bool stop() {
    running_.notify(false);
    return wake();
  }

  // Check whether the current thread is the loop thread.
  bool is_loop_thread() const noexcept { return current_loop_ == this; }

  // Block until `run` enters its polling loop. Returns false on timeout.
  [[nodiscard]] bool wait_until_running(int timeout_ms = -1) {
    if (timeout_ms < 0) timeout_ms = 60000;
    return running_.wait_for_value(std::chrono::milliseconds{timeout_ms},
        true);
  }

  // Drain `post_queue_`, submit pending SQEs, wait for CQEs, and dispatch.
  // Returns the number of I/O events dispatched, or -1 on error.
  [[nodiscard]] int run_once(int timeout_ms = -1) {
    assert(is_loop_thread());

    if (!drain_post_queue()) return -1;

    // Flush any SQEs queued by post-queue callbacks (e.g., cancel + re-add
    // from `do_enable_interest`, new subscriptions from `do_register_socket`).
    if (ring_.submit() < 0) return -1;

    // Wait for CQEs.
    const int n = ring_.wait(timeout_ms);
    if (n < 0) {
      // `EINTR` is not a hard error; just return 0 to let the loop retry.
      return os_file::is_hard_error() ? -1 : 0;
    }
    if (n == 0) return 0;

    // Process all ready CQEs.
    int dispatched = 0;
    for (;;) {
      const io_uring_cqe* cqe = ring_.peek_cqe();
      if (!cqe) break;

      const uint64_t user_data = cqe->user_data;
      const uint32_t cqe_flags = cqe->flags;
      const int32_t res = cqe->res;
      ring_.advance_cq(1);

      // Internal wakeup from the eventfd.
      if (user_data == k_wake_sentinel) {
        if (!wake_fd_.read()) return -1; // drain the eventfd counter
        // Re-arm the single-shot poll for the next wakeup.
        if (!submit_wake_poll()) return -1;
        continue;
      }

      // Decode fd and generation from `user_data`.
      const auto [fd, gen] = decode_user_data(user_data);
      auto found = find_opt(registrations_, fd);
      if (!found) continue; // already unregistered
      if (found->generation != gen)
        continue; // stale CQE from old subscription

      const bool has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;

      if (res < 0) {
        if (res == -ECANCELED || res == -ENOENT) {
          // Our own cancel (for mask update or unregister). Ignore.
          continue;
        }
        // Unexpected error on the poll subscription: treat as `on_error`.
        ++dispatched;
        if (!dispatch_event(fd, EPOLLERR)) return -1;
        continue;
      }

      // `res` is the poll event mask that fired (POLLIN, POLLOUT, etc.),
      // which on Linux equals the epoll event flags (EPOLLIN, EPOLLOUT, etc.).
      ++dispatched;
      if (!dispatch_event(fd, static_cast<uint32_t>(res))) return -1;

      // If the multi-shot subscription terminated unexpectedly (no CQE_F_MORE
      // and res >= 0), re-arm it so the fd remains watched.
      if (!has_more) {
        auto found2 = find_opt(registrations_, fd);
        if (found2 && found2->generation == gen) {
          if (!submit_poll_add_multi(fd, found2->conn->events,
                  encode_user_data(fd, gen)))
            return -1;
        }
      }
    }

    // Flush any SQEs queued during dispatch (e.g., cancel/re-add from
    // `do_enable_interest` called by a handler). They will be submitted
    // by the next `wait()` call at the top of the next `run_once`, but
    // flushing here avoids an extra iteration delay for high-priority paths.
    (void)ring_.submit();

    return dispatched;
  }

  // Establish the current thread as the loop thread without calling `run`.
  // Useful when calling `run_once` directly from test code.
  [[nodiscard]] auto poll_thread_scope() const {
    return scoped_value<const iouring_loop*>{current_loop_, this};
  }

  // Dispatch events in a loop until `stop` is called or `run_once` returns
  // -1. May be called at most once.
  [[nodiscard]] bool run(int timeout_ms = -1) {
    if (!has_run_.kill()) return false;

    const auto scope = poll_thread_scope();
    running_.notify(true);
    scope_exit on_exit{[&] { running_.notify(false); }};

    bool ok = true;
    for (; running_.get();)
      if (run_once(timeout_ms) < 0) {
        ok = false;
        break;
      }

    return ok;
  }

private:
  // Per-registered-fd state. Tracks the connection and a generation counter
  // used to tag `POLL_ADD` submissions and detect stale cancel CQEs.
  struct fd_reg {
    std::shared_ptr<iou_io_conn> conn;
    uint32_t generation{0};
  };

  // `user_data` sentinel for wakeup CQEs (eventfd poll). High bit set so it
  // never collides with a valid encoded (fd, generation) value.
  static constexpr uint64_t k_wake_sentinel = uint64_t{1} << 63;

  // Encode an (fd, generation) pair into the `user_data` field of an SQE.
  static uint64_t encode_user_data(int fd, uint32_t gen) noexcept {
    return (uint64_t{gen} << 32) | uint32_t(fd);
  }

  // Decode `user_data` back into (fd, generation).
  static std::pair<int, uint32_t> decode_user_data(uint64_t ud) noexcept {
    return {static_cast<int>(ud & 0xFFFFFFFFu),
        static_cast<uint32_t>(ud >> 32)};
  }

  // Submit a `IORING_OP_POLL_ADD` multi-shot SQE for `fd` with `events`
  // as the poll mask and `user_data` as the completion tag.
  [[nodiscard]] bool
  submit_poll_add_multi(int fd, uint32_t events, uint64_t user_data) noexcept {
    io_uring_sqe* sqe = ring_.get_sqe();
    if (!sqe) return false;
    sqe->opcode = IORING_OP_POLL_ADD;
    sqe->fd = fd;
    sqe->poll32_events = events;
    sqe->len = IORING_POLL_ADD_MULTI;
    sqe->user_data = user_data;
    return true;
  }

  // Submit a `IORING_OP_POLL_REMOVE` SQE targeting `old_user_data`.
  [[nodiscard]] bool submit_poll_remove(uint64_t old_user_data) noexcept {
    io_uring_sqe* sqe = ring_.get_sqe();
    if (!sqe) return false;
    sqe->opcode = IORING_OP_POLL_REMOVE;
    sqe->addr = old_user_data;
    sqe->user_data = 0; // cancel CQE user_data; we don't need to track it
    return true;
  }

  // Submit a single-shot `IORING_OP_POLL_ADD` for the wakeup eventfd.
  [[nodiscard]] bool submit_wake_poll() noexcept {
    io_uring_sqe* sqe = ring_.get_sqe();
    if (!sqe) return false;
    sqe->opcode = IORING_OP_POLL_ADD;
    sqe->fd = wake_fd_.handle();
    sqe->poll32_events = EPOLLIN; // EPOLLIN == POLLIN on Linux
    // No IORING_POLL_ADD_MULTI: one-shot. We re-arm after each wakeup.
    sqe->user_data = k_wake_sentinel;
    return true;
  }

  // Register `conn` with the loop. Loop-thread only.
  [[nodiscard]] bool do_register_socket(std::shared_ptr<iou_io_conn>&& conn,
      bool readable, bool writable) {
    assert(is_loop_thread());
    const int fd = conn->sock().handle();
    if (registrations_.contains(fd)) return false;

    const uint32_t events = make_event_mask(readable, writable);
    fd_reg& reg = registrations_[fd];
    reg.conn = conn;
    reg.generation = 0;

    if (!submit_poll_add_multi(fd, events, encode_user_data(fd, 0))) {
      registrations_.erase(fd);
      return false;
    }

    conn->events = events;
    return true;
  }

  // Unregister `conn`. Loop-thread only.
  [[nodiscard]] bool do_unregister_socket(iou_io_conn& conn) {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    auto it = registrations_.find(fd);
    if (it == registrations_.end()) return false;

    const uint64_t old_ud = encode_user_data(fd, it->second.generation);
    registrations_.erase(it);
    // Cancel the multi-shot subscription. Its CQE (res=-ECANCELED) will
    // arrive after the registration has been removed, and will be silently
    // discarded by `run_once` (fd not found in `registrations_`).
    return submit_poll_remove(old_ud);
  }

  // Add or remove `flag` from the interest mask for `conn`. If the mask
  // changes, cancels the existing subscription and submits a new one with
  // the updated mask and an incremented generation. Loop-thread only.
  [[nodiscard]] bool
  do_enable_interest(iou_io_conn& conn, uint32_t flag, bool on) noexcept {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    auto it = registrations_.find(fd);
    assert(it != registrations_.end());

    const uint32_t old_mask = conn.events;
    const uint32_t new_mask = on ? (old_mask | flag) : (old_mask & ~flag);
    if (new_mask == old_mask) return true; // no change, skip syscall

    fd_reg& reg = it->second;
    const uint64_t old_ud = encode_user_data(fd, reg.generation);
    ++reg.generation; // invalidate in-flight CQEs from the old subscription
    const uint64_t new_ud = encode_user_data(fd, reg.generation);

    if (!submit_poll_remove(old_ud)) return false;
    if (!submit_poll_add_multi(fd, new_mask, new_ud)) return false;

    conn.events = new_mask;
    return true;
  }

  // Compute the initial event mask. `EPOLLERR | EPOLLHUP | EPOLLRDHUP` are
  // always armed. `EPOLLIN` and `EPOLLOUT` are optional.
  static constexpr uint32_t
  make_event_mask(bool readable = false, bool writable = false) noexcept {
    constexpr uint32_t always_on = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    return always_on | (readable ? uint32_t{EPOLLIN} : uint32_t{0}) |
           (writable ? uint32_t{EPOLLOUT} : uint32_t{0});
  }

  // Swap-and-drain `post_queue_` under the mutex, then invoke each callback.
  [[nodiscard]] bool drain_post_queue() {
    assert(is_loop_thread());
    std::vector<std::function<bool()>> pending;
    {
      std::scoped_lock lock{post_mutex_};
      pending.reserve(post_queue_.size());
      pending.swap(post_queue_);
    }
    for (auto& fn : pending) (void)fn();
    return true;
  }

  // Dispatch a single CQE event for `fd` with poll mask `ev`.
  // Mirrors `epoll_loop::dispatch_event`: `EPOLLIN | EPOLLRDHUP` are routed
  // to `on_readable`, `EPOLLERR | EPOLLHUP` to `on_error`, `EPOLLOUT` to
  // `on_writable`. Same ordering as epoll to ensure buffered data is read
  // before a connection-teardown error is processed.
  [[nodiscard]] bool dispatch_event(int fd, uint32_t ev) {
    assert(is_loop_thread());
    auto found = find_opt(registrations_, fd);
    if (!found) return true;

    // Retain the conn across the entire dispatch.
    const auto conn = found->conn;

    if (ev & (EPOLLIN | EPOLLRDHUP)) (void)conn->on_readable();
    if (ev & (EPOLLERR | EPOLLHUP)) return conn->on_error() || true;
    if (ev & EPOLLOUT) (void)conn->on_writable();
    return true;
  }

  // Write to `wake_fd_` to interrupt a sleeping `ring_.wait`. Thread-safe.
  [[nodiscard]] bool wake() { return wake_fd_.notify(); }

  static io_uring_ring create_ring() {
    auto r = io_uring_ring::create();
    if (!r.is_open())
      throw std::system_error(errno, std::generic_category(),
          "io_uring_ring::create");
    return r;
  }

  static event_fd create_eventfd() {
    auto f = event_fd::create();
    if (!f.is_open())
      throw std::system_error(errno, std::generic_category(), "eventfd");
    return f;
  }

  io_uring_ring ring_;
  const event_fd wake_fd_;

  inline static thread_local const iouring_loop* current_loop_ = nullptr;

  std::unordered_map<int, fd_reg> registrations_;

  std::mutex post_mutex_;
  std::vector<std::function<bool()>> post_queue_;
  const std::chrono::milliseconds post_and_wait_poll_interval_;

  tombstone has_run_;
  notifiable<std::atomic_bool> running_{false};
};

// Runs an `iouring_loop` in its own background thread. API-compatible with
// `epoll_loop_runner`.
class iouring_loop_runner {
public:
  explicit iouring_loop_runner(
      std::chrono::milliseconds post_and_wait_poll_interval =
          iouring_loop::default_post_and_wait_poll_interval)
      : loop_{std::make_shared<iouring_loop>(post_and_wait_poll_interval)},
        thread_{[this](const std::stop_token& st) { run(st); }} {
    if (!loop_->wait_until_running(1000)) {
      thread_.request_stop();
      throw std::runtime_error("iouring_loop_runner failed to start");
    }
  }

  iouring_loop_runner(const iouring_loop_runner&) = delete;
  iouring_loop_runner& operator=(const iouring_loop_runner&) = delete;
  iouring_loop_runner(iouring_loop_runner&&) = delete;
  iouring_loop_runner& operator=(iouring_loop_runner&&) = delete;

  ~iouring_loop_runner() = default; // jthread requests stop and joins

  void stop() { thread_.request_stop(); }

  [[nodiscard]] const std::shared_ptr<iouring_loop>& loop() noexcept {
    return loop_;
  }
  [[nodiscard]] operator iouring_loop&() noexcept { return *loop_; }
  [[nodiscard]] iouring_loop* operator->() noexcept { return loop_.get(); }

private:
  void run(const std::stop_token& st) {
    std::stop_callback on_stop{st, [this] { (void)loop_->stop(); }};
    (void)loop_->run(100);
  }

  std::shared_ptr<iouring_loop> loop_;
  std::jthread thread_;
};

}} // namespace corvid::proto
