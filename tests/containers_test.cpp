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
#include <vector>

#include "../corvid/containers.h"
#include "minitest.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::internal;
using namespace corvid::sequence;

void OptionalPtrTest_Construction() {
  if (true) {
    optional_ptr<int*> o;
    EXPECT_FALSE(o.has_value());
  }
  if (true) {
    optional_ptr<int*> o{nullptr};
    EXPECT_FALSE(o.has_value());
  }
  if (true) {
    optional_ptr<int*> o{std::nullopt};
    EXPECT_FALSE(o.has_value());
  }
  if (true) {
    int i{42};
    optional_ptr o{&i};
    EXPECT_TRUE(o.has_value());
    o.reset();
    EXPECT_FALSE(o.has_value());
  }
  if (true) {
    optional_ptr o{new int{42}};
    EXPECT_TRUE(o.has_value());
    delete o.get();
    o.reset();
    EXPECT_FALSE(o.has_value());
  }
  if (true) {
    int i{42};
    optional_ptr o{&i};
    optional_ptr qo{std::move(o)};
    // NOLINTBEGIN
    optional_ptr ro{o};
    EXPECT_TRUE(o.has_value());
    // NOLINTEND
    EXPECT_TRUE(qo.has_value());
    EXPECT_TRUE(ro.has_value());
  }
  if (true) {
    auto test{"test"s};
    optional_ptr o{std::make_unique<std::string>(test)};
    // * optional_ptr qo{o};
    optional_ptr ro{std::move(o)};
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(o.has_value());
    EXPECT_TRUE(ro.has_value());
  }
}

void OptionalPtrTest_Access() {
  if (true) {
    auto test{"test"s};
    optional_ptr o = &test;
    EXPECT_TRUE(o.has_value());
    EXPECT_EQ(o.value(), test);
    std::string* p = o;
    EXPECT_EQ(p, o);
    EXPECT_EQ(o->size(), test.size());
    EXPECT_EQ(p, o.get());
    o.reset();
    EXPECT_THROW(o.value(), std::bad_optional_access);
    bool f = o ? true : false;
    EXPECT_FALSE(f);

    // Raw pointers evaluate to bool in any context.
    f = o;
    (void)f;

    o.reset(&test);
    EXPECT_EQ(o->size(), 4U);
  }
  if (true) {
    auto const test{"test"s};
    optional_ptr o = &test;
    auto p = o.get();
    // * o.get()->resize(test.size());
    p++;
    (void)p[6];
    p = p + 1;
    (void)p;
    // * o++;
    // * o[6];
    // * o + 1;
  }
}

void OptionalPtrTest_OrElse() {
  if (true) {
    optional_ptr<std::string*> o;
    EXPECT_FALSE(o.has_value());
    std::string empty{};
    auto test{"test"s};
    auto l = [&]() { return test; };
    EXPECT_EQ(o.value_or(test), test);
    EXPECT_EQ(o.value_or(), empty);
    EXPECT_EQ(o.value_or_ptr(&test), test);
    EXPECT_EQ(o.value_or_fn(l), test);
  }
}

void OptionalPtrTest_ConstOrPtr() {
  if (true) {
    const auto test{"test"s};
    optional_ptr<const std::string*> o;
    auto& p = o.value_or_ptr(&test);
    auto b = std::is_same_v<decltype(p), const std::string&>;
    EXPECT_TRUE(b);
  }
  if (true) {
    const auto test{"test"s};
    optional_ptr<std::string*> o;
    auto& p = o.value_or_ptr(&test);
    EXPECT_TRUE((std::is_same_v<decltype(p), const std::string&>));
  }
  if (true) {
    auto test{"test"s};
    optional_ptr<const std::string*> o;
    auto& p = o.value_or_ptr(&test);
    EXPECT_TRUE((std::is_same_v<decltype(p), const std::string&>));
  }
}

void OptionalPtrTest_Smart() {
  if (true) {
    auto test{"test"s};
    optional_ptr o = std::make_unique<std::string>(test);
    EXPECT_TRUE(o.has_value());
    EXPECT_EQ(o->size(), test.size());

    // * auto qo{o};
    auto qo{std::move(o)};
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_FALSE(o.has_value());
    EXPECT_TRUE(qo.has_value());

    EXPECT_EQ(*qo, test);
    auto& q = *qo;
    q.resize(q.size());
    EXPECT_EQ(q, test);

    // * auto p{qo.get()};
    auto p{std::move(qo).get()};

    auto l = [&test]() {
      return optional_ptr{std::make_unique<std::string>(test)};
    };

    p = l().get();
    p.reset();

    // This was moved from, so it's empty.
    bool f = o ? true : false;
    EXPECT_FALSE(f);

    // Does not compile because operator bool is marked as explicit and this is
    // not a predicate context.
    // * f = o;

    o.reset(std::make_unique<std::string>(test));
    EXPECT_EQ(o->size(), 4U);
    o.reset();
    EXPECT_FALSE(o.has_value());
  }
  if (true) {
    auto test{"test"s};
    optional_ptr o = std::make_shared<std::string>(test);
    EXPECT_TRUE(o.has_value());
    EXPECT_EQ(o->size(), test.size());

    auto qo{o};
    EXPECT_TRUE(o.has_value());
    EXPECT_TRUE(qo.has_value());

    EXPECT_EQ(*qo, test);
    auto& q = *qo;
    q.resize(q.size());
    EXPECT_EQ(q, test);

    auto p{qo.get()};

    bool f = o ? true : false;
    EXPECT_TRUE(f);

    // Does not compile because operator bool is marked as explicit and this is
    // not a predicate context.
    // * f = o;

    EXPECT_EQ(o->size(), 4U);
  }
}

void OptionalPtrTest_Dumb() {
  using O = optional_ptr<int*>;

  if (true) {
    O o = nullptr;
    EXPECT_FALSE(o);
    O p(nullptr);
    EXPECT_FALSE(p);
    EXPECT_FALSE(O{nullptr});
  }
  if (true) {
    int i;
    O o(&i);
    EXPECT_TRUE(o);
    auto& p = (o = nullptr);
    EXPECT_FALSE(o);
    EXPECT_FALSE(p);
  }
  if (true) {
    int i;
    O a(&i), b;
    EXPECT_TRUE(a != b);
    EXPECT_TRUE(!(a == b));
    EXPECT_FALSE(a == O());
    EXPECT_TRUE(b == O());
    EXPECT_FALSE(a == nullptr);
    EXPECT_TRUE(b == nullptr);
    EXPECT_TRUE(a != O());
    EXPECT_FALSE(b != O());
    EXPECT_TRUE(a != nullptr);
    EXPECT_FALSE(b != nullptr);
  }
}

