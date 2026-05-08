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
#include "../corvid/strings/enum_conversion.h"
#include "minitest.h"

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

void OsFile_Lifecycle() {
  // Default-constructed file is invalid.
  if (true) {
    os_file f;
    EXPECT_FALSE(f.is_open());
    EXPECT_FALSE(static_cast<bool>(f));
    EXPECT_EQ(f.handle(), os_file::invalid_file_handle);
    EXPECT_FALSE(f.close());
  }

  // An adopted file handle is open; closing it twice is idempotent.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    EXPECT_TRUE(reader.is_open());
    EXPECT_TRUE(static_cast<bool>(reader));
    EXPECT_NE(reader.handle(), os_file::invalid_file_handle);
    EXPECT_TRUE(reader.close());
    EXPECT_FALSE(reader.is_open());
    EXPECT_FALSE(reader.close());
  }

  // Destructor closes an open file (no crash or leak).
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    (void)writer;
  }
}

#pragma endregion
#pragma region Move

void OsFile_Move() {
  // Move constructor transfers ownership; source becomes invalid.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.handle();
    os_file moved{std::move(reader)};
    EXPECT_FALSE(reader.is_open());
    EXPECT_TRUE(moved.is_open());
    EXPECT_EQ(moved.handle(), h);
  }

  // Move assignment closes the destination and transfers the source.
  if (true) {
    auto [reader_a, writer_a] = make_nb_pipe();
    auto [reader_b, writer_b] = make_nb_pipe();
    const auto h = reader_a.handle();
    reader_b = std::move(reader_a);
    EXPECT_FALSE(reader_a.is_open());
    EXPECT_TRUE(reader_b.is_open());
    EXPECT_EQ(reader_b.handle(), h);
  }

  // Self-assignment is a no-op.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.handle();
    auto* p = &reader;
    reader = std::move(*p);
    EXPECT_TRUE(reader.is_open());
    EXPECT_EQ(reader.handle(), h);
  }
}

#pragma endregion
#pragma region ReleaseFlags

void OsFile_ReleaseFlags() {
  // `release()` yields the handle without closing it; file becomes invalid.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    const auto h = reader.release();
    EXPECT_NE(h, os_file::invalid_file_handle);
    EXPECT_FALSE(reader.is_open());
    ::close(h);
  }

  // Flag helpers round-trip non-blocking mode through `fcntl`.
  if (true) {
    auto [reader, writer] = make_nb_pipe();
    auto flags = reader.get_flags();
    EXPECT_TRUE(flags.has_value());
    EXPECT_TRUE(bitmask::has(*flags, o_flags::nonblock));

    EXPECT_TRUE(reader.set_nonblocking(false));
    flags = reader.get_flags();
    EXPECT_TRUE(flags.has_value());
    EXPECT_FALSE(bitmask::has(*flags, o_flags::nonblock));

    EXPECT_TRUE(reader.set_nonblocking(true));
    flags = reader.get_flags();
    EXPECT_TRUE(flags.has_value());
    EXPECT_TRUE(bitmask::has(*flags, o_flags::nonblock));
  }
}

#pragma endregion
#pragma region WriteRead

void OsFile_WriteRead() {
  auto [reader, writer] = make_nb_pipe();

  // A small write drains fully and the read side sees the same bytes.
  auto msg = std::string_view{"hello"};
  EXPECT_TRUE(writer.write(msg));
  EXPECT_TRUE(msg.empty());

  std::string buf;
  no_zero::enlarge_to(buf, 16);
  EXPECT_TRUE(reader.read(buf));
  EXPECT_EQ(buf, "hello");

  // An empty non-blocking read is a soft failure: success with no bytes read.
  no_zero::enlarge_to(buf, 16);
  EXPECT_TRUE(reader.read(buf));
  EXPECT_TRUE(buf.empty());
  EXPECT_EQ(errno, EAGAIN);

  // EOF leaves the caller's buffer unchanged and returns false.
  EXPECT_TRUE(writer.close());
  buf = "sentinel";
  EXPECT_FALSE(reader.read(buf));
  EXPECT_EQ(buf, "sentinel");
}

