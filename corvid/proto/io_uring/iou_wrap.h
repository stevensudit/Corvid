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
#include <limits>
#include <linux/time_types.h>
#include <system_error>

#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include "../../filesys/event_fd.h"
#include "../net_endpoint.h"
#include "../../enums/bitmask_enum.h"

// Wrapper around `io_uring`'s C API, with the primary goal of adding C++
// conveniences.

namespace corvid { inline namespace proto { namespace iouring {

#pragma region iou flags

// `IORING_SETUP_*` wrapper.
enum class iou_setup_flags : uint32_t {
  setup_iopoll = IORING_SETUP_IOPOLL,                         // 0x0001
  setup_sqpoll = IORING_SETUP_SQPOLL,                         // 0x0002
  setup_sq_aff = IORING_SETUP_SQ_AFF,                         // 0x0004
  setup_cqsize = IORING_SETUP_CQSIZE,                         // 0x0008
  setup_clamp = IORING_SETUP_CLAMP,                           // 0x0010
  setup_attach_wq = IORING_SETUP_ATTACH_WQ,                   // 0x0020
  setup_r_disabled = IORING_SETUP_R_DISABLED,                 // 0x0040
  setup_submit_all = IORING_SETUP_SUBMIT_ALL,                 // 0x0080
  setup_coop_taskrun = IORING_SETUP_COOP_TASKRUN,             // 0x0100
  setup_taskrun_flag = IORING_SETUP_TASKRUN_FLAG,             // 0x0200
  setup_sqe128 = IORING_SETUP_SQE128,                         // 0x0400
  setup_cqe32 = IORING_SETUP_CQE32,                           // 0x0800
  setup_single_issuer = IORING_SETUP_SINGLE_ISSUER,           // 0x1000
  setup_defer_taskrun = IORING_SETUP_DEFER_TASKRUN,           // 0x2000
  setup_no_mmap = IORING_SETUP_NO_MMAP,                       // 0x4000
  setup_registered_fd_only = IORING_SETUP_REGISTERED_FD_ONLY, // 0x8000
  IORING_SETUP_MUST_BE_UINT32 = 0x7FFF'FFFF
};

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

// `IORING_TIMEOUT_*` wrapper.
enum class iou_timeout_flags : uint32_t {
  rel = 0,                                          // 0x00
  abs = IORING_TIMEOUT_ABS,                         // 0x01
  update = IORING_TIMEOUT_UPDATE,                   // 0x02
  boot_time = IORING_TIMEOUT_BOOTTIME,              // 0x04
  real_time = IORING_TIMEOUT_REALTIME,              // 0x08
  link_timeout_update = IORING_LINK_TIMEOUT_UPDATE, // 0x10
  etime_success = IORING_TIMEOUT_ETIME_SUCCESS,     // 0x20
  multishot = IORING_TIMEOUT_MULTISHOT,             // 0x40
  clock_mask = IORING_TIMEOUT_CLOCK_MASK,           // 0x0c
  update_mask = IORING_TIMEOUT_UPDATE_MASK,         // 0x12
  IORING_TIMEOUT_MUST_BE_INT32 = 0x7FFF'FFFF
};

// `POLL*` wrapper.
enum class poll_flags : uint16_t {
  POLL_0 = 0,      // 0x000
  in = POLLIN,     // 0x001
  pri = POLLPRI,   // 0x002
  out = POLLOUT,   // 0x004
  err = POLLERR,   // 0x008
  hup = POLLHUP,   // 0x010
  nval = POLLNVAL, // 0x020
  POLL_MUST_BE_INT16 = 0x7FFF
};

// `SHUT_*` wrapper for `prep_shutdown`.
enum class shutdown_how : int {
  rd = SHUT_RD,     // 0
  wr = SHUT_WR,     // 1
  rdwr = SHUT_RDWR, // 2
  SHUT_MUST_BE_INT = 0x7FFF'FFFF
};

}}} // namespace corvid::proto::iouring

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::iou_setup_flags> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::proto::iouring::iou_setup_flags,
        "setup_registered_fd_only, setup_no_mmap, setup_defer_taskrun, "
        "setup_single_issuer, setup_cqe32, setup_sqe128, setup_taskrun_flag, "
        "setup_coop_taskrun, setup_submit_all, setup_r_disabled, "
        "setup_attach_wq, setup_clamp, setup_cqsize, setup_sq_aff, "
        "setup_sqpoll, setup_iopoll">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::iou_cqe_flags> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::proto::iouring::iou_cqe_flags,
        "notif, sock_nonempty, more, buffer">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::iou_sqe_flags> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::proto::iouring::iou_sqe_flags,
        "cqe_skip_success, buffer_select, async, io_hardlink, io_link, "
        "io_drain, fixed_file">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::iou_timeout_flags> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::proto::iouring::iou_timeout_flags,
        "multishot, etime_success, link_timeout_update, real_time, boot_time, "
        "update, abs">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::poll_flags> =
    corvid::enums::bitmask::make_bitmask_enum_spec<
        corvid::proto::iouring::poll_flags, "nval, hup, err, out, pri, in">();

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<
    corvid::proto::iouring::shutdown_how> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::proto::iouring::shutdown_how, "rd, wr, rdwr">();

