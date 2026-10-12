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
#include <cstdint>
#include <iterator>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "corvid/containers.h"
#include "corvid/enums.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;

// A registered sequence enum, to index an arena by a strong type.
enum class word_id : uint32_t {};
consteval auto corvid_enum_spec(word_id*) {
  return corvid::enums::sequence::make_sequence_enum_spec<word_id, "">();
}

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(readability-function-size)

#pragma region Append

TEST_CASE("Append", "[StringArena]") {
  string_arena<> arena;
  CHECK(arena.empty());
  CHECK(arena.size() == 0U);
  CHECK(arena.begin() == arena.end());

  CHECK(arena.append("alpha") == 0U);
  CHECK(arena.append("") == 1U);
  CHECK(arena.append("gamma") == 2U);
  CHECK_FALSE(arena.empty());
  CHECK(arena.size() == 3U);
  CHECK(arena[0] == "alpha");
  CHECK(arena[1] == "");
  CHECK(arena[2] == "gamma");

  // Views are re-fetched after an append that may reallocate the buffer.
  const std::string long_text(1024, 'x');
  CHECK(arena.append(long_text) == 3U);
  CHECK(arena[0] == "alpha");
  CHECK(arena[3] == long_text);
  CHECK(arena.max_id() == 3U);
}

#pragma endregion
#pragma region Iteration

TEST_CASE("Iteration", "[StringArena]") {
  u8string_arena<> arena;
  arena.reserve(3, 16);
  (void)arena.append(u8"one");
  (void)arena.append(u8"two");
  (void)arena.append(u8"three");

  std::vector<std::u8string_view> seen;
  for (const auto text : arena) seen.push_back(text);
  CHECK(seen == std::vector{u8"one"sv, u8"two"sv, u8"three"sv});
  CHECK(std::ranges::distance(arena) == 3);

  auto it = arena.end();
  --it;
  CHECK(*it == u8"three");
  CHECK(*it++ == u8"three");
  CHECK(it == arena.end());
}

#pragma endregion
#pragma region StrongIndex

TEST_CASE("StrongIndex", "[StringArena]") {
  string_arena<word_id> arena;
  const auto first = arena.append("first");
  const auto second = arena.append("second");
  static_assert(std::is_same_v<decltype(first), const word_id>);
  CHECK(first == word_id{0});
  CHECK(second == word_id{1});
  CHECK(arena[first] == "first");
  CHECK(arena[second] == "second");
  CHECK(arena.size() == 2U);
  CHECK(arena.max_id() == second);
}

#pragma endregion

// NOLINTEND(readability-function-size)
// NOLINTEND(readability-function-cognitive-complexity)
