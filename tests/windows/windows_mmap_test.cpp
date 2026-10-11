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
#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <utility>

#include "corvid/filesys.h"
#include "corvid/enums/enum_conversion.h"
#include "corvid/strings/conversion.h"
#include "catch2_main.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// A temporary file holding `content`.
//
// Opened with `FILE_FLAG_DELETE_ON_CLOSE`, so the system removes it once the
// last handle to it is closed, a mapping's included; an aborted test leaves
// nothing behind. Reopening it by path needs `FILE_SHARE_DELETE`, which
// `map_file` supplies.
struct temp_file {
  std::filesystem::path path;
  os_file file;

  explicit temp_file(std::string_view content) {
    static int counter{};
    path = std::filesystem::temp_directory_path() /
           std::format("corvid_mmap_{}_{}.tmp", ::GetCurrentProcessId(),
               ++counter);
    file = os_file{::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr)};
    REQUIRE(file);
    REQUIRE(file.write_all(content));
  }
};

// Two allocation granules and a bit, with a recognizable byte pattern.
std::string make_content() {
  std::string s((2 * mapped_view::allocation_granularity()) + 100, '\0');
  for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char>(i % 251);
  return s;
}

bool all_zero(std::span<const std::byte> bytes) {
  return std::ranges::all_of(bytes, [](std::byte b) {
    return (b == std::byte{});
  });
}

} // namespace

#pragma region Anonymous

TEST_CASE("Anonymous mapping", "[Mmap]") {
  const auto page = mapped_view::page_size();
  const auto granule = mapped_view::allocation_granularity();
  CHECK(page > 0);
  CHECK(std::has_single_bit(page));
  CHECK(granule >= page);
  // `page` was checked above; the analyzer cannot see through CHECK.
  // NOLINTNEXTLINE(clang-analyzer-core.DivideZero)
  CHECK(granule % page == 0);

  auto m = mapped_view::create_anonymous(2 * page);
  REQUIRE(m);
  CHECK(m.is_mapped());
  CHECK(m.size() == 2 * page);
  CHECK(m.data());

  // Paging-file-backed pages start out zeroed and are writable.
  auto bytes = m.bytes();
  CHECK(bytes.size() == 2 * page);
  CHECK(all_zero(bytes));
  bytes[0] = std::byte{0x5A};
  bytes[page] = std::byte{0xA5};
  CHECK(static_cast<std::byte*>(m.data())[0] == std::byte{0x5A});
  CHECK(static_cast<std::byte*>(m.data())[page] == std::byte{0xA5});

  // Flush over the whole view, and over a sub-range.
  CHECK(m.flush());
  CHECK(m.flush(page, page));
}

TEST_CASE("Two views of one mapping object", "[Mmap]") {
  // Views of the same object are coherent, and the object may be closed while
  // they live.
  const auto page = mapped_view::page_size();
  mapped_view a;
  mapped_view b;
  if (true) {
    const auto mapping = file_mapping::create_anonymous(page);
    REQUIRE(mapping);
    CHECK(mapping.is_open());
    CHECK(mapping.handle());
    a = mapped_view::create(mapping, file_map::write, page);
    b = mapped_view::create(mapping, file_map::read, page);
    REQUIRE(a);
    REQUIRE(b);
  }
  CHECK(a.data() != b.data());
  a.bytes()[7] = std::byte{0x42};
  CHECK(b.bytes()[7] == std::byte{0x42});

  // A read-only view of a read-only object cannot be mapped for writing.
  const auto readonly =
      file_mapping::create_anonymous(page, page_protect::readonly);
  REQUIRE(readonly);
  const auto w = mapped_view::create(readonly, file_map::write, page);
  const auto err = os_error::last();
  CHECK_FALSE(w);
  CHECK(err.code() == EC::access_denied);
}

TEST_CASE("Anonymous mapping failures", "[Mmap]") {
  // A closed mapping object has nothing to map.
  const file_mapping closed;
  CHECK_FALSE(closed);
  const auto m =
      mapped_view::create(closed, file_map::read, mapped_view::page_size());
  const auto err = os_error::last();
  CHECK_FALSE(m);
  CHECK(err.code() == EC::invalid_handle);
}

#pragma endregion
#pragma region Lifecycle