void FindOptTest_Maps() {
  if (true) {
    const auto key = "key"s;
    const auto value = "value"s;
    using C = std::map<std::string, std::string>;
    C m{{key, value}};
    EXPECT_EQ(*find_opt(m, key), value);
    EXPECT_EQ(find_opt(m, value).value_or_ptr(&key), key);
    EXPECT_TRUE(KeyFindable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::map<std::string_view, int>;
    const auto key = "key"sv;
    const auto value = 42;
    C m{{key, value}};
    EXPECT_EQ(*find_opt(m, key), value);
    EXPECT_EQ(find_opt(m, "missing"sv).value_or(0), 0);
    EXPECT_TRUE(KeyFindable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
  }
  if (true) {
    extensible_arena arena{4096};
    extensible_arena::scope s{arena};
    using C = arena_map<std::string_view, int>;
    const auto key = "key"sv;
    const auto value = 42;
    C m{{key, value}};
    EXPECT_EQ(*find_opt(m, key), value);
    EXPECT_EQ(find_opt(m, "missing"sv).value_or(0), 0);
    EXPECT_TRUE(KeyFindable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
  }
}

void FindOptTest_Sets() {
  const auto value = "value"s;
  using C = std::set<std::string>;
  C s{value};
  EXPECT_EQ(*find_opt(s, value), value);
  EXPECT_EQ(find_opt(s, "").value_or("nope"), "nope");
  EXPECT_TRUE(KeyFindable<C>);
  EXPECT_FALSE(RangeWithoutFind<C>);
}

void FindOptTest_Vectors() {
  const auto value = "value"s;
  using C = std::vector<std::string>;
  C s{value};
  EXPECT_EQ(*find_opt(s, value), value);
  EXPECT_EQ(find_opt(s, "").value_or("nope"), "nope");
  EXPECT_FALSE(KeyFindable<C>);
  EXPECT_TRUE(RangeWithoutFind<C>);
}

void FindOptTest_Arrays() {
  int s[]{1, 2, 3, 4};
  using C = decltype(s);
  EXPECT_EQ(*find_opt(s, 3), 3);
  EXPECT_EQ(find_opt(s, 5).value_or(-1), -1);
  EXPECT_FALSE(KeyFindable<C>);
  EXPECT_TRUE(RangeWithoutFind<C>);
}

void FindOptTest_Strings() {
  if (true) {
    using C = std::string;
    C s{"value"};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(KeyFindable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::string_view;
    C s{"value"};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(KeyFindable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::vector<char>;
    C s{'v', 'a', 'l', 'u', 'e'};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(KeyFindable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
}

void Intervals_Ctors() {
  if (true) {
    interval i;
    EXPECT_TRUE(i.empty());
    EXPECT_FALSE(i.invalid());
  }
  if (true) {
    interval i{42};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1U);
    EXPECT_EQ(i.front(), 42);
    EXPECT_EQ(i.back(), 42);
  }
  if (true) {
    interval i{40, 42};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 3U);
    EXPECT_EQ(i.front(), 40);
    EXPECT_EQ(i.back(), 42);
  }
  if (true) {
    // Next line asserts.
    // * interval i{42, 40};
    interval i{40};
    i.min(42);
    EXPECT_TRUE(i.empty());
    EXPECT_TRUE(i.invalid());
  }
}

void IntervalTest_Insert() {
  if (true) {
    interval i;
    EXPECT_TRUE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_TRUE(i.insert(0));
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1U);
    EXPECT_EQ(i.front(), 0);
    EXPECT_EQ(i.back(), 0);

    EXPECT_TRUE(i.insert(5));
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 6U);
    EXPECT_EQ(i.front(), 0);
    EXPECT_EQ(i.back(), 5);

    EXPECT_TRUE(i.insert(-5));
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 11U);
    EXPECT_EQ(i.front(), -5);
    EXPECT_EQ(i.back(), 5);

    EXPECT_FALSE(i.insert(-5));
    EXPECT_FALSE(i.insert(0));
    EXPECT_FALSE(i.insert(5));
  }
  if (true) {
    interval i{5};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1U);

    EXPECT_FALSE(i.push_back(0));
    EXPECT_FALSE(i.push_back(5));
    EXPECT_TRUE(i.push_back(6));
    EXPECT_TRUE(i.push_back(7));
    EXPECT_FALSE(i.push_back(6));
    EXPECT_EQ(i.size(), 3U);
    EXPECT_EQ(i.front(), 5);
    EXPECT_EQ(i.back(), 7);

    i.pop_back();
    EXPECT_EQ(i.size(), 2U);
    EXPECT_EQ(i.front(), 5);
    EXPECT_EQ(i.back(), 6);
    i.pop_back(2);
    EXPECT_TRUE(i.empty());
  }
  if (true) {
    interval i{5};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1U);

    EXPECT_FALSE(i.push_front(7));
    EXPECT_FALSE(i.push_front(6));
    EXPECT_FALSE(i.push_front(5));
    EXPECT_TRUE(i.push_front(4));
    EXPECT_TRUE(i.push_front(3));
    EXPECT_FALSE(i.push_front(6));
    EXPECT_EQ(i.size(), 3U);
    EXPECT_EQ(i.front(), 3);
    EXPECT_EQ(i.back(), 5);

    i.pop_front();
    EXPECT_EQ(i.size(), 2U);
    EXPECT_EQ(i.front(), 4);
    EXPECT_EQ(i.back(), 5);
    i.pop_front(2);
    EXPECT_TRUE(i.empty());
  }
}

void IntervalTest_ForEach() {
  auto i = interval{1, 4};

  int64_t c{}, s{};
  for (auto e : i) {
    ++c;
    s += e;
  }

  EXPECT_EQ(c, 4);
  EXPECT_EQ(s, 1 + 2 + 3 + 4);
}

