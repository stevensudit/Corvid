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
#include <utility>

#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace corvid { inline namespace proto {

namespace details {
// Platform file handle type and invalid-handle sentinel.
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
using file_handle_t = int;
inline constexpr file_handle_t invalid_file_handle = -1;
#else
// Placeholder for non-POSIX platforms (e.g., Windows `HANDLE`).
using file_handle_t = int;
inline constexpr file_handle_t invalid_file_handle = -1;
#endif
} // namespace details

// RAII wrapper around an OS file descriptor or handle.
//
// `os_file` owns a single file and closes it on destruction.
// It is movable and non-copyable. `control()` wraps `fcntl`; `get_flags()`
// and `set_nonblocking()` are named helpers for common fd-level operations.
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
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    if (::close(handle_) != 0) return false;
#endif
    handle_ = invalid_file_handle;
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
  // as success with no progress.
  [[nodiscard]] bool write(std::string_view& data) const {
#if defined(__unix__) || defined(__linux__) || defined(__APPLE__)
    if (data.empty()) return true;

    auto res = ::write(handle_, data.data(), data.size());

    // If we wrote something, remove it from the front of `data` and return
    // success.
    if (res > 0) {
      data.remove_prefix(static_cast<size_t>(res));
      return true;
    }

    // If we failed due to a soft error, treat it as a success with no
    // progress.
    const auto err = errno;
    if (err == EAGAIN || err == EWOULDBLOCK) return true;

    // Otherwise, it's a hard error. Return failure with `data` unchanged.
    return false;
#endif
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

  // Return the fd status flags via `fcntl(F_GETFL)`. Returns -1 on failure.
  [[nodiscard]] int get_flags() const noexcept { return control(F_GETFL); }

  // Enable or disable non-blocking I/O via `fcntl(F_SETFL, O_NONBLOCK)`.
  [[nodiscard]] bool set_nonblocking(bool on = true) const noexcept {
    const int flags = get_flags();
    if (flags < 0) return false;
    const int new_flags = on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    return control(F_SETFL, new_flags) == 0;
  }
#endif

private:
  file_handle_t handle_{invalid_file_handle};
};

}} // namespace corvid::proto
