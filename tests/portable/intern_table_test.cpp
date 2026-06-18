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

#include "corvid/containers.h"
#include "catch2_main.h"

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
consteval auto corvid_enum_spec(string_id*) {
  return corvid::enums::sequence::make_sequence_enum_spec<string_id,
      "missing">();
}

using interned_string = interned_value<std::string, string_id>;
using string_intern_test = intern_test<std::string, string_id>;
using arena_string_intern_test = intern_test<arena_string, string_id>;
using string_intern_table = intern_table<std::string, string_id>;
using string_intern_table_value = string_intern_table::interned_value_t;

template class std::deque<std::string>;

#pragma region Basic

TEST_CASE("Basic", "[InternTableTest]") {
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
    CHECK(used_to_crash);
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
    CHECK_FALSE(extensible_arena::contains(&abc.value()));
    CHECK_FALSE(extensible_arena::contains(abc.value().data()));
    CHECK_FALSE(extensible_arena::contains(&bcd.value()));
    CHECK_FALSE(extensible_arena::contains(bcd.value().data()));
    CHECK(abc == abc);
    CHECK(abc != bcd);
    CHECK(abc.value() == "abc");
    CHECK((abc) < (bcd));
    CHECK(abc.value() == abc_str);
    CHECK(bcd.value() == bcd_str);
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
    CHECK_FALSE(extensible_arena::contains(&abc.value()));
    CHECK_FALSE(extensible_arena::contains(abc.value().data()));
    CHECK_FALSE(extensible_arena::contains(&bcd.value()));
    // Short-string optimization is why "abc" isn't in the arena.
    CHECK_FALSE(extensible_arena::contains(abc.value().data()));
    CHECK(extensible_arena::contains(bcd.value().data()));
    CHECK(abc == abc);
    CHECK(abc != bcd);
    CHECK(abc.value() == "abc"sv);
    CHECK((abc) < (bcd));
    CHECK(abc.value() == abc_str);
    CHECK(bcd.value() == bcd_str);
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
    CHECK_FALSE(iv);
    iv = sit.intern("abc");
    CHECK(iv);
    CHECK(iv.id() == string_id{1});
    CHECK(iv.value() == "abc");
    // Both the string and its contents are in the arena.
    CHECK(extensible_arena::contains(&iv.value()));
    CHECK(extensible_arena::contains(iv.value().data()));
    iv = SIT::interned_value_t{};
    CHECK_FALSE(iv);
    using C = SIT::lookup_by_value_t;
    CHECK(KeyFindable<C>);
    CHECK_FALSE(RangeWithoutFind<C>);
    iv = sit("abc");
    CHECK(iv);
    CHECK(iv.id() == string_id{1});
    CHECK(iv.value() == "abc");
    CHECK(extensible_arena::contains(&iv.value()));
    CHECK(extensible_arena::contains(iv.value().data()));

    // Plain `{}` forwards to the value's formatter (honoring its spec);
    // debug `{:?}` shows the `(value, id)` pair, with the id as a number.
    CHECK(std::format("{}", iv) == "abc");
    CHECK(std::format("{:>5}", iv) == "  abc");
    CHECK(std::format("{:?}", iv) == "(\"abc\", 1)");

    iv = sit("defghijklmnopqrstuvwxyz"sv);
    CHECK_FALSE(iv);
    iv = sit.intern("defghijklmnopqrstuvwxyz"sv);
    CHECK(iv);
    CHECK(iv.id() == string_id{2});
    CHECK(iv.value() == "defghijklmnopqrstuvwxyz"sv);
    // Non-short strings are in the arena.
    CHECK(extensible_arena::contains(&iv.value()));
    CHECK(extensible_arena::contains(iv.value().data()));

    iv = string_intern_table_value{csit, "ghi"s};
    CHECK_FALSE(iv);
    iv = string_intern_table_value{sit, "ghi"s};
    CHECK(iv);
    CHECK(iv.id() == string_id{3});
    CHECK(iv.value() == "ghi"s);

    iv = sit("jkl");
    CHECK_FALSE(iv);
    iv = sit.intern("jkl");
    CHECK_FALSE(iv);

    iv = string_intern_table_value{csit, string_id{3}};
    CHECK(iv.id() == string_id{3});
    CHECK(iv.value() == "ghi"s);

    iv = string_intern_table_value{csit, "abc"};
    CHECK(iv.id() == string_id{1});
    CHECK(iv.value() == "abc");
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