void IntervalTest_Reverse() {
  if (true) {
    auto i = interval{1, 4};

    int64_t c{}, s{}, l{};
    auto b = std::begin(i), e = std::end(i);
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    EXPECT_EQ(c, 4);
    EXPECT_EQ(s, 1 + 2 + 3 + 4);
    EXPECT_EQ(l, 4);
  }
  if (true) {
    auto i = interval{1, 4};

    int64_t c{}, s{}, l{};
    auto b = std::reverse_iterator(std::end(i)),
         e = std::reverse_iterator(std::begin(i));
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    EXPECT_EQ(c, 4);
    EXPECT_EQ(s, 1 + 2 + 3 + 4);
    EXPECT_EQ(l, 1);
  }
  if (true) {
    auto i = interval{1, 4};

    int64_t c{}, s{}, l{};
    auto b = std::rbegin(i), e = std::rend(i);
    std::for_each(b, e, [&c, &s, &l](auto e) {
      ++c;
      s += e;
      l = e;
    });

    EXPECT_EQ(c, 4);
    EXPECT_EQ(s, 1 + 2 + 3 + 4);
    EXPECT_EQ(l, 1);
  }
}

void IntervalTest_MinMax() {
  auto i = interval{1, 4};

  EXPECT_EQ(i.min(), 1);
  EXPECT_EQ(i.max(), 4);
  i.min(42);
  EXPECT_EQ(i.min(), 42);
  EXPECT_TRUE(i.invalid());
  i.max(64);
  EXPECT_EQ(i.max(), 64);
  EXPECT_FALSE(i.invalid());
}

void IntervalTest_CompareAndSwap() {
  auto i = interval{1, 4};
  auto j = interval{2, 3};
  EXPECT_TRUE(i == i);
  EXPECT_TRUE(j == j);
  EXPECT_TRUE(i != j);
  EXPECT_EQ(i.back(), 4);
  using std::swap;
  swap(i, j);
  EXPECT_EQ(j.back(), 4);
  i.swap(j);
  EXPECT_EQ(i.back(), 4);
}

void IntervalTest_Append() {
  if (true) {
    auto i = interval{1, 4};
    using I = decltype(i);
    EXPECT_FALSE(is_pair_v<decltype(i)>);
    EXPECT_TRUE(is_pair_convertible_v<decltype(i)>);

    auto s = ""s;
    I::append_fn(s, i);
    EXPECT_EQ(s, "1, 4");
    s = strings::concat(i);
    EXPECT_EQ(s, "1, 4");

    s = strings::join<strings::join_opt::json>(i);
    EXPECT_EQ(s, "[1, 4]");

    i.clear();
    s = strings::join<strings::join_opt::json>(i);
    EXPECT_EQ(s, "[]");

    // Note: make_interval is tested in bitmask_enum_test.cpp and
    // sequence_enum_test.cpp.
  }
}

void TransparentTest_General() {
  const auto ks = "key"s;
  const auto ksv = "key"sv;
  if (true) {
    std::map<std::string, int> m;
    string_map<int> tm;
    EXPECT_EQ(m.size(), 0U);
    EXPECT_EQ(tm.size(), 0U);
    m[ks] = 42;
    tm[ks] = 42;
    // * tm[ksv] = 42; // error: no match for ‘operator[]’
    int* p;
    p = find_opt(m, ks);
    EXPECT_TRUE(p);
    EXPECT_EQ(*p, 42);
    // * p = find_opt(m, ksv); // error: no known conversion
    p = find_opt(tm, ksv);
    EXPECT_TRUE(p);
    EXPECT_EQ(*p, 42);
  }
  if (true) {
    string_set tss;
    EXPECT_FALSE(tss.contains(ks));
    EXPECT_FALSE(tss.contains(ksv));
    tss.insert(ks);
    EXPECT_TRUE(tss.contains(ks));
    EXPECT_TRUE(tss.contains(ksv));
  }
  if (true) {
    string_unordered_map<int> tm;
    tm[ks] = 42;
    // * tm[ksv] = 42; // error: no known conversion
    int* p = find_opt(tm, ksv);
    EXPECT_TRUE(p);
    EXPECT_EQ(*p, 42);
  }
  if (true) {
    string_unordered_set tss;
    EXPECT_FALSE(tss.contains(ks));
    EXPECT_FALSE(tss.contains(ksv));
    tss.insert(ks);
    EXPECT_TRUE(tss.contains(ks));
    EXPECT_TRUE(tss.contains(ksv));
  }
}

void IndirectKey_Basic() {
  using IHK = indirect_hash_key<std::string>;
  std::unordered_map<IHK, int> um;
  const auto key{"abc"s};
  // TODO: Follow up on why key can't be a temporary.
  um[key] = 42;
  EXPECT_EQ(um[key], 42);

  using IMK = indirect_map_key<std::string>;
  std::map<IMK, int> m;
  m[key] = 42;
  EXPECT_EQ(m[key], 42);
}

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
constexpr inline auto registry::enum_spec_v<string_id> =
    sequence::make_sequence_enum_spec<string_id, "missing">();

using interned_string = interned_value<std::string, string_id>;
using string_intern_test = intern_test<std::string, string_id>;
using arena_string_intern_test = intern_test<arena_string, string_id>;
using string_intern_table = intern_table<std::string, string_id>;
using string_intern_table_value = string_intern_table::interned_value_t;

template class std::deque<std::string>;

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
    // TODO: We need proper isolated unit tests just for the arena header.
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

  // TODO: Add a test for edge case of arena capacity use. Allocate a small
  // thing, then allocate up to the edge. If the two are contiguous, then it
  // fit.

#if 0
  // TODO: When the traits wrap the key in an unnecessary indirect_hash_key, it
  // breaks terribly. We need to test an arbitrary type that has no natural
  // view as a key, or maybe just use `bad_key`.
#endif
}

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

class SpecialIntDeleter {
public:
  SpecialIntDeleter() = delete;
  SpecialIntDeleter(int x) : x_(x) {}

  void operator()(int* p) const { delete p; }
  int x_;
};

class DefaultIntDeleter {
public:
  void operator()(int* p) const { delete p; }
};

struct D // deleter
{
  static inline std::string_view action;

  D() { action = "ctor"sv; }
  D(const D&) { action = "copy"sv; }
  D(D&) { action = "non-const copy"sv; }
  D(D&&) { action = "move"sv; }
  void operator()(int* p) const {
    action = "delete"sv;
    delete p;
  };
};

