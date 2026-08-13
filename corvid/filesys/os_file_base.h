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
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "../strings/no_zero.h"
#include "os_error.h"

namespace corvid { inline namespace filesys {

using corvid::strings::no_zero;

#pragma region os_file_base

// CRTP base defining the shared `os_file` interface.
//
// `os_file` is an RAII wrapper around an OS file handle: a file descriptor on
// Linux and a `HANDLE` on Windows. These are exposed as `file_handle_t` with
// the invalid sentinel, `invalid_file_handle`. An `os_file` owns a single file
// and closes it on destruction; it is movable and non-copyable.
//
// The whole interface, including the I/O operations, reads here. The platform
// implementation derives from this base, supplying only the `do_` workers
// (adopt, close, and the single-call read/write primitives) plus any extras,
// such as the `fcntl` helpers on Linux.
//
//   auto data = read_request();
//   while (!data.empty())
//     if (!file.write(data)) return fail("write: {}", os_error::last());
template<typename Derived, typename HandleT, HandleT Invalid>
class os_file_base {
public:
#pragma region Types

  using file_handle_t = HandleT;
  static constexpr file_handle_t invalid_file_handle = Invalid;

protected:
  // Outcome of one `do_read_some` primitive: what happened and, when data
  // arrived, how many bytes.
  enum class read_status : uint8_t { data, eof, soft, hard };

  struct read_result {
    read_status status;
    size_t bytes = 0;
  };

  // Outcome of one `do_write_some` primitive: the bytes written, 0 for a soft
  // (retriable) failure, or nullopt for a hard one.
  using write_result = std::optional<size_t>;

public:
#pragma endregion
#pragma region Construction

  // Public so that `Derived` can expose it with `using base::base`; only
  // `Derived` may actually inherit from this class.
  // NOLINTBEGIN(bugprone-crtp-constructor-accessibility)

  // Adopt an existing handle, normalizing any alternate invalid sentinel to
  // `invalid_file_handle`. Defaults to an invalid (closed) file.
  explicit os_file_base(file_handle_t h = invalid_file_handle) noexcept
      : handle_{Derived::do_adopt(h)} {}

  os_file_base(const os_file_base&) = delete;
  os_file_base& operator=(const os_file_base&) = delete;

  os_file_base(os_file_base&& other) noexcept : handle_{other.release()} {}

  os_file_base& operator=(os_file_base&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.release();
    }
    return *this;
  }

  // NOLINTEND(bugprone-crtp-constructor-accessibility)

  ~os_file_base() { close(); }

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

  // Close the file.
  //
  // Idempotent. Returns true when the file was open and is now closed, false
  // if it could not be closed (likely because it already was). Note that, on
  // failure, the file is left in a closed state to avoid potential reuse of a
  // stale handle.
  bool close() noexcept {
    if (!is_open()) return false;
    const auto old_handle = handle_;
    handle_ = invalid_file_handle;
    return Derived::do_close(old_handle);
  }

  // Release ownership and return the handle without closing it.
  [[nodiscard]] file_handle_t release() noexcept {
    const auto h = handle_;
    handle_ = invalid_file_handle;
    return h;
  }

#pragma endregion
#pragma region I/O

  // Write as much of `data` as possible to the file.
  //
  // On success, removes the written prefix from `data` and returns true. On
  // failure, leaves `data` unchanged and returns false. A soft failure (see
  // `os_error`) is treated as success with no progress.
  [[nodiscard]] bool write(std::string_view& data) const {
    if (data.empty()) return true;

    const auto n = self().do_write_some(data.data(), data.size());
    if (!n) return false;

    data.remove_prefix(*n);
    return true;
  }

  // Read up to `data.size()` bytes from the file into `data`.
  //
  // Use `no_zero{data}.enlarge_to_cap()` or `no_zero{data}.enlarge_to(n)` to
  // get the desired size.
  //
  // On success, resizes `data` to the number of bytes read and returns true.
  // A soft failure is treated as success with zero bytes read. On
  // EOF/disconnect, leaves `data` unchanged and returns false. On hard
  // failure, clears `data` and returns false.
  [[nodiscard]] bool read(std::string& data) const {
    if (data.empty()) return true;

    const auto [status, bytes] = self().do_read_some(data.data(), data.size());

    // Update `data` to the size actually read.
    if (status == read_status::data) {
      no_zero{data}.trim_to(bytes);
      return true;
    }

    // EOF/disconnect. Return false without clearing `data`.
    if (status == read_status::eof) return false;

    // If retriable, treat as a success with nothing read, while a hard error
    // is a failure; either way, `data` ends up cleared.
    data.clear();
    return status == read_status::soft;
  }

  // Write all of `data` to the file, retrying after partial writes and soft
  // errors.
  //
  // Returns true only when all bytes have been written. On hard failure,
  // returns false with an indeterminate prefix of `data` already sent.
  // Intended for blocking I/O; on a non-blocking file, a full kernel buffer
  // causes a busy-loop.
  [[nodiscard]] bool write_all(std::string_view data) const {
    while (!data.empty())
      if (!write(data)) return false;
    return true;
  }

  // Read exactly `data.size()` bytes into `data`, retrying after partial
  // reads and soft errors. Size `data` with `data.resize(n)` or
  // `no_zero{data}.enlarge_to(n)` before calling.
  //
  // Returns true only when all bytes have been read. On EOF before
  // completion, trims `data` to the bytes received and returns false. On
  // hard failure, clears `data` and returns false. Intended for blocking
  // I/O; on a non-blocking file, an empty kernel buffer causes a busy-loop.
  [[nodiscard]] bool read_exact(std::string& data) const {
    size_t offset{};
    const auto target = data.size();
    while (offset < target) {
      const auto [status, bytes] =
          self().do_read_some(data.data() + offset, target - offset);
      if (status == read_status::data) {
        offset += bytes;
        continue;
      }
      if (status == read_status::soft) continue;
      // On EOF, trim to bytes received and fail.
      if (status == read_status::eof) {
        no_zero{data}.trim_to(offset);
        return false;
      }
      // On hard error, clear `data` and fail.
      data.clear();
      return false;
    }
    return true;
  }

#pragma endregion
#pragma region Errors

  // Check whether the last error was a hard error (true) or a soft error
  // (false); see `os_error::is_hard_error`.
  //
  // Note that the last error is only meaningful immediately after a failing
  // OS call and may be overwritten by the next one.
  [[nodiscard]] static bool is_hard_error(
      os_error err = os_error::last()) noexcept {
    return err.is_hard_error();
  }

#pragma endregion
#pragma region Helpers
private:
  [[nodiscard]] constexpr const Derived& self() const noexcept {
    return static_cast<const Derived&>(*this);
  }

#pragma endregion
#pragma region Data members

  file_handle_t handle_ = invalid_file_handle;

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys
