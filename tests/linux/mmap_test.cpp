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
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <sys/mman.h>
#include <unistd.h>

#include "corvid/filesys.h"
#include "corvid/enums/enum_conversion.h"
#include "catch2_main.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// A temporary file holding `content`, unlinked when destroyed.
struct temp_file {
  std::string path;
  os_file file;

  explicit temp_file(std::string_view content) {
    path = (std::filesystem::temp_directory_path() / "corvid_mmap_XXXXXX")
               .string();
    file = os_file{::mkstemp(path.data())};
    REQUIRE(file);
    REQUIRE(file.write_all(content));
  }

  ~temp_file() { ::unlink(path.c_str()); }

  temp_file(const temp_file&) = delete;
  temp_file& operator=(const temp_file&) = delete;
};

// Two pages and a bit, with a recognizable byte pattern.
std::string make_content() {
  std::string s((2 * memory_map::page_size()) + 100, '\0');
  for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char>(i % 251);
  return s;
}

bool same_bytes(std::span<const std::byte> bytes, std::string_view s) {
  return (bytes.size() == s.size()) &&
         (std::memcmp(bytes.data(), s.data(), s.size()) == 0);
}

} // namespace

#pragma region Anonymous

TEST_CASE("Anonymous mapping", "[Mmap]") {
  const auto page = memory_map::page_size();
  CHECK(page > 0);
  CHECK(std::has_single_bit(page));

  auto m = memory_map::create(2 * page, mmap_prot::read | mmap_prot::write,
      mmap_mask::map_private | mmap_mask::anonymous);
  REQUIRE(m);
  CHECK(m.is_mapped());
  CHECK(m.size() == 2 * page);
  CHECK(m.data());

  // Anonymous pages start out zeroed and are writable.
  auto bytes = m.bytes();
  CHECK(bytes.size() == 2 * page);
  CHECK(std::ranges::all_of(bytes, [](std::byte b) {
    return b == std::byte{};
  }));
  bytes[0] = std::byte{0x5A};
  bytes[page] = std::byte{0xA5};
  CHECK(static_cast<std::byte*>(m.data())[0] == std::byte{0x5A});
  CHECK(static_cast<std::byte*>(m.data())[page] == std::byte{0xA5});

  // Advice over the whole region, and over a page-aligned sub-range.
  CHECK(m.advise(mmap_advice::sequential));
  CHECK(m.advise(page, page, mmap_advice::normal));

  // An unaligned sub-range is rejected.
  CHECK_FALSE(m.advise(1, 10, mmap_advice::normal));
  const auto advise_errno = errno;
  CHECK(advise_errno == EINVAL);
}

TEST_CASE("Zero-length mapping fails", "[Mmap]") {
  const auto m = memory_map::create(0, mmap_prot::read,
      mmap_mask::map_private | mmap_mask::anonymous);
  const auto create_errno = errno;
  CHECK_FALSE(m);
  CHECK(m.size() == 0);
  CHECK(m.data() == nullptr);
  CHECK(create_errno == EINVAL);
}

#pragma endregion
#pragma region Lifecycle

TEST_CASE("Move and unmap", "[Mmap]") {
  const auto page = memory_map::page_size();
  auto a = memory_map::create(page, mmap_prot::read | mmap_prot::write,
      mmap_mask::map_private | mmap_mask::anonymous);
  REQUIRE(a);
  auto* base = a.data();

  // Move construction transfers the region.
  memory_map b{std::move(a)};
  CHECK_FALSE(a);       // NOLINT(bugprone-use-after-move)
  CHECK(a.size() == 0); // NOLINT(clang-analyzer-cplusplus.Move)
  CHECK(b.data() == base);
  CHECK(b.size() == page);

  // Move assignment onto a mapped object unmaps its old region first.
  auto c = memory_map::create(page, mmap_prot::read,
      mmap_mask::map_private | mmap_mask::anonymous);
  REQUIRE(c);
  c = std::move(b);
  CHECK_FALSE(b); // NOLINT(bugprone-use-after-move)
  CHECK(c.data() == base);
  CHECK(c.size() == page);

  // Unmap is idempotent and reports whether it did anything.
  CHECK(c.unmap());
  CHECK_FALSE(c);
  CHECK_FALSE(c.unmap());

  // A default-constructed mapping is empty.
  const memory_map empty;
  CHECK_FALSE(empty);
  CHECK(empty.size() == 0);
  CHECK(empty.bytes().empty());
}

