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

// `tagged_string_view` is the minimal, unconstrained child of
// `string_view_wrapper`, so testing it thoroughly doubles as the comprehensive
// test of the CRTP base's contract: the read-only passthrough, the null/empty
// distinction, the optional-like API, comparison, and the mutators. The
// tagged-specific sections cover its own additions (type safety and
// reslicing).

#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

#include "../corvid/strings/core/tagged_string_view.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;

namespace {
struct alpha_tag {};
struct beta_tag {};
using alpha = tagged_string_view<alpha_tag>;
using beta = tagged_string_view<beta_tag>;

// UDLs that build each tagged type straight from a string literal.
consteval alpha operator""_alpha(const char* s, std::size_t n) {
  return alpha{std::string_view{s, n}};
}
consteval beta operator""_beta(const char* s, std::size_t n) {
  return beta{std::string_view{s, n}};
}
} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

#pragma region TypeSafety

TEST_CASE("TaggedTypeSafety", "[tagged_string_view]") {
  // Converts implicitly TO a view, but never FROM one.
  static_assert(std::is_convertible_v<alpha, std::string_view>);
  static_assert(!std::is_convertible_v<std::string_view, alpha>);
  // The view-taking constructor is still available, just explicit.
  static_assert(std::is_constructible_v<alpha, std::string_view>);

  // Distinct tags are distinct types that don't implicitly interconvert.
  static_assert(!std::is_same_v<alpha, beta>);
  static_assert(!std::is_convertible_v<alpha, beta>);
  static_assert(!std::is_convertible_v<beta, alpha>);

  // It is not a `std::string_view`.
  static_assert(!std::is_same_v<alpha, std::string_view>);

  // There is no view-taking assignment (would re-admit a raw view).
  static_assert(!std::is_assignable_v<alpha&, std::string_view>);
  // But same-tag copy assignment works.
  static_assert(std::is_copy_assignable_v<alpha>);

  CHECK(true);
}

#pragma endregion
#pragma region Construction

TEST_CASE("TaggedConstruction", "[tagged_string_view]") {
  SECTION("default is null") {
    alpha a;
    CHECK(a.null());
    CHECK(a.empty());
    CHECK(a.data() == nullptr);
    CHECK(a.begin() == a.end());
  }
  SECTION("explicit from view") {
    alpha a{"abc"sv};
    CHECK_FALSE(a.null());
    CHECK(a.size() == 3U);
    CHECK(a == "abc");
  }
  SECTION("empty but not null") {
    alpha a{""sv};
    CHECK(a.empty());
    CHECK_FALSE(a.null());
    CHECK(a.data() != nullptr);
  }
  SECTION("copy and same-tag assignment") {
    alpha a{"abc"sv};
    alpha b = a;
    CHECK(b == "abc");
    alpha c;
    c = a;
    CHECK(c == "abc");
  }
}

#pragma endregion
#pragma region Literals

TEST_CASE("TaggedLiterals", "[tagged_string_view]") {
  // The UDLs yield the tagged type directly, distinct per tag.
  auto a = "abc"_alpha;
  auto b = "xyz"_beta;
  static_assert(std::is_same_v<decltype(a), alpha>);
  static_assert(std::is_same_v<decltype(b), beta>);
  CHECK(a == "abc");
  CHECK(b == "xyz");
  CHECK_FALSE(a.null());

  // An empty literal still yields a non-null, empty view.
  CHECK(""_alpha.empty());
  CHECK_FALSE(""_alpha.null());
}

#pragma endregion
#pragma region Passthrough

TEST_CASE("TaggedPassthrough", "[tagged_string_view]") {
  alpha a{"hello world"sv};

  SECTION("iteration") {
    CHECK(std::string(a.begin(), a.end()) == "hello world");
    CHECK(a.cbegin() != a.cend());
    CHECK(*a.rbegin() == 'd');
    CHECK(*a.crbegin() == 'd');
  }
  SECTION("element access") {
    CHECK(a[0] == 'h');
    CHECK(a.at(1) == 'e');
    CHECK(a.front() == 'h');
    CHECK(a.back() == 'd');
  }
  SECTION("size") {
    CHECK(a.size() == 11U);
    CHECK(a.length() == 11U);
    CHECK_FALSE(a.empty());
    CHECK(a.max_size() >= a.size());
  }
  SECTION("search") {
    CHECK(a.find("world") == 6U);
    CHECK(a.find('z') == alpha::npos);
    CHECK(a.rfind('o') == 7U);
    CHECK(a.find_first_of("aeiou") == 1U);
    CHECK(a.find_last_of("aeiou") == 7U);
    CHECK(a.compare("hello world") == 0);
    CHECK(a.compare("hello") > 0);

    alpha pad{"  x  "sv};
    CHECK(pad.find_first_not_of(' ') == 2U);
    CHECK(pad.find_last_not_of(' ') == 2U);
  }
  SECTION("copy") {
    char buf[6] = {};
    auto n = a.copy(buf, 5);
    CHECK(n == 5U);
    CHECK(std::string_view(buf, 5) == "hello");
  }
}