namespace corvid { inline namespace proto { namespace iouring {

#pragma endregion
#pragma region iou_timespec

// Wrapper over `__kernel_timespec`, which can represent either an absolute or
// relative time depending on `TIMER_ABSTIME`.
//
// When using `CLOCK_REALTIME` as the kernel clock ID, the matching C++ clock
// is `std::chrono::system_clock` and the Unix epoch. For `CLOCK_MONOTONIC`,
// use `std::chrono::steady_clock` and the system boot time. For
// `CLOCK_BOOTTIME`, you will need to wrap `clock_gettime`.
//
// Warning: `iou_timespec` is passed by pointer and only read after submission,
// so you need to ensure that it remains valid until then. Ideally, it should
// live as long as the connection.
class iou_timespec {
public:
  using raw_timespec = __kernel_timespec;
  using duration_t = std::chrono::nanoseconds;

  // Default; invalid.
  constexpr iou_timespec() noexcept : ts_{.tv_sec = 0, .tv_nsec = 0} {}

  // Conversion from raw.
  constexpr explicit iou_timespec(const raw_timespec& ts) noexcept : ts_(ts) {}

  // Copy.
  constexpr iou_timespec(const iou_timespec& other) noexcept = default;

  // Assign.
  constexpr iou_timespec& operator=(
      const iou_timespec& other) noexcept = default;

  // Construct from duration.
  template<typename Rep, typename Period>
  constexpr explicit iou_timespec(
      std::chrono::duration<Rep, Period> d) noexcept
      : iou_timespec(from_duration(d)) {}

  // Construct from time point.
  template<typename Clock, typename Duration>
  constexpr explicit iou_timespec(
      std::chrono::time_point<Clock, Duration> tp) noexcept
      : iou_timespec(from_time_point(tp)) {}

  // As duration.
  template<typename Duration = std::chrono::nanoseconds>
  [[nodiscard]] constexpr Duration as_duration() const noexcept {
    return to_duration<Duration>(ts_);
  }

  // As time point.
  template<typename Clock = std::chrono::steady_clock,
      typename Duration = std::chrono::nanoseconds>
  [[nodiscard]] constexpr std::chrono::time_point<Clock, Duration>
  as_time_point() const noexcept {
    return to_time_point<Clock, Duration>(ts_);
  }

  // Raw value.
  [[nodiscard]] auto* pointer(this auto& self) noexcept { return &self.ts_; }
  [[nodiscard]] decltype(auto) value(this auto& self) noexcept {
    return self.ts_;
  }

  // Conditional passthrough.
  static raw_timespec* as_pointer(iou_timespec* ts) noexcept {
    raw_timespec* ptr{};
    if (ts) ptr = ts->pointer();
    return ptr;
  }

  static const raw_timespec* as_pointer(const iou_timespec* ts) noexcept {
    const raw_timespec* ptr{};
    if (ts) ptr = ts->pointer();
    return ptr;
  }

  // Static raw from duration.
  template<typename Rep, typename Period>
  static constexpr raw_timespec
  from_duration(std::chrono::duration<Rep, Period> d) noexcept {
    long long s{};
    long long ns{};
    if (d >= std::chrono::duration<Rep, Period>::zero()) {
      s = static_cast<long long>(
          std::chrono::duration_cast<std::chrono::seconds>(d).count());
      ns = static_cast<long long>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(
              d - std::chrono::seconds(s))
              .count());
    }
    return {.tv_sec = s, .tv_nsec = ns};
  }

  // Static raw from time point.
  template<typename Clock, typename Duration>
  static constexpr raw_timespec
  from_time_point(std::chrono::time_point<Clock, Duration> tp) noexcept {
    return from_duration(tp.time_since_epoch());
  }

  // Static raw to duration.
  template<typename Duration = std::chrono::nanoseconds>
  static constexpr Duration to_duration(const raw_timespec& ts) noexcept {
    auto total_ns =
        std::chrono::seconds{ts.tv_sec} + std::chrono::nanoseconds{ts.tv_nsec};
    return std::chrono::duration_cast<Duration>(total_ns);
  }

