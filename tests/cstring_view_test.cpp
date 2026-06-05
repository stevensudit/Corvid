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
#include <map>
#include <set>
#include <span>

#include "../corvid/strings/core/cstring_view.h"
#include "../corvid/meta.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

#pragma region Construction

TEST_CASE("Construction", "[CStringViewTest]") {
  // Default-constructed string_view.
  if (true) {
    std::string_view v;
    CHECK(v.empty());
    CHECK(v.data() == nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Default-constructed cstring_view.
  if (true) {
    cstring_view v;
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct string_view on null pointer.
  if (true) {
    const char* p{};
    (void)p;
    // This doesn't even throw, as such. It's undefined, so you (probably) get
    // an access violation. We could try to test for this with a try/catch
    // block and ellipses, but it's not guaranteed to behave on all platforms.
    // * CHECK_THROWS_AS((std::string_view(p)), std::runtime_error);
  }
  // Construct cstring_view on null pointer.
  if (true) {
    // Works same as default construction.
    const char* p{};
    cstring_view v{p};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on nullptr.
  if (true) {
    // Works same as default construction.
    cstring_view v{nullptr};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on nullopt.
  if (true) {
    // Works same as default construction.
    cstring_view v{std::nullopt};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct string_view on null and 0.
  if (true) {
    // Same as default.
    const char* p{};
    // NOLINTNEXTLINE(bugprone-string-constructor)
    std::string_view v{p, 0};
    CHECK(v.empty());
    CHECK(v.data() == nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on null and 0.
  if (true) {
    // Same as default.
    const char* p{};
    cstring_view v{p, 0};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on null and a nonzero length. A null pointer always
  // yields null rather than dereferencing the bogus length.
  if (true) {
    const char* p{};
    cstring_view v{p, 5};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.c_str() != nullptr);
  }
  // Construct string_view on empty.
  if (true) {
    const char* p = "";
    std::string_view v{p};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on empty.
  if (true) {
    const char* p = "";
    cstring_view v{p};
    CHECK(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct string_view on empty and 0.
  if (true) {
    const char* p = "";
    // NOLINTNEXTLINE(bugprone-string-constructor)
    std::string_view v{p, 0};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on empty and 0.
  if (true) {
    const char* p = "";
    CHECK_THROWS_AS((cstring_view(p, 0)), std::length_error);
    cstring_view v{p, 1};
    CHECK(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct string_view on empty string.
  if (true) {
    std::string s;
    std::string_view v{s};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on empty string.
  if (true) {
    std::string s;
    cstring_view v{s};
    CHECK(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct string_view on null string_view.
  if (true) {
    std::string_view sv;
    CHECK(sv.data() == nullptr);
    std::string_view v{sv};
    CHECK(v.empty());
    CHECK(v.data() == nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on null string_view.
  if (true) {
    std::string_view sv;
    CHECK(sv.data() == nullptr);
    cstring_view v(sv);
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct string_view on empty string_view.
  if (true) {
    std::string_view sv("");
    CHECK(sv.data() != nullptr);
    std::string_view v{sv};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on empty string_view.
  if (true) {
    std::string_view svbad("");
    CHECK(svbad.data() != nullptr);
    CHECK_THROWS_AS((cstring_view(svbad)), std::length_error);
    // NOLINTNEXTLINE(bugprone-string-constructor)
    std::string_view sv{"", 1};
    cstring_view v(sv);
    CHECK(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct string_view on arbitrary string_view.
  if (true) {
    std::string_view sv("abc");
    std::string_view v{sv};
    CHECK_FALSE(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.data() != v.data() + v.size());
  }
  // Construct cstring_view on arbitrary string_view.
  if (true) {
    std::string_view svbad("abc");
    CHECK_THROWS_AS((cstring_view(svbad)), std::invalid_argument);
    // NOLINTNEXTLINE(bugprone-string-constructor)
    std::string_view sv("abc", 4);
    cstring_view v(sv);
    CHECK_FALSE(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() != v.data() + v.size());
  }
  // Construct string_view on it/end.
  if (true) {
    std::span<const char> r{"abc"};
    std::string_view v{r.begin(), r.end()};
    CHECK_FALSE(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.data() != v.data() + v.size());
    CHECK(r.size() == 4U);
    CHECK(v.size() == 4U);
  }
  // Construct cstring_view on it/end
  if (true) {
    std::span<const char> r{"abc"};
    cstring_view v(r.begin(), r.end());
    CHECK_FALSE(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.c_str() != nullptr);
    CHECK(v.data() != v.data() + v.size());
    CHECK(r.size() == 4U);
    CHECK(v.size() == 3U);
  }
  // Construct using UDL.
  if (true) {
    auto a = ""_csv;
    CHECK(a.empty());
    CHECK_FALSE(a.null());
    auto b = "abc"_csv;
    CHECK(b.size() == 3U);
    // Embedded zeros are permitted.
    auto c = "abc\0def"_csv;
    CHECK(c.size() == 7U);
    auto d = cstring_view(c.c_str());
    CHECK(d.size() == 3U);
    auto e = 0_csv;
    CHECK(e.null());
  }
}

#pragma endregion
#pragma region Optional

TEST_CASE("Optional", "[CStringViewTest]") {
  if (true) {
    std::optional<std::string> opt;
    cstring_view csv{opt};
    CHECK(csv.null());
    opt = "test";
    csv = opt;
    CHECK(csv == "test");
    // * cstring_view bad{std::optional<int>{}};
  }
}

#pragma endregion

auto accept_string_view(std::string_view v) { return v; }
auto accept_cstring_view(cstring_view v) { return v; }

void accept_string_view_ref(std::string_view& v) { v = "changed"; }
void accept_string_view_rref(std::string_view&& v) { v = "changed"; }

std::string_view accept_overloaded(std::string_view) { return "sv"; }
std::string_view accept_overloaded(cstring_view) { return "csv"; }

#pragma region Cast

TEST_CASE("Cast", "[CStringViewTest]") {
  // Casts "up" implicitly.
  CHECK("abc"_csv == "abc"_csv);
  CHECK(accept_string_view("abc"sv) == "abc");
  auto abc_str = "abc"s;
  CHECK(accept_string_view(abc_str) == "abc");
  CHECK(accept_string_view("abc"_csv) == "abc");

  CHECK(accept_cstring_view("abc"_csv) == "abc");
  CHECK(accept_cstring_view(abc_str) == "abc");

  // But not down.
  // * CHECK(accept_cstring_view("abc"sv) == "abc");

  // Handles overloading just fine.
  CHECK(accept_overloaded("abc"sv) == "sv");
  CHECK(accept_overloaded("abc"_csv) == "csv");

  // But not this ambiguity.
  // Need to either cast here or add a specific overload.
  // * CHECK(accept_overloaded("abc"s) == "sv");

  // It's not a std::string_view but can be converted to one.
  CHECK_FALSE((std::is_same_v<cstring_view, std::string_view>));
  CHECK(StringViewConvertible<cstring_view>);

  auto csv = "abc"_csv;
  std::string_view sv = csv;
  accept_string_view_ref(sv);
  // But not this, so we can maintain our invariant.
  // * accept_string_view_ref(csv);
  // NOLINTNEXTLINE(performance-move-const-arg)
  accept_string_view_rref(std::move(sv));
  // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
  CHECK(sv == "changed");
  // Weird but correct, and invariant is maintained.
  accept_string_view_rref(csv);
  CHECK(csv == "abc");
  // Same thing happens with `std::string`. If you want this not to happen, you
  // need to prevent conversion (by hiding behind `enable_if` or forcing a
  // conversion to a transitional type).
  auto s = ""s;
  accept_string_view_rref(s);
  CHECK(s == "");

  s = csv;
  CHECK(s == "abc");
  CHECK(std::string(csv) == "abc");
}

#pragma endregion
#pragma region Equal

TEST_CASE("CStringViewTestEqual", "[CStringViewTestEqual]") {
  // sv
  CHECK("abc"sv == "abc");
  CHECK("abc"sv == "abc"sv);
  CHECK("abc"sv == "abc"s);

  // csv
  CHECK("abc"_csv == "abc");
  CHECK("abc"_csv == "abc"sv);
  CHECK("abc"_csv == "abc"s);
  CHECK("abc"_csv == "abc"_csv);

  // commutative
  CHECK("abc" == "abc"_csv);
  CHECK("abc"_csv == "abc");

  CHECK("abc"sv == "abc"_csv);
  CHECK("abc"_csv == "abc"sv);

  CHECK("abc"s == "abc"_csv);
  CHECK("abc"_csv == "abc"s);

  // null and empty.
  constexpr auto e = ""_csv;
  constexpr auto n = 0_csv;

  // `0_csv` is consteval, so `n` is computed at compile time; a non-zero
  // literal like `1_csv` would not compile.
  if constexpr (n.empty()) { CHECK(true); }

  auto csv = cstring_view{"abc"};
  // In contrast, the next line won't compile.
  // * if constexpr (csv.empty()) { CHECK(true); }

  CHECK(e.empty());
  CHECK_FALSE(e.null());

  CHECK(n.empty());
  CHECK(n.null());

  CHECK(e == n);
  CHECK_FALSE(e.same(n));

  CHECK(e.data() != nullptr);
  CHECK(n.data() == nullptr);

  CHECK(std::string_view{e.c_str()} == std::string_view{n.c_str()});
  CHECK(e.data() != n.data());

  if (n) {
    CHECK(false);
  } else {
    CHECK(true);
  }

  if (!n) {
    CHECK(true);
  } else {
    CHECK(false);
  }

  if (csv) {
    CHECK(true);
  } else {
    CHECK(false);
  }

  int i{};
  i = n ? 42 : 24;
  CHECK(i == 24);
  i = !n ? 24 : 42;
  CHECK(i == 24);

  CHECK("abc"_csv == "abc"_csv);
  CHECK(("abc"_csv) < ("def"_csv));

  // Hash test.
  std::set<cstring_view> ss;
  ss.insert("abc"_csv);
  CHECK(ss.contains("abc"_csv));
}

#pragma endregion
#pragma region Env

TEST_CASE("Env", "[CStringViewTest]") {
  auto path = "PATH"_env;
  CHECK(path != "");
  auto missing = "sdfk4r345dLKLJldksfdlkl"_env;
  CHECK(missing.null());
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
