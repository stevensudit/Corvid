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
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#include <sys/epoll.h>
#include <sys/mman.h>

#include "../concurrency/notifiable.h"
#include "../concurrency/tombstone.h"
#include "../containers/scoped_value.h"
#include "../containers/scope_exit.h"
#include "../containers/opt_find.h"
#include "../filesys/io_uring_ring.h"
#include "../filesys/event_fd.h"
#include "../filesys/net_socket.h"

// NOTICE: This is purely vibe-coded. It is not ready for production.

namespace corvid { inline namespace proto {

using namespace corvid::container::value_scoping;

// Provided-buffer ring for `IORING_OP_RECV` with `IOSQE_BUFFER_SELECT`.
//
// Registers a pool of fixed-size buffers with the kernel via
// `IORING_REGISTER_PBUF_RING`. The kernel picks a buffer from the pool for
// each completed recv operation and reports the chosen `buf_id` in the upper
// 16 bits of the CQE `flags` field. After the caller processes the data it
// must return the buffer via `return_buf(buf_id)` to make it available again.
//
// `count` must be a power of two and no greater than 65536.
// `buf_size` is the byte capacity of each individual buffer.
class pbuf_ring {
public:
  static constexpr uint16_t default_bgid = 0;
  static constexpr unsigned default_count = 256;
  static constexpr size_t default_buf_size = 4096;

  pbuf_ring() noexcept = default;
  pbuf_ring(const pbuf_ring&) = delete;
  pbuf_ring& operator=(const pbuf_ring&) = delete;

  ~pbuf_ring() { do_close(); }

