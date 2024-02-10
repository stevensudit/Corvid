// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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
#include "AccutestShim.h"

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
    optional_ptr ro{o};
    EXPECT_TRUE(o.has_value());
    EXPECT_TRUE(qo.has_value());
    EXPECT_TRUE(ro.has_value());
  }
  if (true) {
    auto test{"test"s};
    optional_ptr o{std::make_unique<std::string>(test)};
    // * optional_ptr qo{o};
    optional_ptr ro{std::move(o)};
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

    o.reset(&test);
    EXPECT_EQ(o->size(), 4u);
  }
  if (true) {
    auto const test{"test"s};
    optional_ptr o = &test;
    auto p = o.get();
    // * o.get()->resize(test.size());
    p++;
    (void)p[6];
    p = p + 1;
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
    EXPECT_EQ(o->size(), 4u);
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

    EXPECT_EQ(o->size(), 4u);
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
    EXPECT_TRUE(Findable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::map<std::string_view, int>;
    const auto key = "key"sv;
    const auto value = 42;
    C m{{key, value}};
    EXPECT_EQ(*find_opt(m, key), value);
    EXPECT_EQ(find_opt(m, "missing"sv).value_or(0), 0);
    EXPECT_TRUE(Findable<C>);
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
    EXPECT_TRUE(Findable<C>);
    EXPECT_FALSE(RangeWithoutFind<C>);
  }
}

void FindOptTest_Sets() {
  const auto value = "value"s;
  using C = std::set<std::string>;
  C s{value};
  EXPECT_EQ(*find_opt(s, value), value);
  EXPECT_EQ(find_opt(s, "").value_or("nope"), "nope");
  EXPECT_TRUE(Findable<C>);
  EXPECT_FALSE(RangeWithoutFind<C>);
}

void FindOptTest_Vectors() {
  const auto value = "value"s;
  using C = std::vector<std::string>;
  C s{value};
  EXPECT_EQ(*find_opt(s, value), value);
  EXPECT_EQ(find_opt(s, "").value_or("nope"), "nope");
  EXPECT_FALSE(Findable<C>);
  EXPECT_TRUE(RangeWithoutFind<C>);
}

void FindOptTest_Arrays() {
  int s[]{1, 2, 3, 4};
  using C = decltype(s);
  EXPECT_EQ(*find_opt(s, 3), 3);
  EXPECT_EQ(find_opt(s, 5).value_or(-1), -1);
  EXPECT_FALSE(Findable<C>);
  EXPECT_TRUE(RangeWithoutFind<C>);
}

