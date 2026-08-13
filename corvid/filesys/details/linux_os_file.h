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
#ifndef _WIN32

#include <cstddef>
#include <optional>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

#include "../os_enums.h"
#include "../os_file_base.h"

// Linux implementation of "os_file.h", wrapping a file descriptor.

namespace corvid { inline namespace filesys {

#pragma region os_file

// RAII wrapper around a file descriptor. The shared interface is defined by
// `os_file_base` in "os_file_base.h".
//
// Beyond the shared surface, `control` wraps `fcntl`, and `get_flags`,
// `set_flags`, and `set_nonblocking` are named helpers for common fd-level
// operations.
class [[nodiscard]] os_file: public os_file_base<os_file, int, -1> {
  using base = os_file_base<os_file, int, -1>;
  friend base;

public:
  using base::base;

#pragma region Control

  // Invoke `fcntl(cmd, args...)` on the handle.
  //
  // Returns -1 on failure.
  template<typename... Args>
  [[nodiscard]] int control(fcntl_ops cmd, Args&&... args) const noexcept {
    return ::fcntl(handle(), *cmd, std::forward<Args>(args)...);
  }

  // Return the fd status flags via `fcntl(F_GETFL)`.
  [[nodiscard]] std::optional<o_flags> get_flags() const noexcept {
    const o_flags flags{control(fcntl_ops::getfl)};
    if (*flags == -1) return std::nullopt;
    return flags;
  }

  // Set the fd status flags via `fcntl(F_SETFL)`.
  [[nodiscard]] bool set_flags(o_flags flags) const noexcept {
    return control(fcntl_ops::setfl, *flags) == 0;
  }

  // Enable or disable non-blocking I/O via `fcntl(F_SETFL, O_NONBLOCK)`.
  //
  // But consider opening with `O_NONBLOCK` in the first place.
  [[nodiscard]] bool set_nonblocking(bool on = true) const noexcept {
    const auto flags = get_flags();
    if (!flags) return false;
    const auto new_flags = bitmask::set_to(*flags, o_flags::nonblock, on);
    return set_flags(new_flags);
  }

#pragma endregion
#pragma region Workers
private:
  // Adopt as-is: the only invalid fd sentinel is -1.
  [[nodiscard]] static file_handle_t do_adopt(file_handle_t h) noexcept {
    return h;
  }

  // Close via `::close`.
  [[nodiscard]] static bool do_close(file_handle_t h) noexcept {
    return ::close(h) == 0;
  }

  // One `::write`.
  //
  // Returns the bytes written, 0 for a soft failure, or nullopt for a hard
  // one. Note that this call can raise SIGPIPE on broken pipes/sockets, so use
  // `net_socket::send` with MSG_NOSIGNAL instead.
  [[nodiscard]] std::optional<size_t>
  do_write_some(const char* p, size_t len) const noexcept {
    const auto n = ::write(handle(), p, len);
    if (n > 0) return static_cast<size_t>(n);
    if ((n == 0) || is_hard_error()) return std::nullopt;
    return std::optional<size_t>{0};
  }

  // One `::read`.
  //
  // EOF is a zero return; a negative one classifies soft or hard by `errno`.
  [[nodiscard]] read_result do_read_some(char* p, size_t len) const noexcept {
    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    const auto n = ::read(handle(), p, len);
    if (n > 0) return {read_status::data, static_cast<size_t>(n)};
    if (n == 0) return {read_status::eof};
    return {is_hard_error() ? read_status::hard : read_status::soft};
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys

#endif // !_WIN32
