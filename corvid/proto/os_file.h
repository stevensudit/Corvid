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
#include <algorithm>
#include <cerrno>
#include <utility>
#include <csignal>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

#include "../strings/no_zero.h"

namespace corvid { inline namespace proto {

using namespace corvid::strings::no_zero_funcs;

namespace details {
// Platform file handle type and invalid-handle sentinel.
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
using file_handle_t = int;
constexpr file_handle_t invalid_file_handle = -1;
#else
// Placeholder for non-POSIX platforms (e.g., Windows `HANDLE`).
using file_handle_t = int;
constexpr file_handle_t invalid_file_handle = -1;
#endif
} // namespace details

// RAII wrapper around an OS file descriptor or handle.
//
// `os_file` owns a single file and closes it on destruction.
// It is movable and non-copyable. `control()` wraps `fcntl`; `get_flags()`,
// `set_flags()`, and `set_nonblocking()` are named helpers for common
// fd-level operations.
//
// Platform-specific code is isolated in a guarded section.
class os_file {
public:
  using file_handle_t = details::file_handle_t;
  static constexpr file_handle_t invalid_file_handle =
      details::invalid_file_handle;

  // Adopt an existing handle. Defaults to an invalid (closed) file.
  explicit os_file(file_handle_t h = invalid_file_handle) noexcept
      : handle_{h} {}

  os_file(const os_file&) = delete;
  os_file& operator=(const os_file&) = delete;

  os_file(os_file&& other) noexcept : handle_{other.release()} {}

  os_file& operator=(os_file&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.release();
    }
    return *this;
  }

  ~os_file() { close(); }

  // True if the handle is valid (i.e., the file is open).
  [[nodiscard]] bool is_open() const noexcept {
    return handle_ != invalid_file_handle;
  }

  explicit operator bool() const noexcept { return is_open(); }

  // Return the raw platform handle.
  [[nodiscard]] file_handle_t handle() const noexcept { return handle_; }

  // Close the file. Idempotent. Returns true when the file was open and is
  // now closed, false if it failed to be closed (likely because it already
  // was).
  bool close() noexcept {
    if (handle_ == invalid_file_handle) return false;
    const auto old_handle = handle_;
    handle_ = invalid_file_handle;
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    if (::close(old_handle) != 0) return false;
#endif
    return true;
  }

  // Release ownership and return the handle without closing it.
  [[nodiscard]] file_handle_t release() noexcept {
    const auto h = handle_;
    handle_ = invalid_file_handle;
    return h;
  }

  // Write as much of `data` as possible to the file. On success, removes the
  // written prefix from `data` and returns true. On failure, leaves `data`
  // unchanged and returns false. A "soft" failure (e.g., EAGAIN) is treated
  // as success with no progress. Note that this call can invoke a SIGPIPE on a
  // socket, so use `ip_sock::send` instead.
  [[nodiscard]] bool write(std::string_view& data) const {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    if (data.empty()) return true;

    const ssize_t n = ::write(handle_, data.data(), data.size());
    if (n <= 0) return !is_hard_error();

    data.remove_prefix(static_cast<size_t>(n));
#else
    (void)data;
#endif
    return true;
  }

  // Read up to `data.size()` bytes from the file into `data`. Use
  // `no_zero::resize_to_cap` or `no_zero::enlarge_to` to get the desired size.
  //
  // On success, resizes `data` to the number of bytes read and returns true. A
  // "soft" failure (e.g., EAGAIN) is treated as success with zero bytes read.
  // On EOF/disconnect, leaves `data` unchanged and returns false. On hard
  // failure, clears `data` and returns false.
  [[nodiscard]] bool read(std::string& data) const {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    if (data.empty()) return true;

    // Read up to the current size.
    const ssize_t n = ::read(handle_, data.data(), data.size());

    // EOF/disconnect. Return false without clearing `data`.
    if (n == 0) return false;

    // Update `data` to the size actually read.
    no_zero::resize_to(data, static_cast<size_t>(std::max(n, ssize_t{0})));

    // If retriable, treat as a success with nothing read, while a hard error
    // is a failure with `data` cleared.
    if (n < 0) return !is_hard_error();
#else
    (void)data;
#endif
    return true;
  }

  // Platform-specific fd control and named helpers.
  // Isolated here so that porting to a new OS requires changes only in this
  // guarded section and the platform header includes above.

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
  // Invoke `fcntl(cmd, args...)` on the handle. Returns -1 on failure.
  template<typename... Args>
  [[nodiscard]] int control(int cmd, Args&&... args) const noexcept {
    return ::fcntl(handle_, cmd, std::forward<Args>(args)...);
  }

  // Return the fd status flags via `fcntl(F_GETFL)`.
  [[nodiscard]] std::optional<int> get_flags() const noexcept {
    const auto res = control(F_GETFL);
    return res == -1 ? std::optional<int>{} : std::optional<int>{res};
  }

  // Set the fd status flags via `fcntl(F_SETFL)`. Returns false on failure.
  [[nodiscard]] bool set_flags(int flags) const noexcept {
    return control(F_SETFL, flags) == 0;
  }

  // Enable or disable non-blocking I/O via `fcntl(F_SETFL, O_NONBLOCK)`.
  [[nodiscard]] bool set_nonblocking(bool on = true) const noexcept {
    const auto flags = get_flags();
    if (!flags) return false;
    const int new_flags = on ? (*flags | O_NONBLOCK) : (*flags & ~O_NONBLOCK);
    return set_flags(new_flags);
  }
#endif

  // Checks whether the last error was a hard error (true) or a soft error
  // (false).
  static bool is_hard_error(int err = errno) noexcept {
    return (err != EAGAIN && err != EWOULDBLOCK && err != EINTR);
  }

private:
  file_handle_t handle_{invalid_file_handle};
};
}} // namespace corvid::proto
