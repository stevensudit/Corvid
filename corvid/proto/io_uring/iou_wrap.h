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

#include "../../filesys/event_fd.h"

// Wrapper around `io_uring`'s C API, with the primary goal of adding C++
// conveniences.

namespace corvid { inline namespace proto { inline namespace iouring {

// Wrapper for `int` results from `io_uring` operations, where `res` >= 0 is a
// byte count (or 0 for ops with no data) and `res` < 0 is a negated `errno`
// value.
class iou_res {
public:
  explicit iou_res(int res) : res_(res) {}

  [[nodiscard]] operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  [[nodiscard]] bool ok(int r = 0) const noexcept { return res_ >= r; }
  [[nodiscard]] int value() const noexcept { return res_; }
  [[nodiscard]] int err() const noexcept { return -res_; }

  // True if the result is a "soft" error that can be retried.
  [[nodiscard]] bool is_soft_error() const {
    return res_ == -ETIME || res_ == -EINTR;
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
  using ptr_t = io_uring_sqe*;

  iou_sqe() = default;
  explicit iou_sqe(ptr_t sqe) : sqe_(sqe) {}

  [[nodiscard]] operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  [[nodiscard]] ptr_t value() const noexcept { return sqe_; }
  [[nodiscard]] ptr_t* pointer() noexcept { return &sqe_; }

  [[nodiscard]] bool ok() const noexcept { return sqe_; }

  void prep_nop() noexcept { io_uring_prep_nop(sqe_); }

  void prep_poll_oneshot(int fd, short poll_mask = POLLIN) noexcept {
    io_uring_prep_poll_add(sqe_, fd, poll_mask);
  }

  void prep_poll_multishot(int fd, short poll_mask = POLLIN) noexcept {
    io_uring_prep_poll_multishot(sqe_, fd, poll_mask);
  }

  void set_data_pointer(void* data) noexcept {
    io_uring_sqe_set_data(sqe_, data);
  }

  void set_data_int(uint64_t data) noexcept {
    io_uring_sqe_set_data64(sqe_, data);
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
  [[nodiscard]] uint32_t flags() const noexcept { return cqe_->flags; }

  template<typename T = void>
  [[nodiscard]] T* get_data_ptr() const noexcept {
    return static_cast<T*>(io_uring_cqe_get_data(cqe_));
  }

  [[nodiscard]] uint64_t get_data_int() const noexcept {
    return io_uring_cqe_get_data64(cqe_);
  }

  // TODO: This goes away.
  template<typename T = std::function<bool(iou_res)>>
  [[nodiscard]] bool dispatch() const noexcept {
    bool ok{};
    auto* cb = get_data_ptr<T>();
    if (cb) {
      ok = (*cb)(res());
      delete cb;
    }
    return ok;
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

private:
  io_uring ring_{};
};

}}} // namespace corvid::proto::iouring
