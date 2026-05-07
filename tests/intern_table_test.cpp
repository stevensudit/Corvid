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

#include <deque>
#include <string>

#include "../corvid/containers.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::internal;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

namespace corvid { inline namespace container { inline namespace intern {

// Test fixture to allow access to internals.
template<typename T, SequentialEnum ID>
struct intern_test {
  using interned_value_t = interned_value<T, ID>;
  using allow = restrict_intern_construction::allow;
  template<typename U>
  static interned_value_t make(U&& u, ID id = {}) {
    return interned_value_t{allow::ctor, std::forward<U>(u), id};
  }
};
}}} // namespace corvid::container::intern

enum class string_id : std::uint8_t { missing };

template<>
constexpr inline auto corvid::enums::registry::enum_spec_v<string_id> =
    corvid::enums::sequence::make_sequence_enum_spec<string_id, "missing">();

using interned_string = interned_value<std::string, string_id>;
using string_intern_test = intern_test<std::string, string_id>;
using arena_string_intern_test = intern_test<arena_string, string_id>;
using string_intern_table = intern_table<std::string, string_id>;
using string_intern_table_value = string_intern_table::interned_value_t;

template class std::deque<std::string>;

#pragma region Basic

void InternTableTest_Basic() {
  if (true) {
    // Test arena in isolation to reproduce corrected bugs.
    extensible_arena arena{128};
    extensible_arena::scope s{arena};

    arena_string as_abc{"abc"};
    arena_string as;

    // This causes a new node to be allocated, which triggered a fencepost bug.
    // That was compounded by a second bug, in which the new buffer was too
    // small.
    as.resize(256);
    bool used_to_crash = as_abc > as;
    EXPECT_TRUE(used_to_crash);
  }
  if (true) {
    extensible_arena arena{4096};
    extensible_arena::scope s{arena};
    //  using arena_value_t = SIT::arena_value_t;
    //  using key_t = SIT::key_t;
    // using lookup_by_id_t = SIT::lookup_by_id_t;

    // lookup_by_id_t
    std::string key{"abc"};
    std::deque<std::string> dq{42};
    arena_deque<arena_string> adq{42};
    auto z = key + key;
    (void)z;
  }
  if (true) {
    // Show that, when we're not using arena-specialized types, we can create
    // interned values that aren't actually in an arena.
    extensible_arena arena{4096};
    extensible_arena::scope s{arena};
    std::string abc_str{"abc"};
    std::string bcd_str{"bcdefghijklmnopqrstuvwxyz"};
    // These are `interned_value` objects but the value pointed at is not
    // interned or in the arena.
    auto abc = string_intern_test::make(abc_str);
    auto bcd = string_intern_test::make(bcd_str);
    EXPECT_FALSE(extensible_arena::contains(&abc.value()));
    EXPECT_FALSE(extensible_arena::contains(abc.value().data()));
    EXPECT_FALSE(extensible_arena::contains(&bcd.value()));
    EXPECT_FALSE(extensible_arena::contains(bcd.value().data()));
    EXPECT_EQ(abc, abc);
    EXPECT_NE(abc, bcd);
    EXPECT_EQ(abc.value(), "abc");
    EXPECT_LT(abc, bcd);
    EXPECT_EQ(abc.value(), abc_str);
    EXPECT_EQ(bcd.value(), bcd_str);
  }
  if (true) {
    // Show that, when we do use arena-specialized types, the values we create
    // are not in the arena, but what's contained within them is.
    extensible_arena arena{4096};
    extensible_arena::scope s{arena};
    // Does not use arena despite being an arena_string because it's short.
    arena_string abc_str{"abc"};
    // Does use arena.
    arena_string bcd_str{"bcdefghijklmnopqrstuvwxyz"};
    // These are `interned_value` objects but the value pointed at is not
    // interned. The contents of `bcd` are in the arena, however.
    auto abc = arena_string_intern_test::make(abc_str);
    auto bcd = arena_string_intern_test::make(bcd_str);
    EXPECT_FALSE(extensible_arena::contains(&abc.value()));
    EXPECT_FALSE(extensible_arena::contains(abc.value().data()));
    EXPECT_FALSE(extensible_arena::contains(&bcd.value()));
    // Short-string optimization is why "abc" isn't in the arena.
    EXPECT_FALSE(extensible_arena::contains(abc.value().data()));
    EXPECT_TRUE(extensible_arena::contains(bcd.value().data()));
    EXPECT_EQ(abc, abc);
    EXPECT_NE(abc, bcd);
    EXPECT_EQ(abc.value(), "abc"sv);
    EXPECT_LT(abc, bcd);
    EXPECT_EQ(abc.value(), abc_str);
    EXPECT_EQ(bcd.value(), bcd_str);
  }
  if (true) {
    // Show that we can intern strings.
    extensible_arena arena{4096};
    extensible_arena::scope s{arena};
    auto sit_ptr = string_intern_table::make(string_id{0}, string_id{3});
    auto& sit = *sit_ptr;
    const auto& csit = sit;
    using SIT = std::remove_reference_t<decltype(sit)>;

    auto iv = sit("abc"s);
    EXPECT_FALSE(iv);
    iv = sit.intern("abc");
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{1});
    EXPECT_EQ(iv.value(), "abc");
    // Both the string and its contents are in the arena.
    EXPECT_TRUE(extensible_arena::contains(&iv.value()));
    EXPECT_TRUE(extensible_arena::contains(iv.value().data()));
    iv = SIT::interned_value_t{};
    EXPECT_FALSE(iv);
    using C = SIT::lookup_by_value_t;
    EXPECT_TRUE(KeyFindable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
    iv = sit("abc");
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{1});
    EXPECT_EQ(iv.value(), "abc");
    EXPECT_TRUE(extensible_arena::contains(&iv.value()));
    EXPECT_TRUE(extensible_arena::contains(iv.value().data()));

    iv = sit("defghijklmnopqrstuvwxyz"sv);
    EXPECT_FALSE(iv);
    iv = sit.intern("defghijklmnopqrstuvwxyz"sv);
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{2});
    EXPECT_EQ(iv.value(), "defghijklmnopqrstuvwxyz"sv);
    // Non-short strings are in the arena.
    EXPECT_TRUE(extensible_arena::contains(&iv.value()));
    EXPECT_TRUE(extensible_arena::contains(iv.value().data()));

    iv = string_intern_table_value{csit, "ghi"s};
    EXPECT_FALSE(iv);
    iv = string_intern_table_value{sit, "ghi"s};
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{3});
    EXPECT_EQ(iv.value(), "ghi"s);

    iv = sit("jkl");
    EXPECT_FALSE(iv);
    iv = sit.intern("jkl");
    EXPECT_FALSE(iv);

    iv = string_intern_table_value{csit, string_id{3}};
    EXPECT_EQ(iv.id(), string_id{3});
    EXPECT_EQ(iv.value(), "ghi"s);

    iv = string_intern_table_value{csit, "abc"};
    EXPECT_EQ(iv.id(), string_id{1});
    EXPECT_EQ(iv.value(), "abc");
  }
}
#pragma endregion

