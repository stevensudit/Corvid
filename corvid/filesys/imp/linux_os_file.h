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
#ifndef CORVID_IMP_LINUX_OS_FILE_H
#ifndef CORVID_OS_FILE_ENTRY
#if defined(CORVID_CLANGD) || !defined(NDEBUG)
// Reroute a standalone dev-time inclusion through the entry point.
#include "../os_file.h"
#else
#error "Include \"os_file.h\" instead of this implementation header."
#endif
#else
#define CORVID_IMP_LINUX_OS_FILE_H

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "../../meta/formatting.h"
#include "../../strings/no_zero.h"
#include "../os_error.h"

// Linux implementation of "os_file.h", wrapping a file descriptor.

namespace corvid { inline namespace filesys {

using corvid::strings::no_zero;

#pragma region details

namespace details {
// Platform file handle type and invalid-handle sentinel.
using file_handle_t = int;
constexpr file_handle_t invalid_file_handle = -1;
} // namespace details

#pragma endregion
#pragma region os_file

// RAII wrapper around a file descriptor. See "os_file.h" for the contract.
//
// Beyond the portable surface, `control` wraps `fcntl`, and `get_flags`,
// `set_flags`, and `set_nonblocking` are named helpers for common fd-level
// operations.
class [[nodiscard]] os_file {
public:
#pragma region Types

  using file_handle_t = details::file_handle_t;
  static constexpr file_handle_t invalid_file_handle =
      details::invalid_file_handle;

#pragma endregion
#pragma region Construction

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

#pragma endregion
#pragma region Accessors

  // True if the handle is valid (i.e., the file is open).
  [[nodiscard]] bool is_open() const noexcept {
    return handle_ != invalid_file_handle;
  }

  [[nodiscard]] explicit operator bool() const noexcept { return is_open(); }
  [[nodiscard]] bool operator!() const noexcept { return !is_open(); }

  // Return the raw platform handle.
  [[nodiscard]] file_handle_t handle() const noexcept { return handle_; }
  [[nodiscard]] file_handle_t operator*() const noexcept { return handle_; }

#pragma endregion
#pragma region Close and release

  // Close the file. Idempotent. Returns true when the file was open and is
  // now closed, false if it could not be closed (likely because it already
  // was). Note that, on failure, the file is left in a closed state to avoid
  // potential reuse of a stale handle.
  bool close() noexcept {
    if (!is_open()) return false;
    const auto old_handle = handle_;
    handle_ = invalid_file_handle;
    return (::close(old_handle) == 0);
  }

  // Release ownership and return the handle without closing it.
  [[nodiscard]] file_handle_t release() noexcept {
    const auto h = handle_;
    handle_ = invalid_file_handle;
    return h;
  }

#pragma endregion
#pragma region I/O

  // Write as much of `data` as possible to the file. On success, removes the
  // written prefix from `data` and returns true. On failure, leaves `data`
  // unchanged and returns false. A "soft" failure (e.g., EAGAIN) is treated
  // as success with no progress. Note that this call can invoke a SIGPIPE on
  // broken pipes/sockets, so use `net_socket::send` with MSG_NOSIGNAL instead.
  [[nodiscard]] bool write(std::string_view& data) const {
    if (data.empty()) return true;

    const auto n = ::write(handle_, data.data(), data.size());
    if (n <= 0) return !is_hard_error();

    data.remove_prefix(static_cast<size_t>(n));
    return true;
  }

  // Read up to `data.size()` bytes from the file into `data`. Use
  // `no_zero{data}.enlarge_to_cap()` or `no_zero{data}.enlarge_to(n)` to get
  // the desired size.
  //
  // On success, resizes `data` to the number of bytes read and returns true. A
  // "soft" failure (e.g., EAGAIN) is treated as success with zero bytes read.
  // On EOF/disconnect, leaves `data` unchanged and returns false. On hard
  // failure, clears `data` and returns false.
  [[nodiscard]] bool read(std::string& data) const {
    if (data.empty()) return true;

    // Read up to the current size.
    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    const auto n = ::read(handle_, data.data(), data.size());

    // EOF/disconnect. Return false without clearing `data`.
    if (n == 0) return false;

    // Update `data` to the size actually read.
    no_zero{data}.trim_to(n);

    // If retriable, treat as a success with nothing read, while a hard error
    // is a failure with `data` cleared.
    if (n < 0) return !is_hard_error();
    return true;
  }

