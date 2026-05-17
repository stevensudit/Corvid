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

#include "../corvid/proto.h"
#include "../corvid/concurrency/jthread_stoppable_sleep.h"

#include <charconv>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <unistd.h>

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

using namespace corvid;
using namespace std::string_literals;
using namespace std::chrono_literals;

bool is_codex() {
  const char* value = std::getenv("CODEX_SANDBOX_NETWORK_DISABLED");
  return value && std::string_view{value} == "1";
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(bugprone-unchecked-optional-access)

// `http_head_codec` unit tests.

// Verify that a well-formed HTTP/1.1 GET request is parsed correctly.
#pragma region Complete

// ASCII and fully contained multibyte sequences leave the validator complete.
TEST_CASE("Complete", "[Utf8Checker]") {
  utf8_checker v;
  CHECK(v.state() == utf8_checker::validation::complete);
  CHECK(v.validate("hello") == utf8_checker::validation::complete);
  CHECK(v.validate("\xE2\x82\xAC") == utf8_checker::validation::complete);
  CHECK(v.is_complete());
}

#pragma endregion
#pragma region IncompleteThenComplete

// A split multibyte sequence transitions to incomplete, then back to complete.
TEST_CASE("IncompleteThenComplete", "[Utf8Checker]") {
  utf8_checker v;
  CHECK(v.validate("\xF0\x9F") == utf8_checker::validation::incomplete);
  CHECK(v.is_incomplete());
  CHECK(v.validate("\x98\x80") == utf8_checker::validation::complete);
  CHECK(v.is_complete());
}

#pragma endregion
#pragma region InvalidSticky

// Invalid leading and continuation bytes move the validator to sticky invalid.
TEST_CASE("InvalidSticky", "[Utf8Checker]") {
  utf8_checker v;
  CHECK(v.validate("\x80") == utf8_checker::validation::failed);
  CHECK(v.is_failed());
  CHECK(v.validate("abc") == utf8_checker::validation::failed);
  CHECK(v.is_failed());
}

#pragma endregion
#pragma region RejectsInvalidSequences

// Reject overlongs, surrogate code points, and code points past U+10FFFF.
TEST_CASE("RejectsInvalidSequences", "[Utf8Checker]") {
  utf8_checker v;
  CHECK(v.validate("\xE0\x80\x80") == utf8_checker::validation::failed);

  v.reset();
  CHECK(v.validate("\xED\xA0\x80") == utf8_checker::validation::failed);

  v.reset();
  CHECK(v.validate("\xF4\x90\x80\x80") == utf8_checker::validation::failed);
}

#pragma endregion

// NOLINTEND(bugprone-unchecked-optional-access)
// NOLINTEND(readability-function-cognitive-complexity)
