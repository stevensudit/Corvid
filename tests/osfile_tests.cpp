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

// Codex note: in this sandbox, creating AF_INET/AF_INET6 sockets can fail
// with EPERM, so the network-socket portions of this test file may fail even
// when the code is correct in a normal local environment.

#include "../corvid/filesys.h"
#include "../corvid/proto/net_endpoint.h"
#include "../corvid/strings/utils/enum_conversion.h"
#include "catch2_main.h"

#include <cstdlib>
#include <fcntl.h>
#include <string_view>
#include <unistd.h>
#include <system_error>
#include <sys/socket.h>

using namespace corvid;

bool is_codex() {
  const char* value = std::getenv("CODEX_SANDBOX_NETWORK_DISABLED");
  return value && std::string_view{value} == "1";
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

// Helper: create a non-blocking pipe and wrap each end in an `os_file`.
std::pair<os_file, os_file> make_nb_pipe() {
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
    throw std::system_error(errno, std::generic_category(), "pipe2");
  return {os_file{fds[0]}, os_file{fds[1]}};
}

// Helper: create a blocking pipe and wrap each end in an `os_file`.
std::pair<os_file, os_file> make_blocking_pipe() {
  int fds[2];
  if (::pipe2(fds, O_CLOEXEC) != 0)
    throw std::system_error(errno, std::generic_category(), "pipe2");
  return {os_file{fds[0]}, os_file{fds[1]}};
}

#pragma region Lifecycle

TEST_CASE("Lifecycle", "[OsFile]") {
  // Default-constructed file is invalid.
  if (true) {
    os_file f;
    CHECK_FALSE(f.is_open());
    CHECK_FALSE(static_cast<bool>(f));
    CHECK(f.handle() == os_file::invalid_file_handle);
    CHECK_FALSE(f.close());
  }

  // An adopted file handle is open; closing it twice is idempotent.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    CHECK(reader.is_open());
    CHECK(static_cast<bool>(reader));
    CHECK(reader.handle() != os_file::invalid_file_handle);
    CHECK(reader.close());
    CHECK_FALSE(reader.is_open());
    CHECK_FALSE(reader.close());
  }

  // Destructor closes an open file (no crash or leak).
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    (void)writer;
  }
}

#pragma endregion
#pragma region Move

TEST_CASE("Move", "[OsFile]") {
  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.handle();
    os_file moved{std::move(reader)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(reader.is_open());
    CHECK(moved.is_open());
    CHECK(moved.handle() == h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    auto [reader_a, writer_a] = make_nb_pipe();
    auto [reader_b, writer_b] = make_nb_pipe();
    const auto h = reader_a.handle();
    reader_b = std::move(reader_a);
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(reader_a.is_open());
    CHECK(reader_b.is_open());
    CHECK(reader_b.handle() == h);
  }

  // Self-assignment is a no-op.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.handle();
    auto* p = &reader;
    reader = std::move(*p);
    CHECK(reader.is_open());
    CHECK(reader.handle() == h);
  }
}

#pragma endregion
#pragma region ReleaseFlags

TEST_CASE("ReleaseFlags", "[OsFile]") {
  // `release` yields the handle without closing it; file becomes invalid.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.release();
    CHECK(h != os_file::invalid_file_handle);
    CHECK_FALSE(reader.is_open());
    ::close(h);
  }

  // Flag helpers round-trip non-blocking mode through `fcntl`.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    auto flags = reader.get_flags();
    CHECK(flags.has_value());
    CHECK(bitmask::has(*flags, o_flags::nonblock));

    CHECK(reader.set_nonblocking(false));
    flags = reader.get_flags();
    CHECK(flags.has_value());
    CHECK_FALSE(bitmask::has(*flags, o_flags::nonblock));

    CHECK(reader.set_nonblocking(true));
    flags = reader.get_flags();
    CHECK(flags.has_value());
    CHECK(bitmask::has(*flags, o_flags::nonblock));
  }
}

#pragma endregion
#pragma region WriteRead

TEST_CASE("WriteRead", "[OsFile]") {
  auto [reader, writer] = make_nb_pipe();

  // A small write drains fully and the read side sees the same bytes.
  auto msg = std::string_view{"hello"};
  CHECK(writer.write(msg));
  CHECK(msg.empty());

  std::string buf;
  no_zero::enlarge_to(buf, 16);
  CHECK(reader.read(buf));
  CHECK(buf == "hello");

  // An empty non-blocking read is a soft failure: success with no bytes read.
  no_zero::enlarge_to(buf, 16);
  CHECK(reader.read(buf));
  CHECK(buf.empty());
  CHECK(errno == EAGAIN);

  // EOF leaves the caller's buffer unchanged and returns false.
  CHECK(writer.close());
  buf = "sentinel";
  CHECK_FALSE(reader.read(buf));
  CHECK(buf == "sentinel");
}

#pragma endregion
#pragma region WriteAllReadExact

TEST_CASE("WriteAllReadExact", "[OsFile]") {
  // write_all sends all bytes; read_exact receives exactly that many.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    const std::string_view msg = "hello, world";
    CHECK(writer.write_all(msg));

    std::string buf(msg.size(), '\0');
    CHECK(reader.read_exact(buf));
    CHECK(buf == msg);
  }

  // read_exact on EOF before the buffer is filled trims `data` to the
  // bytes received and returns false.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    CHECK(writer.write_all(std::string_view{"hi"}));
    CHECK(writer.close());

    std::string buf(8, '\0');
    CHECK_FALSE(reader.read_exact(buf));
    CHECK(buf == "hi");
  }

  // Empty write_all and read_exact are no-ops that return true.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    CHECK(writer.write_all(std::string_view{}));
    std::string buf;
    CHECK(reader.read_exact(buf));
  }
}

