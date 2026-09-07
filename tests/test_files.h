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

#include <filesystem>
#include <format>
#include <string_view>

#ifndef _WIN32
#include <fcntl.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include "corvid/filesys/os_enums.h"
#include "corvid/filesys/os_file.h"

// File helpers for the tests that must touch real files, such as the memory
// mapping wrappers. Tests that can avoid file I/O should.
//
// Both helpers exist because `os_file` has no path-based open on either
// platform; they hold the platform-specific opens in one place.

namespace corvid::tests {

// A temporary file holding `content`, open read-write.
//
// The file takes the OS's self-deleting form, `O_TMPFILE` on Linux and
// `FILE_FLAG_DELETE_ON_CLOSE` on Windows, so it disappears once the last
// reference to it is gone, a mapping's included; an aborted test leaves
// nothing behind.
struct temp_file {
  filesys::os_file file;

  explicit temp_file(std::string_view content) {
    const auto dir = std::filesystem::temp_directory_path();
#ifdef _WIN32
    static int counter{};
    const auto path =
        dir / std::format("corvid_test_{}_{}.tmp", ::GetCurrentProcessId(),
                  ++counter);
    file = filesys::os_file{::CreateFileW(path.c_str(),
        GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr)};
#else
    file = filesys::os_file{::open(dir.c_str(),
        *(filesys::o_flags::tmpfile | filesys::o_flags::rdwr |
            filesys::o_flags::cloexec),
        0600)};
#endif
    REQUIRE(file);
    REQUIRE(file.write_all(content));
  }
};

// Open the existing file at `path` read-only, sharing reads; a missing file
// yields a closed `os_file`.
[[nodiscard]] inline filesys::os_file open_read_only(
    const std::filesystem::path& path) {
#ifdef _WIN32
  return filesys::os_file{::CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
      nullptr)};
#else
  return filesys::os_file{::open(path.c_str(),
      *(filesys::o_flags::rdonly | filesys::o_flags::cloexec))};
#endif
}

} // namespace corvid::tests
