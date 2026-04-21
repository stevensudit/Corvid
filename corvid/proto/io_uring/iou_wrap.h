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
#include <liburing.h>

#include <cstddef>
#include <chrono>
#include <system_error>

#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "../../filesys/event_fd.h"
#include "../net_endpoint.h"
#include "../../enums/bitmask_enum.h"

// Wrapper around `io_uring`'s C API, with the primary goal of adding C++
// conveniences.

namespace corvid { inline namespace proto { inline namespace iouring {

// `IORING_CQE_F_*` wrapper.
enum class iou_cqe_flags : uint32_t {
  buffer = IORING_CQE_F_BUFFER, // 0x1 (upper 16 bits are buffer ID if set)
  more = IORING_CQE_F_MORE,     // 0x2
  sock_nonempty = IORING_CQE_F_SOCK_NONEMPTY, // 0x4
  notif = IORING_CQE_F_NOTIF,                 // 0x8
  buffer_id = 0xffff0000,
};

// `IOSQE_*` wrapper.
enum class iou_sqe_flags : uint8_t {
  fixed_file = IOSQE_FIXED_FILE,             // 0x1
  io_drain = IOSQE_IO_DRAIN,                 // 0x2
  io_link = IOSQE_IO_LINK,                   // 0x4
  io_hardlink = IOSQE_IO_HARDLINK,           // 0x8
  async = IOSQE_ASYNC,                       // 0x10
  buffer_select = IOSQE_BUFFER_SELECT,       // 0x20
  cqe_skip_success = IOSQE_CQE_SKIP_SUCCESS, // 0x40
};

}}} // namespace corvid::proto::iouring

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::iou_cqe_flags> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::proto::iouring::iou_cqe_flags,
        "buffer, more, sock_nonempty, notif">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::iou_sqe_flags> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::proto::iouring::iou_sqe_flags,
        "fixed_file, io_drain, io_link, io_hardlink, async, buffer_select, "
        "cqe_skip_success">();

namespace corvid { inline namespace proto { inline namespace iouring {

// Wrapper for `int` results from `io_uring` operations, where `res` >= 0 is a
// byte count (or 0 for ops with no data) and `res` < 0 is a negated `errno`
// value.
class iou_res {
public:
  explicit iou_res(int res = 0) : res_(res) {}

  [[nodiscard]] operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  [[nodiscard]] bool ok(int r = 0) const noexcept { return res_ >= r; }
  [[nodiscard]] int value() const noexcept { return res_; }
  [[nodiscard]] int err() const noexcept { return -res_; }
  [[nodiscard]] size_t bytes() const noexcept {
    return static_cast<size_t>(res_);
  }

  // True if the result is a "soft" error that can be retried.
  [[nodiscard]] bool is_soft_error() const {
    return err() == ETIME || err() == EINTR || err() == EAGAIN ||
           err() == ENOMEM || err() == ERESTART;
  }

  void throw_if_error(const std::string& context, int r = 0) const {
    if (ok(r)) return;
    throw std::system_error(err(), std::system_category(),
        "io_uring error in " + context);
  }

private:
  int res_;
};

// Submission queue event. Wrapper for `io_uring_sqe*`, without adding
// ownership.
class iou_sqe {
public:
  using span_t = std::span<std::byte>;
  using const_span_t = std::span<const std::byte>;

  using ptr_t = io_uring_sqe*;

  iou_sqe() = default;
  explicit iou_sqe(ptr_t sqe) : sqe_(sqe) {}

  [[nodiscard]] operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  [[nodiscard]] ptr_t value() const noexcept { return sqe_; }
  [[nodiscard]] ptr_t* pointer() noexcept { return &sqe_; }

  [[nodiscard]] bool ok() const noexcept { return sqe_; }

  bool prep_nop() noexcept {
    io_uring_prep_nop(sqe_);
    return true;
  }

  bool prep_poll_oneshot(int fd, short poll_mask = POLLIN) noexcept {
    io_uring_prep_poll_add(sqe_, fd, poll_mask);
    return true;
  }

  bool prep_poll_multishot(int fd, short poll_mask = POLLIN) noexcept {
    io_uring_prep_poll_multishot(sqe_, fd, poll_mask);
    return true;
  }

  bool prep_recv(int fd, span_t span, int flags = 0) noexcept {
    io_uring_prep_recv(sqe_, fd, span.data(), span.size(), flags);
    return true;
  }