void OwnPtrTest_Ctor() {
  {
    own_ptr<int> p;
    own_ptr<int, DefaultIntDeleter> q;

    // If there's no deleter, it points to the object itself.
    EXPECT_EQ(((const void*)&p.get_deleter()), ((const void*)&p));

    // Requires defaultable constructor.
    //* own_ptr<int, SpecialIntDeleter> r;

    own_ptr<int, SpecialIntDeleter> r{nullptr, SpecialIntDeleter{42}};
    EXPECT_GT(sizeof(r), sizeof(int*));

    EXPECT_EQ(sizeof(p), sizeof(int*));
    EXPECT_EQ(sizeof(q), sizeof(int*));
    auto p2 = std::move(p);

    EXPECT_EQ(r.get_deleter().x_, 42);
    auto r2 = std::move(r);
    EXPECT_EQ(r2.get_deleter().x_, 42);
  }
  {
    using P0 = own_ptr<int>;
    EXPECT_TRUE(P0::is_deleter_non_reference_v);
    EXPECT_FALSE(P0::is_deleter_lvalue_reference_v);
    EXPECT_FALSE(P0::is_deleter_const_lvalue_reference_v);
    using P1 = own_ptr<int, D>;
    EXPECT_TRUE(P1::is_deleter_non_reference_v);
    EXPECT_FALSE(P1::is_deleter_lvalue_reference_v);
    EXPECT_FALSE(P1::is_deleter_const_lvalue_reference_v);
    using P2 = own_ptr<int, D&>;
    EXPECT_FALSE(P2::is_deleter_non_reference_v);
    EXPECT_TRUE(P2::is_deleter_lvalue_reference_v);
    EXPECT_FALSE(P2::is_deleter_const_lvalue_reference_v);
    using P3 = own_ptr<int, const D&>;
    EXPECT_FALSE(P3::is_deleter_non_reference_v);
    EXPECT_FALSE(P3::is_deleter_lvalue_reference_v);
    EXPECT_TRUE(P3::is_deleter_const_lvalue_reference_v);
  }

  // Cases from https://en.cppreference.com/w/cpp/memory/unique_ptr.
  {
    // Example constructor(1).
    using P = own_ptr<int>;
    P p;
    P q{nullptr};
    EXPECT_EQ(p.get(), nullptr);
    EXPECT_EQ(q.get(), nullptr);
  }
  {
    // Example constructor(2)
    using P = own_ptr<int>;
    P{new int};
  }
  D d;
  EXPECT_EQ(D::action, "ctor"sv);
  {
    // Example constructor(3a)
    // Non-reference is copied when lvalue.
    using P = own_ptr<int, D>;
    P p{new int, d}; // Copy of d
    EXPECT_EQ(D::action, "copy"sv);
  }
  EXPECT_EQ(D::action, "delete"sv);
  {
    // Example constructor(3b)
    // Reference is held when lvalue.
    using P = own_ptr<int, D&>;
    D::action = "referenced"sv;
    P p{new int, d}; // Reference to d
    EXPECT_EQ(D::action, "referenced"sv);
  }
  EXPECT_EQ(D::action, "delete"sv);
  {
    // Example constructor(4)
    // Non-reference is moved when rvalue.
    using P = own_ptr<int, D>;
    P p{new int, D{}}; // Move of D
    EXPECT_EQ(D::action, "move"sv);
  }
  EXPECT_EQ(D::action, "delete"sv);
  {
    // Example constructor(5)
    // Ownership transfer.
    using P = own_ptr<int>;
    P p{new int};
    P q{std::move(p)};
  }
  EXPECT_EQ(D::action, "delete"sv);
  {
    // Example constructor(6ab)
    // Non-reference is copied when lvalue.
    using P = own_ptr<int, D>;
    P p{new int, d}; // Copy of d
    EXPECT_EQ(D::action, "copy"sv);
    P q{std::move(p)}; // Move of d
    EXPECT_EQ(D::action, "move"sv);
  }
  EXPECT_EQ(D::action, "delete"sv);
  {
    // Example constructor(6cd)
    // Non-reference is copied when lvalue.
    using P = own_ptr<int, D&>;
    using Q = own_ptr<int, D>;
    D::action = "referenced"sv;
    // It cannot be moved. Implicitly deleted.
    //* P q(new int, D{});
    P p{new int, d}; // Copy of d
    EXPECT_EQ(D::action, "referenced"sv);
    Q q{std::move(p)}; // Move of d
    EXPECT_EQ(D::action, "non-const copy"sv);
    // This correctly fails.
    //* P r{new int, D{}};
  }
  EXPECT_EQ(D::action, "delete"sv);
  {
    using P = own_ptr<int, const D&>;
    D::action = "referenced"sv;
    P p{new int, d}; // Reference to d
    EXPECT_EQ(D::action, "referenced"sv);
    // It cannot be moved. Deleted.
    //* P q(new int, D{});
  }
  {
#if 0
    // Regression test.
    using P = own_ptr<int, D&>;
    using Q = own_ptr<int, const D&>;
    // This fails correctly because the only available constructor requires an
    // lvalue, not an rvalue. It takes a `D&`.
    P p{new int, D{}};
    // This now fails correctly but didn't before. It needed an explicit
    // deletion, but also a fix to is_deleter.
    Q q{new int, D{}};
#endif
  }
  {
    // CTAD.
    //* auto pp = std::unique_ptr{new int};
    //* auto p = own_ptr{new int};
    auto pp = std::unique_ptr<int>{new int};
    auto p = own_ptr<int>{new int};

    // auto q = own_ptr{new int, D{}};
#if 0
    auto q{std::move(p)};
    auto uq{std::move(up)};
    auto r = own_ptr{new int, std::default_delete<int>{}};
#endif
    // sabotage with deduction guides to void

    // std::unique_ptr up{new int};
    // (void)up;
  }

  //  EXPECT_FALSE(p);
  //  std::unique_ptr<int> up;

  {
    auto p = own_ptr<int>::make(42);
    EXPECT_EQ(*p, 42);
  }
  // TODO: Test with a move-only pointer type.
}

template<typename T, typename D = std::default_delete<T>>
class Holder {
public:
  template<typename U = void>
  requires std::is_same_v<U, void>
  Holder(T* t) : t_(t) {}

