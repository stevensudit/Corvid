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

// The body drops out on the wrong platform, keeping cross-platform viewing
// quiet.
#ifdef _WIN32

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>

// <windows.h> comes via "os_enums.h".
#include "../os_enums.h"
#include "../os_file_base.h"

// Windows implementation of "os_file.h", wrapping a `HANDLE`.

namespace corvid { inline namespace filesys {

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

#pragma region Workers
private:
  // Canonicalize the alternate invalid sentinel to null.
  [[nodiscard]] static file_handle_t do_adopt(file_handle_t h) noexcept {
    return h == INVALID_HANDLE_VALUE ? invalid_file_handle : h;
  }

  // Close via `CloseHandle`.
  [[nodiscard]] static bool do_close(file_handle_t h) noexcept {
    return ::CloseHandle(h);
  }

  // One `WriteFile`. Returns the bytes written, 0 for a soft failure, or
  // nullopt for a hard one.
  [[nodiscard]] std::optional<size_t>
  do_write_some(const char* p, size_t len) const noexcept {
    DWORD written{};
    if (!::WriteFile(handle(), p, clamp_len(len), &written, nullptr))
      return is_hard_error() ? std::nullopt : std::optional<size_t>{0};
    return size_t{written};
  }

  // One `ReadFile`.
  //
  // A zero-byte success or a broken pipe is EOF; any other failure classifies
  // soft or hard by `GetLastError`.
  [[nodiscard]] read_result do_read_some(char* p, size_t len) const noexcept {
    DWORD got{};
    if (!::ReadFile(handle(), p, clamp_len(len), &got, nullptr)) {
      const auto err = os_error::last();
      if (err.code() == EC::broken_pipe) return {read_status::eof};
      return {err.is_hard_error() ? read_status::hard : read_status::soft};
    }
    if (got == 0) return {read_status::eof};
    return {read_status::data, size_t{got}};
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

#endif // _WIN32