// This is not technically a `std::string`, so it uses the general traits,
// including the indirect wrappers.
struct bad_key: std::string {
  bad_key() = default;
  explicit bad_key(const std::string& s) : std::string(s) {}
  bad_key(const bad_key&) = default;
  bad_key(bad_key&&) = default;
  bad_key& operator=(bad_key&&) = default;
  bad_key& operator=(const bad_key&) = delete;

  friend auto operator<=>(const bad_key& a, const bad_key& b) {
    return static_cast<const std::string&>(a) <=>
           static_cast<const std::string&>(b);
  }
};

template<>
struct std::hash<bad_key>: std::hash<std::string> {};

template<>
struct std::equal_to<bad_key>: std::equal_to<std::string> {};

using interned_badkey = interned_value<bad_key, string_id>;
using badkey_intern_test = intern_test<bad_key, string_id>;
using badkey_intern_table = intern_table<bad_key, string_id>;

#pragma region Badkey

void InternTableTest_Badkey() {
  if (true) {
    const auto bk_abc = bad_key{"abc"};
    const auto bk_bcd = bad_key{"bcd"};
    auto abc = badkey_intern_test::make(bk_abc);
    auto bcd = badkey_intern_test::make(bk_bcd);
    EXPECT_EQ(abc, abc);
    EXPECT_NE(abc, bcd);
    EXPECT_EQ(abc.value(), bk_abc);
    EXPECT_LT(abc, bcd);
  }
  if (true) {
    auto sit_ptr = badkey_intern_table::make(string_id{0}, string_id{3});
    auto& sit = *sit_ptr;
    using SIT = std::remove_reference_t<decltype(sit)>;
    const auto bk_abc = bad_key{"abc"};
    const auto bk_bcd = bad_key{"bcd"};

    auto iv = sit(bk_abc);

    EXPECT_FALSE(iv);
    iv = sit.intern(bk_abc);
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{1});
    EXPECT_EQ(iv.value(), bk_abc);
    iv = SIT::interned_value_t{};
    EXPECT_FALSE(iv);
    iv = sit(bk_abc);
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{1});
    EXPECT_EQ(iv.value(), bk_abc);

    const auto bk_def = bad_key{"def"};
    iv = sit(bk_def);
    EXPECT_FALSE(iv);
    iv = sit.intern(bk_def);
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{2});
    EXPECT_EQ(iv.value(), bk_def);

    const auto bk_ghi = bad_key{"ghi"};
    iv = sit(bk_ghi);
    EXPECT_FALSE(iv);
    iv = sit.intern(bk_ghi);
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{3});
    EXPECT_EQ(iv.value(), bk_ghi);

    const auto bk_jkl = bad_key{"jkl"};
    iv = sit(bk_jkl);
    EXPECT_FALSE(iv);
    iv = sit.intern(bk_jkl);
    EXPECT_FALSE(iv);
  }
}
#pragma endregion

MAKE_TEST_LIST(InternTableTest_Basic, InternTableTest_Badkey);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
