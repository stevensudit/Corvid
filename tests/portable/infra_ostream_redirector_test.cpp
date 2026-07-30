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

#include "corvid/infra/ostream_redirector.h"

#include "catch2_main.h"

#include <iostream>
#include <sstream>
#include <type_traits>

using namespace corvid;

#pragma region OstreamRedirectorTraits

TEST_CASE("OstreamRedirectorTraits", "[OstreamRedirector]") {
  using R = ostream_redirector;
  static_assert(!std::is_copy_constructible_v<R>);
  static_assert(!std::is_copy_assignable_v<R>);
  static_assert(!std::is_move_constructible_v<R>);
  static_assert(!std::is_move_assignable_v<R>);
}

#pragma endregion
#pragma region OstreamRedirectorRestore

TEST_CASE("OstreamRedirectorRestore", "[OstreamRedirector]") {
  auto* orig = std::cout.rdbuf();
  {
    std::stringstream ss;
    {
      ostream_redirector r(std::cout, ss);
      std::cout << "abc";
      CHECK(ss.str() == "abc");
      CHECK_FALSE(std::cout.rdbuf() == orig);
    }
    CHECK(std::cout.rdbuf() == orig);
  }
}

#pragma endregion