  // Initialize the ring on `ring_fd`. Throws `std::system_error` on failure.
  void init(int ring_fd, uint16_t bgid = default_bgid,
      unsigned count = default_count, size_t buf_size = default_buf_size) {
    assert(count >= 1 && (count & (count - 1)) == 0);
    assert(count <= 65536);
    ring_fd_ = ring_fd;
    bgid_ = bgid;
    count_ = count;
    buf_size_ = buf_size;

    // Register with the kernel using the mmap path (kernel allocates the ring
    // structure; we mmap it back).
    io_uring_buf_reg reg{};
    reg.ring_entries = count;
    reg.bgid = bgid;
    reg.flags = IOU_PBUF_RING_MMAP;
    if (iou_register(ring_fd, IORING_REGISTER_PBUF_RING, &reg, 1) < 0)
      throw std::system_error(errno, std::generic_category(),
          "IORING_REGISTER_PBUF_RING");
    registered_ = true;

    // Mmap the ring structure at the kernel-reserved offset.
    const size_t ring_sz = count * sizeof(io_uring_buf);
    const off_t ring_off =
        static_cast<off_t>(IORING_OFF_PBUF_RING) |
        (static_cast<off_t>(bgid) << IORING_OFF_PBUF_SHIFT);
    ring_ = static_cast<io_uring_buf_ring*>(::mmap(nullptr, ring_sz,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring_fd, ring_off));
    if (ring_ == MAP_FAILED) {
      ring_ = nullptr;
      throw std::system_error(errno, std::generic_category(),
          "mmap pbuf_ring");
    }
    ring_mmap_size_ = ring_sz;

    // Allocate the data buffers (anonymous private mapping; never touched by
    // the kernel directly -- the kernel DMA's into them via the registered
    // addresses).
    const size_t data_sz = count * buf_size;
    bufs_ = ::mmap(nullptr, data_sz, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (bufs_ == MAP_FAILED) {
      bufs_ = nullptr;
      throw std::system_error(errno, std::generic_category(),
          "mmap pbuf_data");
    }
    bufs_mmap_size_ = data_sz;

    // Populate all slots and set tail = count (all buffers available).
    const auto mask = static_cast<uint16_t>(count - 1);
    for (uint16_t i = 0; i < static_cast<uint16_t>(count); ++i) {
      io_uring_buf& slot = ring_->bufs[i & mask];
      slot.addr = reinterpret_cast<__u64>(data(i));
      slot.len = static_cast<__u32>(buf_size);
      slot.bid = i;
      // slot.resv is NOT written; bufs[0].resv == ring_->tail (see below).
    }
    // Atomically publish tail. Note: ring_->tail aliases bufs[0].resv.
    __atomic_store_n(&ring_->tail, static_cast<__u16>(count),
        __ATOMIC_RELEASE);
    ring_tail_ = static_cast<uint16_t>(count);
  }

  [[nodiscard]] bool is_open() const noexcept { return ring_ != nullptr; }
  [[nodiscard]] uint16_t bgid() const noexcept { return bgid_; }
  [[nodiscard]] size_t buf_size() const noexcept { return buf_size_; }

  // Pointer to the data for buffer `id`.
  [[nodiscard]] void* data(uint16_t id) const noexcept {
    return static_cast<char*>(bufs_) + (static_cast<size_t>(id) * buf_size_);
  }

  // Return buffer `id` to the pool. The caller must have finished reading
  // `data(id)` before calling this.
  void return_buf(uint16_t id) noexcept {
    assert(is_open());
    const auto mask = static_cast<uint16_t>(count_ - 1);
    io_uring_buf& slot = ring_->bufs[ring_tail_ & mask];
    slot.addr = reinterpret_cast<__u64>(data(id));
    slot.len = static_cast<__u32>(buf_size_);
    slot.bid = id;
    // ring_->tail aliases bufs[0].resv; do NOT write slot.resv separately.
    __atomic_store_n(&ring_->tail, static_cast<__u16>(ring_tail_ + 1),
        __ATOMIC_RELEASE);
    ++ring_tail_;
  }

private:
  void do_close() noexcept {
    if (bufs_ && bufs_ != MAP_FAILED) {
      ::munmap(bufs_, bufs_mmap_size_);
      bufs_ = nullptr;
    }
    if (ring_ && ring_ != MAP_FAILED) {
      ::munmap(ring_, ring_mmap_size_);
      ring_ = nullptr;
    }
    if (registered_) {
      io_uring_buf_reg reg{};
      reg.bgid = bgid_;
      iou_register(ring_fd_, IORING_UNREGISTER_PBUF_RING, &reg, 1);
      registered_ = false;
    }
  }

  int ring_fd_{-1};
  uint16_t bgid_{0};
  size_t buf_size_{0};
  unsigned count_{0};

  io_uring_buf_ring* ring_{nullptr};
  size_t ring_mmap_size_{0};

  void* bufs_{nullptr};
  size_t bufs_mmap_size_{0};

  // Local tail (wraps at 2^16). Tracks the next slot to write when returning
  // a buffer. Matches ring_->tail after each return_buf call.
  uint16_t ring_tail_{0};
  bool registered_{false};
};

// Abstract base for objects registered with `iouring_loop`.
//
// Two dispatch modes are supported:
//
//  Poll mode: the fd is watched via `IORING_OP_POLL_ADD` (multi-shot). CQEs
//  trigger `on_readable`, `on_writable`, or `on_error`. Used for listening
//  sockets (accept loop) and the brief connect-pending phase of outbound
//  connections. Identical to the `io_conn` used by `epoll_loop`.
//
//  Completion mode: reads use `IORING_OP_RECV` with `IOSQE_BUFFER_SELECT`
//  (multi-shot); writes use `IORING_OP_SEND` (one-shot). CQEs trigger
//  `on_recv_complete` and `on_send_complete`. Used for established
//  connections.
//
// Poll-mode callbacks (`on_readable`, `on_writable`, `on_error`):
//   Return value is ignored (always treated as "keep going").
//
// Completion-mode callbacks:
//   `on_recv_complete(res, data, len)`: called when a recv CQE fires.
//     `res` > 0: `len` bytes received at `data` (valid only for this call).
//     `res` == 0: peer closed the write side (EOF).
//     `res` < 0: error (negative errno, e.g. `-ECANCELED`).
//   `on_send_complete(res)`: called when a send CQE fires.
//     `res` >= 0: bytes sent (may be less than requested -- short send).
//     `res` < 0: error.
struct iou_io_conn: std::enable_shared_from_this<iou_io_conn> {
  explicit iou_io_conn(net_socket&& sock) : sock_(std::move(sock)) {}
  net_socket& sock() noexcept { return sock_; }
  const net_socket& sock() const noexcept { return sock_; }

