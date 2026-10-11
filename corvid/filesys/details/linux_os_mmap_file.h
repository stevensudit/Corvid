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
#if !defined(CORVID_OS_MMAP_FILE_ENTRY) && defined(NDEBUG) &&                 \
    !defined(CORVID_CLANGD)
#error "Include \"os_mmap_file.h\" instead of this implementation header."
#endif

// The body drops out on the wrong platform, keeping cross-platform viewing
// quiet.
#ifndef _WIN32

#include <cstddef>
#include <span>
#include <utility>

#include "../linux_mmap.h"
#include "../os_file.h"

// Linux implementation of "os_mmap_file.h", over a `memory_map`.

namespace corvid { inline namespace filesys {

#pragma region os_mmap_file

// Read-only memory mapping of a whole open file. The portable contract is
// documented in "os_mmap_file.h".
class [[nodiscard]] os_mmap_file {
public:
#pragma region Construction

  os_mmap_file() noexcept = default;

  // Map the whole of `file` read-only.
  [[nodiscard]] static os_mmap_file map(const os_file& file) noexcept {
    return os_mmap_file{memory_map::map_all(file)};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool is_mapped() const noexcept { return map_.is_mapped(); }
  [[nodiscard]] explicit operator bool() const noexcept { return is_mapped(); }

  // The file contents.
  [[nodiscard]] std::span<const std::byte> bytes() const noexcept {
    return map_.bytes();
  }

#pragma endregion
#pragma region Data members
private:
  explicit os_mmap_file(memory_map&& map) noexcept : map_{std::move(map)} {}

  memory_map map_;

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys

#endif // _WIN32
