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
#ifndef CORVID_IMP_WINDOWS_OS_FILE_H
#ifndef CORVID_OS_FILE_ENTRY
#if defined(CORVID_CLANGD) || !defined(NDEBUG)
// Reroute a standalone dev-time inclusion through the entry point.
#include "../os_file.h"
#else
#error "Include \"os_file.h\" instead of this implementation header."
#endif
#else
#define CORVID_IMP_WINDOWS_OS_FILE_H

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

// <windows.h> comes via "os_error.h".
#include "../../meta/formatting.h"
#include "../../strings/no_zero.h"
#include "../os_error.h"

// Windows implementation of "os_file.h", wrapping a `HANDLE`.

namespace corvid { inline namespace filesys {

using corvid::strings::no_zero;

#pragma region details

namespace details {
// Platform file handle type and invalid-handle sentinel. The Windows API has
// two failure sentinels (`CreateFile` returns `INVALID_HANDLE_VALUE` while
// `CreateEvent` returns null), so `os_file` canonicalizes on null at adoption.
using file_handle_t = HANDLE;
constexpr file_handle_t invalid_file_handle = nullptr;
} // namespace details

#pragma endregion
#pragma region os_file

// RAII wrapper around a `HANDLE`. See "os_file.h" for the contract.
class [[nodiscard]] os_file {
public:
#pragma region Types

  using file_handle_t = details::file_handle_t;
  static constexpr file_handle_t invalid_file_handle =
      details::invalid_file_handle;

#pragma endregion
#pragma region Construction

  // Adopt an existing handle, normalizing `INVALID_HANDLE_VALUE` to the null
  // sentinel. Defaults to an invalid (closed) file.
  explicit os_file(file_handle_t h = invalid_file_handle) noexcept
      : handle_{h == INVALID_HANDLE_VALUE ? invalid_file_handle : h} {}

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
    return ::CloseHandle(old_handle) != 0;
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
  // unchanged and returns false. A "soft" failure is treated as success with
  // no progress.
  [[nodiscard]] bool write(std::string_view& data) const {
    if (data.empty()) return true;

    DWORD written{};
    if (!::WriteFile(handle_, data.data(), clamp_len(data.size()), &written,
            nullptr))
      return !is_hard_error();

    data.remove_prefix(written);
    return true;
  }

  // Read up to `data.size()` bytes from the file into `data`. Use
  // `no_zero{data}.enlarge_to_cap()` or `no_zero{data}.enlarge_to(n)` to get
  // the desired size.
  //
  // On success, resizes `data` to the number of bytes read and returns true. A
  // "soft" failure is treated as success with zero bytes read. On
  // EOF/disconnect (including a broken pipe), leaves `data` unchanged and
  // returns false. On hard failure, clears `data` and returns false.
  [[nodiscard]] bool read(std::string& data) const {
    if (data.empty()) return true;

    DWORD got{};
    if (!::ReadFile(handle_, data.data(), clamp_len(data.size()), &got,
            nullptr))
    {
      const auto err = os_error::last();

      // A broken pipe reads as EOF/disconnect: fail without clearing `data`.
      if (err.code() == EC::broken_pipe) return false;

      // If retriable, treat as a success with nothing read, while a hard
      // error is a failure with `data` cleared.
      data.clear();
      return !err.is_hard_error();
    }

    // EOF/disconnect. Return false without clearing `data`.
    if (got == 0) return false;

    // Update `data` to the size actually read.
    no_zero{data}.trim_to(got);
    return true;
  }

  // Write all of `data` to the file, retrying after partial writes and soft
  // errors. Returns true only when all bytes have been written. On hard
  // failure, returns false with an indeterminate prefix of `data` already
  // sent. Intended for blocking I/O.
  [[nodiscard]] bool write_all(std::string_view data) const {
    while (!data.empty())
      if (!write(data)) return false;
    return true;
  }

  // Read exactly `data.size()` bytes into `data`, retrying after partial
  // reads and soft errors. Size `data` with `data.resize(n)` or
  // `no_zero{data}.enlarge_to(n)` before calling.
  //
  // Returns true only when all bytes have been read. On EOF before completion
  // (including a broken pipe), trims `data` to the bytes received and returns
  // false. On hard failure, clears `data` and returns false. Intended for
  // blocking I/O.
  [[nodiscard]] bool read_exact(std::string& data) const {
    size_t offset{};
    const auto target = data.size();
    while (offset < target) {
      DWORD got{};
      if (!::ReadFile(handle_, data.data() + offset,
              clamp_len(target - offset), &got, nullptr))
      {
        const auto err = os_error::last();
        // A broken pipe is EOF: trim to bytes received and fail.
        if (err.code() == EC::broken_pipe) {
          no_zero{data}.trim_to(offset);
          return false;
        }
        if (!err.is_hard_error()) continue;
        // On hard error, clear `data` and fail.
        data.clear();
        return false;
      }
      // On EOF, trim to bytes received and fail.
      if (got == 0) {
        no_zero{data}.trim_to(offset);
        return false;
      }
      offset += got;
    }
    return true;
  }

#pragma endregion
#pragma region Errors

  // Check whether the last error was a hard error (true) or a soft error
  // (false); see `os_error::is_hard_error`.
  //
  // Note that the last error is only meaningful immediately after a failing
  // Win32 call and may be overwritten by the next one.
  static bool is_hard_error(os_error err = os_error::last()) noexcept {
    return err.is_hard_error();
  }

#pragma endregion
#pragma region Helpers
private:
  // Clamp a buffer size to what a single Win32 I/O call can carry.
  static DWORD clamp_len(size_t n) noexcept {
    return static_cast<DWORD>(
        std::min<size_t>(n, std::numeric_limits<DWORD>::max()));
  }

#pragma endregion
#pragma region Data members

  file_handle_t handle_ = invalid_file_handle;

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys

#pragma region formatter

// Format an `os_file` as its handle pointer, or `(closed)` when it holds no
// open file. As a `nullable_formatter`, an open file forwards its handle to
// the pointer formatter (so it takes the full pointer spec grammar) while a
// closed one renders the sentinel, padded to width.
template<corvid::CharType CharT>
struct std::formatter<corvid::filesys::os_file, CharT>
    : corvid::nullable_formatter<void*, CharT> {
  constexpr formatter() noexcept
      : corvid::nullable_formatter<void*, CharT>{"(closed)"} {}
};

#pragma endregion

#endif // CORVID_OS_FILE_ENTRY
#endif // CORVID_IMP_WINDOWS_OS_FILE_H