  // Static raw to time point.
  template<typename Clock, typename Duration>
  static constexpr std::chrono::time_point<Clock, Duration>
  to_time_point(const raw_timespec& ts) noexcept {
    return std::chrono::time_point<Clock, Duration>(to_duration<Duration>(ts));
  }

private:
  raw_timespec ts_;
};

#pragma endregion
#pragma region iou_itimerspec

// Wrapper over `__kernel_itimerspec`, which consists of two
// `__kernel_timespec`s, which represent how often the timer should repeat
// after the first expiration, and when the first expiration should occur.
class iou_itimerspec {
public:
  using raw_timespec = iou_timespec::raw_timespec;
  using raw_itimerspec = __kernel_itimerspec;

  // Default.
  constexpr iou_itimerspec() noexcept
      : ts_{.it_interval = {.tv_sec = 0, .tv_nsec = 0},
            .it_value = {.tv_sec = 0, .tv_nsec = 0}} {}

  // Copy.
  constexpr iou_itimerspec(const iou_itimerspec& other) noexcept = default;

  // Assign.
  constexpr iou_itimerspec& operator=(
      const iou_itimerspec& other) noexcept = default;

  // Construct from interval and value.
  constexpr iou_itimerspec(const iou_timespec& interval,
      const iou_timespec& value) noexcept
      : ts_{.it_interval = interval.value(), .it_value = value.value()} {}

  // Access underlying.
  [[nodiscard]] auto* pointer(this auto& self) noexcept { return &self.ts_; }
  [[nodiscard]] decltype(auto) value(this auto& self) noexcept {
    return self.ts_;
  }

  // Access interval and value.
  [[nodiscard]] auto& it_interval(this const auto& self) noexcept {
    return self.ts_.it_interval;
  }
  [[nodiscard]] auto& it_value(this const auto& self) noexcept {
    return self.ts_.it_value;
  }

private:
  raw_itimerspec ts_;
};

#pragma endregion
#pragma region iou_res

// Wrapper for `int` results from `io_uring` operations, where `res` >= 0 is
// a byte count (or 0 for ops with no data) and `res` < 0 is a negated
// `errno` value.
class iou_res {
public:
  explicit iou_res(int res = 0) : res_(res) {}

  [[nodiscard]] operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  [[nodiscard]] bool ok(int r = 0) const noexcept { return res_ >= r; }
  [[nodiscard]] int value() const noexcept { return res_; }
  [[nodiscard]] errno_code err() const noexcept { return errno_code{-res_}; }
  [[nodiscard]] size_t bytes() const noexcept {
    return static_cast<size_t>(res_);
  }

  // True if the result is a "soft" error that can be retried.
  [[nodiscard]] bool is_soft_error() const {
    return err() == EC::time || err() == EC::intr || err() == EC::again ||
           err() == EC::nomem || err() == EC::restart;
  }

  void throw_if_error(const std::string& context, int r = 0) const {
    if (ok(r)) return;
    throw std::system_error(*err(), std::system_category(),
        "io_uring error in " + context);
  }

private:
  int res_;
};
#pragma endregion
#pragma region iou_sqe

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

  bool
  prep_poll_oneshot(int fd, poll_flags poll_mask = poll_flags::in) noexcept {
    io_uring_prep_poll_add(sqe_, fd, *poll_mask);
    return true;
  }

  bool
  prep_poll_multishot(int fd, poll_flags poll_mask = poll_flags::in) noexcept {
    io_uring_prep_poll_multishot(sqe_, fd, *poll_mask);
    return true;
  }

  bool prep_recv(int fd, span_t span, msg_flags flags = {}) noexcept {
    io_uring_prep_recv(sqe_, fd, span.data(), span.size(), *flags);
    return true;
  }

  bool prep_send(int fd, const_span_t span, msg_flags flags = {}) noexcept {
    io_uring_prep_send(sqe_, fd, span.data(), span.size(), *flags);
    return true;
  }

  // Read into a pre-registered fixed buffer. `buf_index` is the slot index
  // from `io_uring_register_buffers`. For sockets, `file_offset` must be 0 or
  // -1.
  bool prep_read_fixed(int fd, span_t span, size_t buf_index,
      uint64_t file_offset = -1) noexcept {
    io_uring_prep_read_fixed(sqe_, fd, span.data(),
        static_cast<unsigned>(span.size()), static_cast<off_t>(file_offset),
        static_cast<int>(buf_index));
    return true;
  }

  // Write from a pre-registered fixed buffer. `buf_index` is the slot index
  // from `io_uring_register_buffers`. For sockets, `file_offset` must be 0 or
  // -1.
  bool prep_write_fixed(int fd, const_span_t span, size_t buf_index,
      uint64_t file_offset = -1) noexcept {
    io_uring_prep_write_fixed(sqe_, fd, span.data(),
        static_cast<unsigned>(span.size()), static_cast<off_t>(file_offset),
        static_cast<int>(buf_index));
    return true;
  }

  bool prep_accept(int fd,
      socket_type flags = socket_type::nonblock_cloexec) noexcept {
    io_uring_prep_accept(sqe_, fd, nullptr, nullptr, *flags);
    return true;
  }

  bool prep_accept_multishot(int fd,
      socket_type flags = socket_type::nonblock_cloexec) noexcept {
    io_uring_prep_multishot_accept(sqe_, fd, nullptr, nullptr, *flags);
    return true;
  }

  bool prep_connect(int fd, const net_endpoint* endpoint) noexcept {
    auto [addr, addrlen] = endpoint->as_sockaddr();
    io_uring_prep_connect(sqe_, fd, addr, addrlen);
    return true;
  }

  bool prep_recvmsg(int fd, msghdr* msg, msg_flags flags = {}) noexcept {
    io_uring_prep_recvmsg(sqe_, fd, msg, *flags);
    return true;
  }

  bool prep_sendmsg(int fd, const msghdr* msg, msg_flags flags = {}) noexcept {
    io_uring_prep_sendmsg(sqe_, fd, msg, *flags);
    return true;
  }

  // Close `fd` asynchronously. Caller must have released ownership of `fd`
  // (e.g., via `os_file::release`) before submitting, so the kernel is the
  // sole closer.
  bool prep_close(int fd) noexcept {
    io_uring_prep_close(sqe_, fd);
    return true;
  }

  // Shutdown `fd` asynchronously. Caller must have released ownership of `fd`
  // (e.g., via `os_file::release`) before submitting, so the kernel is the
  // sole closer. `how` is the same as for `shutdown(2)`.
  bool prep_shutdown(int fd, shutdown_how how) noexcept {
    io_uring_prep_shutdown(sqe_, fd, *how);
    return true;
  }

  // Cancel all pending operations targeting `fd`. // TODO: Flags?!
  bool prep_cancel_fd(int fd, unsigned flags = 0) noexcept {
    io_uring_prep_cancel_fd(sqe_, fd, flags);
    return true;
  }

  // Cancel all pending operations with `user_data` matching the given value.
  bool prep_cancel_user_data(uint64_t user_data, int flags = 0) noexcept {
    io_uring_prep_cancel64(sqe_, user_data, flags);
    return true;
  }

  // Standalone timeout: fires with `-ETIME` after `duration` elapses, or
  // with `-ECANCELED` when canceled by a linked predecessor completing first.
  // In single-shot mode, `cqe_count` is the number of CQEs that must be
  // completed before the timeout fires, where 0 means that CQEs are not
  // relevant and only the `timespec` matters.
  //
  // The `flags` parameter specifies the timeout behavior, including
  // `io_timeout_flags::multishot` for repeating timeouts. For these multi-shot
  // timeouts, the `cqe_count` instead specifies how many times the timeout
  // fires before expiring, with 0 meaning infinite.
  bool prep_timeout(iou_timespec* duration,
      iou_timeout_flags flags = iou_timeout_flags::rel,
      size_t cqe_count = 0) noexcept {
    io_uring_prep_timeout(sqe_, iou_timespec::as_pointer(duration), cqe_count,
        *flags);
    return true;
  }

  // Linked timeout: must be the second SQE in a linked pair (first SQE must
  // have `IOSQE_IO_LINK` set). Cancels the preceding op if `duration`
  // expires before it completes. For multi-shot ops, does not work the way you
  // might hope.
  bool prep_link_timeout(iou_timespec* duration,
      iou_timeout_flags flags = iou_timeout_flags::rel) noexcept {
    io_uring_prep_link_timeout(sqe_, iou_timespec::as_pointer(duration),
        *flags);
    return true;
  }

  // TODO: Consider combining duration, flags, and count into a single struct
  // and stick it in an object pool. Perhaps instead of an object pool,
  // something like interning, as there are a finite number of configurations
  // possible.

  // Remove an existing timeout. Like regular cancel, but timeout-specific.
  bool prep_timeout_remove(uint64_t user_data) noexcept {
    io_uring_prep_timeout_remove(sqe_, user_data, 0);
    return true;
  }

