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
#include <set>
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
    // Both the string and its contents are in the table's own arena, not the
    // ambient one. (The ambient checks read true before the `scope` restore
    // fix, because the table's arena leaked into the thread-local slot and
    // stayed installed; they now pin that regression.)
    CHECK(sit.contains(&iv.value()));
    CHECK(sit.contains(iv.value().data()));
    CHECK_FALSE(extensible_arena::contains(&iv.value()));
    CHECK_FALSE(extensible_arena::contains(iv.value().data()));
    iv = SIT::interned_value_t{};
    CHECK_FALSE(iv);
    using C = SIT::lookup_by_value_t;
    CHECK(KeyFindable<C>);
    CHECK_FALSE(RangeWithoutFind<C>);
    iv = sit("abc");
    CHECK(iv);
    CHECK(iv.id() == string_id{1});
    CHECK(iv.value() == "abc");
    CHECK(sit.contains(&iv.value()));
    CHECK(sit.contains(iv.value().data()));

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
    // Non-short strings are in the arena too.
    CHECK(sit.contains(&iv.value()));
    CHECK(sit.contains(iv.value().data()));

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

#pragma region Comparison

TEST_CASE("Comparison", "[InternTableTest]") {
  if (true) {
    // Equality is identity, so equal contents interned in unrelated tables
    // compare unequal; ordering breaks the value tie by address, keeping
    // ordering equivalence consistent with equality.
    auto lhs_ptr = string_intern_table::make();
    auto rhs_ptr = string_intern_table::make();
    auto a = lhs_ptr->intern("foo");
    auto b = rhs_ptr->intern("foo");
    CHECK(a != b);
    // The extra parens keep Catch2 from decomposing the ordering-vs-0
    // comparison, which trips on the consteval literal-zero parameter.
    CHECK(((a <=> b) != 0));
    CHECK(((a < b) != (b < a)));
    CHECK(((a <=> a) == 0));

    // Mixed-type equality asks about content, so both duplicates match the
    // same view while remaining unequal to each other.
    CHECK(a == "foo"sv);
    CHECK(b == "foo"sv);

    // The duplicates land as distinct-but-adjacent keys in an ordered
    // container, while the same singleton collapses.
    auto a2 = lhs_ptr->intern("foo");
    CHECK(a == a2);
    std::set<interned_string> set{a, b, a2};
    CHECK(set.size() == 2);
  }
  if (true) {
    // Value order dominates ordering; the address tie-break only decides
    // equal values.
    auto sit_ptr = string_intern_table::make();
    auto abc = sit_ptr->intern("abc");
    auto bcd = sit_ptr->intern("bcd");
    CHECK(abc < bcd);
    CHECK(bcd > abc);
    CHECK(abc <= abc);
    CHECK(abc >= abc);

    // Heterogeneous equality compares content in both directions, matching
    // the heterogeneous ordering that already existed.
    CHECK(abc == "abc"sv);
    CHECK("abc"sv == abc);
    CHECK(abc != "bcd"sv);
    CHECK(abc == "abc"s);
    CHECK(abc < "b"sv);
    CHECK("b"sv > abc);
  }
  if (true) {
    // Empty orders below every non-empty value and ties with empty, the
    // `std::optional` model, so the order stays total.
    auto sit_ptr = string_intern_table::make();
    auto abc = sit_ptr->intern("abc");
    interned_string empty;
    interned_string empty2;
    CHECK(empty == empty2);
    CHECK(((empty <=> empty2) == 0));
    CHECK(empty < abc);
    CHECK(abc > empty);
    CHECK_FALSE(empty == abc);

    // An ordered container admits one empty alongside the values.
    std::set<interned_string> set{empty, abc, empty2};
    CHECK(set.size() == 2);
    CHECK(set.contains(empty));

    // An empty equals no view, not even an empty one; it orders below all.
    CHECK_FALSE(empty == "foo"sv);
    CHECK_FALSE("foo"sv == empty);
    CHECK(empty != ""sv);
    CHECK(empty != ""s);
    CHECK(empty < "foo"sv);
    CHECK("foo"sv > empty);
    CHECK(""sv > empty);
  }
}
#pragma endregion

#pragma region Chaining

TEST_CASE("Chaining", "[InternTableTest]") {
  if (true) {
    // A `make_next` table starts one past the base and resolves chained
    // lookups, by ID and by value, to the base's singletons.
    auto base_ptr = string_intern_table::make(string_id{0}, string_id{2});
    auto& base = *base_ptr;
    auto alpha = base.intern("alpha");
    CHECK(alpha.id() == string_id{1});

    auto derived_ptr = base_ptr->make_next(string_id{5});
    auto& derived = *derived_ptr;
    auto delta = derived.intern("delta");
    CHECK(delta.id() == string_id{3});

    auto found = derived.get(string_id{1});
    CHECK(found);
    CHECK(&found.value() == &alpha.value());
    found = derived.get("alpha"sv);
    CHECK(found);
    CHECK(found.id() == string_id{1});
    CHECK(&found.value() == &alpha.value());

    // A caller-held attestation on the derived table stays on the derived
    // table; the chained call takes the base's own lock. (Forwarding the
    // attestation across the chain would throw on the mixed-lock check.)
    lock att{derived.sync};
    found = derived.get("alpha"sv, att);
    CHECK(found);
    CHECK(&found.value() == &alpha.value());
  }
  if (true) {
    // An explicit `min_id` alongside `next` is honored, allowing a reserved
    // gap in the ID space; it used to be silently overwritten with one past
    // the base's `max_id`.
    auto base_ptr = string_intern_table::make(string_id{0}, string_id{2});
    auto alpha = base_ptr->intern("alpha");
    auto gap_ptr =
        string_intern_table::make(string_id{10}, string_id{12}, base_ptr);
    auto& gap = *gap_ptr;
    auto omega = gap.intern("omega");
    CHECK(omega.id() == string_id{10});

    // IDs in the gap resolve to empty through the chain, while base IDs and
    // values still resolve to the base's singletons.
    CHECK_FALSE(gap.get(string_id{5}));
    CHECK(gap.get(string_id{1}).id() == string_id{1});
    CHECK(&gap.get("alpha"sv).value() == &alpha.value());
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