TEST_CASE("Move and unmap", "[Mmap]") {
  const auto page = mapped_view::page_size();
  auto a = mapped_view::create_anonymous(page);
  REQUIRE(a);
  auto* base = a.data();

  // Move construction transfers the view.
  mapped_view b{std::move(a)};
  CHECK_FALSE(a);       // NOLINT(bugprone-use-after-move)
  CHECK(a.size() == 0); // NOLINT(clang-analyzer-cplusplus.Move)
  CHECK(b.data() == base);
  CHECK(b.size() == page);

  // Move assignment onto a mapped object unmaps its old view first.
  auto c = mapped_view::create_anonymous(page);
  REQUIRE(c);
  c = std::move(b);
  CHECK_FALSE(b); // NOLINT(bugprone-use-after-move)
  CHECK(c.data() == base);
  CHECK(c.size() == page);

  // Unmap is idempotent and reports whether it did anything.
  CHECK(c.unmap());
  CHECK_FALSE(c);
  CHECK_FALSE(c.unmap());

  // A default-constructed view is empty.
  const mapped_view empty;
  CHECK_FALSE(empty);
  CHECK(empty.size() == 0);
  CHECK(empty.bytes().empty());
}

TEST_CASE("Release", "[Mmap]") {
  const auto page = mapped_view::page_size();
  auto m = mapped_view::create_anonymous(page);
  REQUIRE(m);
  auto* base = m.release();
  CHECK(base);
  CHECK_FALSE(m);
  CHECK(m.size() == 0);
  CHECK(::UnmapViewOfFile(base));
}

#pragma endregion
#pragma region Files

TEST_CASE("File mapping", "[Mmap]") {
  const auto granule = mapped_view::allocation_granularity();
  const auto content = make_content();
  const temp_file tf{content};

  // Whole file by path: the file handle is opened and closed inside.
  if (true) {
    const auto m = mapped_view::map_file(tf.path.c_str());
    REQUIRE(m);
    CHECK(m.size() == content.size());
    CHECK(strings::as_string_view(m.bytes()) == content);
  }

  // Whole file through an open `os_file`.
  if (true) {
    const auto m = mapped_view::map_all(tf.file);
    REQUIRE(m);
    CHECK(strings::as_string_view(m.bytes()) == content);
  }

  // A range from a granule-aligned offset.
  if (true) {
    const auto m = mapped_view::map(tf.file, 100, granule);
    REQUIRE(m);
    CHECK(m.size() == 100);
    CHECK(strings::as_string_view(m.bytes()) ==
          std::string_view{content}.substr(granule, 100));
  }

  // An unaligned offset is rejected.
  if (true) {
    const auto m = mapped_view::map(tf.file, 100, 1);
    const auto err = os_error::last();
    CHECK_FALSE(m);
    CHECK(err.code() == EC::mapped_alignment);
  }

  // A read-write view writes through to the file, visible to a read-only
  // view of it.
  if (true) {
    auto rw = mapped_view::map_all(tf.file, page_protect::readwrite,
        file_map::write);
    REQUIRE(rw);
    rw.bytes()[3] = std::byte{0xEE};
    CHECK(rw.flush());
    const auto ro = mapped_view::map_all(tf.file);
    REQUIRE(ro);
    CHECK(ro.bytes()[3] == std::byte{0xEE});
    // Put the byte back so later blocks see the original content.
    rw.bytes()[3] = static_cast<std::byte>(content[3]);
    CHECK(ro.bytes()[3] == static_cast<std::byte>(content[3]));
  }

  // The references run down from the view: it keeps the mapping object alive
  // once that handle is closed, and the file open once the `os_file` is
  // closed. With delete-on-close, the close is the moment the file would
  // otherwise go.
  auto file = std::move(const_cast<temp_file&>(tf).file);
  mapped_view m;
  if (true) {
    const auto mapping = file_mapping::create(file);
    REQUIRE(mapping);
    m = mapped_view::create(mapping, file_map::read, content.size());
    REQUIRE(m);
  }
  CHECK(file.close());
  CHECK(strings::as_string_view(m.bytes()) == content);
}

