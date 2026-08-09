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
#ifndef CORVID_IMP_WINDOWS_OS_ERROR_H
#ifndef CORVID_OS_ERROR_ENTRY
#if defined(CORVID_CLANGD) || !defined(NDEBUG)
// Reroute a standalone dev-time inclusion through the entry point.
#include "../os_error.h"
#else
#error "Include \"os_error.h\" instead of this implementation header."
#endif
#else
#define CORVID_IMP_WINDOWS_OS_ERROR_H

#include <cstdint>

#include "../os_enums.h"

// Link Winsock for `WSAGetLastError`.
#pragma comment(lib, "ws2_32.lib")

// Windows implementation of "os_error.h", wrapping `GetLastError` and
// `WSAGetLastError` values as a `win_error_code`.

namespace corvid { inline namespace filesys {

#pragma region os_error

// Value wrapper for an OS error code, holding a `win_error_code`. The
// interface is defined by `os_error_base` in "os_error.h".
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
  [[nodiscard]] constexpr bool do_is_soft_error() const noexcept {
    return code() == code_t::wouldblock || code() == code_t::intr ||
           code() == code_t::io_pending || code() == code_t::io_incomplete;
  }
};

#pragma endregion
}} // namespace corvid::filesys

#endif // CORVID_OS_ERROR_ENTRY
#endif // CORVID_IMP_WINDOWS_OS_ERROR_H
