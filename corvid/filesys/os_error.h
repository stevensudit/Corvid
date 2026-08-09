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
#include "../enums/enum_formatter.h"

// `os_error`: value wrapper for an OS error code. The interface is defined
// and documented by `os_error_base` in "os_error_base.h"; each platform
// derives its `os_error` from it.

#define CORVID_OS_ERROR_ENTRY
#ifdef _WIN32
#include "imp/windows_os_error.h"
#else
#include "imp/linux_os_error.h"
#endif
#undef CORVID_OS_ERROR_ENTRY

#pragma region formatter

// Format an `os_error` as its code, forwarding to the registered enum
// formatter, so named values print as their name and unnamed ones print
// numerically.
template<corvid::CharType CharT>
struct std::formatter<corvid::filesys::os_error, CharT>
    : std::formatter<corvid::filesys::os_error::code_t, CharT> {
  template<typename FormatContext>
  auto format(const corvid::filesys::os_error& err, FormatContext& ctx) const {
    return std::formatter<corvid::filesys::os_error::code_t, CharT>::format(
        err.code(), ctx);
  }
};

#pragma endregion
