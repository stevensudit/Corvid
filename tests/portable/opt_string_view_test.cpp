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

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory_resource>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "corvid/strings/cstring_view.h"
#include "corvid/strings/interned_string_view.h"
#include "corvid/strings/opt_string_view.h"
#include "corvid/meta.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)
// NOLINTBEGIN(readability-function-size)

#pragma region Construction

TEST_CASE("Construction", "[OptStringViewTest]") {
  // Default-constructed string_view.
  if (true) {
    std::string_view v;
    CHECK(v.empty());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Default-constructed opt_string_view.
  if (true) {
    opt_string_view v;
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
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
  // Construct opt_string_view on null pointer.
  if (true) {
    // Works same as default construction.
    const char* p{};
    opt_string_view v{p};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on nullptr.
  if (true) {
    // Works same as default construction.
    opt_string_view v{nullptr};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on nullopt.
  if (true) {
    // Works same as default construction.
    opt_string_view v{std::nullopt};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct string_view on null and 0.
  if (true) {
    // Same as default.
    const char* p{};
    // NOLINTNEXTLINE(bugprone-string-constructor)
    std::string_view v{p, 0};
    CHECK(v.empty());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on null and 0.
  if (true) {
    // Same as default.
    const char* p{};
    opt_string_view v{p, 0};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on null and 1.
  if (true) {
    // Same as null and 0.
    const char* p{};
    opt_string_view v{p, 1};
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct string_view on empty.
  if (true) {
    const char* p = "";
    std::string_view v{p};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on empty.
  if (true) {
    const char* p = "";
    opt_string_view v{p};
    CHECK(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct string_view on empty and 0.
  if (true) {
    const char* p = "";
    // NOLINTNEXTLINE(bugprone-string-constructor)
    std::string_view v{p, 0};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on empty and 0.
  if (true) {
    const char* p = "";
    opt_string_view v{p, 0};
    CHECK(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct string_view on empty string.
  if (true) {
    std::string s;
    std::string_view v{s};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on empty string.
  if (true) {
    std::string s;
    opt_string_view v{s};
    CHECK(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct string_view on null string_view.
  if (true) {
    std::string_view sv;
    CHECK(sv.data() == nullptr);
    std::string_view v{sv};
    CHECK(v.empty());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on null string_view.
  if (true) {
    std::string_view sv;
    CHECK(sv.data() == nullptr);
    opt_string_view v(sv);
    CHECK(v.empty());
    CHECK(v.null());
    CHECK(v.data() == nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct string_view on empty string_view.
  if (true) {
    // NOLINTNEXTLINE(readability-redundant-string-init): non-null empty.
    std::string_view sv("");
    CHECK(sv.data() != nullptr);
    std::string_view v{sv};
    CHECK(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct opt_string_view on empty string_view.
  if (true) {
    // NOLINTNEXTLINE(readability-redundant-string-init): non-null empty.
    std::string_view sv{""};
    opt_string_view v(sv);
    CHECK(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() == v.end());
  }
  // Construct string_view on arbitrary string_view.
  if (true) {
    std::string_view sv("abc");
    std::string_view v{sv};
    CHECK_FALSE(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() != v.end());
  }
  // Construct opt_string_view on arbitrary string_view.
  if (true) {
    std::string_view sv("abc");
    opt_string_view v(sv);
    CHECK_FALSE(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() != v.end());
  }
  // Construct string_view on it/end.
  if (true) {
    std::span<const char> r{"abc"};
    std::string_view v{r.begin(), r.end()};
    CHECK_FALSE(v.empty());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() != v.end());
    CHECK(r.size() == 4U);
    CHECK(v.size() == 4U);
  }
  // Construct opt_string_view on it/end
  if (true) {
    std::span<const char> r{"abc"};
    opt_string_view v(r.begin(), r.end());
    CHECK_FALSE(v.empty());
    CHECK_FALSE(v.null());
    CHECK(v.data() != nullptr);
    CHECK(v.begin() != v.end());
    CHECK(r.size() == 4U);
    CHECK(v.size() == 4U);
  }
  // Construct using UDL.
  if (true) {
    auto a = ""_optsv;
    CHECK(a.empty());
    CHECK_FALSE(a.null());
    auto b = "abc"_optsv;
    CHECK(b.size() == 3U);
    // Embedded zeros are permitted.
    auto c = "abc\0def"_optsv;
    CHECK(c.size() == 7U);
    // NOLINTNEXTLINE(bugprone-suspicious-stringview-data-usage)
    auto d = opt_string_view(c.data());
    CHECK(d.size() == 3U);
    auto e = 0_optsv;
    CHECK(e.null());
    // The following won't even compile:
    //*    1_optsv;
  }
}

#pragma endregion
#pragma region Optional

TEST_CASE("Optional", "[OptStringViewTest]") {
  if (true) {
    std::optional<std::string> opt;
    opt_string_view osv{opt};
    CHECK(osv.null());
    opt = "test";
    osv = opt;
    CHECK(osv == "test");
    // * opt_string_view bad{std::optional<int>{}};
  }
  if (true) {
    std::optional<std::string_view> opt;
    opt_string_view osv{opt};
    CHECK(osv.null());
    opt = "test";
    osv = opt;
    CHECK(osv == "test");
  }
  // On an rvalue wrapper, value() and operator*() return the child by value,
  // so the result cannot dangle.
  static_assert(
      std::is_same_v<decltype(std::declval<const opt_string_view&>().value()),
          const opt_string_view&>);
  static_assert(std::is_same_v<
      decltype(std::declval<opt_string_view&&>().value()), opt_string_view>);
  static_assert(std::is_same_v<decltype(*std::declval<opt_string_view&&>()),
      opt_string_view>);
  CHECK(opt_string_view{"abc"sv}.value() == "abc");
}

#pragma endregion
#pragma region Workalike

TEST_CASE("AllocatorAndTraits", "[OptStringViewTest]") {
  // Any allocator converts, directly and through an optional.
  std::pmr::string ps{"abc"};
  opt_string_view v{ps};
  CHECK(v == "abc");
  static_assert(std::is_convertible_v<std::pmr::string, opt_string_view>);
  static_assert(
      std::is_convertible_v<std::optional<std::pmr::string>, opt_string_view>);

  // Non-default traits do not, even when they are the default's twin.
  struct twin_traits: std::char_traits<char> {};
  using twin_string = std::basic_string<char, twin_traits>;
  static_assert(!std::is_constructible_v<opt_string_view, twin_string>);
}

TEST_CASE("Workalike", "[OptStringViewTest]") {
  // Verify optional-like interface.
  if (true) {
    opt_string_view osv;
    CHECK_FALSE(osv.has_value());
    CHECK(osv.value_or("def") == "def");
    osv = "abc"sv;
    CHECK(osv.has_value());
    CHECK(osv.value() == "abc");
    CHECK(osv.value_or("def") == "abc");
    osv.reset();
    CHECK_FALSE(osv.has_value());
  }

  // value throws bad_optional_access when null; operator* and operator-> are
  // undefined on null, so only the present case is exercised for them.
  if (true) {
    opt_string_view absent;
    CHECK_THROWS_AS(absent.value(), std::bad_optional_access);

    opt_string_view present{"abc"};
    CHECK(present.value() == "abc");
    CHECK(*present == "abc");
    CHECK(present->size() == 3);
  }

  // value_or preserves the argument's type: an SV fallback yields an SV, while
  // a same-typed child fallback yields the child (keeping its invariants).
  if (true) {
    opt_string_view present{"abc"};
    opt_string_view absent;
    CHECK(
        (std::is_same_v<decltype(present.value_or("x"sv)), std::string_view>));
    CHECK((std::is_same_v<decltype(present.value_or(opt_string_view{})),
        opt_string_view>));
    CHECK(present.value_or(opt_string_view{"def"}) == "abc");
    CHECK(absent.value_or(opt_string_view{"def"}) == "def");
    CHECK_FALSE(absent.value_or(opt_string_view{"def"}).null());
  }

  // Interaction with temporaries and moved-from values.
  if (true) {
    CHECK(opt_string_view{"tmp"}.value_or("def") == "tmp");

    opt_string_view src{"xyz"};
    // NOLINTNEXTLINE(performance-move-const-arg)
    opt_string_view dst{std::move(src)};
    // Moving doesn't clear opt_string_view
    // NOLINTNEXTLINE(bugprone-use-after-move, clang-analyzer-cplusplus.Move)
    CHECK(src.has_value());
    CHECK(dst.has_value());

    dst.reset();
    CHECK_FALSE(dst.has_value());
  }

  // Methods operate correctly on rvalues.
  if (true) {
    opt_string_view osv{"qqq"};
    // NOLINTNEXTLINE(bugprone-use-after-move,performance-move-const-arg)
    CHECK(std::move(osv).value_or("def") == "qqq");
  }
}

auto accept_string_view(std::string_view v) { return v; }
auto accept_opt_string_view(opt_string_view v) { return v; }

void accept_string_view_ref(std::string_view& v) { v = "changed"; }

std::string_view accept_overloaded(std::string_view) { return "sv"; }
std::string_view accept_overloaded(opt_string_view) { return "osv"; }

#pragma endregion
#pragma region Cast

TEST_CASE("Cast", "[OptStringViewTest]") {
  // Casts "up" implicitly.
  CHECK("abc"_optsv == "abc"_optsv);
  CHECK(accept_string_view("abc"sv) == "abc");
  auto abc_str = "abc"s;
  CHECK(accept_string_view(abc_str) == "abc");
  CHECK(accept_string_view("abc"_optsv) == "abc");

  CHECK(accept_opt_string_view("abc"_optsv) == "abc");
  CHECK(accept_opt_string_view(abc_str) == "abc");

  // Or down.
  CHECK(accept_opt_string_view("abc"sv) == "abc");

  // Handles overloading just fine.
  CHECK(accept_overloaded("abc"sv) == "sv");
  CHECK(accept_overloaded("abc"_optsv) == "osv");

  // But not this ambiguity. Need to either cast here or add a specific
  // overload.
  // * CHECK(accept_overloaded("abc"s) == "sv");

  // It's not a std::string_view but can be converted to one.
  CHECK_FALSE((std::is_same_v<opt_string_view, std::string_view>));
  CHECK(StringViewConvertible<opt_string_view>);

  auto osv = "abc"_optsv;

  // Converts to `std::string_view` by value. The conversion yields a copy, so
  // (unlike the old design that inherited from `std::string_view`)
  // `opt_string_view` is not a mutable alias: mutating the copy leaves `osv`
  // alone, and binding `osv` to a `std::string_view&` no longer compiles.
  std::string_view sv = osv;
  CHECK(sv == "abc");
  accept_string_view_ref(sv);
  CHECK(sv == "changed");
  CHECK(osv == "abc");
  // * accept_string_view_ref(osv);

  // It still converts into a `std::string`.
  std::string s;
  s = osv;
  CHECK(s == "abc");
  CHECK(std::string(osv) == "abc");
}

TEST_CASE("Sanity", "[OptStringViewTest]") {
  opt_string_view osv{"hello"};
  osv = nullptr;
  CHECK(osv.null());
  osv = "world";
  CHECK(osv == "world");
  osv = "bird"_optsv;
  CHECK(osv == "bird");
}

#pragma endregion
#pragma region Assignment

using user_view = tagged_string_view<struct user_tag>;
using species_view = interned_string_view<struct species_tag>;

TEST_CASE("Assignment", "[OptStringViewTest]") {
  // What the constructors accept assigns through them, without ambiguity.
  static_assert(std::is_assignable_v<opt_string_view&, std::string_view>);
  static_assert(std::is_assignable_v<opt_string_view&, std::string>);
  static_assert(std::is_assignable_v<opt_string_view&, std::pmr::string>);
  static_assert(std::is_assignable_v<opt_string_view&, const char*>);
  static_assert(std::is_assignable_v<opt_string_view&, std::nullptr_t>);
  static_assert(std::is_assignable_v<opt_string_view&, std::nullopt_t>);
  static_assert(
      std::is_assignable_v<opt_string_view&, std::optional<std::string>>);

  // Anything else that converts to a view assigns too.
  static_assert(std::is_assignable_v<opt_string_view&, cstring_view>);
  static_assert(std::is_assignable_v<opt_string_view&, user_view>);

  // An interned view converts only explicitly, so it does not.
  static_assert(!std::is_assignable_v<opt_string_view&, species_view>);

  // A tagged view takes a raw view only through its explicit constructor.
  static_assert(std::is_assignable_v<user_view&, user_view>);
  static_assert(!std::is_assignable_v<user_view&, std::string_view>);
  static_assert(!std::is_assignable_v<user_view&, cstring_view>);
  static_assert(!std::is_assignable_v<user_view&, opt_string_view>);

  // Wide views follow the same rules, null included.
  static_assert(std::is_assignable_v<opt_wstring_view&, std::nullptr_t>);
  static_assert(std::is_assignable_v<opt_wstring_view&, std::wstring>);
  static_assert(std::is_assignable_v<opt_wstring_view&, wcstring_view>);
  static_assert(!std::is_assignable_v<opt_wstring_view&, cstring_view>);

  if (true) {
    const std::string s{"abc"};
    opt_string_view o;
    o = s;
    CHECK(o.identical(s));

    const cstring_view c{"def"};
    o = c;
    CHECK(o.identical(c));

    o = user_view{"ghi"};
    CHECK(o == "ghi");

    o = nullptr;
    CHECK(o.null());
  }
  if (true) {
    opt_wstring_view w{L"abc"};
    w = wcstring_view{L"def"};
    CHECK(w == L"def");

    w = nullptr;
    CHECK(w.null());
  }
}

#pragma endregion
#pragma region Equal

TEST_CASE("OptStringViewTestEqual", "[OptStringViewTestEqual]") {
  // sv
  CHECK("abc"sv == "abc");
  CHECK("abc"sv == "abc"sv);
  CHECK("abc"sv == "abc"s);

  // osv
  CHECK("abc"_optsv == "abc");
  CHECK("abc"_optsv == "abc"sv);
  CHECK("abc"_optsv == "abc"s);
  CHECK("abc"_optsv == "abc"_optsv);

  // commutative
  CHECK("abc" == "abc"_optsv);
  CHECK("abc"_optsv == "abc");

  CHECK("abc"sv == "abc"_optsv);
  CHECK("abc"_optsv == "abc"sv);

  CHECK("abc"s == "abc"_optsv);
  CHECK("abc"_optsv == "abc"s);

  // operator!= is synthesized from operator== (C++20), in both directions.
  CHECK("abc"_optsv != "def");
  CHECK("def" != "abc"_optsv);
  CHECK_FALSE("abc"_optsv != "abc");

  // null and empty.
  constexpr auto e = ""_optsv;
  constexpr auto n = 0_optsv;

  // The following won't even compile.
  // * 1_optsv

  // It's really constexpr, despite throwing on non-0, because it knows at
  // compile-time that it's 0.
  if constexpr (n.empty()) { CHECK(true); }

  auto csv = opt_string_view{"abc"};
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

  CHECK("abc"_optsv == "abc"_optsv);
  CHECK(("abc"_optsv) < ("def"_optsv));

  // Hash test.
  std::set<opt_string_view> ss;
  ss.insert("abc"_optsv);
  CHECK(ss.contains("abc"_optsv));
}

#pragma endregion

// NOLINTEND(readability-function-size)
// NOLINTEND(readability-function-cognitive-complexity)
