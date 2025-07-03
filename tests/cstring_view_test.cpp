// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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

#include "../corvid/strings/cstring_view.h"
#include "../corvid/meta.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;

void CStringViewTest_Construction() {
  // Default-constructed string_view.
  if (true) {
    std::string_view v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Default-constucted cstring_view.
  if (true) {
    cstring_view v;
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct string_view on null pointer.
  if (true) {
    const char* p{};
    (void)p;
    // This doesn't even throw, as such. It's undefined, so you (probably) get
    // an access violation. We could try to test for this with a try/catch
    // block and ellipses, but it's not guaranteed to behave on all platforms.
    // * EXPECT_THROW((std::string_view(p)), std::runtime_error);
  }
  // Construct cstring_view on null pointer.
  if (true) {
    // Works same as default construction.
    const char* p{};
    cstring_view v{p};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on nullptr.
  if (true) {
    // Works same as default construction.
    cstring_view v{nullptr};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on nullopt.
  if (true) {
    // Works same as default construction.
    cstring_view v{std::nullopt};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct string_view on null and 0.
  if (true) {
    // Same as default.
    const char* p{};
    std::string_view v{p, 0};
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on null and 0.
  if (true) {
    // Same as default.
    const char* p{};
    cstring_view v{p, 0};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct string_view on empty.
  if (true) {
    const char* p = "";
    std::string_view v{p};
    EXPECT_TRUE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on empty.
  if (true) {
    const char* p = "";
    cstring_view v{p};
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct string_view on empty and 0.
  if (true) {
    const char* p = "";
    std::string_view v{p, 0};
    EXPECT_TRUE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on empty and 0.
  if (true) {
    const char* p = "";
    EXPECT_THROW((cstring_view(p, 0)), std::length_error);
    cstring_view v{p, 1};
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct string_view on empty string.
  if (true) {
    std::string s;
    std::string_view v{s};
    EXPECT_TRUE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on empty string.
  if (true) {
    std::string s;
    cstring_view v{s};
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct string_view on null string_view.
  if (true) {
    std::string_view sv;
    EXPECT_EQ(sv.data(), nullptr);
    std::string_view v{sv};
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on null string_view.
  if (true) {
    std::string_view sv;
    EXPECT_EQ(sv.data(), nullptr);
    cstring_view v(sv);
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct string_view on empty string_view.
  if (true) {
    std::string_view sv("");
    EXPECT_NE(sv.data(), nullptr);
    std::string_view v{sv};
    EXPECT_TRUE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on empty string_view.
  if (true) {
    std::string_view svbad("");
    EXPECT_NE(svbad.data(), nullptr);
    EXPECT_THROW((cstring_view(svbad)), std::length_error);
    std::string_view sv{"", 1};
    cstring_view v(sv);
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_EQ(&*v.begin(), &*v.end());
  }
  // Construct string_view on arbitrary string_view.
  if (true) {
    std::string_view sv("abc");
    std::string_view v{sv};
    EXPECT_FALSE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(&*v.begin(), &*v.end());
  }
  // Construct cstring_view on arbitrary string_view.
  if (true) {
    std::string_view svbad("abc");
    EXPECT_THROW((cstring_view(svbad)), std::invalid_argument);
    std::string_view sv("abc", 4);
    cstring_view v(sv);
    EXPECT_FALSE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_NE(&*v.begin(), &*v.end());
  }
  // Construct string_view on it/end.
  if (true) {
    std::span<const char> r{"abc"};
    std::string_view v{r.begin(), r.end()};
    EXPECT_FALSE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(&*v.begin(), &*v.end());
    EXPECT_EQ(r.size(), 4u);
    EXPECT_EQ(v.size(), 4u);
  }
  // Construct cstring_view on it/end
  if (true) {
    std::span<const char> r{"abc"};
    cstring_view v(r.begin(), r.end());
    EXPECT_FALSE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.c_str(), nullptr);
    EXPECT_NE(&*v.begin(), &*v.end());
    EXPECT_EQ(r.size(), 4u);
    EXPECT_EQ(v.size(), 3u);
  }
  // Construct using UDL.
  if (true) {
    auto a = ""_csv;
    EXPECT_TRUE(a.empty());
    EXPECT_FALSE(a.null());
    auto b = "abc"_csv;
    EXPECT_EQ(b.size(), 3u);
    // Embedded zeros are permitted.
    auto c = "abc\0def"_csv;
    EXPECT_EQ(c.size(), 7u);
    auto d = cstring_view(c.c_str());
    EXPECT_EQ(d.size(), 3u);
    auto e = 0_csv;
    EXPECT_TRUE(e.null());
    EXPECT_THROW((1_csv), std::out_of_range);
  }
}

void CStringViewTest_Optional() {
  if (true) {
    std::optional<std::string> opt;
    cstring_view csv{opt};
    EXPECT_TRUE(csv.null());
    opt = "test";
    csv = opt;
    EXPECT_EQ(csv, "test");
    // * cstring_view bad{std::optional<int>{}};
  }
}

auto accept_string_view(std::string_view v) { return v; }
auto accept_cstring_view(cstring_view v) { return v; }

void accept_string_view_ref(std::string_view& v) { v = "changed"; }
void accept_string_view_rref(std::string_view&& v) { v = "changed"; }

std::string_view accept_overloaded(std::string_view) { return "sv"; }
std::string_view accept_overloaded(cstring_view) { return "csv"; }

void CStringViewTest_Cast() {
  // Casts "up" implicitly.
  EXPECT_EQ("abc"_csv, "abc"_csv);
  EXPECT_EQ(accept_string_view("abc"sv), "abc");
  EXPECT_EQ(accept_string_view("abc"s), "abc");
  EXPECT_EQ(accept_string_view("abc"_csv), "abc");

  EXPECT_EQ(accept_cstring_view("abc"_csv), "abc");
  EXPECT_EQ(accept_cstring_view("abc"s), "abc");

  // But not down.
  // * EXPECT_EQ(accept_cstring_view("abc"sv), "abc");

  // Handles overloading just fine.
  EXPECT_EQ(accept_overloaded("abc"sv), "sv");
  EXPECT_EQ(accept_overloaded("abc"_csv), "csv");

  // But not this ambiguity.
  // Need to either cast here or add a specific overload.
  // * EXPECT_EQ(accept_overloaded("abc"s), "sv");

  // It's not a std::string_view but can be converted to one.
  EXPECT_FALSE((std::is_same_v<cstring_view, std::string_view>));
  EXPECT_TRUE((StringViewConvertible<cstring_view>));

  auto csv = "abc"_csv;
  std::string_view sv = csv;
  accept_string_view_ref(sv);
  // But not this, so we can maintain our invariant.
  // * accept_string_view_ref(csv);
  accept_string_view_rref(std::move(sv));
  EXPECT_EQ(sv, "changed");
  // Weird but correct, and invariant is maintained.
  accept_string_view_rref(csv);
  EXPECT_EQ(csv, "abc");
  // Same thing happens with `std::string`. If you want this not to happen, you
  // need to prevent conversion (by hiding behind `enable_if` or forcing a
  // conversion to a transitional type).
  auto s = ""s;
  accept_string_view_rref(s);
  EXPECT_EQ(s, "");

  s = csv;
  EXPECT_EQ(s, "abc");
  EXPECT_EQ(std::string(csv), "abc");
}

void CStringViewTestEqual() {
  // sv
  EXPECT_EQ("abc"sv, "abc");
  EXPECT_EQ("abc"sv, "abc"sv);
  EXPECT_EQ("abc"sv, "abc"s);

  // csv
  EXPECT_EQ("abc"_csv, "abc");
  EXPECT_EQ("abc"_csv, "abc"sv);
  EXPECT_EQ("abc"_csv, "abc"s);
  EXPECT_EQ("abc"_csv, "abc"_csv);

  // commutative
  EXPECT_EQ("abc", "abc"_csv);
  EXPECT_EQ("abc"_csv, "abc");

  EXPECT_EQ("abc"sv, "abc"_csv);
  EXPECT_EQ("abc"_csv, "abc"sv);

  EXPECT_EQ("abc"s, "abc"_csv);
  EXPECT_EQ("abc"_csv, "abc"s);

  // null and empty.
  constexpr auto e = ""_csv;
  constexpr auto n = 0_csv;

  EXPECT_THROW(1_csv, std::out_of_range);

  // It's really constexpr, despite throwing on non-0, because it knows at
  // compile-time that it's 0.
  if constexpr (n.empty()) {
    EXPECT_TRUE(true);
  }

  auto csv = cstring_view{"abc"};
  // In contrast, the next line won't compile.
  // * if constexpr (csv.empty()) { EXPECT_TRUE(true); }

  EXPECT_TRUE(e.empty());
  EXPECT_FALSE(e.null());

  EXPECT_TRUE(n.empty());
  EXPECT_TRUE(n.null());

  EXPECT_TRUE((e == n));
  EXPECT_FALSE(e.same(n));

  EXPECT_NE(e.data(), nullptr);
  EXPECT_EQ(n.data(), nullptr);

  EXPECT_EQ(std::string_view{e.c_str()}, std::string_view{n.c_str()});
  EXPECT_NE(e.data(), n.data());

  if (n) {
    EXPECT_TRUE(false);
  } else {
    EXPECT_TRUE(true);
  }

  if (!n) {
    EXPECT_TRUE(true);
  } else {
    EXPECT_TRUE(false);
  }

  if (csv) {
    EXPECT_TRUE(true);
  } else {
    EXPECT_TRUE(false);
  }

  int i{};
  i = n ? 42 : 24;
  EXPECT_EQ(i, 24);
  i = !n ? 24 : 42;
  EXPECT_EQ(i, 24);

  EXPECT_EQ("abc"_csv, "abc"_csv);
  EXPECT_LT("abc"_csv, "def"_csv);

  // Hash test.
  std::set<cstring_view> ss;
  ss.insert("abc"_csv);
  EXPECT_TRUE(ss.contains("abc"_csv));
}

void CStringViewTest_Env() {
  auto path = "PATH"_env;
  EXPECT_NE(path, "");
  auto missing = "sdfk4r345dLKLJldksfdlkl"_env;
  EXPECT_TRUE(missing.null());
}

MAKE_TEST_LIST(CStringViewTest_Construction, CStringViewTest_Optional,
    CStringViewTest_Cast, CStringViewTestEqual, CStringViewTest_Env);