  // Write all of `data` to the file, retrying after partial writes and soft
  // errors (e.g., `EINTR`). Returns true only when all bytes have been
  // written. On hard failure, returns false with an indeterminate prefix of
  // `data` already sent. Intended for blocking I/O; on non-blocking fds, a
  // full kernel buffer causes a busy-loop.
  [[nodiscard]] bool write_all(std::string_view data) const {
    while (!data.empty())
      if (!write(data)) return false;
    return true;
  }

  // Read exactly `data.size()` bytes into `data`, retrying after partial
  // reads and soft errors (e.g., `EINTR`). Size `data` with `data.resize(n)`
  // or `no_zero{data}.enlarge_to(n)` before calling.
  //
  // Returns true only when all bytes have been read. On EOF before
  // completion, trims `data` to the bytes received and returns false. On hard
  // failure, clears `data` and returns false. Intended for blocking I/O; on
  // non-blocking fds, an empty kernel buffer causes a busy-loop.
  [[nodiscard]] bool read_exact(std::string& data) const {
    size_t offset{};
    const auto target = data.size();
    while (offset < target) {
      const auto n = ::read(handle_, data.data() + offset, target - offset);
      // On EOF, trim to bytes received and fail.
      if (n == 0) {
        no_zero{data}.trim_to(offset);
        return false;
      }
      // On hard error, clear `data` and fail.
      if (n < 0) {
        if (!is_hard_error()) continue;
        data.clear();
        return false;
      }
      offset += static_cast<size_t>(n);
    }
    return true;
  }

#pragma endregion
#pragma region Control

  // Invoke `fcntl(cmd, args...)` on the handle. Returns -1 on failure.
  template<typename... Args>
  [[nodiscard]] int control(fcntl_ops cmd, Args&&... args) const noexcept {
    return ::fcntl(handle_, *cmd, std::forward<Args>(args)...);
  }

  // Return the fd status flags via `fcntl(F_GETFL)`.
  [[nodiscard]] std::optional<o_flags> get_flags() const noexcept {
    const o_flags flags{control(fcntl_ops::getfl)};
    if (*flags == -1) return std::nullopt;
    return flags;
  }

  // Set the fd status flags via `fcntl(F_SETFL)`. Returns false on failure.
  // These are the "O_" flags.
  [[nodiscard]] bool set_flags(o_flags flags) const noexcept {
    return control(fcntl_ops::setfl, *flags) == 0;
  }

  // Enable or disable non-blocking I/O via `fcntl(F_SETFL, O_NONBLOCK)`.
  // But consider opening with `O_NONBLOCK` in the first place.
  [[nodiscard]] bool set_nonblocking(bool on = true) const noexcept {
    const auto flags = get_flags();
    if (!flags) return false;
    const auto new_flags = bitmask::set_to(*flags, o_flags::nonblock, on);
    return set_flags(new_flags);
  }

#pragma endregion
#pragma region Errors

  // Check whether the last error was a hard error (true) or a soft error
  // (false); see `os_error::is_hard_error`.
  //
  // Note that `errno` is only meaningful immediately after a failure
  // return from a system call and is invalidated by the next system call.
  static bool is_hard_error(os_error err = os_error::last()) noexcept {
    return err.is_hard_error();
  }

#pragma endregion
#pragma region Data members
private:
  file_handle_t handle_ = invalid_file_handle;

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys

#pragma region formatter

// Format an `os_file` as its handle, or `(closed)` when it holds no open file.
// As a `nullable_formatter`, an open file forwards its handle to the `int`
// formatter (so it takes the full integer spec grammar) while a closed one
// renders the sentinel, padded to width.
template<corvid::CharType CharT>
struct std::formatter<corvid::filesys::os_file, CharT>
    : corvid::nullable_formatter<int, CharT> {
  constexpr formatter() noexcept
      : corvid::nullable_formatter<int, CharT>{"(closed)"} {}
};

#pragma endregion

#endif // CORVID_OS_FILE_ENTRY
#endif // CORVID_IMP_LINUX_OS_FILE_H