TEST_CASE("Release", "[Mmap]") {
  const auto page = memory_map::page_size();
  auto m = memory_map::create(page, mmap_prot::read,
      mmap_mask::map_private | mmap_mask::anonymous);
  REQUIRE(m);
  auto* base = m.release();
  CHECK(base);
  CHECK_FALSE(m);
  CHECK(m.size() == 0);
  CHECK(::munmap(base, page) == 0);
}

#pragma endregion
#pragma region Files

TEST_CASE("File mapping", "[Mmap]") {
  const auto page = memory_map::page_size();
  const auto content = make_content();
  const temp_file tf{content};

  // Whole file by path: the descriptor is opened and closed inside.
  if (true) {
    const auto m = memory_map::map_file(tf.path);
    REQUIRE(m);
    CHECK(m.size() == content.size());
    CHECK(same_bytes(m.bytes(), content));
  }

  // Whole file through an open `os_file`.
  if (true) {
    const auto m = memory_map::map_all(tf.file);
    REQUIRE(m);
    CHECK(same_bytes(m.bytes(), content));
  }

  // A range from a page-aligned offset.
  if (true) {
    const auto m = memory_map::map(tf.file, 100, static_cast<off_t>(page));
    REQUIRE(m);
    CHECK(m.size() == 100);
    CHECK(same_bytes(m.bytes(), std::string_view{content}.substr(page, 100)));
  }

  // An unaligned offset is rejected.
  if (true) {
    const auto m = memory_map::map(tf.file, 100, 1);
    const auto map_errno = errno;
    CHECK_FALSE(m);
    CHECK(map_errno == EINVAL);
  }

  // The mapping outlives the file: unlink and close, then read it again.
  auto m = memory_map::map_all(tf.file);
  REQUIRE(m);
  ::unlink(tf.path.c_str());
  auto file = std::move(const_cast<temp_file&>(tf).file);
  CHECK(file.close());
  CHECK(same_bytes(m.bytes(), content));
}

TEST_CASE("File mapping failures", "[Mmap]") {
  // A path that does not exist.
  if (true) {
    const auto m = memory_map::map_file("/nonexistent/corvid_mmap_test");
    const auto open_errno = errno;
    CHECK_FALSE(m);
    CHECK(open_errno == ENOENT);
  }

  // A closed file has no descriptor to map, whether the length comes from
  // `fstat` or from the caller: without the anonymous flag, `mmap` rejects
  // the -1 descriptor itself.
  if (true) {
    const os_file closed;
    const auto all = memory_map::map_all(closed);
    const auto fstat_errno = errno;
    CHECK_FALSE(all);
    CHECK(fstat_errno == EBADF);

    const auto ranged = memory_map::map(closed, memory_map::page_size());
    const auto map_errno = errno;
    CHECK_FALSE(ranged);
    CHECK(map_errno == EBADF);
  }

  // An empty file has nothing to map.
  if (true) {
    const temp_file tf{""};
    const auto m = memory_map::map_all(tf.file);
    const auto map_errno = errno;
    CHECK_FALSE(m);
    CHECK(map_errno == EINVAL);
  }
}

#pragma endregion
#pragma region Enum strings

TEST_CASE("MmapProtString", "[Mmap]") {
  // Bitmask enum: exec(4) > write(2) > read(1), plus the stack-growth bits
  // growsdown(24) and growsup(25); none(0) has no bit name.
  using namespace corvid::strings;
  using P = mmap_prot;
  if (true) {
    CHECK(enum_as_string(P::none) == "0x00000000");
    CHECK(enum_as_string(P::read) == "read");
    CHECK(enum_as_string(P::write) == "write");
    CHECK(enum_as_string(P::exec) == "exec");
    CHECK(enum_as_string(P::exec | P::read) == "exec + read");
    CHECK(enum_as_string(P::growsdown) == "growsdown");
    CHECK(enum_as_string(P::growsup) == "growsup");
    CHECK(enum_as_string(P::growsdown | P::write | P::read) ==
          "growsdown + write + read");
  }
  if (true) {
    constexpr P bad{};
    CHECK(parse_enum("read", bad) == P::read);
    CHECK(parse_enum("write", bad) == P::write);
    CHECK(parse_enum("exec", bad) == P::exec);
    CHECK(parse_enum("exec + read", bad) == (P::exec | P::read));
    CHECK(parse_enum("growsdown + read", bad) == (P::growsdown | P::read));
  }
}