#pragma endregion
#pragma region WriteAllReadExact

void OsFile_WriteAllReadExact() {
  // write_all sends all bytes; read_exact receives exactly that many.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    const std::string_view msg = "hello, world";
    EXPECT_TRUE(writer.write_all(msg));

    std::string buf(msg.size(), '\0');
    EXPECT_TRUE(reader.read_exact(buf));
    EXPECT_EQ(buf, msg);
  }

  // read_exact on EOF before the buffer is filled trims `data` to the
  // bytes received and returns false.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    EXPECT_TRUE(writer.write_all(std::string_view{"hi"}));
    EXPECT_TRUE(writer.close());

    std::string buf(8, '\0');
    EXPECT_FALSE(reader.read_exact(buf));
    EXPECT_EQ(buf, "hi");
  }

  // Empty write_all and read_exact are no-ops that return true.
  if (true) {
    auto [reader, writer] = make_blocking_pipe();
    EXPECT_TRUE(writer.write_all(std::string_view{}));
    std::string buf;
    EXPECT_TRUE(reader.read_exact(buf));
  }
}

#pragma endregion
#pragma region MsgFlagsString

void OsFile_MsgFlagsString() {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  // `none` (value 0) has no bit name and prints as "0x00000000".
  using namespace corvid::strings;
  using F = msg_flags;
  if (true) {
    EXPECT_EQ(enum_as_string(F{}), "0x00000000");
    EXPECT_EQ(enum_as_string(F::oob), "oob");
    EXPECT_EQ(enum_as_string(F::peek), "peek");
    EXPECT_EQ(enum_as_string(F::nosignal), "nosignal");
    EXPECT_EQ(enum_as_string(F::cloexec), "cloexec");
  }
  if (true) {
    // Higher bits print first.
    EXPECT_EQ(enum_as_string(F::dontwait | F::peek), "dontwait + peek");
  }
  if (true) {
    constexpr F bad{0x00200000}; // bit 21, above all named flags
    EXPECT_EQ(parse_enum("oob", bad), F::oob);
    EXPECT_EQ(parse_enum("nosignal", bad), F::nosignal);
    EXPECT_EQ(parse_enum("cloexec", bad), F::cloexec);
    EXPECT_EQ(parse_enum("dontwait + peek", bad), F::dontwait | F::peek);
  }
}

#pragma endregion
#pragma region ErrnoCodeString

void OsFile_ErrnoCodeString() {
  // Sequence enum: named values 0 ("ok") through 133 ("hwpoison").
  using namespace corvid::strings;
  using EC = filesys::errno_code;
  if (true) {
    EXPECT_EQ(enum_as_string(EC::ok), "ok");
    EXPECT_EQ(enum_as_string(EC::noent), "noent");
    EXPECT_EQ(enum_as_string(EC::again), "again");
    // `wouldblock` is an alias for `again` (both have value 11); only one
    // name.
    EXPECT_EQ(enum_as_string(EC::wouldblock), "again");
    EXPECT_EQ(enum_as_string(EC::hwpoison), "hwpoison");
  }
  if (true) {
    // Out-of-range values print as their numeric value.
    EXPECT_EQ(enum_as_string(EC{-1}), "-1");
    EXPECT_EQ(enum_as_string(EC{134}), "134");
  }
  if (true) {
    // `enum_as_view` returns "(unknown)" for out-of-range or unnamed values.
    EXPECT_EQ(enums::sequence::enum_as_view(EC::ok), "ok");
    EXPECT_EQ(enums::sequence::enum_as_view(EC::noent), "noent");
    EXPECT_EQ(enums::sequence::enum_as_view(EC{-1}), "(unknown)");
    EXPECT_EQ(enums::sequence::enum_as_view(EC{134}), "(unknown)");
  }
  if (true) {
    constexpr EC bad{-1};
    EXPECT_EQ(parse_enum("ok", bad), EC::ok);
    EXPECT_EQ(parse_enum("noent", bad), EC::noent);
    EXPECT_EQ(parse_enum("again", bad), EC::again);
    EXPECT_EQ(parse_enum("hwpoison", bad), EC::hwpoison);
  }
}

