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

// gcc and clang predefine `unix` and `linux` as macros in the GNU dialects
// (`-std=gnu++NN`), which is what CMake selects unless `CMAKE_CXX_EXTENSIONS`
// is off. This suite builds in the ISO dialect, where they are absent, so
// they are defined by hand here, ahead of every header, to stand in for a
// consumer who builds in a GNU dialect.
#ifndef unix
#define unix 1
#endif
#ifndef linux
#define linux 1
#endif

#include "corvid/proto.h"
#include "catch2_main.h"

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region GNU dialect macros

// Compiling this file is the test: the proto headers have to parse with
// `unix` and `linux` defined as macros. The checks only confirm that no
// header undefined them along the way.
TEST_CASE("Proto headers compile with the GNU dialect macros",
    "[gnu_dialect]") {
  CHECK(unix == 1);
  CHECK(linux == 1);
}

#pragma endregion
// NOLINTEND(readability-function-cognitive-complexity)