  virtual bool on_readable() { return false; }
  virtual bool on_writable() { return false; }
  virtual bool on_error() { return on_readable(); }

  // `data` is valid only for the duration of this call; copy before returning.
  virtual bool
  on_recv_complete(int32_t /*res*/, const void* /*data*/, size_t /*len*/) {
    return false;
  }
  virtual bool on_send_complete(int32_t /*res*/) { return false; }

  virtual ~iou_io_conn() = default;

  // Current epoll-compatible interest mask. Written only by `iouring_loop`
  // while on the loop thread (poll mode only).
  uint32_t events{0};

private:
  net_socket sock_;
};

// `io_uring`-based I/O event loop.
//
// Two registration modes (per connection):
//
//   Poll mode (`register_socket`): uses `IORING_OP_POLL_ADD` with
//   `IORING_POLL_ADD_MULTI` for level-triggered readiness notification.
//   Suitable for listeners, connecting sockets, and raw `iou_io_conn` users.
//   Dispatches `on_readable` / `on_writable` / `on_error`.
//
//   Completion mode (`register_conn` + `arm_recv` / `submit_send`): uses
//   `IORING_OP_RECV` with `IORING_RECV_MULTISHOT` + `IOSQE_BUFFER_SELECT`
//   for receives (kernel picks a buffer from the `pbuf_ring` pool), and
//   `IORING_OP_SEND` for sends. Dispatches `on_recv_complete` /
//   `on_send_complete`. Suitable for established stream connections.
//
// A connection transitions from poll mode to completion mode by calling
// `cancel_poll` (from a poll-mode callback) and then `arm_recv`.
//
// Thread-wakeup (for `post` and `stop`) uses an `eventfd` with a single-shot
// `IORING_OP_POLL_ADD` that is re-armed after each wakeup CQE.
class iouring_loop {
public:
  static constexpr size_t max_events = 64;
  static constexpr std::chrono::milliseconds
      default_post_and_wait_poll_interval{100};

  // Create the ring, initialize the provided-buffer pool, and arm the wakeup
  // eventfd. Throws `std::system_error` on failure.
  explicit iouring_loop(
      std::chrono::milliseconds post_and_wait_poll_interval =
          default_post_and_wait_poll_interval)
      : ring_{create_ring()}, wake_fd_{create_eventfd()},
        post_and_wait_poll_interval_{post_and_wait_poll_interval} {
    pbuf_.init(ring_.handle());
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

  // -----------------------------------------------------------------------
  // Poll-mode registration (IORING_OP_POLL_ADD). Suitable for listeners,
  // connectors, and raw `iou_io_conn` subclasses.
  // -----------------------------------------------------------------------

  // Register `conn` with POLL_ADD. `readable` arms EPOLLIN; `writable` arms
  // EPOLLOUT. EPOLLERR, EPOLLHUP, and EPOLLRDHUP are always armed.
  // Returns false if already registered or SQ is full; auto-promotes to
  // `post` when called off the loop thread.
  [[nodiscard]] bool register_socket(std::shared_ptr<iou_io_conn> conn,
      bool readable = true, bool writable = false) {
    return execute_or_post(
        [this, conn = std::move(conn), readable, writable]() mutable {
          return do_register_socket(std::move(conn), readable, writable);
        });
  }

  // Add or remove `EPOLLIN` from the poll mask. No-op in completion mode.
  [[nodiscard]] bool enable_reads(iou_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      auto found = find_opt(registrations_, sp->sock().handle());
      if (!found) return false;
      if (found->completion_mode) {
        if (on) {
          if (found->recv_armed) return true; // multishot still active
          return do_arm_recv(sp->sock().handle(), *found);
        }
        return do_cancel_recv(sp->sock().handle(), *found);
      }
      return do_enable_interest(*sp, EPOLLIN, on);
    });
  }

