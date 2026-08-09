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

// Registered enums wrapping the OS-level flag and error macros used by the
// filesys classes.
//
// Each platform contributes its own vocabulary, and most of it is
// platform-specific by nature: Linux provides `o_flags`, `msg_flags`,
// `fcntl_ops`, the `mmap_*` family, and `errno_code`, while Windows provides
// `win_error_code`.
//
// The one portable guarantee is the platform error code enum (`errno_code` or
// `win_error_code`), which always has an `ok` member, is aliased as `EC`, and
// serves as `os_error::code_t`.

#define CORVID_OS_ENUMS_ENTRY
#ifdef _WIN32
#include "imp/windows_os_enums.h"
#else
#include "imp/linux_os_enums.h"
#endif
#undef CORVID_OS_ENUMS_ENTRY