  [[nodiscard]] const T& get() const { return *t_; }

private:
  T* t_;
};

// deduction guide for holder
template<typename T>
Holder(T*) -> Holder<float>;

void DeductionTest_Experimental() {
  int i = 42;
  Holder<int> h0{&i};
  //  Holder h1{&i};
  //  Holder h2{42.0};
}

// NOLINTNEXTLINE(performance-enum-size)
enum class FileDescriptor : int { invalid = -1 };

struct fd_deleter {
  using pointer = custom_handle<fd_deleter, FileDescriptor, int, -1>;

  void operator()(pointer p) {
    if (*p != FileDescriptor::invalid) ++close_count;
  }
  static inline size_t close_count{};
};

using unique_fd = std::unique_ptr<FileDescriptor, fd_deleter>;

void CustomHandleTest_Basic() {
#if 0
  // Baseline unique_ptr.
  if (true) {
    using P = std::unique_ptr<int>;
    P p;
    P q{new int};
    p.reset(q.release());
    q = std::move(p);
  }
  // Custom deleter for unique_ptr.
  if (true) {
    using P = unique_fd;
    EXPECT_EQ(fd_deleter::close_count, 0U);
    P p;
    EXPECT_EQ(sizeof(p), sizeof(int));
    P q{FileDescriptor{42}};
    auto* x = (int*)&p;
    p.reset(FileDescriptor{49});
    auto y = *p;
    EXPECT_EQ(*p, FileDescriptor{42});
    p.reset(q.release());
    q = std::move(p);
    p.reset(FileDescriptor{43});
    EXPECT_EQ(fd_deleter::close_count, 0U);
    FileDescriptor i{49};
    p.reset(i);
    EXPECT_EQ(fd_deleter::close_count, 1U);
    EXPECT_EQ(i, FileDescriptor{42});
    p = unique_fd{std::move(i)};
    EXPECT_EQ(fd_deleter::close_count, 2U);
    EXPECT_EQ(i, FileDescriptor::invalid);
    const FileDescriptor j{42};
    p = unique_fd{j};
    EXPECT_EQ(fd_deleter::close_count, 3U);
    EXPECT_EQ(j, FileDescriptor{42});
    // * p = unique_fd{std::move(j)};
    p.reset();
    EXPECT_EQ(fd_deleter::close_count, 4U);
    i = FileDescriptor{46};
    p.reset(i);
    EXPECT_EQ(*p, FileDescriptor{46});
    EXPECT_EQ(i, FileDescriptor{46});
    i = FileDescriptor{47};

    // Proof that 0 is not the nullptr.
    p = unique_fd{FileDescriptor{0}};
    EXPECT_EQ(*p.get(), FileDescriptor{0});
    EXPECT_EQ(*p, FileDescriptor{0});
    bool is_present = p ? true : false;
    EXPECT_EQ(is_present, true);
    p.reset();
    EXPECT_EQ(fd_deleter::close_count, 6U);
  }
  EXPECT_EQ(fd_deleter::close_count, 8U);
#endif
}

void NoInitResize_Basic() {
  std::vector<int> v;
  v.resize(2);
  std::string s;
  // s.resize_and_overwrite(2);
  (void)s;
}

using FirstName = strong_type<std::string, struct FirstNameTag>;
using LastName = strong_type<std::string, struct LastNameTag>;
using PersonAge = strong_type<long, struct PersonAgeTag>;

using WeakPersonFn =
    std::function<PersonAge(const FirstName&, const LastName&)>;
using PersonFn = strong_type<WeakPersonFn, struct PersonFnTag>;
using WeakPointlessFn = std::function<void(const FirstName&, const LastName&)>;
using PointlessFn = strong_type<WeakPointlessFn, struct PointlessFnTag>;

void StrongType_Basic() {
  FirstName fn{"John"};
  LastName ln{"Smith"};
  EXPECT_EQ(fn.value(), "John");
  EXPECT_EQ(ln.value(), "Smith");
  EXPECT_EQ(fn, "John"s);
  EXPECT_EQ(fn, fn);
  EXPECT_EQ(fn, FirstName{"John"});
  EXPECT_EQ(fn, FirstName{"John"});
  EXPECT_NE(fn, FirstName{"Jane"});
  EXPECT_NE(fn, FirstName{"Jane"});
  // Does not compile, giving clean error message.
  //* EXPECT_NE(fn, ln);
  //++fn;
  //* fn = fn * fn;
  //* fn = fn * 2;
  EXPECT_EQ(fn, FirstName{"John"});
  std::map<FirstName, LastName> m;
  m[fn] = ln;
  std::unordered_map<FirstName, LastName> um;
  um[fn] = ln;
  PersonAge age{42};
  EXPECT_EQ(age.value(), 42);
  EXPECT_EQ(age, 42);
  EXPECT_EQ(age, PersonAge{42});
  EXPECT_NE(age, PersonAge{43});
  ++age;
  EXPECT_EQ(age, 43);
  age = age + 1L;
  EXPECT_EQ(age, 44);
  age = age - 1;
  EXPECT_EQ(age, 43);
  age = age << 1;
}

