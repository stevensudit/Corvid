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

// `os_file`: RAII wrapper around an OS file handle, a file descriptor on
// Linux and a `HANDLE` on Windows (exposed as `file_handle_t`).
//
// An `os_file` owns a single file and closes it on destruction. It is movable
// and non-copyable. The portable surface is adoption of a raw handle, `close`
// and `release`, `is_open` (also as `operator bool`), `handle` (also as
// `operator*`), the `write`/`read`/`write_all`/`read_exact` operations, and
// `is_hard_error`. Each platform adds its own extras, such as the `fcntl`
// helpers on Linux; portable callers must stick to the portable surface.
//
//   auto data = read_request();
//   while (!data.empty())
//     if (!file.write(data)) return fail("write: {}", os_error::last());

#define CORVID_OS_FILE_ENTRY
#ifdef _WIN32
#include "imp/windows_os_file.h"
#else
#include "imp/linux_os_file.h"
#endif
#undef CORVID_OS_FILE_ENTRY
