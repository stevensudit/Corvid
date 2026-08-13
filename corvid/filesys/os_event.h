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

// `os_event`: wake-up event for signaling across threads, implemented as an
// `eventfd` on Linux and a Win32 event on Windows.
//
// An `os_event` derives from `os_file` and owns its handle the same way. The
// portable surface is `create()`, `notify()` (make at least one waiter wake),
// and `drain()` (consume pending notifications so the next wait blocks).
// Platform-specific code may access the raw file descriptor or handle
// directly, such as for `epoll`.
//
// Each platform adds its own extras, such as the counter and semaphore modes
// on Linux, but portable callers must stick to the portable surface.
//
//   if (was_empty) (void)wake.notify();

#define CORVID_OS_EVENT_ENTRY
#ifdef _WIN32
#include "details/windows_os_event.h"
#else
#include "details/linux_os_event.h"
#endif
#undef CORVID_OS_EVENT_ENTRY