void FindOptTest_Strings() {
  if (true) {
    using C = std::string;
    C s{"value"};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(Findable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::string_view;
    C s{"value"};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(Findable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
  if (true) {
    using C = std::vector<char>;
    C s{'v', 'a', 'l', 'u', 'e'};
    EXPECT_EQ(*find_opt(s, 'a'), 'a');
    EXPECT_FALSE(contains(s, 'z'));
    EXPECT_FALSE(Findable<C>);
    EXPECT_TRUE(RangeWithoutFind<C>);
  }
}

void FindOptTest_Reversed() {
  if (true) {
    using C = std::vector<char>;
    C s{'v', 'a', 'l', 'u', 'e'};
    char c{};
    for (auto e : s) {
      c = e;
    }
    EXPECT_EQ(c, 'e');
  }
  if (true) {
    using C = std::vector<char>;
    C s{'v', 'a', 'l', 'u', 'e'};
    char c{};
    for (auto e : reversed_range(s)) {
      c = e;
    }
    EXPECT_EQ(c, 'v');
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
    EXPECT_EQ(i.size(), 1u);
    EXPECT_EQ(i.front(), 42);
    EXPECT_EQ(i.back(), 42);
  }
  if (true) {
    interval i{40, 42};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 3u);
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
    EXPECT_EQ(i.size(), 1u);
    EXPECT_EQ(i.front(), 0);
    EXPECT_EQ(i.back(), 0);

    EXPECT_TRUE(i.insert(5));
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 6u);
    EXPECT_EQ(i.front(), 0);
    EXPECT_EQ(i.back(), 5);

    EXPECT_TRUE(i.insert(-5));
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 11u);
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
    EXPECT_EQ(i.size(), 1u);

    EXPECT_FALSE(i.push_back(0));
    EXPECT_FALSE(i.push_back(5));
    EXPECT_TRUE(i.push_back(6));
    EXPECT_TRUE(i.push_back(7));
    EXPECT_FALSE(i.push_back(6));
    EXPECT_EQ(i.size(), 3u);
    EXPECT_EQ(i.front(), 5);
    EXPECT_EQ(i.back(), 7);

    i.pop_back();
    EXPECT_EQ(i.size(), 2u);
    EXPECT_EQ(i.front(), 5);
    EXPECT_EQ(i.back(), 6);
    i.pop_back(2);
    EXPECT_TRUE(i.empty());
  }
  if (true) {
    interval i{5};
    EXPECT_FALSE(i.empty());
    EXPECT_FALSE(i.invalid());
    EXPECT_EQ(i.size(), 1u);

    EXPECT_FALSE(i.push_front(7));
    EXPECT_FALSE(i.push_front(6));
    EXPECT_FALSE(i.push_front(5));
    EXPECT_TRUE(i.push_front(4));
    EXPECT_TRUE(i.push_front(3));
    EXPECT_FALSE(i.push_front(6));
    EXPECT_EQ(i.size(), 3u);
    EXPECT_EQ(i.front(), 3);
    EXPECT_EQ(i.back(), 5);

    i.pop_front();
    EXPECT_EQ(i.size(), 2u);
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
  EXPECT_EQ(i, i);
  EXPECT_EQ(j, j);
  EXPECT_NE(i, j);
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
    EXPECT_EQ(m.size(), 0u);
    EXPECT_EQ(tm.size(), 0u);
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
  um[IHK{"abc"}] = 42;
  EXPECT_EQ(um[IHK{"abc"}], 42);

  using IMK = indirect_map_key<std::string>;
  std::map<IMK, int> m;
  m[IMK{"abc"}] = 42;
  EXPECT_EQ(m[IMK{"abc"}], 42);
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

enum class string_id { missing };

template<>
constexpr inline auto registry::enum_spec_v<string_id> =
    sequence::make_sequence_enum_spec<string_id, "missing">();

using interned_string = interned_value<std::string, string_id>;
using string_intern_test = intern_test<std::string, string_id>;
using arena_string_intern_test = intern_test<arena_string, string_id>;
using string_intern_table = intern_table<std::string, string_id>;
using string_intern_table_value = string_intern_table::interned_value_t;

void InternTableTest_Basic() {
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

    auto iv = sit("abc");
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
    EXPECT_TRUE(Findable<C>);
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
  bad_key(bad_key&&) = default;
  bad_key& operator=(bad_key&&) = default;
  bad_key(const bad_key&) = delete;
  bad_key& operator=(const bad_key&) = delete;
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
    auto abc = badkey_intern_test::make(bad_key{"abc"});
    auto bcd = badkey_intern_test::make(bad_key{"bcd"});
    EXPECT_EQ(abc, abc);
    EXPECT_NE(abc, bcd);
    EXPECT_EQ(abc.value(), bad_key{"abc"});
    EXPECT_LT(abc, bcd);
  }
  if (true) {
    auto sit_ptr = badkey_intern_table::make(string_id{0}, string_id{3});
    auto& sit = *sit_ptr;
    using SIT = std::remove_reference_t<decltype(sit)>;
    auto iv = sit(bad_key{"abc"});

    EXPECT_FALSE(iv);
    iv = sit.intern(bad_key{"abc"});
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{1});
    EXPECT_EQ(iv.value(), bad_key{"abc"});
    iv = SIT::interned_value_t{};
    EXPECT_FALSE(iv);
    iv = sit(bad_key{"abc"});
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{1});
    EXPECT_EQ(iv.value(), bad_key{"abc"});

    iv = sit(bad_key{"def"});
    EXPECT_FALSE(iv);
    iv = sit.intern(bad_key{"def"});
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{2});
    EXPECT_EQ(iv.value(), bad_key{"def"});

    iv = sit(bad_key{"ghi"s});
    EXPECT_FALSE(iv);
    iv = sit.intern(bad_key{"ghi"s});
    EXPECT_TRUE(iv);
    EXPECT_EQ(iv.id(), string_id{3});
    EXPECT_EQ(iv.value(), bad_key{"ghi"s});

    iv = sit(bad_key{"jkl"});
    EXPECT_FALSE(iv);
    iv = sit.intern(bad_key{"jkl"});
    EXPECT_FALSE(iv);
  }
}

MAKE_TEST_LIST(OptionalPtrTest_Construction, OptionalPtrTest_Access,
    OptionalPtrTest_OrElse, OptionalPtrTest_ConstOrPtr, OptionalPtrTest_Dumb,
    FindOptTest_Maps, FindOptTest_Sets, FindOptTest_Vectors,
    FindOptTest_Arrays, FindOptTest_Strings, FindOptTest_Reversed,
    Intervals_Ctors, IntervalTest_Insert, IntervalTest_ForEach,
    IntervalTest_Reverse, IntervalTest_MinMax, IntervalTest_CompareAndSwap,
    IntervalTest_Append, TransparentTest_General, IndirectKey_Basic,
    InternTableTest_Basic, InternTableTest_Badkey);
