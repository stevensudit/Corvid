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

// Standalone inclusion is permitted in dev builds and under clangd, so this
// file can be viewed and parsed on its own; release builds must come through
// the entry point.
#if !defined(CORVID_OS_FILE_ENTRY) && defined(NDEBUG) &&                      \
    !defined(CORVID_CLANGD)
#error "Include \"os_file.h\" instead of this implementation header."
#endif

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <string_view>

// <windows.h> comes via "os_enums.h".
#include "../../strings/no_zero.h"
#include "../os_enums.h"
#include "../os_file_base.h"

// Windows implementation of "os_file.h", wrapping a `HANDLE`.

namespace corvid { inline namespace filesys {

using corvid::strings::no_zero;

#pragma region os_file

// RAII wrapper around a `HANDLE`. The shared interface is defined by
// `os_file_base` in "os_file_base.h".
//
// The Windows API has two failure sentinels (`CreateFile` returns
// `INVALID_HANDLE_VALUE` while `CreateEvent` returns null), so adoption
// canonicalizes on null.
class [[nodiscard]] os_file: public os_file_base<os_file, HANDLE, nullptr> {
  using base = os_file_base<os_file, HANDLE, nullptr>;
  friend base;

public:
  using base::base;

#pragma region I/O

  // Write as much of `data` as possible to the file. On success, removes the
  // written prefix from `data` and returns true. On failure, leaves `data`
  // unchanged and returns false. A "soft" failure is treated as success with
  // no progress.
  [[nodiscard]] bool write(std::string_view& data) const {
    if (data.empty()) return true;

    DWORD written{};
    if (!::WriteFile(handle(), data.data(), clamp_len(data.size()), &written,
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
    if (!::ReadFile(handle(), data.data(), clamp_len(data.size()), &got,
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
      if (!::ReadFile(handle(), data.data() + offset,
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
#pragma region Workers
private:
  // Canonicalize the alternate invalid sentinel to null.
  [[nodiscard]] static file_handle_t do_adopt(file_handle_t h) noexcept {
    return h == INVALID_HANDLE_VALUE ? invalid_file_handle : h;
  }

  // Close via `CloseHandle`.
  [[nodiscard]] static bool do_close(file_handle_t h) noexcept {
    return ::CloseHandle(h) != 0;
  }

  // Clamp a buffer size to what a single Win32 I/O call can carry.
  static DWORD clamp_len(size_t n) noexcept {
    return static_cast<DWORD>(
        std::min<size_t>(n, std::numeric_limits<DWORD>::max()));
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys
