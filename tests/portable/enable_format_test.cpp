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

#include <format>
#include <map>
#include <string>
#include <unordered_map>
#include <variant>

#include "corvid/strings/enable_format.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::strings::format_wrapping;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region EnableFormat

TEST_CASE("EnableFormat", "[EnableFormat]") {
  const std::map<std::string, std::string> cities{{"NYC", "New York City"},
      {"LA", "Los Angeles"}};

  // Literal keys, looked up at format time; one arg serves many fields.
  if (true) {
    CHECK(std::format("{0:NYC}", enable_format{cities}) == "New York City");
    CHECK(std::format("{0:NYC} & {0:LA}", enable_format{cities}) ==
          "New York City & Los Angeles");
  }
  // A nested spec after the key applies to the looked-up value.
  if (true) {
    const std::map<std::string, double> temps{{"NYC", 12.3456}};
    CHECK(std::format("{0:NYC:.2f}", enable_format{temps}) == "12.35");
    CHECK(std::format("{0:NYC:>5.1f}", enable_format{temps}) == " 12.3");
    CHECK(std::format("{0:LA:*>7}", enable_format{cities}) == "Los Angeles");
    CHECK(std::format("{0:NYC:.7}", enable_format{cities}) == "New Yor");
    // Dynamic width in the nested spec works with a manual arg id.
    CHECK(std::format("{0:NYC:>{1}.1f}", enable_format{temps}, 6) == "  12.3");
  }
  // A missing key throws unless a missing-value stand-in was provided, in
  // which case the lookup reports it as if all was well.
  if (true) {
    CHECK_THROWS_AS(std::format("{0:SF}", enable_format{cities}),
        std::format_error);
    CHECK(
        std::format("{0:SF}", enable_format{cities, "unknown"}) == "unknown");
    CHECK(std::format("{0:LA}", enable_format{cities, "unknown"}) ==
          "Los Angeles");
  }
  // Dynamic keys: `{n}` (or `{}` under automatic numbering) reads the key
  // from another string-like arg.
  if (true) {
    const auto selected = "NYC"s;
    CHECK(std::format("{1} = {0:{1}}", enable_format{cities}, selected) ==
          "NYC = New York City");
    CHECK(std::format("{:{}}", enable_format{cities}, "LA") == "Los Angeles");
    // A non-string key arg is rejected at format time.
    CHECK_THROWS_AS(std::format("{0:{1}}", enable_format{cities}, 42),
        std::format_error);
  }
  // A variant mapped type formats its active alternative, with the nested
  // spec applied to it.
  if (true) {
    using field = std::variant<int, double, std::string>;
    const std::map<std::string, field> rec{{"city", "NYC"s},
        {"temperature", 12.3456}, {"count", 3}};
    CHECK(std::format("{0:city}: {0:temperature:.2f}", enable_format{rec}) ==
          "NYC: 12.35");
    CHECK(std::format("{0:count:03}", enable_format{rec}) == "003");
    // The stand-in coerces into the variant, here as its string alternative.
    CHECK(std::format("{0:humidity}", enable_format{rec, "n/a"}) == "n/a");
  }
  // The variant wrapper stands alone: the whole spec applies to the active
  // alternative.
  if (true) {
    using field = std::variant<int, double, std::string>;
    const field fi{42};
    const field fd{2.5};
    const field fs{"hi"s};
    CHECK(std::format("{}", enable_format{fi}) == "42");
    CHECK(std::format("{:.1f}", enable_format{fd}) == "2.5");
    CHECK(std::format("{:>4}", enable_format{fs}) == "  hi");
    // A spec the active alternative rejects throws at format time.
    CHECK_THROWS_AS(std::format("{:.1f}", enable_format{fs}),
        std::format_error);
  }
  // A variant can hold a keyed collection, but it then formats whole through
  // the std map formatter; key lookup does not reach through a variant. To
  // look up by key, extract the map with std::get and wrap that.
  if (true) {
    using map_t = std::map<std::string, int>;
    const std::variant<int, map_t> v{map_t{{"a", 1}, {"b", 2}}};
    CHECK(std::format("{}", enable_format{v}) == R"({"a": 1, "b": 2})");
    CHECK_THROWS_AS(std::format("{0:a}", enable_format{v}), std::format_error);
    CHECK(std::format("{0:b}", enable_format{std::get<map_t>(v)}) == "2");
  }
  // Multi-keyed collections format the whole equal range through the std
  // range formatter, even for a single value.
  if (true) {
    const std::multimap<std::string, std::string> tags{{"city", "NYC"},
        {"city", "LA"}, {"state", "NY"}};
    CHECK(std::format("{0:city}", enable_format{tags}) == R"(["NYC", "LA"])");
    CHECK(std::format("{0:state}", enable_format{tags}) == R"(["NY"])");
    CHECK_THROWS_AS(std::format("{0:zip}", enable_format{tags}),
        std::format_error);
    CHECK(
        std::format("{0:zip}", enable_format{tags, "none"}) == R"(["none"])");
  }
  // Multi plus variant: each value routes through the variant wrapper, and
  // string alternatives debug-quote the way plain strings do in ranges.
  if (true) {
    using field = std::variant<int, double, std::string>;
    const std::multimap<std::string, field> readings{{"m", "abc"s}, {"m", 3},
        {"m", 2.5}};
    CHECK(
        std::format("{0:m}", enable_format{readings}) == R"(["abc", 3, 2.5])");
    CHECK(
        std::format("{0:x}", enable_format{readings, "n/a"}) == R"(["n/a"])");
  }
  // Hashed and transparent-comparator collections work the same; only the
  // lookup mechanics differ.
  if (true) {
    const std::unordered_map<std::string, int> counts{{"a", 1}, {"b", 2}};
    CHECK(std::format("{0:b}", enable_format{counts}) == "2");
    const std::map<std::string, int, std::less<>> hetero{{"x", 9}};
    CHECK(std::format("{0:x}", enable_format{hetero}) == "9");
  }
  // Wide code units follow the key's code unit.
  if (true) {
    const std::map<std::wstring, std::wstring> wcities{{L"NYC", L"New York"}};
    CHECK(std::format(L"{0:NYC}", enable_format{wcities}) == L"New York");
  }
  // Grammar errors surface as format_error through vformat (the compile-time
  // path would reject them at compile time).
  if (true) {
    const enable_format d{cities};
    CHECK_THROWS_AS(std::vformat("{0:}", std::make_format_args(d)),
        std::format_error);
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
