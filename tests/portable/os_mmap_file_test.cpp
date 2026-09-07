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
#include <cstddef>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#ifndef _WIN32
#include <fcntl.h>
#endif

#include "corvid/filesys.h"
#include "corvid/strings/conversion.h"
#include "catch2_main.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// A temporary file holding `content`, open read-write.
//
// The file takes the OS's self-deleting form, `O_TMPFILE` on Linux and
// `FILE_FLAG_DELETE_ON_CLOSE` on Windows, so it disappears once the last
// reference to it is gone, a mapping's included; an aborted test leaves
// nothing behind. The open is platform-specific because `os_file` has no
// path-based open on either platform.
struct temp_file {
  os_file file;

  explicit temp_file(std::string_view content) {
    const auto dir = std::filesystem::temp_directory_path();
#ifdef _WIN32
    static int counter{};
    const auto path =
        dir / std::format("corvid_os_mmap_file_{}_{}.tmp",
                  ::GetCurrentProcessId(), ++counter);
    file = os_file{::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
        nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr)};
#else
    file = os_file{::open(dir.c_str(),
        *(o_flags::tmpfile | o_flags::rdwr | o_flags::cloexec), 0600)};
#endif
    REQUIRE(file);
    REQUIRE(file.write_all(content));
  }
};

// Several pages' worth on any platform, with a recognizable byte pattern.
std::string make_content() {
  std::string s(200'000, '\0');
  for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char>(i % 251);
  return s;
}

} // namespace

TEST_CASE("Map a file", "[OsMmapFile]") {
  const auto content = make_content();
  temp_file tf{content};

  // The whole file, read-only.
  const auto m = os_mmap_file::map(tf.file);
  REQUIRE(m);
  CHECK(m.is_mapped());
  CHECK(strings::as_string_view(m.bytes()) == content);

  // The mapping holds the file: close the `os_file` and read again.
  CHECK(tf.file.close());
  CHECK(strings::as_string_view(m.bytes()) == content);
}

TEST_CASE("Move", "[OsMmapFile]") {
  const auto content = make_content();
  const temp_file tf{content};
  auto a = os_mmap_file::map(tf.file);
  REQUIRE(a);
  const auto* base = a.bytes().data();

  // Move construction transfers the mapping.
  os_mmap_file b{std::move(a)};
  CHECK_FALSE(a);           // NOLINT(bugprone-use-after-move)
  CHECK(a.bytes().empty()); // NOLINT(clang-analyzer-cplusplus.Move)
  CHECK(b.bytes().data() == base);
  CHECK(strings::as_string_view(b.bytes()) == content);

  // Move assignment onto a mapped object releases its old mapping first.
  auto c = os_mmap_file::map(tf.file);
  REQUIRE(c);
  c = std::move(b);
  CHECK_FALSE(b); // NOLINT(bugprone-use-after-move)
  CHECK(c.bytes().data() == base);

  // A default-constructed object is unmapped.
  const os_mmap_file empty;
  CHECK_FALSE(empty);
  CHECK(empty.bytes().empty());
}

TEST_CASE("Map failures", "[OsMmapFile]") {
  // A closed file has nothing to map.
  if (true) {
    const os_file closed;
    const auto m = os_mmap_file::map(closed);
    const auto err = os_error::last();
    CHECK_FALSE(m);
    CHECK(m.bytes().empty());
    CHECK_FALSE(err.ok());
  }

  // An empty file has nothing to map.
  if (true) {
    const temp_file tf{""};
    const auto m = os_mmap_file::map(tf.file);
    const auto err = os_error::last();
    CHECK_FALSE(m);
    CHECK_FALSE(err.ok());
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
