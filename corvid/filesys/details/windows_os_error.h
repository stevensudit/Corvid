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
#if !defined(CORVID_OS_ERROR_ENTRY) && defined(NDEBUG) &&                     \
    !defined(CORVID_CLANGD)
#error "Include \"os_error.h\" instead of this implementation header."
#endif

// The body drops out on the wrong platform, keeping cross-platform viewing
// quiet.
#ifdef _WIN32

#include <cstdint>

#include "../os_enums.h"
#include "../os_error_base.h"

// Link Winsock for `WSAGetLastError`.
#pragma comment(lib, "ws2_32.lib")

// Windows implementation of "os_error.h", wrapping `GetLastError` and
// `WSAGetLastError` values as a `win_error_code`.

namespace corvid { inline namespace filesys {

#pragma region os_error

// Value wrapper for an OS error code, holding a `win_error_code`. The
// interface is defined by `os_error_base` in "os_error_base.h".
class os_error: public os_error_base<os_error, win_error_code> {
  using base = os_error_base<os_error, win_error_code>;
  friend base;

public:
  using base::base;

private:
  // Read `GetLastError`.
  [[nodiscard]] static code_t do_last() noexcept {
    return win_error_code{::GetLastError()};
  }

  // Read `WSAGetLastError`, the socket-specific channel.
  [[nodiscard]] static code_t do_last_socket() noexcept {
    return win_error_code{static_cast<uint32_t>(::WSAGetLastError())};
  }

  // The soft errors are the retriable flow control conditions:
  // `WSAEWOULDBLOCK` and `WSAEINTR`, plus the overlapped-pending pair
  // `ERROR_IO_PENDING` and `ERROR_IO_INCOMPLETE`.
  //
  // TODO: When Windows pipe support lands, add `ERROR_NO_DATA` (an empty
  // PIPE_NOWAIT read, the `EAGAIN` analogue) to the soft set.
  [[nodiscard]] constexpr bool do_is_soft_error() const noexcept {
    return code() == code_t::wouldblock || code() == code_t::intr ||
           code() == code_t::io_pending || code() == code_t::io_incomplete;
  }
};

#pragma endregion
}} // namespace corvid::filesys

#endif // _WIN32
