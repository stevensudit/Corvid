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

#include <cerrno>

#include "../os_enums.h"
#include "../os_error_base.h"

// Linux implementation of "os_error.h", wrapping `errno` as an `errno_code`.

namespace corvid { inline namespace filesys {

#pragma region os_error

// Value wrapper for an OS error code, holding an `errno_code`. The interface
// is defined by `os_error_base` in "os_error_base.h".
class os_error: public os_error_base<os_error, errno_code> {
  using base = os_error_base<os_error, errno_code>;
  friend base;

public:
  using base::base;

private:
  // Read `errno`.
  [[nodiscard]] static code_t do_last() noexcept { return errno_code{errno}; }

  // Read `errno`; Linux sockets report through it like any other fd.
  [[nodiscard]] static code_t do_last_socket() noexcept { return do_last(); }

  // The soft errors are the retriable flow control conditions:
  // `EAGAIN`/`EWOULDBLOCK` and `EINTR`.
  [[nodiscard]] constexpr bool do_is_soft_error() const noexcept {
    return code() == code_t::again || code() == code_t::wouldblock ||
           code() == code_t::intr;
  }
};

#pragma endregion
}} // namespace corvid::filesys