  // Add or remove `EPOLLRDHUP` from the poll mask. No-op in completion mode
  // (EOF detected via `res == 0` in `on_recv_complete`).
  [[nodiscard]] bool enable_rdhup(iou_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      auto found = find_opt(registrations_, sp->sock().handle());
      if (!found) return false;
      if (found->completion_mode) return true;
      return do_enable_interest(*sp, EPOLLRDHUP, on);
    });
  }

  // Add or remove `EPOLLOUT` from the poll mask. No-op in completion mode
  // (sends are submitted directly via `submit_send`).
  [[nodiscard]] bool enable_writes(iou_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      auto found = find_opt(registrations_, sp->sock().handle());
      if (!found) return false;
      if (found->completion_mode) return true;
      return do_enable_interest(*sp, EPOLLOUT, on);
    });
  }

  // Unregister by connection object. Cancels any in-flight poll/recv/send.
  [[nodiscard]] bool unregister_socket(iou_io_conn& conn) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp)] {
      return do_unregister_socket(*sp);
    });
  }

  // Unregister by socket handle. Cancels any in-flight poll/recv/send.
  [[nodiscard]] bool unregister_socket(const net_socket& sock) {
    const auto fd = sock.handle();
    return execute_or_post([this, fd] {
      auto found = find_opt(registrations_, fd);
      if (!found) return false;
      return do_unregister_socket(*found->conn);
    });
  }

  // Look up `fd` in the registration table. Loop-thread only.
  [[nodiscard]] std::shared_ptr<iou_io_conn> find_fd(
      os_file::file_handle_t fd) const {
    assert(is_loop_thread());
    auto found = find_opt(registrations_, fd);
    if (!found) return nullptr;
    return found->conn;
  }

  // -----------------------------------------------------------------------
  // Completion-mode registration (IORING_OP_RECV / IORING_OP_SEND).
  // Suitable for established stream connections.
  // -----------------------------------------------------------------------

  // Register `conn` without any POLL_ADD. Creates the fd_reg in completion
  // mode; call `arm_recv` afterwards to start receiving. Returns false if
  // already registered. Auto-promotes to `post` when called off-thread.
  [[nodiscard]] bool register_conn(std::shared_ptr<iou_io_conn> conn) {
    return execute_or_post([this, conn = std::move(conn)]() mutable {
      return do_register_conn(std::move(conn));
    });
  }

  // Submit a multi-shot `IORING_OP_RECV` with `IOSQE_BUFFER_SELECT` for
  // `conn`. The kernel delivers CQEs (each with a pool buffer) until the
  // subscription is cancelled or an error occurs. Loop-thread only.
  [[nodiscard]] bool arm_recv(iou_io_conn& conn) {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    auto it = registrations_.find(fd);
    if (it == registrations_.end()) return false;
    if (it->second.recv_armed) return true;
    return do_arm_recv(fd, it->second);
  }

  // Cancel an in-flight multi-shot recv for `conn`. Loop-thread only.
  [[nodiscard]] bool cancel_recv(iou_io_conn& conn) {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    auto it = registrations_.find(fd);
    if (it == registrations_.end()) return false;
    if (!it->second.recv_armed) return true;
    return do_cancel_recv(fd, it->second);
  }

  // Submit a one-shot `IORING_OP_SEND` for `conn` with `data[0..len)`. Only
  // one send may be in flight at a time per connection. Returns false if a
  // send is already in flight or the SQ is full. Loop-thread only.
  [[nodiscard]] bool
  submit_send(iou_io_conn& conn, const void* data, size_t len) {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    auto it = registrations_.find(fd);
    if (it == registrations_.end()) return false;
    if (it->second.send_armed) return false; // already in flight
    return do_submit_send(fd, it->second, data, len);
  }

  // Cancel the POLL_ADD subscription for `conn` (e.g., after a connect
  // completes) and switch its fd_reg to completion mode. The fd_reg remains
  // registered so subsequent `arm_recv` / `submit_send` calls work.
  // Loop-thread only.
  [[nodiscard]] bool cancel_poll(iou_io_conn& conn) {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    auto it = registrations_.find(fd);
    if (it == registrations_.end()) return false;
    auto& reg = it->second;
    if (reg.completion_mode) return true; // already switched
    const uint64_t old_ud = encode_ud(fd, reg.poll_gen, ud_type::poll);
    ++reg.poll_gen;
    conn.events = 0;
    reg.completion_mode = true;
    return submit_poll_remove(old_ud);
  }

  // -----------------------------------------------------------------------
  // Thread-safe control.
  // -----------------------------------------------------------------------

  // Schedule `fn` to run at the top of the next `run_once`. Thread-safe.
  [[nodiscard]] bool post(std::function<bool()> fn) {
    {
      std::scoped_lock lock{post_mutex_};
      post_queue_.push_back(std::move(fn));
    }
    return wake();
  }

  // Run `fn` on the loop thread and block until it returns. Returns false if
  // the loop is not running. Thread-safe.
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

  // Run `fn` immediately if on the loop thread, otherwise post it.
  template<typename FN>
  [[nodiscard]] bool execute_or_post(FN&& fn) {
    if (is_loop_thread()) return fn();
    return post(std::forward<FN>(fn));
  }

  // Signal the loop to exit. Thread-safe.
  [[nodiscard]] bool stop() {
    running_.notify(false);
    return wake();
  }

  bool is_loop_thread() const noexcept { return current_loop_ == this; }

  // Block until `run` enters its polling loop. Returns false on timeout.
  [[nodiscard]] bool wait_until_running(int timeout_ms = -1) {
    if (timeout_ms < 0) timeout_ms = 60000;
    return running_.wait_for_value(std::chrono::milliseconds{timeout_ms},
        true);
  }

  // -----------------------------------------------------------------------
  // Event loop.
  // -----------------------------------------------------------------------

  // Drain `post_queue_`, submit pending SQEs, wait for CQEs, and dispatch.
  // Returns the number of events dispatched, or -1 on error.
  // NOLINTNEXTLINE(readability-function-cognitive-complexity)
  [[nodiscard]] int run_once(int timeout_ms = -1) {
    assert(is_loop_thread());

    if (!drain_post_queue()) return -1;

    if (ring_.submit() < 0) return -1;

    const int n = ring_.wait(timeout_ms);
    if (n < 0) return os_file::is_hard_error() ? -1 : 0;
    if (n == 0) return 0;

    int dispatched = 0;
    for (;;) {
      const io_uring_cqe* cqe = ring_.peek_cqe();
      if (!cqe) break;

      const uint64_t user_data = cqe->user_data;
      const uint32_t cqe_flags = cqe->flags;
      const int32_t res = cqe->res;
      ring_.advance_cq(1);

      // Wakeup from the internal eventfd.
      if (user_data == k_wake_sentinel) {
        if (!wake_fd_.read()) return -1;
        if (!submit_wake_poll()) return -1;
        continue;
      }

      // Discard sentinel (cancel ACKs, etc.).
      if (user_data == k_discard_sentinel) continue;

      const ud_type type = decode_ud_type(user_data);
      const auto [fd, gen] = decode_ud(user_data);
      auto found = find_opt(registrations_, fd);
      if (!found) continue; // already unregistered

      if (type == ud_type::poll) {
        if (found->poll_gen != gen) continue; // stale from old subscription

        const bool has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;

        if (res < 0) {
          if (res == -ECANCELED || res == -ENOENT) continue;
          ++dispatched;
          if (!dispatch_poll_event(fd, EPOLLERR)) return -1;
          continue;
        }

        ++dispatched;
        if (!dispatch_poll_event(fd, static_cast<uint32_t>(res))) return -1;

        if (!has_more) {
          // Multi-shot subscription terminated; re-arm if still registered.
          auto found2 = find_opt(registrations_, fd);
          if (found2 && found2->poll_gen == gen && !found2->completion_mode) {
            if (!submit_poll_add_multi(fd, found2->conn->events,
                    encode_ud(fd, gen, ud_type::poll)))
              return -1;
          }
        }

      } else if (type == ud_type::recv) {
        if (found->recv_gen != gen) continue; // stale

        const bool has_more = (cqe_flags & IORING_CQE_F_MORE) != 0;
        if (!has_more) found->recv_armed = false;

        const bool has_buf = (cqe_flags & IORING_CQE_F_BUFFER) != 0;
        const uint16_t buf_id =
            has_buf
                ? static_cast<uint16_t>(cqe_flags >> IORING_CQE_BUFFER_SHIFT)
                : 0;
        const void* data = (has_buf && res > 0) ? pbuf_.data(buf_id) : nullptr;
        const size_t len = res > 0 ? static_cast<size_t>(res) : 0;

        ++dispatched;
        (void)found->conn->on_recv_complete(res, data, len);

        // Return buffer AFTER the callback has finished reading from it.
        if (has_buf) pbuf_.return_buf(buf_id);

      } else if (type == ud_type::send) {
        if (found->send_gen != gen) continue; // stale
        found->send_armed = false;

        ++dispatched;
        (void)found->conn->on_send_complete(res);
      }
    }

    (void)ring_.submit();
    return dispatched;
  }

  // Establish the current thread as the loop thread. Useful for single-
  // threaded `run_once` calls in tests.
  [[nodiscard]] auto poll_thread_scope() const {
    return scoped_value<const iouring_loop*>{current_loop_, this};
  }

  // Run the event loop until `stop` is called or `run_once` returns -1.
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
  // -----------------------------------------------------------------------
  // Per-fd registration state.
  // -----------------------------------------------------------------------

  enum class ud_type : uint8_t { poll = 0, recv = 1, send = 2 };

  // `user_data` layout (64 bits):
  //   bit  63   : wake sentinel (handled before decode)
  //   bits 62-61: ud_type (0=poll, 1=recv, 2=send)
  //   bits 60-32: generation (29 bits)
  //   bits 31-0 : fd
  static constexpr uint64_t k_wake_sentinel = uint64_t{1} << 63;
  static constexpr uint64_t k_discard_sentinel = 0;

  static uint64_t encode_ud(int fd, uint32_t gen, ud_type t) noexcept {
    return (static_cast<uint64_t>(static_cast<uint8_t>(t)) << 61) |
           (static_cast<uint64_t>(gen & 0x1FFFFFFFU) << 32) |
           static_cast<uint32_t>(fd);
  }

  static ud_type decode_ud_type(uint64_t ud) noexcept {
    return static_cast<ud_type>((ud >> 61) & 3);
  }

  static std::pair<int, uint32_t> decode_ud(uint64_t ud) noexcept {
    return {static_cast<int>(ud & 0xFFFFFFFFU),
        static_cast<uint32_t>((ud >> 32) & 0x1FFFFFFFU)};
  }

  // Keep legacy name for the poll path (used by existing helpers).
  static uint64_t encode_user_data(int fd, uint32_t gen) noexcept {
    return encode_ud(fd, gen, ud_type::poll);
  }

  struct fd_reg {
    std::shared_ptr<iou_io_conn> conn;
    uint32_t poll_gen{0};
    uint32_t recv_gen{0};
    uint32_t send_gen{0};
    bool recv_armed{false};
    bool send_armed{false};
    bool completion_mode{false};
  };

  // -----------------------------------------------------------------------
  // SQE submission helpers.
  // -----------------------------------------------------------------------

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

  [[nodiscard]] bool submit_poll_remove(uint64_t old_user_data) noexcept {
    io_uring_sqe* sqe = ring_.get_sqe();
    if (!sqe) return false;
    sqe->opcode = IORING_OP_POLL_REMOVE;
    sqe->addr = old_user_data;
    sqe->user_data = k_discard_sentinel;
    return true;
  }

  [[nodiscard]] bool submit_wake_poll() noexcept {
    io_uring_sqe* sqe = ring_.get_sqe();
    if (!sqe) return false;
    sqe->opcode = IORING_OP_POLL_ADD;
    sqe->fd = wake_fd_.handle();
    sqe->poll32_events = EPOLLIN;
    sqe->user_data = k_wake_sentinel;
    return true;
  }

  // Submit a multi-shot IORING_OP_RECV with IOSQE_BUFFER_SELECT.
  [[nodiscard]] bool submit_recv_multishot(int fd, uint32_t gen) noexcept {
    io_uring_sqe* sqe = ring_.get_sqe();
    if (!sqe) return false;
    sqe->opcode = IORING_OP_RECV;
    sqe->fd = fd;
    sqe->addr = 0; // kernel fills from pool
    sqe->len = 0;  // use each buffer's registered length
    sqe->buf_group = pbuf_.bgid();
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->ioprio = IORING_RECV_MULTISHOT;
    sqe->user_data = encode_ud(fd, gen, ud_type::recv);
    return true;
  }

  // Submit IORING_OP_ASYNC_CANCEL targeting `target_ud`.
  [[nodiscard]] bool submit_async_cancel(uint64_t target_ud) noexcept {
    io_uring_sqe* sqe = ring_.get_sqe();
    if (!sqe) return false;
    sqe->opcode = IORING_OP_ASYNC_CANCEL;
    sqe->addr = target_ud;
    sqe->user_data = k_discard_sentinel;
    return true;
  }

  // -----------------------------------------------------------------------
  // Registration management.
  // -----------------------------------------------------------------------

  [[nodiscard]] bool do_register_socket(std::shared_ptr<iou_io_conn>&& conn,
      bool readable, bool writable) {
    assert(is_loop_thread());
    const int fd = conn->sock().handle();
    if (registrations_.contains(fd)) return false;

    const uint32_t events = make_event_mask(readable, writable);
    fd_reg& reg = registrations_[fd];
    reg.conn = conn;
    reg.completion_mode = false;

    if (!submit_poll_add_multi(fd, events, encode_ud(fd, 0, ud_type::poll))) {
      registrations_.erase(fd);
      return false;
    }

    conn->events = events;
    return true;
  }

  [[nodiscard]] bool do_register_conn(std::shared_ptr<iou_io_conn>&& conn) {
    assert(is_loop_thread());
    const int fd = conn->sock().handle();
    if (registrations_.contains(fd)) return false;

    fd_reg& reg = registrations_[fd];
    reg.conn = std::move(conn);
    reg.completion_mode = true;
    return true;
  }

  [[nodiscard]] bool do_unregister_socket(iou_io_conn& conn) {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    auto it = registrations_.find(fd);
    if (it == registrations_.end()) return false;

    auto& reg = it->second;
    if (reg.completion_mode) {
      if (reg.recv_armed) {
        (void)submit_async_cancel(encode_ud(fd, reg.recv_gen, ud_type::recv));
      }
      if (reg.send_armed) {
        (void)submit_async_cancel(encode_ud(fd, reg.send_gen, ud_type::send));
      }
    } else {
      (void)submit_poll_remove(encode_ud(fd, reg.poll_gen, ud_type::poll));
    }

    registrations_.erase(it);
    return true;
  }

  [[nodiscard]] bool do_arm_recv(int fd, fd_reg& reg) noexcept {
    assert(!reg.recv_armed);
    if (!submit_recv_multishot(fd, reg.recv_gen)) return false;
    reg.recv_armed = true;
    return true;
  }

  [[nodiscard]] bool do_cancel_recv(int fd, fd_reg& reg) noexcept {
    if (!reg.recv_armed) return true;
    const uint64_t old_ud = encode_ud(fd, reg.recv_gen, ud_type::recv);
    ++reg.recv_gen; // invalidate in-flight CQEs
    reg.recv_armed = false;
    return submit_async_cancel(old_ud);
  }

  [[nodiscard]] bool
  do_submit_send(int fd, fd_reg& reg, const void* data, size_t len) noexcept {
    assert(!reg.send_armed);
    io_uring_sqe* sqe = ring_.get_sqe();
    if (!sqe) return false;
    sqe->opcode = IORING_OP_SEND;
    sqe->fd = fd;
    sqe->addr = reinterpret_cast<uint64_t>(data);
    sqe->len = static_cast<uint32_t>(std::min(len, size_t{UINT32_MAX}));
    sqe->user_data = encode_ud(fd, reg.send_gen, ud_type::send);
    reg.send_armed = true;
    return true;
  }

  [[nodiscard]] bool
  do_enable_interest(iou_io_conn& conn, uint32_t flag, bool on) noexcept {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    auto it = registrations_.find(fd);
    assert(it != registrations_.end());

    const uint32_t old_mask = conn.events;
    const uint32_t new_mask = on ? (old_mask | flag) : (old_mask & ~flag);
    if (new_mask == old_mask) return true;

    auto& reg = it->second;
    const uint64_t old_ud = encode_ud(fd, reg.poll_gen, ud_type::poll);
    ++reg.poll_gen;
    const uint64_t new_ud = encode_ud(fd, reg.poll_gen, ud_type::poll);

    if (!submit_poll_remove(old_ud)) return false;
    if (!submit_poll_add_multi(fd, new_mask, new_ud)) return false;

    conn.events = new_mask;
    return true;
  }

  // -----------------------------------------------------------------------
  // Event dispatch.
  // -----------------------------------------------------------------------

  [[nodiscard]] bool dispatch_poll_event(int fd, uint32_t ev) {
    assert(is_loop_thread());
    auto found = find_opt(registrations_, fd);
    if (!found) return true;

    const auto conn = found->conn;
    if (ev & (EPOLLIN | EPOLLRDHUP)) (void)conn->on_readable();
    if (ev & (EPOLLERR | EPOLLHUP)) return conn->on_error() || true;
    if (ev & EPOLLOUT) (void)conn->on_writable();
    return true;
  }

  [[nodiscard]] bool drain_post_queue() {
    assert(is_loop_thread());
    std::vector<std::function<bool()>> pending;
    {
      std::scoped_lock lock{post_mutex_};
      pending.swap(post_queue_);
    }
    for (auto& fn : pending) (void)fn();
    return true;
  }

  // -----------------------------------------------------------------------
  // Utilities.
  // -----------------------------------------------------------------------

  [[nodiscard]] bool wake() { return wake_fd_.notify(); }

  static constexpr uint32_t
  make_event_mask(bool readable = false, bool writable = false) noexcept {
    constexpr uint32_t always_on = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    return always_on | (readable ? uint32_t{EPOLLIN} : uint32_t{0}) |
           (writable ? uint32_t{EPOLLOUT} : uint32_t{0});
  }

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

  // -----------------------------------------------------------------------
  // Members.
  // -----------------------------------------------------------------------

  io_uring_ring ring_;
  const event_fd wake_fd_;
  pbuf_ring pbuf_;

  inline static thread_local const iouring_loop* current_loop_ = nullptr;

  std::unordered_map<int, fd_reg> registrations_;

  std::mutex post_mutex_;
  std::vector<std::function<bool()>> post_queue_;
  const std::chrono::milliseconds post_and_wait_poll_interval_;

  tombstone has_run_;
  notifiable<std::atomic_bool> running_{false};
};

// Runs an `iouring_loop` in its own background thread.
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

  ~iouring_loop_runner() = default;

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