TEST_CASE("Mapping object alone does not hold the file", "[Mmap]") {
  // Closing the last handle to a delete-on-close file deletes it even while
  // a mapping object over it exists; a view mapped afterward can still be
  // created but no longer sees the contents.
  const auto content = make_content();
  temp_file tf{content};
  const auto mapping = file_mapping::create(tf.file);
  REQUIRE(mapping);
  CHECK(tf.file.close());
  const auto m = mapped_view::create(mapping, file_map::read, content.size());
  REQUIRE(m);
  CHECK_FALSE(strings::as_string_view(m.bytes()) == content);
}

TEST_CASE("File mapping failures", "[Mmap]") {
  // A path that does not exist.
  if (true) {
    const auto m = mapped_view::map_file(L"C:\\nonexistent\\corvid_mmap_test");
    const auto err = os_error::last();
    CHECK_FALSE(m);
    CHECK(err.code() == EC::path_not_found);
  }

  // A closed file has no handle to map: `GetFileSizeEx` rejects the null
  // handle as a handle, while `CreateFileMapping` rejects it as a parameter,
  // since null is neither a file nor the paging-file sentinel.
  if (true) {
    const os_file closed;
    const auto all = mapped_view::map_all(closed);
    const auto size_err = os_error::last();
    CHECK_FALSE(all);
    CHECK(size_err.code() == EC::invalid_handle);

    const auto ranged = mapped_view::map(closed, mapped_view::page_size());
    const auto map_err = os_error::last();
    CHECK_FALSE(ranged);
    CHECK(map_err.code() == EC::invalid_parameter);
  }

  // An empty file has nothing to map.
  if (true) {
    const temp_file tf{""};
    const auto m = mapped_view::map_all(tf.file);
    const auto err = os_error::last();
    CHECK_FALSE(m);
    CHECK(err.code() == EC::file_invalid);
  }
}

#pragma endregion
#pragma region Enum strings

TEST_CASE("PageProtectString", "[Mmap]") {
  // Bitmask enum: the page protections in the low byte and the section
  // attributes in the high byte; `image_no_execute` is `image + nocache`.
  using namespace corvid::strings;
  using P = page_protect;
  if (true) {
    CHECK(enum_as_string(P{}) == "0x00000000");
    CHECK(enum_as_string(P::readonly) == "readonly");
    CHECK(enum_as_string(P::readwrite) == "readwrite");
    CHECK(enum_as_string(P::execute_readwrite) == "execute_readwrite");
    CHECK(enum_as_string(P::readwrite | P::commit) == "commit + readwrite");
    CHECK(enum_as_string(P::readwrite | P::commit | P::large_pages) ==
          "large_pages + commit + readwrite");
    CHECK(enum_as_string(P::image_no_execute) == "nocache + image");
  }
  if (true) {
    constexpr P bad{};
    CHECK(parse_enum("readonly", bad) == P::readonly);
    CHECK(parse_enum("readwrite", bad) == P::readwrite);
    CHECK(parse_enum("commit + readwrite", bad) == (P::commit | P::readwrite));
    CHECK(parse_enum("large_pages + commit + readwrite", bad) ==
          (P::large_pages | P::commit | P::readwrite));
  }
}

TEST_CASE("FileMapString", "[Mmap]") {
  // Bitmask enum: copy(0), write(1), read(2), execute(5), and the three
  // high bits.
  using namespace corvid::strings;
  using F = file_map;
  if (true) {
    CHECK(enum_as_string(F{}) == "0x00000000");
    CHECK(enum_as_string(F::copy) == "copy");
    CHECK(enum_as_string(F::write) == "write");
    CHECK(enum_as_string(F::read) == "read");
    CHECK(enum_as_string(F::execute) == "execute");
    CHECK(enum_as_string(F::large_pages) == "large_pages");
    CHECK(enum_as_string(F::targets_invalid) == "targets_invalid");
    CHECK(enum_as_string(F::reserve) == "reserve");
    CHECK(enum_as_string(F::read | F::write) == "read + write");
    CHECK(enum_as_string(F::large_pages | F::write) == "large_pages + write");
  }
  if (true) {
    constexpr F bad{};
    CHECK(parse_enum("read", bad) == F::read);
    CHECK(parse_enum("write", bad) == F::write);
    CHECK(parse_enum("copy", bad) == F::copy);
    CHECK(parse_enum("read + write", bad) == (F::read | F::write));
    CHECK(
        parse_enum("large_pages + write", bad) == (F::large_pages | F::write));
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