  bool prep_send(int fd, const_span_t span, int flags = 0) noexcept {
    io_uring_prep_send(sqe_, fd, span.data(), span.size(), flags);
    return true;
  }

  // Read into a pre-registered fixed buffer. `buf_index` is the slot index
  // from `io_uring_register_buffers`. For sockets, `offset` is ignored.
  bool prep_read_fixed(int fd, span_t span, size_t buf_index,
      uint64_t offset = 0) noexcept {
    io_uring_prep_read_fixed(sqe_, fd, span.data(),
        static_cast<unsigned>(span.size()), static_cast<off_t>(offset),
        static_cast<int>(buf_index));
    return true;
  }

  // Write from a pre-registered fixed buffer. `buf_index` is the slot index
  // from `io_uring_register_buffers`. For sockets, `offset` is ignored.
  bool prep_write_fixed(int fd, const_span_t span, size_t buf_index,
      uint64_t offset = 0) noexcept {
    io_uring_prep_write_fixed(sqe_, fd, span.data(),
        static_cast<unsigned>(span.size()), static_cast<off_t>(offset),
        static_cast<int>(buf_index));
    return true;
  }

  bool prep_accept(int fd, int flags = SOCK_NONBLOCK | SOCK_CLOEXEC) noexcept {
    io_uring_prep_accept(sqe_, fd, nullptr, nullptr, flags);
    return true;
  }

  bool prep_accept_multishot(int fd,
      int flags = SOCK_NONBLOCK | SOCK_CLOEXEC) noexcept {
    io_uring_prep_multishot_accept(sqe_, fd, nullptr, nullptr, flags);
    return true;
  }

  bool prep_connect(int fd, const net_endpoint& endpoint) noexcept {
    auto [addr, addrlen] = endpoint.as_sockaddr();
    io_uring_prep_connect(sqe_, fd, addr, addrlen);
    return true;
  }

  bool prep_recvmsg(int fd, msghdr* msg, int flags = 0) noexcept {
    io_uring_prep_recvmsg(sqe_, fd, msg, flags);
    return true;
  }

  bool
  prep_sendmsg(int fd, const msghdr* msg, int flags = MSG_NOSIGNAL) noexcept {
    io_uring_prep_sendmsg(sqe_, fd, msg, flags);
    return true;
  }

  // Close `fd` asynchronously. Caller must have released ownership of `fd`
  // (e.g., via `os_file::release`) before submitting, so the kernel is the
  // sole closer.
  bool prep_close(int fd) noexcept {
    io_uring_prep_close(sqe_, fd);
    return true;
  }

  // Cancel all pending operations targeting `fd`.
  bool prep_cancel_fd(int fd, unsigned flags = 0) noexcept {
    io_uring_prep_cancel_fd(sqe_, fd, flags);
    return true;
  }

  // Cancel all pending operations with `user_data` matching the given value.
  bool prep_cancel_user_data(uint64_t user_data, int flags = 0) noexcept {
    io_uring_prep_cancel64(sqe_, user_data, flags);
    return true;
  }

  // Standalone timeout: fires with `-ETIME` after `duration` elapses, or with
  // `-ECANCELED` when canceled by a linked predecessor completing first.
  bool prep_timeout(__kernel_timespec* duration) noexcept {
    io_uring_prep_timeout(sqe_, duration, 0, 0);
    return true;
  }

  // Linked timeout: must be the second SQE in a linked pair (first SQE must
  // have `IOSQE_IO_LINK` set). Cancels the preceding op if `duration` expires
  // before it completes.
  bool prep_link_timeout(__kernel_timespec* duration) noexcept {
    io_uring_prep_link_timeout(sqe_, duration, 0);
    return true;
  }

  // Set additional SQE flags (e.g., `IOSQE_IO_LINK` for linked SQEs).
  bool set_sqe_flags(iou_sqe_flags flags) noexcept {
    sqe_->flags |= *flags;
    return true;
  }

  bool set_data_pointer(void* data) noexcept {
    io_uring_sqe_set_data(sqe_, data);
    return true;
  }

  bool set_data_int(uint64_t data) noexcept {
    io_uring_sqe_set_data64(sqe_, data);
    return true;
  }

private:
  ptr_t sqe_{};
};

// Completion queue event. Wrapper for `io_uring_cqe*`, without adding
// ownership.
class iou_cqe {
public:
  using ptr_t = io_uring_cqe*;