#pragma endregion
#pragma region FcntlOpsString

void OsFile_FcntlOpsString() {
  // Sequence enum: named values 0 ("dupfd") through 16 ("getownex").
  using namespace corvid::strings;
  using FO = filesys::fcntl_ops;
  if (true) {
    EXPECT_EQ(enum_as_string(FO::dupfd), "dupfd");
    EXPECT_EQ(enum_as_string(FO::getfd), "getfd");
    EXPECT_EQ(enum_as_string(FO::setfl), "setfl");
    EXPECT_EQ(enum_as_string(FO::getownex), "getownex");
  }
  if (true) {
    // Out-of-range values (including the non-contiguous `dupfd_cloexec`) print
    // as their numeric value.
    EXPECT_EQ(enum_as_string(FO{-1}), "-1");
    EXPECT_EQ(enum_as_string(FO{17}), "17");
    EXPECT_EQ(enum_as_string(FO::dupfd_cloexec), "1030");
  }
  if (true) {
    constexpr FO bad{-1};
    EXPECT_EQ(parse_enum("dupfd", bad), FO::dupfd);
    EXPECT_EQ(parse_enum("setfl", bad), FO::setfl);
    EXPECT_EQ(parse_enum("getownex", bad), FO::getownex);
  }
}

#pragma endregion
#pragma region MmapProtString

void OsFile_MmapProtString() {
  // Bitmask enum: exec(4) > write(2) > read(1); none(0) has no bit name.
  using namespace corvid::strings;
  using P = mmap_prot;
  if (true) {
    EXPECT_EQ(enum_as_string(P::none), "0x00000000");
    EXPECT_EQ(enum_as_string(P::read), "read");
    EXPECT_EQ(enum_as_string(P::write), "write");
    EXPECT_EQ(enum_as_string(P::exec), "exec");
    EXPECT_EQ(enum_as_string(P::exec | P::read), "exec + read");
  }
  if (true) {
    constexpr P bad{};
    EXPECT_EQ(parse_enum("read", bad), P::read);
    EXPECT_EQ(parse_enum("write", bad), P::write);
    EXPECT_EQ(parse_enum("exec", bad), P::exec);
    EXPECT_EQ(parse_enum("exec + read", bad), P::exec | P::read);
  }
}

#pragma endregion
#pragma region MmapAdviceString

void OsFile_MmapAdviceString() {
  // Sequence enum: values 0-4 and 8-25 named; 5-7 are gaps; 26+ out of range.
  using namespace corvid::strings;
  using MA = mmap_advice;
  if (true) {
    EXPECT_EQ(enum_as_string(MA::normal), "normal");
    EXPECT_EQ(enum_as_string(MA::dontneed), "dontneed");
    EXPECT_EQ(enum_as_string(MA::free), "free");
    EXPECT_EQ(enum_as_string(MA::remove), "remove");
    EXPECT_EQ(enum_as_string(MA::dontfork), "dontfork");
    EXPECT_EQ(enum_as_string(MA::dofork), "dofork");
    EXPECT_EQ(enum_as_string(MA::hugepage), "hugepage");
    EXPECT_EQ(enum_as_string(MA::wipeonfork), "wipeonfork");
    EXPECT_EQ(enum_as_string(MA::collapse), "collapse");
  }
  if (true) {
    // Gap values 5-7 print numerically; hwpoison=100 and -1 are out of range.
    EXPECT_EQ(enum_as_string(MA{5}), "5");
    EXPECT_EQ(enum_as_string(MA{26}), "26");
    EXPECT_EQ(enum_as_string(MA::hwpoison), "100");
    EXPECT_EQ(enum_as_string(MA{-1}), "-1");
    EXPECT_EQ(enums::sequence::enum_as_view(MA::normal), "normal");
    EXPECT_EQ(enums::sequence::enum_as_view(MA{5}), "(unknown)");
    EXPECT_EQ(enums::sequence::enum_as_view(MA{26}), "(unknown)");
    EXPECT_EQ(enums::sequence::enum_as_view(MA{-1}), "(unknown)");
  }
  if (true) {
    constexpr MA bad{-1};
    EXPECT_EQ(parse_enum("normal", bad), MA::normal);
    EXPECT_EQ(parse_enum("dontneed", bad), MA::dontneed);
    EXPECT_EQ(parse_enum("free", bad), MA::free);
    EXPECT_EQ(parse_enum("dofork", bad), MA::dofork);
    EXPECT_EQ(parse_enum("hugepage", bad), MA::hugepage);
    EXPECT_EQ(parse_enum("collapse", bad), MA::collapse);
  }
}

