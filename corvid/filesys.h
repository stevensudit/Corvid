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
// Umbrella header for the Corvid filesys module.
//
// Includes:
//  os_enums  - registered enums wrapping OS flag and error macros
//  os_error  - value wrapper for an OS error code
//  os_file   - RAII OS file-handle ownership and read/write
//  os_event  - wake-up event for signaling across threads
//  epoll     - RAII Linux epoll handle with control and wait helpers (Linux)
//  mmap      - memory_map, RAII mapping with file and advice helpers (Linux)
//  net_socket - RAII socket handle with type-safe option methods (Linux)
#include "filesys/os_enums.h"
#include "filesys/os_error.h"
#include "filesys/os_file.h"
#include "filesys/os_event.h"
#ifndef _WIN32
#include "filesys/epoll.h"
#include "filesys/mmap.h"
#endif
