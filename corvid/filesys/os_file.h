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
#include "../meta/formatting.h"

// `os_file`: RAII wrapper around an OS file handle. The shared interface is
// defined and documented by `os_file_base` in "os_file_base.h"; each platform
// derives its `os_file` from it, adding the I/O operations and its own
// extras.

#define CORVID_OS_FILE_ENTRY
#ifdef _WIN32
#include "imp/windows_os_file.h"
#else
#include "imp/linux_os_file.h"
#endif
#undef CORVID_OS_FILE_ENTRY

#pragma region formatter

// Format an `os_file` as its handle, or `(closed)` when it holds no open
// file. As a `nullable_formatter`, an open file forwards its handle to the
// platform handle type's formatter (so it takes that type's full spec
// grammar) while a closed one renders the sentinel, padded to width.
template<corvid::CharType CharT>
struct std::formatter<corvid::filesys::os_file, CharT>
    : corvid::nullable_formatter<corvid::filesys::os_file::file_handle_t,
          CharT> {
  constexpr formatter() noexcept
      : corvid::nullable_formatter<corvid::filesys::os_file::file_handle_t,
            CharT>{"(closed)"} {}
};

#pragma endregion