#pragma endregion
#pragma region MsgFlagsString

TEST_CASE("MsgFlagsString", "[OsFile]") {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  // `none` (value 0) has no bit name and prints as "0x00000000".
  using namespace corvid::strings;
  using F = msg_flags;
  if (true) {
    CHECK(enum_as_string(F{}) == "0x00000000");
    CHECK(enum_as_string(F::oob) == "oob");
    CHECK(enum_as_string(F::peek) == "peek");
    CHECK(enum_as_string(F::nosignal) == "nosignal");
    CHECK(enum_as_string(F::cloexec) == "cloexec");
  }
  if (true) {
    // Higher bits print first.
    CHECK(enum_as_string(F::dontwait | F::peek) == "dontwait + peek");
  }
  if (true) {
    constexpr F bad{0x00200000}; // bit 21, above all named flags
    CHECK(parse_enum("oob", bad) == F::oob);
    CHECK(parse_enum("nosignal", bad) == F::nosignal);
    CHECK(parse_enum("cloexec", bad) == F::cloexec);
    CHECK(parse_enum("dontwait + peek", bad) == (F::dontwait | F::peek));
  }
}

#pragma endregion
#pragma region ErrnoCodeString

TEST_CASE("ErrnoCodeString", "[OsFile]") {
  // Sequence enum: named values 0 ("ok") through 133 ("hwpoison").
  using namespace corvid::strings;
  using EC = filesys::errno_code;
  if (true) {
    CHECK(enum_as_string(EC::ok) == "ok");
    CHECK(enum_as_string(EC::noent) == "noent");
    CHECK(enum_as_string(EC::again) == "again");
    // `wouldblock` is an alias for `again` (both have value 11); only one
    // name.
    CHECK(enum_as_string(EC::wouldblock) == "again");
    CHECK(enum_as_string(EC::hwpoison) == "hwpoison");
  }
  if (true) {
    // Out-of-range values print as their numeric value.
    CHECK(enum_as_string(EC{-1}) == "-1");
    CHECK(enum_as_string(EC{134}) == "134");
  }
  if (true) {
    // `enum_as_view` returns "" for out-of-range or unnamed values.
    CHECK(enums::sequence::enum_as_view(EC::ok) == "ok");
    CHECK(enums::sequence::enum_as_view(EC::noent) == "noent");
    CHECK(enums::sequence::enum_as_view(EC{-1}) == "");
    CHECK(enums::sequence::enum_as_view(EC{134}) == "");
  }
  if (true) {
    constexpr EC bad{-1};
    CHECK(parse_enum("ok", bad) == EC::ok);
    CHECK(parse_enum("noent", bad) == EC::noent);
    CHECK(parse_enum("again", bad) == EC::again);
    CHECK(parse_enum("hwpoison", bad) == EC::hwpoison);
  }
}

#pragma endregion
#pragma region FcntlOpsString

TEST_CASE("FcntlOpsString", "[OsFile]") {
  // Sequence enum: named values 0 ("dupfd") through 16 ("getownex").
  using namespace corvid::strings;
  using FO = filesys::fcntl_ops;
  if (true) {
    CHECK(enum_as_string(FO::dupfd) == "dupfd");
    CHECK(enum_as_string(FO::getfd) == "getfd");
    CHECK(enum_as_string(FO::setfl) == "setfl");
    CHECK(enum_as_string(FO::getownex) == "getownex");
  }
  if (true) {
    // Out-of-range values (including the non-contiguous `dupfd_cloexec`) print
    // as their numeric value.
    CHECK(enum_as_string(FO{-1}) == "-1");
    CHECK(enum_as_string(FO{17}) == "17");
    CHECK(enum_as_string(FO::dupfd_cloexec) == "1030");
  }
  if (true) {
    constexpr FO bad{-1};
    CHECK(parse_enum("dupfd", bad) == FO::dupfd);
    CHECK(parse_enum("setfl", bad) == FO::setfl);
    CHECK(parse_enum("getownex", bad) == FO::getownex);
  }
}

#pragma endregion
#pragma region MmapProtString

TEST_CASE("MmapProtString", "[OsFile]") {
  // Bitmask enum: exec(4) > write(2) > read(1); none(0) has no bit name.
  using namespace corvid::strings;
  using P = mmap_prot;
  if (true) {
    CHECK(enum_as_string(P::none) == "0x00000000");
    CHECK(enum_as_string(P::read) == "read");
    CHECK(enum_as_string(P::write) == "write");
    CHECK(enum_as_string(P::exec) == "exec");
    CHECK(enum_as_string(P::exec | P::read) == "exec + read");
  }
  if (true) {
    constexpr P bad{};
    CHECK(parse_enum("read", bad) == P::read);
    CHECK(parse_enum("write", bad) == P::write);
    CHECK(parse_enum("exec", bad) == P::exec);
    CHECK(parse_enum("exec + read", bad) == (P::exec | P::read));
  }
}

#pragma endregion
#pragma region MmapAdviceString

TEST_CASE("MmapAdviceString", "[OsFile]") {
  // Sequence enum: values 0-4 and 8-25 named; 5-7 are gaps; 26+ out of range.
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
    // Gap values 5-7 print numerically; hwpoison=100 and -1 are out of range.
    CHECK(enum_as_string(MA{5}) == "5");
    CHECK(enum_as_string(MA{26}) == "26");
    CHECK(enum_as_string(MA::hwpoison) == "100");
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
  }
}

#pragma endregion
#pragma region MmapMaskString

TEST_CASE("MmapMaskString", "[OsFile]") {
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
// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