#pragma endregion
#pragma region Conversion

TEST_CASE("TaggedConversion", "[tagged_string_view]") {
  SECTION("view and operator view_t") {
    alpha a{"abc"sv};
    std::string_view sv = a;
    CHECK(sv == "abc");
    CHECK(a.view() == "abc");
  }
  SECTION("null vs empty and same") {
    alpha nul;
    alpha emp{""sv};
    CHECK(nul.null());
    CHECK(emp.empty());
    CHECK_FALSE(emp.null());
    CHECK(emp.data() != nullptr);
    CHECK(nul.data() == nullptr);

    // `==` treats null and empty as equal; `same` distinguishes them, and it
    // imposes the regime on a plain view too.
    CHECK(nul == emp);
    CHECK_FALSE(nul.same(emp));
    CHECK(emp.same(""sv));
    CHECK_FALSE(emp.same(std::string_view{}));
  }
}

#pragma endregion
#pragma region Optional

TEST_CASE("TaggedOptional", "[tagged_string_view]") {
  alpha present{"abc"sv};
  alpha absent;

  SECTION("presence") {
    CHECK(present.has_value());
    CHECK(static_cast<bool>(present));
    CHECK_FALSE(absent.has_value());
    CHECK_FALSE(static_cast<bool>(absent));
  }
  SECTION("value / operator* / operator-> return the child") {
    static_assert(std::is_same_v<decltype(present.value()), const alpha&>);
    static_assert(std::is_same_v<decltype(*present), const alpha&>);
    static_assert(
        std::is_same_v<decltype(present.operator->()), const alpha*>);
    CHECK(present.value() == "abc");
    CHECK(*present == "abc");
    CHECK(present->size() == 3U);
  }
  SECTION("value_or preserves altitude") {
    static_assert(
        std::is_same_v<decltype(absent.value_or("x"sv)), std::string_view>);
    static_assert(std::is_same_v<decltype(absent.value_or(alpha{})), alpha>);
    CHECK(absent.value_or("def"sv) == "def");
    CHECK(present.value_or("def"sv) == "abc");
    CHECK(absent.value_or(alpha{"def"sv}) == "def");
    CHECK(present.value_or(alpha{"def"sv}) == "abc");
  }
  SECTION("as_optional") {
    static_assert(
        std::is_same_v<decltype(present.as_optional()), std::optional<alpha>>);
    auto o = present.as_optional();
    REQUIRE(o.has_value());
    CHECK(*o == "abc");
    CHECK_FALSE(absent.as_optional().has_value());
  }
}

#pragma endregion
#pragma region Mutators

TEST_CASE("TaggedMutators", "[tagged_string_view]") {
  SECTION("reset") {
    alpha a{"abc"sv};
    CHECK(a.has_value());
    a.reset();
    CHECK_FALSE(a.has_value());
    CHECK(a.null());
  }
  SECTION("swap") {
    alpha a{"aaa"sv};
    alpha b{"bbb"sv};
    a.swap(b);
    CHECK(a == "bbb");
    CHECK(b == "aaa");
  }
  SECTION("reslicing restored by tagged") {
    static_assert(std::is_same_v<decltype(alpha{}.substr(0)), alpha>);
    alpha a{"hello world"sv};
    CHECK(a.substr(6) == "world");
    CHECK(a.substr(0, 5) == "hello");

    alpha b{"hello world"sv};
    b.remove_prefix(6);
    CHECK(b == "world");

    alpha c{"hello world"sv};
    c.remove_suffix(6);
    CHECK(c == "hello");
  }
}

#pragma endregion
#pragma region Comparison

TEST_CASE("TaggedComparison", "[tagged_string_view]") {
  alpha a{"abc"sv};

  SECTION("equality against anything viewable") {
    CHECK(a == "abc");
    CHECK(a == "abc"sv);
    CHECK(a == "abc"s);
    CHECK(a == alpha{"abc"sv});
    // Commutative via the C++20 reversed candidate.
    CHECK("abc" == a);
    CHECK("abc"sv == a);
    CHECK("abc"s == a);
    // operator!= is synthesized from operator==.
    CHECK(a != "def");
    CHECK("def" != a);
    CHECK_FALSE(a != "abc");
  }
  SECTION("ordering and set") {
    CHECK(alpha{"abc"sv} < alpha{"def"sv});
    CHECK((alpha{"abc"sv} <=> alpha{"abc"sv}) == 0);
    CHECK((alpha{"abc"sv} <=> alpha{"def"sv}) < 0);

    std::set<alpha> s;
    s.insert(alpha{"abc"sv});
    s.insert(alpha{"def"sv});
    CHECK(s.contains(alpha{"abc"sv}));
    CHECK(s.size() == 2U);
  }
  SECTION("ostream") {
    std::ostringstream os;
    os << a;
    CHECK(os.str() == "abc");
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
