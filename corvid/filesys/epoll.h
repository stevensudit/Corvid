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
#include <sys/epoll.h>

#include "os_file.h"

namespace corvid { inline namespace filesys {

// RAII wrapper around Linux `epoll`.
//
// `epoll` owns a single epoll instance, inherits the general fd helpers from
// `os_file`, and adds named helpers for `epoll_ctl` and `epoll_wait`.
class [[nodiscard]] epoll: public os_file {
public:
  using handle_t = os_file::file_handle_t;
  static constexpr handle_t invalid_handle = os_file::invalid_file_handle;
  static constexpr int default_flags = EPOLL_CLOEXEC;

  epoll() noexcept = default;
  explicit epoll(int flags) noexcept : os_file(::epoll_create1(flags)) {}
  explicit epoll(os_file&& file) noexcept : os_file(std::move(file)) {}

  epoll(epoll&&) noexcept = default;
  epoll(const epoll&) = delete;

  epoll& operator=(epoll&&) noexcept = default;
  epoll& operator=(const epoll&) = delete;

  ~epoll() = default;

  // Create an `epoll` instance with `flags` (default: `EPOLL_CLOEXEC`).
  [[nodiscard]] static epoll create(int flags = default_flags) noexcept {
    return epoll{flags};
  }

  // Invoke `epoll_ctl` on this epoll instance.
  [[nodiscard]] bool
  control(int op, int fd, epoll_event* ev = nullptr) const noexcept {
    return ::epoll_ctl(handle(), op, fd, ev) == 0;
  }

  // Invoke `epoll_ctl` on this epoll instance.
  [[nodiscard]] bool control(int op, int fd, epoll_event& ev) const noexcept {
    return control(op, fd, &ev);
  }

  // Register `fd` with interest described by `ev`.
  [[nodiscard]] bool add(int fd, epoll_event& ev) const noexcept {
    return control(EPOLL_CTL_ADD, fd, ev);
  }

  // Replace the current interest mask for `fd` with `ev`.
  [[nodiscard]] bool modify(int fd, epoll_event& ev) const noexcept {
    return control(EPOLL_CTL_MOD, fd, ev);
  }

  // Unregister `fd` from this epoll instance.
  [[nodiscard]] bool remove(int fd) const noexcept {
    return control(EPOLL_CTL_DEL, fd);
  }

  // Wait for up to `maxevents` ready entries, optionally timing out.
  // A `timeout_ms` of -1 means to wait indefinitely (which is probably
  // unwise), while 0 means to return immediately. Returns the number of ready
  // entries on success, or `nullopt` on error. Note that `errno == EINTR` is
  // not a failure but a signal interruption; callers should retry in this
  // case.
  [[nodiscard]] std::optional<int>
  wait(epoll_event* events, int maxevents, int timeout_ms) const noexcept {
    const int n = ::epoll_wait(handle(), events, maxevents, timeout_ms);
    return n == -1 ? std::optional<int>{} : n;
  }

  // Wait overload for a fixed-size array: `maxevents` is deduced from `N`.
  //
  // See other `wait()` overload for more.
  template<std::size_t N>
  [[nodiscard]] std::optional<int>
  wait(epoll_event (&events)[N], int timeout_ms) const noexcept {
    return wait(events, static_cast<int>(N), timeout_ms);
  }
};

}} // namespace corvid::filesys