void StrongType_Extended() {
  // Comprehensive test of all methods and operators for FirstName.

  if (true) {
    // Default ctor.
    FirstName fn;
    EXPECT_EQ(fn, "");
    fn = "John";
    EXPECT_EQ(fn, "John");
    // Copy ctor.
    FirstName fn_copy{fn};
    EXPECT_EQ(fn_copy, "John");
    // Move ctor.
    FirstName fn_moved{std::move(fn_copy)};
    EXPECT_EQ(fn_moved, "John");
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(fn_copy, "");
    // Copy conversion from string.
    std::string name{"Jane"};
    FirstName fn_copy_from_string{name};
    EXPECT_EQ(fn_copy_from_string, "Jane");
    // Move conversion from string.
    FirstName fn_move_from_string{std::move(name)};
    EXPECT_EQ(fn_move_from_string, "Jane");
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(name, "");
    // Conversion from char[].
    char name2[]{"Jim"};
    FirstName fn_from_char_array{name2};
    EXPECT_EQ(fn_from_char_array, "Jim");
  }

  if (true) {
    FirstName fn{"Jane"};
    EXPECT_EQ(fn, "Jane");
    // Homogeneous copy assignment.
    FirstName fn_copy;
    fn_copy = fn;
    EXPECT_EQ(fn_copy, "Jane");
    // Homogeneous move assignment.
    FirstName fn_move;
    fn_move = std::move(fn_copy);
    EXPECT_EQ(fn_move, "Jane");
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(fn_copy, "");
    // Copy from char[].
    char namearray[]{"John"};
    fn = namearray;
    EXPECT_EQ(fn, "John");
    // Copy from string.
    auto name = "Jane"s;
    fn = name;
    EXPECT_EQ(fn, "Jane");
    // Move from string.
    fn->clear();
    fn = std::move(name);
    EXPECT_EQ(fn, "Jane");
    // NOLINTNEXTLINE(bugprone-use-after-move)
    EXPECT_EQ(name, "");
  }

  if (true) {
    // Access and iteration.
    FirstName fn{"John"};
    EXPECT_EQ(fn.value(), "John");
    EXPECT_EQ(fn->size(), 4U);
    EXPECT_EQ(fn->at(0), 'J');
    EXPECT_EQ(fn->front(), 'J');
    EXPECT_EQ(fn->back(), 'n');
    EXPECT_EQ(std::string_view{fn->data()}, "John");
    EXPECT_EQ(std::string_view{fn->c_str()}, "John");
    std::string s;
    for (auto c : fn) s += c;
    EXPECT_EQ(s, "John");
    // You can move through get.
    s.clear();
    s = std::move(*fn);
    EXPECT_EQ(s, "John");
    EXPECT_EQ(fn, "");
  }

  if (true) {
    // Relational ops.
    FirstName fn{"John"};
    FirstName fn2{"Jane"};
    EXPECT_EQ(fn, "John"s);
    EXPECT_EQ(fn, fn);
    // Test spaceship, both heterogeneous and homogeneous.
    EXPECT_TRUE((fn <=> fn) == (std::strong_ordering::equal));
    EXPECT_TRUE((fn <=> fn2) == (std::strong_ordering::greater));
    EXPECT_TRUE((fn2 <=> fn) == (std::strong_ordering::less));
    EXPECT_TRUE((fn <=> "John"s) == (std::strong_ordering::equal));
    EXPECT_TRUE((fn <=> "Zoe"s) == (std::strong_ordering::less));
    EXPECT_TRUE(("Zoe"s <=> fn) == (std::strong_ordering::greater));
    EXPECT_TRUE(("John"s <=> fn) == (std::strong_ordering::equal));
    // Test homogeneous comparisons.
    EXPECT_FALSE(fn == fn2);
    EXPECT_TRUE(fn != fn2);
    EXPECT_FALSE(fn < fn2);
    EXPECT_FALSE(fn <= fn2);
    EXPECT_TRUE(fn > fn2);
    EXPECT_TRUE(fn >= fn2);
    // Test heterogeneous comparisons.
    EXPECT_TRUE(fn == "John"s);
    EXPECT_TRUE((fn <=> "John"s) == (std::strong_ordering::equal));
    EXPECT_FALSE(fn != "John"s);
    EXPECT_TRUE(fn < "Zoe"s);
    EXPECT_TRUE(fn <= "John"s);
    EXPECT_TRUE(fn > "Adam"s);
    EXPECT_TRUE(fn >= "John"s);
    EXPECT_TRUE(("John"s <=> fn) == (std::strong_ordering::equal));
    EXPECT_TRUE("John"s == fn);
    EXPECT_FALSE("John"s != fn);
    EXPECT_TRUE("Zoe"s > fn);
    EXPECT_TRUE("John"s >= fn);
    EXPECT_TRUE("Adam"s < fn);
    EXPECT_TRUE("John"s <= fn);
  }

  // Test unary operators.
  if (true) {
    PersonAge age{42};
    EXPECT_EQ(+age, 42);
    EXPECT_EQ(-age, -42);
    EXPECT_EQ(!age, false);
    EXPECT_EQ(!!age, true);
    EXPECT_EQ(~age, -43);
    EXPECT_EQ(++age, 43);
    EXPECT_EQ(age++, 43);
    EXPECT_EQ(age, 44);
    EXPECT_EQ(--age, 43);
    EXPECT_EQ(age--, 43);
    EXPECT_EQ(age, 42);
    // Test bitwise and bool.
    EXPECT_EQ(age & 1, 0);
    EXPECT_EQ(age | 1, 43);
    EXPECT_EQ(age ^ 1, 43);
    EXPECT_TRUE(age ? true : false);
    EXPECT_EQ(~age, -43);
    EXPECT_EQ(age, static_cast<long>(age));
  }

  // Test binary arithmetic operators.
  if (true) {
    PersonAge age{42};
    EXPECT_EQ(age + 1, 43);
    EXPECT_EQ(age - 1, 41);
    EXPECT_EQ(age * 2, 84);
    EXPECT_EQ(age / 2, 21);
    EXPECT_EQ(age % 5, 2);
    EXPECT_EQ(1 + age, 43);
    EXPECT_EQ(1 - age, -41);
    EXPECT_EQ(2 * age, 84);
    EXPECT_EQ(2 / age, 0);
    EXPECT_EQ(5 % age, 5);
    EXPECT_EQ(age + age, 84);
    EXPECT_EQ(age - age, 0);
    EXPECT_EQ(age * age, 1764);
    EXPECT_EQ(age / age, 1);
    EXPECT_EQ(age % age, 0);
  }

  // Test binary bitwise operators.
  if (true) {
    PersonAge age{42};
    EXPECT_EQ(age & 1, 0);
    EXPECT_EQ(age | 1, 43);
    EXPECT_EQ(age ^ 1, 43);
    EXPECT_EQ(age << 1, 84);
    EXPECT_EQ(age >> 1, 21);
    EXPECT_EQ(1 & age, 0);
    EXPECT_EQ(1 | age, 43);
    EXPECT_EQ(1 ^ age, 43);
    EXPECT_EQ(2 & age, 2);
    EXPECT_EQ(2 | age, 42);
    EXPECT_EQ(2 ^ age, 40);
    // Does not compile:
    //* EXPECT_EQ(2 << age, 16834);
    //* EXPECT_EQ(2 >> age, 0);
    EXPECT_EQ(age & age, 42);
    EXPECT_EQ(age | age, 42);
    EXPECT_EQ(age ^ age, 0);
    age = 1;
    EXPECT_EQ(age << age, 2);
    EXPECT_EQ(age >> age, 0);
  }

  // Test arithmetic assignment operators.
  if (true) {
    PersonAge age{42};
    age += 1;
    EXPECT_EQ(age, 43);
    age -= 1;
    EXPECT_EQ(age, 42);
    age *= 2;
    EXPECT_EQ(age, 84);
    age /= 2;
    EXPECT_EQ(age, 42);
    age %= 5;
    EXPECT_EQ(age, 2);
    long i = 1;
    // Does not compile.
    //* i += age;
    i += *age;
    EXPECT_EQ(i, 3);
  }

  if (true) {
    WeakPersonFn fn = [](const FirstName&, const LastName&) -> PersonAge {
      return PersonAge{42};
    };
    WeakPointlessFn fn2 = [](const FirstName&, const LastName&) {};
    PersonFn pf{fn};
    PointlessFn pf2{fn2};
    EXPECT_EQ((pf.value()(FirstName{"John"}, LastName{"Smith"})),
        PersonAge{42});
    EXPECT_EQ((pf(FirstName{"John"}, LastName{"Smith"})), PersonAge{42});
    // Does not compile, due to nodiscard.
    //* pf(FirstName{"John"}, LastName{"Smith"});
    // This one is void.
    pf2(FirstName{"John"}, LastName{"Smith"});
  }

  if (true) {
    // Test map and unordered_map compatibility.
    std::map<FirstName, LastName> m;
    FirstName fn{"John"};
    LastName ln{"Smith"};
    m[fn] = ln;
    EXPECT_EQ(m[fn].value(), "Smith");
    std::unordered_map<FirstName, LastName> um;
    um[fn] = ln;
    EXPECT_EQ(um[fn].value(), "Smith");

    using StrongMap =
        strong_type<std::map<FirstName, LastName>, struct StrongMapTag>;
    StrongMap sm;
    sm[fn] = ln;
    EXPECT_EQ(sm[fn].value(), "Smith");
  }

  // TODO: Add tests for op->* and perhaps more thorough tests for various
  // different kinds of callbacks: function pointers, mutable lambdas, etc.

  // Assorted tests.
  FirstName fn{"John"};
  LastName ln{"Smith"};
  PersonAge age{42};

  // Test `get` method.
  EXPECT_EQ(fn.value(), "John");
  EXPECT_EQ(ln.value(), "Smith");
  EXPECT_EQ(age.value(), 42);

  // Test equality and inequality operators.
  EXPECT_EQ(fn, "John"s);
  EXPECT_EQ(fn, fn);
  EXPECT_EQ(fn, FirstName{"John"});
  EXPECT_NE(fn, FirstName{"Jane"});
  // Does not compile.
  //*  EXPECT_NE(fn, ln); // Different strong types.

  // Test copy and move constructors.
  FirstName fn_copy{fn};
  EXPECT_EQ(fn_copy, fn);
  FirstName fn_moved{std::move(fn_copy)};
  EXPECT_EQ(fn_moved, fn);

  // Test copy and move assignment operators.
  FirstName fn_assigned = fn;
  EXPECT_EQ(fn_assigned, fn);
  FirstName fn_move_assigned = std::move(fn_assigned);
  EXPECT_EQ(fn_move_assigned, fn);

  // Test arithmetic operators.
  EXPECT_EQ(age + 1, PersonAge{43});
  EXPECT_EQ(age - 1, PersonAge{41});
  EXPECT_EQ(age * 2, PersonAge{84});
  EXPECT_EQ(age / 2, PersonAge{21});
  EXPECT_EQ(age % 5, PersonAge{2});

  // Test arithmetic assignment operators.
  age += 1;
  EXPECT_EQ(age, PersonAge{43});
  age -= 1;
  EXPECT_EQ(age, PersonAge{42});
  age *= 2;
  EXPECT_EQ(age, PersonAge{84});
  age /= 2;
  EXPECT_EQ(age, PersonAge{42});
  age %= 5;
  EXPECT_EQ(age, PersonAge{2});

  // Test increment and decrement operators.
  ++age;
  EXPECT_EQ(age, PersonAge{3});
  age++;
  EXPECT_EQ(age, PersonAge{4});
  --age;
  EXPECT_EQ(age, PersonAge{3});
  age--;
  EXPECT_EQ(age, PersonAge{2});

  // Test bitwise operators.
  EXPECT_EQ(age & 1, PersonAge{0});
  EXPECT_EQ(age | 1, PersonAge{3});
  EXPECT_EQ(age ^ 1, PersonAge{3});
  EXPECT_EQ(age << 1, PersonAge{4});
  EXPECT_EQ(age >> 1, PersonAge{1});

  // Test bitwise assignment operators.
  age &= 1;
  EXPECT_EQ(age, PersonAge{0});
  age |= 3;
  EXPECT_EQ(age, PersonAge{3});
  age ^= 1;
  EXPECT_EQ(age, PersonAge{2});
  age <<= 1;
  EXPECT_EQ(age, PersonAge{4});
  age >>= 1;
  EXPECT_EQ(age, PersonAge{2});

  // Test heterogeneous comparisons.
  EXPECT_TRUE(fn == "John"s);
  EXPECT_FALSE(fn != "John"s);
  EXPECT_TRUE(fn < "Zoe"s);
  EXPECT_TRUE(fn <= "John"s);
  EXPECT_TRUE(fn > "Adam"s);
  EXPECT_TRUE(fn >= "John"s);

  // Test map and unordered_map compatibility.
  std::map<FirstName, LastName> m;
  m[fn] = ln;
  EXPECT_EQ(m[fn].value(), "Smith");

  std::unordered_map<FirstName, LastName> um;
  um[fn] = ln;
  EXPECT_EQ(um[fn].value(), "Smith");

  // Test stream output (if implemented).
  std::ostringstream oss;
  oss << fn;
  EXPECT_EQ(oss.str(), "John");
}

