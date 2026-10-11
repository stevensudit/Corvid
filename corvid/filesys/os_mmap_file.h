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

// `os_mmap_file` provides a read-only memory mapping of a whole open file.
//
// The `map` factory method takes an open `os_file` and maps the whole file
// read-only, `bytes` exposes the contents as a span of const bytes, and the
// mapping is released on destruction.
//
// On both platforms, the mapping holds its own reference to the file, so the
// `os_file` may be closed as soon as `map` returns. The underlying file stays
// open until the mapping is destroyed.
//
// An empty file has nothing to map and fails. Failure is signaled by an
// unmapped object, with the reason left for `os_error::last()`.
//
// The platform vocabulary (`memory_map` and `mapped_view`, with their enums)
// stays in "linux_mmap.h" and "windows_mmap.h"; none of it surfaces here.

#define CORVID_OS_MMAP_FILE_ENTRY
#ifdef _WIN32
#include "details/windows_os_mmap_file.h"
#else
#include "details/linux_os_mmap_file.h"
#endif
#undef CORVID_OS_MMAP_FILE_ENTRY
