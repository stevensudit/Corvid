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
#include <cstddef>
#include <liburing.h>

#include <chrono>
#include <system_error>

#include "../../filesys/event_fd.h"

namespace corvid { inline namespace proto { inline namespace iouring {

// Wrapper around io_uring's C API, with the primary goal of adding C++
// conveniences.

// Wrapper for `int` results from io_uring operations, where `res` >= 0 is a
// byte count (or 0 for ops with no data) and `res` < 0 is a negated errno
// value.
class iou_res {
public:
  explicit iou_res(int res) : res_(res) {}

  [[nodiscard]] operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  [[nodiscard]] bool ok() const noexcept { return res_ >= 0; }
  [[nodiscard]] int32_t value() const noexcept { return res_; }
  [[nodiscard]] int err() const noexcept { return -res_; }

  // True if the result is a "soft" error that may be expected to succeed on
  // retry.
  [[nodiscard]] bool is_soft_error() const {
    return res_ == -ETIME || res_ == -EINTR;
  }

  void throw_if_error(const std::string& context) const {
    if (ok()) return;
    throw std::system_error(err(), std::system_category(),
        "io_uring error in " + context);
  }

private:
  int32_t res_;
};

// Wrapper over `io_uring`.
class iou_ring {
public:
  using duration_t = std::chrono::milliseconds;
  using cqe_ptr = io_uring_cqe*;

  // Construct and initialize an io_uring with the given `ring_size` and
  // `flags`.
  explicit iou_ring(size_t ring_size = 256, int flags = 0) {
    iou_res res{io_uring_queue_init(ring_size, &ring_, flags)};
    if (!res)
      throw std::system_error(res.err(), std::system_category(),
          "io_uring_queue_init");
  }

  iou_ring(const iou_ring&) = delete;
  iou_ring(iou_ring&&) = delete;
  iou_ring& operator=(const iou_ring&) = delete;
  iou_ring& operator=(iou_ring&&) = delete;

  ~iou_ring() { io_uring_queue_exit(&ring_); }

  io_uring* get_ptr() noexcept { return &ring_; }
  operator io_uring*() noexcept { return &ring_; }

  // Timespec from `std::chrono` duration.
  static constexpr __kernel_timespec to_timespec(duration_t timeout) {
    return __kernel_timespec{
        .tv_sec = timeout.count() / 1000,
        .tv_nsec = (timeout.count() % 1000) * 1'000'000LL,
    };
  }

  // Wait for a CQE to become available, with an optional timeout.
  [[nodiscard]] iou_res
  wait_cqe_timeout(cqe_ptr& cqe, duration_t timeout = {}) {
    auto ts = to_timespec(timeout);
    return iou_res(io_uring_wait_cqe_timeout(&ring_, &cqe, &ts));
  }

  // Loop over available CQEs, calling `fn` on each. Advances the CQ head by
  // the number of CQEs processed. Must be called on the loop thread after a
  // call to `wait_cqe_timeout()` that returned at least one CQE. Returns the
  // number of CQEs processed.
  [[nodiscard]] size_t for_each_cqe(auto&& fn) {
    size_t count{};
    cqe_ptr cqe;
    unsigned head;
    io_uring_for_each_cqe(&ring_, head, cqe) {
      (void)fn(cqe);
      ++count;
    }
    io_uring_cq_advance(&ring_, head);
    return count;
  }

private:
  io_uring ring_{};
};

}}} // namespace corvid::proto::iouring
