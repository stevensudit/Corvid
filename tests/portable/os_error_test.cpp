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

#include "corvid/filesys/os_error.h"

#include "catch2_main.h"

#include <format>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("os_error basics") {
  // Default holds `ok`, which is to say, no error.
  constexpr os_error none;
  static_assert(none.ok());
  CHECK(none.code() == os_error::code_t::ok);

  // `ok` is not an error at all, so it is neither soft nor hard.
  CHECK_FALSE(none.is_soft_error());
  CHECK_FALSE(none.is_hard_error());

  // Adopt a code value. `intr` and `wouldblock` are soft on every platform,
  // while `acces` is hard.
  const os_error err{os_error::code_t::intr};
  CHECK_FALSE(err.ok());
  CHECK(err.is_soft_error());
  CHECK_FALSE(err.is_hard_error());
  CHECK_FALSE(os_error{os_error::code_t::wouldblock}.is_hard_error());
  CHECK(os_error{os_error::code_t::acces}.is_hard_error());
  CHECK_FALSE(os_error{os_error::code_t::acces}.is_soft_error());

  // The raw value round-trips.
  CHECK(os_error{err.raw()} == err);

  // The message is human-readable text.
  CHECK_FALSE(err.message().empty());
}

TEST_CASE("os_error capture") {
  // `last` reflects the most recent failing call.
#ifdef _WIN32
  CHECK(::SetEvent(nullptr) == 0);
  CHECK(os_error::last().code() == os_error::code_t::invalid_handle);
#else
  CHECK(::close(-1) == -1);
  CHECK(os_error::last().code() == os_error::code_t::badf);
#endif
}

TEST_CASE("os_error throw_if_error") {
  // `ok` does not throw.
  CHECK_NOTHROW(os_error{}.throw_if_error());
  CHECK_NOTHROW(os_error{}.throw_if_error("unused"));

  // Anything else throws a `system_error` carrying the code in the system
  // category, with `what_arg` as the message prefix when given.
  const os_error err{os_error::code_t::acces};
  CHECK_THROWS_AS(err.throw_if_error(), std::system_error);
  try {
    err.throw_if_error("open");
    FAIL("did not throw");
  }
  catch (const std::system_error& e) {
    CHECK(e.code().value() == static_cast<int>(err.raw()));
    CHECK(e.code().category() == std::system_category());
    CHECK(std::string_view{e.what()}.starts_with("open"));
    CHECK(std::string_view{e.what()}.contains(err.message()));
  }
  try {
    err.throw_if_error();
    FAIL("did not throw");
  }
  catch (const std::system_error& e) {
    CHECK_FALSE(std::string_view{e.what()}.starts_with(':'));
    CHECK(std::string_view{e.what()}.contains(err.message()));
  }
}

TEST_CASE("os_error formatting") {
  // Named codes format as their name. These names exist on every platform,
  // which on Windows also pins the sparse registration's value alignment.
  CHECK(std::format("{}", os_error{os_error::code_t::intr}) == "intr");
  CHECK(
      std::format("{}", os_error{os_error::code_t::connreset}) == "connreset");
  CHECK(std::format("{}", os_error{os_error::code_t::hostunreach}) ==
        "hostunreach");
}

// NOLINTEND(readability-function-cognitive-complexity)