#pragma endregion
#pragma region MmapMaskString

void OsFile_MmapMaskString() {
  // Bitmask enum: named flags at bits 8 (growsdown) through 20
  // (fixed_noreplace); none=0 and multi-bit masks print as hex.
  using namespace corvid::strings;
  using M = mmap_mask;
  if (true) {
    EXPECT_EQ(enum_as_string(M::shared), "shared");
    EXPECT_EQ(enum_as_string(M::map_private), "private");
    EXPECT_EQ(enum_as_string(M::anonymous), "anonymous");
    EXPECT_EQ(enum_as_string(M::fixed), "fixed");
    EXPECT_EQ(enum_as_string(M::growsdown), "growsdown");
    EXPECT_EQ(enum_as_string(M::denywrite), "denywrite");
    EXPECT_EQ(enum_as_string(M::executable), "executable");
    EXPECT_EQ(enum_as_string(M::locked), "locked");
    EXPECT_EQ(enum_as_string(M::noreserve), "noreserve");
    EXPECT_EQ(enum_as_string(M::populate), "populate");
    EXPECT_EQ(enum_as_string(M::nonblock), "nonblock");
    EXPECT_EQ(enum_as_string(M::stack), "stack");
    EXPECT_EQ(enum_as_string(M::hugetlb), "hugetlb");
    EXPECT_EQ(enum_as_string(M::sync), "sync");
    EXPECT_EQ(enum_as_string(M::fixed_noreplace), "fixed_noreplace");
    // Higher bits print first.
    EXPECT_EQ(enum_as_string(M::hugetlb | M::growsdown),
        "hugetlb + growsdown");
  }
  if (true) {
    constexpr M bad{};
    EXPECT_EQ(parse_enum("shared", bad), M::shared);
    EXPECT_EQ(parse_enum("private", bad), M::map_private);
    EXPECT_EQ(parse_enum("anonymous", bad), M::anonymous);
    EXPECT_EQ(parse_enum("fixed", bad), M::fixed);
    EXPECT_EQ(parse_enum("growsdown", bad), M::growsdown);
    EXPECT_EQ(parse_enum("denywrite", bad), M::denywrite);
    EXPECT_EQ(parse_enum("populate", bad), M::populate);
    EXPECT_EQ(parse_enum("hugetlb", bad), M::hugetlb);
    EXPECT_EQ(parse_enum("fixed_noreplace", bad), M::fixed_noreplace);
    EXPECT_EQ(parse_enum("hugetlb + growsdown", bad),
        M::hugetlb | M::growsdown);
  }
}

#pragma endregion
MAKE_TEST_LIST(OsFile_Lifecycle, OsFile_Move, OsFile_ReleaseFlags,
    OsFile_WriteRead, OsFile_WriteAllReadExact, OsFile_MsgFlagsString,
    OsFile_ErrnoCodeString, OsFile_FcntlOpsString, OsFile_MmapProtString,
    OsFile_MmapAdviceString, OsFile_MmapMaskString);
// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