TEST_CASE("MmapAdviceString", "[Mmap]") {
  // Sequence enum: values 0-4 and 8-25 named, plus a sparse segment naming
  // `hwpoison` at 100; 5-7 are gaps.
  using namespace corvid::strings;
  using MA = mmap_advice;
  if (true) {
    CHECK(enum_as_string(MA::normal) == "normal");
    CHECK(enum_as_string(MA::dontneed) == "dontneed");
    CHECK(enum_as_string(MA::free) == "free");
    CHECK(enum_as_string(MA::remove) == "remove");
    CHECK(enum_as_string(MA::dontfork) == "dontfork");
    CHECK(enum_as_string(MA::dofork) == "dofork");
    CHECK(enum_as_string(MA::hugepage) == "hugepage");
    CHECK(enum_as_string(MA::wipeonfork) == "wipeonfork");
    CHECK(enum_as_string(MA::collapse) == "collapse");
  }
  if (true) {
    // Gap and between-segment values print numerically; -1 is out of range.
    CHECK(enum_as_string(MA{5}) == "5");
    CHECK(enum_as_string(MA{26}) == "26");
    CHECK(enum_as_string(MA::hwpoison) == "hwpoison");
    CHECK(enum_as_string(MA{-1}) == "-1");
    CHECK(enums::sequence::enum_as_view(MA::normal) == "normal");
    CHECK(enums::sequence::enum_as_view(MA{5}) == "");
    CHECK(enums::sequence::enum_as_view(MA{26}) == "");
    CHECK(enums::sequence::enum_as_view(MA{-1}) == "");
  }
  if (true) {
    constexpr MA bad{-1};
    CHECK(parse_enum("normal", bad) == MA::normal);
    CHECK(parse_enum("dontneed", bad) == MA::dontneed);
    CHECK(parse_enum("free", bad) == MA::free);
    CHECK(parse_enum("dofork", bad) == MA::dofork);
    CHECK(parse_enum("hugepage", bad) == MA::hugepage);
    CHECK(parse_enum("collapse", bad) == MA::collapse);
    CHECK(parse_enum("hwpoison", bad) == MA::hwpoison);
  }
}

TEST_CASE("MmapMaskString", "[Mmap]") {
  // Bitmask enum: named flags at bits 8 (growsdown) through 20
  // (fixed_noreplace); none=0 and multi-bit masks print as hex.
  using namespace corvid::strings;
  using M = mmap_mask;
  if (true) {
    CHECK(enum_as_string(M::shared) == "shared");
    CHECK(enum_as_string(M::map_private) == "private");
    CHECK(enum_as_string(M::anonymous) == "anonymous");
    CHECK(enum_as_string(M::fixed) == "fixed");
    CHECK(enum_as_string(M::growsdown) == "growsdown");
    CHECK(enum_as_string(M::denywrite) == "denywrite");
    CHECK(enum_as_string(M::executable) == "executable");
    CHECK(enum_as_string(M::locked) == "locked");
    CHECK(enum_as_string(M::noreserve) == "noreserve");
    CHECK(enum_as_string(M::populate) == "populate");
    CHECK(enum_as_string(M::nonblock) == "nonblock");
    CHECK(enum_as_string(M::stack) == "stack");
    CHECK(enum_as_string(M::hugetlb) == "hugetlb");
    CHECK(enum_as_string(M::sync) == "sync");
    CHECK(enum_as_string(M::fixed_noreplace) == "fixed_noreplace");
    // Higher bits print first.
    CHECK((enum_as_string(M::hugetlb | M::growsdown)) ==
          ("hugetlb + growsdown"));
  }
  if (true) {
    constexpr M bad{};
    CHECK(parse_enum("shared", bad) == M::shared);
    CHECK(parse_enum("private", bad) == M::map_private);
    CHECK(parse_enum("anonymous", bad) == M::anonymous);
    CHECK(parse_enum("fixed", bad) == M::fixed);
    CHECK(parse_enum("growsdown", bad) == M::growsdown);
    CHECK(parse_enum("denywrite", bad) == M::denywrite);
    CHECK(parse_enum("populate", bad) == M::populate);
    CHECK(parse_enum("hugetlb", bad) == M::hugetlb);
    CHECK(parse_enum("fixed_noreplace", bad) == M::fixed_noreplace);
    CHECK((parse_enum("hugetlb + growsdown", bad)) ==
          (M::hugetlb | M::growsdown));
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
