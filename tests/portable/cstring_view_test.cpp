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

#include <concepts>
#include <cstdint>
#include <map>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "corvid/strings/cstring_view.h"
#include "corvid/meta.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(readability-function-size)

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
    // NOLINTNEXTLINE(readability-redundant-string-init): non-null empty.
    std::string_view sv("");
    CHECK(sv.data() != nullptr);
    std::string_view v{sv};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.data() == v.data() + v.size());
  }
  // Construct cstring_view on empty string_view.
  if (true) {
    // NOLINTNEXTLINE(readability-redundant-string-init): non-null empty.
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

TEST_CASE("AllocatorAndTraits", "[CStringViewTest]") {
  // A `basic_cstring_view` over a view with any other traits fails its
  // static_assert ("cstring_view honors only the default char_traits").
  // Verified by instantiating it on `std::basic_string_view<char,
  // twin_traits>`. A fails-to-compile case cannot live in the suite.
  // Any allocator converts.
  std::pmr::string ps{"abc"};
  cstring_view v{ps};
  CHECK(v == "abc");
  static_assert(std::is_convertible_v<std::pmr::string, cstring_view>);
  static_assert(
      std::is_convertible_v<std::optional<std::pmr::string>, cstring_view>);

  // Non-default traits do not, even when they are the default's twin.
  struct twin_traits: std::char_traits<char> {};
  using twin_string = std::basic_string<char, twin_traits>;
  static_assert(!std::is_constructible_v<cstring_view, twin_string>);
}

TEST_CASE("WideConstruction", "[CStringViewTest]") {
  // Regression: construction from the matching basic_string was hardcoded to
  // std::string, so wide instantiations fell through to the throwing
  // string_view constructor.
  if (true) {
    const std::wstring ws{L"abc"};
    wcstring_view wv{ws};
    CHECK(wv.size() == 3U);
    CHECK(wv.c_str()[3] == L'\0');
    const std::wstring wempty;
    wcstring_view wev{wempty};
    CHECK(wev.empty());
    CHECK_FALSE(wev.null());
    const std::u16string u16s{u"xy"};
    u16cstring_view u16v{u16s};
    CHECK(u16v.size() == 2U);
  }
  // Regression: the iterator-pair constructor was constrained on `char`, so
  // it did not exist for wide instantiations.
  if (true) {
    std::span<const wchar_t> r{L"abc"};
    wcstring_view v(r.begin(), r.end());
    CHECK_FALSE(v.empty());
    CHECK(v.size() == 3U);
    CHECK(r.size() == 4U);
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
  // The optional constructor is explicit exactly when construction from the
  // payload type itself would be, so a risky view payload no longer converts
  // implicitly.
  static_assert(
      std::is_convertible_v<std::optional<std::string>, cstring_view>);
  static_assert(
      std::is_constructible_v<cstring_view, std::optional<std::string_view>>);
  static_assert(
      !std::is_convertible_v<std::optional<std::string_view>, cstring_view>);
  // A payload that `std::string_view` accepts but `cstring_view` does not is
  // rejected at the constraint, not inside the body.
  static_assert(!std::is_constructible_v<cstring_view,
      std::optional<std::vector<char>>>);
}

TEST_CASE("OptionalInterface", "[CStringViewTest]") {
  // The inherited optional interface keeps the `cstring_view` type, so the
  // termination invariant survives `value`, `value_or`, and `as_optional`.
  const auto v = "abc"_csv;
  const cstring_view n;
  CHECK(v.has_value());
  CHECK_FALSE(n.has_value());
  CHECK(v.value() == "abc");
  CHECK_THROWS_AS(n.value(), std::bad_optional_access);
  static_assert(std::same_as<decltype(v.value_or(v)), cstring_view>);
  static_assert(std::same_as<decltype(v.value_or("x")), std::string_view>);
  CHECK(n.value_or(v) == "abc");
  CHECK(n.value_or("x") == "x");
  CHECK(v.as_optional().has_value());
  CHECK_FALSE(n.as_optional().has_value());
  CHECK(v.as_optional()->c_str()[3] == 0);

  auto a = "left"_csv;
  auto b = "right"_csv;
  a.swap(b);
  CHECK(a == "right");
  CHECK(b == "left");
  a.reset();
  CHECK(a.null());
}

TEST_CASE("SearchPassthrough", "[CStringViewTest]") {
  const auto v = "abcdef"_csv;
  CHECK(v.starts_with("abc"));
  CHECK_FALSE(v.starts_with("bcd"));
  CHECK(v.ends_with("def"));
  CHECK(v.ends_with('f'));
  CHECK(v.contains("cde"));
  CHECK_FALSE(v.contains("xyz"));

  // `compare` is not `noexcept`. The position-taking overloads throw a
  // catchable `std::out_of_range` instead of terminating.
  CHECK(v.compare("abcdef"sv) == 0);
  CHECK_THROWS_AS(v.compare(v.size() + 1, 1, "x"sv), std::out_of_range);
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

  // But not this ambiguity. Need to either cast here or add a specific
  // overload.
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

  // `0_csv` is consteval, so `n` is computed at compile time. A non-zero
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

// NOLINTEND(readability-function-size)
// NOLINTEND(readability-function-cognitive-complexity)