struct RetrievalKey {
  size_t id{};
  std::string name;
};

struct RangeKey {
  size_t start{};
  size_t end{};
};

enum class QueryType : std::uint8_t {
  None,
  Retrieve,
  Range,
  OtherRange,
  Status
};

using QueryVariant = enum_variant<QueryType, std::monostate, RetrievalKey,
    RangeKey, RangeKey, std::string>;

// Utility function to take a variable number of arguments and use
// ostringstream to concatenate them into the returned string.
template<typename... Args>
inline auto format_args(Args&&... args) {
  std::ostringstream oss;
  ((oss << std::forward<Args>(args)), ...);
  return oss.str();
}

// Given a variant type, lists the types in the variant and their indices.
template<typename T>
std::string list_variant_types() {
  std::ostringstream oss;
  [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    ((oss << typeid(std::variant_alternative_t<Is, T>).name() << "\n"), ...);
  }(std::make_index_sequence<std::variant_size_v<T>>{});
  return oss.str();
}

void EnumVariant_Basic() {
  std::variant<std::monostate, int, char, std::string> v;
  list_variant_types<decltype(v)>();
  if (true) {
    QueryVariant qv;
    auto e = qv.index();
    EXPECT_EQ(e, QueryType::None);
  }
  if (true) {
    QueryVariant qv{RetrievalKey{1, "test"}};
    auto e = qv.index();
    EXPECT_EQ(e, QueryType::Retrieve);
  }
  if (true) {
    QueryVariant qv{in_place_enum<QueryType::OtherRange>, RangeKey{10, 20}};
    auto e = qv.index();
    EXPECT_EQ(e, QueryType::OtherRange);
  }
  if (true) {
    QueryVariant qv{in_place_enum<QueryType::Status>};
    auto e = qv.index();
    EXPECT_EQ(e, QueryType::Status);
  }
  if (true) {
    QueryVariant::underlying_type underlying_other_range_key{
        std::in_place_index<3>, RangeKey{10, 20}};
    EXPECT_EQ(underlying_other_range_key.index(),
        (size_t)QueryType::OtherRange);
    QueryVariant qv{in_place_enum<QueryType::OtherRange>, RangeKey{10, 20}};
    auto e = qv.index();
    EXPECT_EQ(e, QueryType::OtherRange);
    auto qv2 = QueryVariant::make<QueryType::Status>();
    qv2 = QueryVariant::make<QueryType::OtherRange>(RangeKey{10, 20});
    //(QueryType::Retrieve, RetrievalKey{2, "retrieve"});
    // QueryVariant qv3{QueryType::OtherRange, RangeKey{10, 20}};
    QueryVariant qv4{QueryType::None};
    qv4 = QueryVariant::make<QueryType::Status>("meh");
    qv4 = QueryType::None;
    e = QueryType::None;
    (void)e;
    qv4 = RetrievalKey{1, "test"};
    qv4 = QueryType::Status;
    qv.emplace<RetrievalKey>(RetrievalKey{1, "retrieve"});
    qv.emplace<RetrievalKey>(1, "retrieve");
    qv.emplace<QueryType::Retrieve>(RetrievalKey{1, "retrieve"});
    qv.emplace<QueryType::Retrieve>(1, "retrieve");
    const auto& r = qv.get<RetrievalKey>();
    EXPECT_EQ(r.id, 1U);
    EXPECT_EQ(r.name, "retrieve");
    // The following won't compile because `e` is not known at compile time.
    // QueryVariant qv5{e};
  }
  if (true) {
    auto visitor = indexed_callbacks( //
        [](std::monostate) { return "None"s; },
        [](const RetrievalKey& rk) {
          return format_args("RetrievalKey(id=", rk.id, ", name=", rk.name,
              ")");
        },
        [](const RangeKey& rk) {
          return format_args("Main RangeKey(start=", rk.start,
              ", end=", rk.end, ")");
        },
        [](const RangeKey& rk) {
          return format_args("Other RangeKey(start=", rk.start,
              ", end=", rk.end, ")");
        },
        [](const std::string& s) { return format_args("Status(", s, ")"); });

    QueryVariant qv{RetrievalKey{1, "retrieve"}};
    std::string s;
    s = visitor.visit(qv);
    switch (qv.index()) {
    case QueryType::None: //
      EXPECT_EQ(s, "None");
      break;
    case QueryType::Retrieve:
      EXPECT_EQ(s, "RetrievalKey(id=1, name=retrieve)");
      break;
    case QueryType::Range:
      EXPECT_EQ(s, "Main RangeKey(start=0, end=0)");
      break;
    case QueryType::OtherRange:
      EXPECT_EQ(s, "Other RangeKey(start=0, end=0)");
      break;
    case QueryType::Status: EXPECT_EQ(s, "Status()"); break;
    }
    qv = QueryVariant::make<QueryType::OtherRange>(RangeKey{10, 20});
    s = visitor.visit(qv);
    EXPECT_EQ(s, "Other RangeKey(start=10, end=20)");
    auto overload_visitor = overloaded_callbacks( //
        [](std::monostate) { return "None"s; },
        [](const RetrievalKey& rk) {
          return format_args("RetrievalKey(id=", rk.id, ", name=", rk.name,
              ")");
        },
        [](const RangeKey& rk) {
          return format_args("Some RangeKey(start=", rk.start,
              ", end=", rk.end, ")");
        },
        [](const std::string& s) { return format_args("Status(", s, ")"); });

    s = overload_visitor.visit(qv);
    EXPECT_EQ(s, "Some RangeKey(start=10, end=20)");
  }
}

MAKE_TEST_LIST(OptionalPtrTest_Construction, OptionalPtrTest_Access,
    OptionalPtrTest_OrElse, OptionalPtrTest_ConstOrPtr, OptionalPtrTest_Dumb,
    FindOptTest_Maps, FindOptTest_Sets, FindOptTest_Vectors,
    FindOptTest_Arrays, FindOptTest_Strings, Intervals_Ctors,
    IntervalTest_Insert, IntervalTest_ForEach, IntervalTest_Reverse,
    IntervalTest_MinMax, IntervalTest_CompareAndSwap, IntervalTest_Append,
    TransparentTest_General, IndirectKey_Basic, InternTableTest_Basic,
    InternTableTest_Badkey, OwnPtrTest_Ctor, DeductionTest_Experimental,
    CustomHandleTest_Basic, NoInitResize_Basic, StrongType_Basic,
    StrongType_Extended, EnumVariant_Basic);

// Ok, so the plan is to make all of the Ptr/Del ctors take the same three
// templated arguments. The third is just a named thing that's defaulted to
// void and then the requires clause requires it to be void. Then we add a
// bunch of deduction guides that set the must-be-void to something else.