  iou_cqe() = default;
  explicit iou_cqe(ptr_t cqe) : cqe_(cqe) {}

  [[nodiscard]] operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  [[nodiscard]] ptr_t value() const noexcept { return cqe_; }
  [[nodiscard]] ptr_t* pointer() noexcept { return &cqe_; }

  [[nodiscard]] bool ok() const noexcept { return cqe_; }

  [[nodiscard]] iou_res res() const noexcept { return iou_res{cqe_->res}; }
  [[nodiscard]] uint64_t user_data() const noexcept { return cqe_->user_data; }
  [[nodiscard]] iou_cqe_flags flags() const noexcept {
    return iou_cqe_flags{cqe_->flags};
  }

  template<typename T = void>
  [[nodiscard]] T* get_data_ptr() const noexcept {
    return static_cast<T*>(io_uring_cqe_get_data(cqe_));
  }

  [[nodiscard]] uint64_t get_data_int() const noexcept {
    return io_uring_cqe_get_data64(cqe_);
  }

private:
  ptr_t cqe_{};
};

// Wrapper over `io_uring`. Non-movable and non-copyable, and has ownership.
class iou_ring {
public:
  using ptr_t = io_uring*;
  using duration_t = std::chrono::milliseconds;

  // Construct and initialize an io_uring with the given `ring_size` and
  // `flags`.
  explicit iou_ring(size_t ring_size = 256, int flags = 0) {
    iou_res res{io_uring_queue_init(ring_size, &ring_, flags)};
    if (res) return;
    throw std::system_error(res.err(), std::system_category(),
        "io_uring_queue_init");
  }

  iou_ring(const iou_ring&) = delete;
  iou_ring(iou_ring&&) = delete;
  iou_ring& operator=(const iou_ring&) = delete;
  iou_ring& operator=(iou_ring&&) = delete;

  ~iou_ring() { io_uring_queue_exit(&ring_); }

  ptr_t get_ptr() noexcept { return &ring_; }
  operator ptr_t() noexcept { return &ring_; }

  // Timespec from `std::chrono` duration.
  static constexpr __kernel_timespec to_timespec(duration_t timeout) {
    return __kernel_timespec{
        .tv_sec = timeout.count() / 1000,
        .tv_nsec = (timeout.count() % 1000) * 1'000'000LL,
    };
  }

  // Wait for a CQE to become available, with an optional timeout.
  [[nodiscard]] iou_res wait_cqe_timeout(duration_t timeout = {}) {
    iou_cqe cqe; // Not returned.
    auto ts = to_timespec(timeout);
    return iou_res{io_uring_wait_cqe_timeout(&ring_, cqe.pointer(), &ts)};
  }

  // Loop over available CQEs, calling `fn` on each. Advances the CQ head by
  // the number of CQEs processed. Must be called on the loop thread after a
  // call to `wait_cqe_timeout()` that returned at least one CQE. Returns the
  // number of CQEs processed.
  [[nodiscard]] size_t for_each_cqe(auto&& fn) {
    size_t count{};
    iou_cqe::ptr_t cqe;
    unsigned head;
    io_uring_for_each_cqe(&ring_, head, cqe) {
      (void)fn(iou_cqe{cqe});
      ++count;
    }
    io_uring_cq_advance(&ring_, count);
    return count;
  }

  // Get the next SQE to fill, or null if the SQ is full. The caller must later
  // call `submit()`.
  iou_sqe next_sqe() noexcept { return iou_sqe{io_uring_get_sqe(&ring_)}; }

  // Submit filled SQEs to the kernel. Returns an `iou_res` with the number of
  // SQEs submitted, or an error. An `ok(n)` on the result is a good idea.
  iou_res submit() noexcept { return iou_res{io_uring_submit(&ring_)}; }

  // Register a fixed buffer table with the kernel. `iovecs` points to an
  // array of `count` `iovec` entries describing the pre-allocated buffers.
  // Each entry's `iov_base`/`iov_len` defines one registered slot used by
  // `read_fixed`/`write_fixed`. Returns false (with `errno` set) on failure.
  [[nodiscard]] bool
  register_buffers(const iovec* iovecs, size_t count) noexcept {
    return io_uring_register_buffers(&ring_, iovecs,
               static_cast<unsigned>(count)) == 0;
  }

private:
  io_uring ring_{};
};

}}} // namespace corvid::proto::iouring