  // Update an existing timeout.
  bool prep_timeout_update(iou_timespec* duration, uint64_t user_data,
      iou_timeout_flags flags = {}) noexcept {
    io_uring_prep_timeout_update(sqe_, iou_timespec::as_pointer(duration),
        user_data, *flags);
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

#pragma endregion
#pragma region iou_cqe

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

#pragma endregion
#pragma region iou_ring

// Wrapper over `io_uring`. Non-movable and non-copyable, and has ownership.
class iou_ring {
public:
  using ptr_t = io_uring*;
  static constexpr size_t max_size =
      0x7FFF'FFFFU; // Max ring size, per kernel docs.

  // Construct and initialize an io_uring with the given `ring_size` and
  // `flags`.
  explicit iou_ring(size_t ring_size = 256, iou_setup_flags flags = {}) {
    iou_res res{io_uring_queue_init(ring_size, &ring_, *flags)};
    if (res) return;
    throw std::system_error(*res.err(), std::system_category(),
        "io_uring_queue_init");
  }

  iou_ring(const iou_ring&) = delete;
  iou_ring(iou_ring&&) = delete;
  iou_ring& operator=(const iou_ring&) = delete;
  iou_ring& operator=(iou_ring&&) = delete;

  ~iou_ring() {
    io_uring_unregister_buffers(&ring_);
    io_uring_queue_exit(&ring_);
  }

  ptr_t get_ptr() noexcept { return &ring_; }
  operator ptr_t() noexcept { return &ring_; }

  // Wait for a CQE to become available, with an optional timeout.
  //
  // Note that the converted `iou_timespec` is used immediately, so it's safe
  // to pass by value, and even convert JIT from `std::chrono::duration`.
  [[nodiscard]] iou_res wait_cqe_timeout(iou_timespec ts = {}) {
    iou_cqe cqe; // Peeked but not returned.
    return iou_res{
        io_uring_wait_cqe_timeout(&ring_, cqe.pointer(), ts.pointer())};
  }

  // Loop over available CQEs, calling `fn` on each, up to `limit` CQEs .
  // Advances the CQ head by the number of CQEs processed. Must be called on
  // the loop thread after a call to `wait_cqe_timeout()` that returned at
  // least one CQE. Returns the number of CQEs processed.
  [[nodiscard]] size_t for_each_cqe(auto&& fn, size_t limit = max_size) {
    size_t count{};
    iou_cqe::ptr_t cqe;
    unsigned head;
    io_uring_for_each_cqe(&ring_, head, cqe) {
      if (limit-- == 0) break;
      (void)fn(iou_cqe{cqe});
      ++count;
    }
    io_uring_cq_advance(&ring_, count);
    return count;
  }

  // Loop over snapshotted CQEs, calling `fn` on each, up to `limit` CQEs. It
  // will only call CQEs that were pending at the start of this call, so it
  // cannot run indefinitely.
  [[nodiscard]] size_t
  for_each_snapshotted_cqe(auto&& fn, size_t limit = max_size) {
    size_t pending = io_uring_cq_ready(&ring_);
    return for_each_cqe(std::forward<decltype(fn)>(fn),
        std::min(pending, limit));
  }

  // Get the number of free SQE slots.
  [[nodiscard]] size_t sqe_available() const noexcept {
    return io_uring_sq_space_left(&ring_);
  }

  // Check whether we have enough SQE slots available..
  [[nodiscard]] bool enough_sqe_available(size_t s = 1U) const noexcept {
    return sqe_available() >= s;
  }

  // Get the next SQE to fill, or null if the SQ is full. The caller must
  // later call `submit()`.
  iou_sqe next_sqe() noexcept { return iou_sqe{io_uring_get_sqe(&ring_)}; }

  // Submit filled SQEs to the kernel. Returns an `iou_res` with the number
  // of SQEs submitted, or an error. An `ok(n)` on the result is a good idea.
  iou_res submit() noexcept { return iou_res{io_uring_submit(&ring_)}; }

  // Submit filled SQEs and wait for at least one CQE, with an optional
  // timeout. Combines `submit()` and `wait_cqe_timeout()` into one syscall,
  // which also flushes deferred task work (required by `setup_defer_taskrun`).
  [[nodiscard]] iou_res submit_and_wait_timeout(
      iou_timespec ts = {}) noexcept {
    iou_cqe cqe; // Peeked but not returned.
    return iou_res{io_uring_submit_and_wait_timeout(&ring_, cqe.pointer(), 1,
        ts.pointer(), nullptr)};
  }

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

#pragma endregion

}}} // namespace corvid::proto::iouring
