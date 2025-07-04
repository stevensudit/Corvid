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

#include "../corvid/strings/opt_string_view.h"
#include "../corvid/meta.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::literals;

void OptStringViewTest_Construction() {
  // Default-constructed string_view.
  if (true) {
    std::string_view v;
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Default-constucted opt_string_view.
  if (true) {
    opt_string_view v;
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
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
  // Construct opt_string_view on null pointer.
  if (true) {
    // Works same as default construction.
    const char* p{};
    opt_string_view v{p};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on nullptr.
  if (true) {
    // Works same as default construction.
    opt_string_view v{nullptr};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on nullopt.
  if (true) {
    // Works same as default construction.
    opt_string_view v{std::nullopt};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct string_view on null and 0.
  if (true) {
    // Same as default.
    const char* p{};
    std::string_view v{p, 0};
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on null and 0.
  if (true) {
    // Same as default.
    const char* p{};
    opt_string_view v{p, 0};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on null and 1.
  if (true) {
    // Same as null and 0.
    const char* p{};
    opt_string_view v{p, 1};
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct string_view on empty.
  if (true) {
    const char* p = "";
    std::string_view v{p};
    EXPECT_TRUE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on empty.
  if (true) {
    const char* p = "";
    opt_string_view v{p};
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct string_view on empty and 0.
  if (true) {
    const char* p = "";
    std::string_view v{p, 0};
    EXPECT_TRUE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on empty and 0.
  if (true) {
    const char* p = "";
    opt_string_view v{p, 0};
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct string_view on empty string.
  if (true) {
    std::string s;
    std::string_view v{s};
    EXPECT_TRUE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on empty string.
  if (true) {
    std::string s;
    opt_string_view v{s};
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct string_view on null string_view.
  if (true) {
    std::string_view sv;
    EXPECT_EQ(sv.data(), nullptr);
    std::string_view v{sv};
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on null string_view.
  if (true) {
    std::string_view sv;
    EXPECT_EQ(sv.data(), nullptr);
    opt_string_view v(sv);
    EXPECT_TRUE(v.empty());
    EXPECT_TRUE(v.null());
    EXPECT_EQ(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct string_view on empty string_view.
  if (true) {
    std::string_view sv("");
    EXPECT_NE(sv.data(), nullptr);
    std::string_view v{sv};
    EXPECT_TRUE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct opt_string_view on empty string_view.
  if (true) {
    std::string_view sv{""};
    opt_string_view v(sv);
    EXPECT_TRUE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_EQ(v.begin(), v.end());
  }
  // Construct string_view on arbitrary string_view.
  if (true) {
    std::string_view sv("abc");
    std::string_view v{sv};
    EXPECT_FALSE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.begin(), v.end());
  }
  // Construct opt_string_view on arbitrary string_view.
  if (true) {
    std::string_view sv("abc");
    opt_string_view v(sv);
    EXPECT_FALSE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.begin(), v.end());
  }
  // Construct string_view on it/end.
  if (true) {
    std::span<const char> r{"abc"};
    std::string_view v{r.begin(), r.end()};
    EXPECT_FALSE(v.empty());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.begin(), v.end());
    EXPECT_EQ(r.size(), 4u);
    EXPECT_EQ(v.size(), 4u);
  }
  // Construct opt_string_view on it/end
  if (true) {
    std::span<const char> r{"abc"};
    opt_string_view v(r.begin(), r.end());
    EXPECT_FALSE(v.empty());
    EXPECT_FALSE(v.null());
    EXPECT_NE(v.data(), nullptr);
    EXPECT_NE(v.begin(), v.end());
    EXPECT_EQ(r.size(), 4u);
    EXPECT_EQ(v.size(), 4u);
  }
  // Construct using UDL.
  if (true) {
    auto a = ""_osv;
    EXPECT_TRUE(a.empty());
    EXPECT_FALSE(a.null());
    auto b = "abc"_osv;
    EXPECT_EQ(b.size(), 3u);
    // Embedded zeros are permitted.
    auto c = "abc\0def"_osv;
    EXPECT_EQ(c.size(), 7u);
    auto d = opt_string_view(c.data());
    EXPECT_EQ(d.size(), 3u);
    auto e = 0_osv;
    EXPECT_TRUE(e.null());
    EXPECT_THROW((1_osv), std::out_of_range);
  }
}

void OptStringViewTest_Optional() {
  if (true) {
    std::optional<std::string> opt;
    opt_string_view osv{opt};
    EXPECT_TRUE(osv.null());
    opt = "test";
    osv = opt;
    EXPECT_EQ(osv, "test");
    // * opt_string_view bad{std::optional<int>{}};
  }
  if (true) {
    std::optional<std::string_view> opt;
    opt_string_view osv{opt};
    EXPECT_TRUE(osv.null());
    opt = "test";
    osv = opt;
    EXPECT_EQ(osv, "test");
  }
  // TODO: Add unit tests for the optional workalike functions. At that point,
  // consider porting them all to cstring_view.
  // TODO: Figure out why the && overloads for the optional workalike functions
  // don't work.
}

void OptStringViewTest_Workalike() {
  // Verify optional-like interface.
  if (true) {
    opt_string_view osv;
    EXPECT_FALSE(osv.has_value());
    EXPECT_EQ(osv.value_or("def"), "def");
    osv.emplace("abc");
    EXPECT_TRUE(osv.has_value());
    EXPECT_EQ(osv.value(), "abc");
    EXPECT_EQ(osv.value_or("def"), "abc");
    osv.reset();
    EXPECT_FALSE(osv.has_value());
  }

  // Interaction with temporaries and moved-from values.
  if (true) {
    EXPECT_EQ(opt_string_view{"tmp"}.value_or("def"), "tmp");

    opt_string_view src{"xyz"};
    opt_string_view dst{std::move(src)};
    // Moving doesn't clear opt_string_view
    EXPECT_TRUE(src.has_value());
    EXPECT_TRUE(dst.has_value());

    dst.reset();
    EXPECT_FALSE(dst.has_value());
  }

  // Methods operate correctly on rvalues.
  if (true) {
    opt_string_view osv{"qqq"};
    EXPECT_EQ(std::move(osv).value_or("def"), "qqq");
    opt_string_view{"aaa"}.emplace("bbb");
  }
}

auto accept_string_view(std::string_view v) { return v; }
auto accept_opt_string_view(opt_string_view v) { return v; }

void accept_string_view_ref(std::string_view& v) { v = "changed"; }
void accept_string_view_rref(std::string_view&& v) { v = "changed"; }

std::string_view accept_overloaded(std::string_view) { return "sv"; }
std::string_view accept_overloaded(opt_string_view) { return "osv"; }

void OptStringViewTest_Cast() {
  // Casts "up" implicitly.
  EXPECT_EQ("abc"_osv, "abc"_osv);
  EXPECT_EQ(accept_string_view("abc"sv), "abc");
  auto abc_str = "abc"s;
  EXPECT_EQ(accept_string_view(abc_str), "abc");
  EXPECT_EQ(accept_string_view("abc"_osv), "abc");

  EXPECT_EQ(accept_opt_string_view("abc"_osv), "abc");
  EXPECT_EQ(accept_opt_string_view(abc_str), "abc");

  // Or down.
  EXPECT_EQ(accept_opt_string_view("abc"sv), "abc");

  // Handles overloading just fine.
  EXPECT_EQ(accept_overloaded("abc"sv), "sv");
  EXPECT_EQ(accept_overloaded("abc"_osv), "osv");

  // But not this ambiguity.
  // Need to either cast here or add a specific overload.
  // * EXPECT_EQ(accept_overloaded("abc"s), "sv");

  // It's not a std::string_view but can be converted to one.
  EXPECT_FALSE((std::is_same_v<opt_string_view, std::string_view>));
  EXPECT_TRUE((StringViewConvertible<opt_string_view>));

  auto osv = "abc"_osv;
  std::string_view sv = osv;
  accept_string_view_ref(sv);
  // And even this.
  accept_string_view_ref(osv);
  accept_string_view_rref(std::move(sv));
  EXPECT_EQ(sv, "changed");
  accept_string_view_rref(std::move(osv));
  EXPECT_EQ(osv, "changed");
  // Same thing happens with `std::string`. If you want this not to happen, you
  // need to prevent conversion (by hiding behind `enable_if` or forcing a
  // conversion to a transitional type).
  auto s = ""s;
  accept_string_view_rref(s);
  EXPECT_EQ(s, "");

  s = osv;
  EXPECT_EQ(s, "changed");
  EXPECT_EQ(std::string(osv), "changed");
}

void OptStringViewTestEqual() {
  // sv
  EXPECT_EQ("abc"sv, "abc");
  EXPECT_EQ("abc"sv, "abc"sv);
  EXPECT_EQ("abc"sv, "abc"s);

  // osv
  EXPECT_EQ("abc"_osv, "abc");
  EXPECT_EQ("abc"_osv, "abc"sv);
  EXPECT_EQ("abc"_osv, "abc"s);
  EXPECT_EQ("abc"_osv, "abc"_osv);

  // commutative
  EXPECT_EQ("abc", "abc"_osv);
  EXPECT_EQ("abc"_osv, "abc");

  EXPECT_EQ("abc"sv, "abc"_osv);
  EXPECT_EQ("abc"_osv, "abc"sv);

  EXPECT_EQ("abc"s, "abc"_osv);
  EXPECT_EQ("abc"_osv, "abc"s);

  // null and empty.
  constexpr auto e = ""_osv;
  constexpr auto n = 0_osv;

  EXPECT_THROW(1_osv, std::out_of_range);

  // It's really constexpr, despite throwing on non-0, because it knows at
  // compile-time that it's 0.
  if constexpr (n.empty()) {
    EXPECT_TRUE(true);
  }

  auto csv = opt_string_view{"abc"};
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

  EXPECT_EQ("abc"_osv, "abc"_osv);
  EXPECT_LT("abc"_osv, "def"_osv);

  // Hash test.
  std::set<opt_string_view> ss;
  ss.insert("abc"_osv);
  EXPECT_TRUE(ss.contains("abc"_osv));
}

MAKE_TEST_LIST(OptStringViewTest_Construction, OptStringViewTest_Optional,
    OptStringViewTest_Workalike, OptStringViewTest_Cast,
    OptStringViewTestEqual);
