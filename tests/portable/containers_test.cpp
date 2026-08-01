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

#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "corvid/containers.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::internal;

// Enum type for testing enum_vector.
enum class test_id_t : size_t { invalid = std::numeric_limits<size_t>::max() };
consteval auto corvid_enum_spec(test_id_t*) {
  return corvid::enums::sequence::make_sequence_enum_spec<test_id_t, "">();
}

// Enum with a narrow underlying type, for the size_as_enum wrap pin.
enum class small_id_t : uint8_t {};
consteval auto corvid_enum_spec(small_id_t*) {
  return corvid::enums::sequence::make_sequence_enum_spec<small_id_t, "">();
}

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

#pragma region TransparentTest_General

TEST_CASE("General", "[TransparentTest]") {
  const auto ks = "key"s;
  const auto ksv = "key"sv;
  if (true) {
    std::map<std::string, int> m;
    string_map<int> tm;
    CHECK(m.size() == 0U);
    CHECK(tm.size() == 0U);
    m[ks] = 42;
    tm[ks] = 42;
    // * tm[ksv] = 42; // error: no match for 'operator[]'
    int* p;
    p = find_opt(m, ks);
    CHECK(p);
    CHECK(*p == 42);
    // * p = find_opt(m, ksv); // error: no known conversion
    p = find_opt(tm, ksv);
    CHECK(p);
    CHECK(*p == 42);
  }
  if (true) {
    string_set tss;
    CHECK_FALSE(tss.contains(ks));
    CHECK_FALSE(tss.contains(ksv));
    tss.insert(ks);
    CHECK(tss.contains(ks));
    CHECK(tss.contains(ksv));
  }
  if (true) {
    string_unordered_map<int> tm;
    tm[ks] = 42;
    // * tm[ksv] = 42; // error: no known conversion
    int* p = find_opt(tm, ksv);
    CHECK(p);
    CHECK(*p == 42);
  }
  if (true) {
    string_unordered_set tss;
    CHECK_FALSE(tss.contains(ks));
    CHECK_FALSE(tss.contains(ksv));
    tss.insert(ks);
    CHECK(tss.contains(ks));
    CHECK(tss.contains(ksv));
  }
}
#pragma endregion

// Hashers with explicit exception specs, to pin the adapters' conditional
// noexcept.
struct nothrow_string_hash {
  size_t operator()(const std::string& s) const noexcept {
    return std::hash<std::string>{}(s);
  }
};
struct throwing_string_hash {
  size_t operator()(const std::string& s) const {
    return std::hash<std::string>{}(s);
  }
};

#pragma region IndirectKey_Basic

TEST_CASE("Basic", "[IndirectKey]") {
  using IHK = indirect_hash_key<std::string>;

  // The functor adapters mirror the wrapped hasher's exception spec instead
  // of overpromising `noexcept`.
  using NHK = indirect_hash_key<std::string, nothrow_string_hash>;
  using THK = indirect_hash_key<std::string, throwing_string_hash>;
  static_assert(noexcept(NHK::hash_equal_to{}(std::declval<const NHK&>())));
  static_assert(!noexcept(THK::hash_equal_to{}(std::declval<const THK&>())));
  std::unordered_map<IHK, int> um;
  const auto key{"abc"s};
  um[key] = 42;
  CHECK(um[key] == 42);

  using IMK = indirect_map_key<std::string>;
  std::map<IMK, int> m;
  m[key] = 42;
  CHECK(m[key] == 42);

  // Each key formats as its referenced value, honoring the value's spec.
  CHECK(std::format("{}", IHK{key}) == "abc");
  CHECK(std::format("{:?}", IHK{key}) == "\"abc\"");
  CHECK(std::format("{}", IMK{key}) == "abc");
  CHECK(std::format("{:>5}", IMK{key}) == "  abc");
}
#pragma endregion

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

#pragma region DeductionTest_Experimental

TEST_CASE("Experimental", "[DeductionTest]") {
  auto i = 42;
  Holder<int> h0{&i};
  //  Holder h1{&i};
  //  Holder h2{42.0};
}
#pragma endregion

#pragma region NoInitResize_Basic

TEST_CASE("Basic", "[NoInitResize]") {
  std::vector<int> v;
  v.resize(2);
  std::string s;
  // s.resize_and_overwrite(2);
  (void)s;
}
#pragma endregion

#pragma region Arena_Basic

TEST_CASE("Basic", "[ArenaTest]") {
  using arena::extensible_arena;
  // Nested scopes: the inner scope routes allocations to its own arena, and
  // ending it restores the outer arena, not the inner one.
  if (true) {
    extensible_arena a{256};
    extensible_arena b{256};
    void* in_a{};
    if (true) {
      extensible_arena::scope sa{a};
      in_a = extensible_arena::allocate(8, 8);
      CHECK(extensible_arena::contains(in_a));
      void* in_b{};
      if (true) {
        extensible_arena::scope sb{b};
        in_b = extensible_arena::allocate(8, 8);
        CHECK(extensible_arena::contains(in_b));
        CHECK_FALSE(extensible_arena::contains(in_a));
      }
      // Back on `a`: both the old and a fresh allocation are in `a`, and
      // `b`'s allocation is not.
      CHECK(extensible_arena::contains(in_a));
      CHECK_FALSE(extensible_arena::contains(in_b));
      auto* p = extensible_arena::allocate(8, 8);
      CHECK(extensible_arena::contains(p));
    }
  }
  // Allocations honor the requested alignment as an address, including
  // alignments above `alignof(max_align_t)`, even after a deliberately odd
  // tail.
  if (true) {
    extensible_arena a{4096};
    extensible_arena::scope sa{a};
    for (const auto align : {8UZ, 16UZ, 32UZ, 64UZ}) {
      auto* p = extensible_arena::allocate(24, align);
      CHECK(reinterpret_cast<uintptr_t>(p) % align == 0U);
      CHECK(extensible_arena::allocate(1, 1) != nullptr);
    }
  }
}
#pragma endregion

using FirstName = strong_type<std::string, struct FirstNameTag>;
using LastName = strong_type<std::string, struct LastNameTag>;
using PersonAge = strong_type<long, struct PersonAgeTag>;

using WeakPersonFn =
    std::function<PersonAge(const FirstName&, const LastName&)>;
using PersonFn = strong_type<WeakPersonFn, struct PersonFnTag>;
using WeakPointlessFn = std::function<void(const FirstName&, const LastName&)>;
using PointlessFn = strong_type<WeakPointlessFn, struct PointlessFnTag>;

#pragma region StrongType_Basic

TEST_CASE("Basic", "[StrongType]") {
  FirstName fn{"John"};
  LastName ln{"Smith"};
  CHECK(fn.value() == "John");
  CHECK(ln.value() == "Smith");
  CHECK(fn == "John"s);
  CHECK(fn == fn);
  CHECK(fn == FirstName{"John"});
  CHECK(fn == FirstName{"John"});
  CHECK(fn != FirstName{"Jane"});
  CHECK(fn != FirstName{"Jane"});
  // Does not compile, giving clean error message.
  //* CHECK(fn != ln);
  //++fn;
  //* fn = fn * fn;
  //* fn = fn * 2;
  CHECK(fn == FirstName{"John"});
  std::map<FirstName, LastName> m;
  m[fn] = ln;
  std::unordered_map<FirstName, LastName> um;
  um[fn] = ln;
  PersonAge age{42};
  CHECK(age.value() == 42);
  CHECK(age == 42);
  CHECK(age == PersonAge{42});
  CHECK(age != PersonAge{43});
  ++age;
  CHECK(age == 43);
  age = age + 1L;
  CHECK(age == 44);
  age = age - 1;
  CHECK(age == 43);
  age = age << 1;
}
#pragma endregion

#pragma region StrongType_Heterogeneous

TEST_CASE("Heterogeneous", "[StrongType]") {
  if (true) {
    // Mixed comparisons are exact in the natural common type; the operand is
    // not quantized into `T`. Pre-fix, `age == 3.7` read true via truncation
    // and `age < 3.5` read false.
    PersonAge age{3};
    CHECK_FALSE(age == 3.7);
    CHECK(age != 3.7);
    CHECK(age < 3.5);
    CHECK(3.5 > age);
    CHECK_FALSE(age < 3.0);
    CHECK(age <= 3.0);
    CHECK(age == 3.0);

    // A type only comparable with `T`, not convertible to it, now works:
    // `std::string_view` converts to `std::string` only explicitly.
    FirstName fn{"John"};
    CHECK(fn == "John"sv);
    CHECK("Jane"sv < fn);
  }
  if (true) {
    // Mixed arithmetic computes in the common type but returns the strong
    // type, landing back in `T`'s domain, narrowing if necessary.
    PersonAge age{3};
    CHECK(age + 0.5 == PersonAge{3});
    CHECK(age + 1.5 == PersonAge{4});
  }
  if (true) {
    // A wrapped callable that returns a reference keeps returning one, so the
    // result aliases the target and writes through it stick. Pre-fix, the
    // `auto` return decayed it to a copy.
    long target{7};
    using GetRef = strong_type<std::function<long&()>, struct GetRefTag>;
    GetRef get_ref{[&target]() -> long& { return target; }};
    static_assert(std::is_same_v<decltype(get_ref()), long&>);
    CHECK(&get_ref() == &target);
    get_ref() = 9;
    CHECK(target == 9);
  }
}
#pragma endregion

#pragma region StrongType_Extended

TEST_CASE("Extended", "[StrongType]") {
  // Comprehensive test of all methods and operators for FirstName.

  if (true) {
    // Default ctor.
    FirstName fn;
    CHECK(fn == "");
    fn = "John";
    CHECK(fn == "John");
    // Copy ctor.
    FirstName fn_copy{fn};
    CHECK(fn_copy == "John");
    // Move ctor.
    FirstName fn_moved{std::move(fn_copy)};
    CHECK(fn_moved == "John");
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(fn_copy == "");
    // Copy conversion from string.
    std::string name{"Jane"};
    FirstName fn_copy_from_string{name};
    CHECK(fn_copy_from_string == "Jane");
    // Move conversion from string.
    FirstName fn_move_from_string{std::move(name)};
    CHECK(fn_move_from_string == "Jane");
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(name == "");
    // Conversion from char[].
    char name2[]{"Jim"};
    FirstName fn_from_char_array{name2};
    CHECK(fn_from_char_array == "Jim");
  }

  if (true) {
    FirstName fn{"Jane"};
    CHECK(fn == "Jane");
    // Homogeneous copy assignment.
    FirstName fn_copy;
    fn_copy = fn;
    CHECK(fn_copy == "Jane");
    // Homogeneous move assignment.
    FirstName fn_move;
    fn_move = std::move(fn_copy);
    CHECK(fn_move == "Jane");
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(fn_copy == "");
    // Copy from char[].
    char namearray[]{"John"};
    fn = namearray;
    CHECK(fn == "John");
    // Copy from string.
    auto name = "Jane"s;
    fn = name;
    CHECK(fn == "Jane");
    // Move from string.
    fn->clear();
    fn = std::move(name);
    CHECK(fn == "Jane");
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK(name == "");
  }

  if (true) {
    // Access and iteration.
    FirstName fn{"John"};
    CHECK(fn.value() == "John");
    CHECK(fn->size() == 4U);
    CHECK(fn->at(0) == 'J');
    CHECK(fn->front() == 'J');
    CHECK(fn->back() == 'n');
    CHECK(std::string_view{fn->data()} == "John");
    CHECK(std::string_view{fn->c_str()} == "John");
    std::string s;
    for (auto c : fn) s += c;
    CHECK(s == "John");
    // You can move through get.
    s.clear();
    s = std::move(*fn);
    CHECK(s == "John");
    CHECK(fn == "");
  }

  if (true) {
    // Relational ops.
    FirstName fn{"John"};
    FirstName fn2{"Jane"};
    CHECK(fn == "John"s);
    CHECK(fn == fn);
    // Test spaceship, both heterogeneous and homogeneous.
    CHECK(fn <=> fn == std::strong_ordering::equal);
    CHECK(fn <=> fn2 == std::strong_ordering::greater);
    CHECK(fn2 <=> fn == std::strong_ordering::less);
    CHECK(fn <=> "John"s == std::strong_ordering::equal);
    CHECK(fn <=> "Zoe"s == std::strong_ordering::less);
    CHECK("Zoe"s <=> fn == std::strong_ordering::greater);
    CHECK("John"s <=> fn == std::strong_ordering::equal);
    // Test homogeneous comparisons.
    CHECK_FALSE(fn == fn2);
    CHECK(fn != fn2);
    CHECK_FALSE(fn < fn2);
    CHECK_FALSE(fn <= fn2);
    CHECK(fn > fn2);
    CHECK(fn >= fn2);
    // Test heterogeneous comparisons.
    CHECK(fn == "John"s);
    CHECK(fn <=> "John"s == std::strong_ordering::equal);
    CHECK_FALSE(fn != "John"s);
    CHECK(fn < "Zoe"s);
    CHECK(fn <= "John"s);
    CHECK(fn > "Adam"s);
    CHECK(fn >= "John"s);
    CHECK("John"s <=> fn == std::strong_ordering::equal);
    CHECK("John"s == fn);
    CHECK_FALSE("John"s != fn);
    CHECK("Zoe"s > fn);
    CHECK("John"s >= fn);
    CHECK("Adam"s < fn);
    CHECK("John"s <= fn);
  }

  // Test unary operators.
  if (true) {
    PersonAge age{42};
    CHECK(+age == 42);
    CHECK(-age == -42);
    CHECK(!age == false);
    CHECK(!!age == true);
    CHECK(~age == -43);
    CHECK(++age == 43);
    CHECK(age++ == 43);
    CHECK(age == 44);
    CHECK(--age == 43);
    CHECK(age-- == 43);
    CHECK(age == 42);
    // Test bitwise and bool.
    CHECK((age & 1) == 0);
    CHECK((age | 1) == 43);
    CHECK((age ^ 1) == 43);
    CHECK((age ? true : false));
    CHECK(~age == -43);
    CHECK(age == static_cast<long>(age));
  }

  // Test binary arithmetic operators.
  if (true) {
    PersonAge age{42};
    CHECK((age + 1) == 43);
    CHECK((age - 1) == 41);
    CHECK((age * 2) == 84);
    CHECK((age / 2) == 21);
    CHECK((age % 5) == 2);
    CHECK((1 + age) == 43);
    CHECK((1 - age) == -41);
    CHECK((2 * age) == 84);
    CHECK((2 / age) == 0);
    CHECK((5 % age) == 5);
    CHECK((age + age) == 84);
    CHECK((age - age) == 0);
    CHECK((age * age) == 1764);
    CHECK((age / age) == 1);
    CHECK((age % age) == 0);
  }

  // Test binary bitwise operators.
  if (true) {
    PersonAge age{42};
    CHECK((age & 1) == 0);
    CHECK((age | 1) == 43);
    CHECK((age ^ 1) == 43);
    CHECK((age << 1) == 84);
    CHECK((age >> 1) == 21);
    CHECK((1 & age) == 0);
    CHECK((1 | age) == 43);
    CHECK((1 ^ age) == 43);
    CHECK((2 & age) == 2);
    CHECK((2 | age) == 42);
    CHECK((2 ^ age) == 40);
    // Does not compile:
    //* CHECK((2 << age) == 16834);
    //* CHECK((2 >> age) == 0);
    CHECK((age & age) == 42);
    CHECK((age | age) == 42);
    CHECK((age ^ age) == 0);
    age = 1;
    CHECK((age << age) == 2);
    CHECK((age >> age) == 0);
  }

  // Test arithmetic assignment operators.
  if (true) {
    PersonAge age{42};
    age += 1;
    CHECK(age == 43);
    age -= 1;
    CHECK(age == 42);
    age *= 2;
    CHECK(age == 84);
    age /= 2;
    CHECK(age == 42);
    age %= 5;
    CHECK(age == 2);
    long i = 1;
    // Does not compile.
    //* i += age;
    i += *age;
    CHECK(i == 3);
  }

  if (true) {
    WeakPersonFn fn = [](const FirstName&, const LastName&) -> PersonAge {
      return PersonAge{42};
    };
    WeakPointlessFn fn2 = [](const FirstName&, const LastName&) {};
    PersonFn pf{fn};
    PointlessFn pf2{fn2};
    CHECK(((pf.value()(FirstName{"John"}, LastName{"Smith"}))) ==
          (PersonAge{42}));
    CHECK(pf(FirstName{"John"}, LastName{"Smith"}) == PersonAge{42});
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
    CHECK(m[fn].value() == "Smith");
    std::unordered_map<FirstName, LastName> um;
    um[fn] = ln;
    CHECK(um[fn].value() == "Smith");

    using StrongMap =
        strong_type<std::map<FirstName, LastName>, struct StrongMapTag>;
    StrongMap sm;
    sm[fn] = ln;
    CHECK(sm[fn].value() == "Smith");
  }

  // Assorted tests.
  FirstName fn{"John"};
  LastName ln{"Smith"};
  PersonAge age{42};

  // Test `get` method.
  CHECK(fn.value() == "John");
  CHECK(ln.value() == "Smith");
  CHECK(age.value() == 42);

  // Test equality and inequality operators.
  CHECK(fn == "John"s);
  CHECK(fn == fn);
  CHECK(fn == FirstName{"John"});
  CHECK(fn != FirstName{"Jane"});
  // Does not compile.
  //*  CHECK(fn != ln); // Different strong types.

  // Test copy and move constructors.
  FirstName fn_copy{fn};
  CHECK(fn_copy == fn);
  FirstName fn_moved{std::move(fn_copy)};
  CHECK(fn_moved == fn);

  // Test copy and move assignment operators.
  FirstName fn_assigned = fn;
  CHECK(fn_assigned == fn);
  FirstName fn_move_assigned = std::move(fn_assigned);
  CHECK(fn_move_assigned == fn);

  // Test arithmetic operators.
  CHECK((age + 1) == PersonAge{43});
  CHECK((age - 1) == PersonAge{41});
  CHECK((age * 2) == PersonAge{84});
  CHECK((age / 2) == PersonAge{21});
  CHECK((age % 5) == PersonAge{2});

  // Test arithmetic assignment operators.
  age += 1;
  CHECK(age == PersonAge{43});
  age -= 1;
  CHECK(age == PersonAge{42});
  age *= 2;
  CHECK(age == PersonAge{84});
  age /= 2;
  CHECK(age == PersonAge{42});
  age %= 5;
  CHECK(age == PersonAge{2});

  // Test increment and decrement operators.
  ++age;
  CHECK(age == PersonAge{3});
  age++;
  CHECK(age == PersonAge{4});
  --age;
  CHECK(age == PersonAge{3});
  age--;
  CHECK(age == PersonAge{2});

  // Test bitwise operators.
  CHECK((age & 1) == PersonAge{0});
  CHECK((age | 1) == PersonAge{3});
  CHECK((age ^ 1) == PersonAge{3});
  CHECK((age << 1) == PersonAge{4});
  CHECK((age >> 1) == PersonAge{1});

  // Test bitwise assignment operators.
  age &= 1;
  CHECK(age == PersonAge{0});
  age |= 3;
  CHECK(age == PersonAge{3});
  age ^= 1;
  CHECK(age == PersonAge{2});
  age <<= 1;
  CHECK(age == PersonAge{4});
  age >>= 1;
  CHECK(age == PersonAge{2});

  // Test heterogeneous comparisons.
  CHECK(fn == "John"s);
  CHECK_FALSE(fn != "John"s);
  CHECK(fn < "Zoe"s);
  CHECK(fn <= "John"s);
  CHECK(fn > "Adam"s);
  CHECK(fn >= "John"s);

  // Test map and unordered_map compatibility.
  std::map<FirstName, LastName> m;
  m[fn] = ln;
  CHECK(m[fn].value() == "Smith");

  std::unordered_map<FirstName, LastName> um;
  um[fn] = ln;
  CHECK(um[fn].value() == "Smith");

  // Test stream output (if implemented).
  std::ostringstream oss;
  oss << fn;
  CHECK(oss.str() == "John");

  // Test std::format: the formatter forwards to the underlying type's
  // formatter, so the spec grammar and debug quoting come along.
  CHECK(std::format("{}", fn) == "John");
  CHECK(std::format("{:>6}", fn) == "  John");
  CHECK(std::format("{:?}", fn) == "\"John\"");
  CHECK(std::format("{}", PersonAge{42}) == "42");
  CHECK(std::format("{:04}", PersonAge{42}) == "0042");
  CHECK(std::format(L"{}", PersonAge{42}) == L"42");

  // A range-typed underlying forwards to the range formatter rather than
  // being enumerated as a strong_type range.
  using Tags = strong_type<std::vector<int>, struct TagsTag>;
  CHECK(std::format("{}", Tags{std::vector<int>{1, 2, 3}}) == "[1, 2, 3]");
}
#pragma endregion

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
  [&]<size_t... Is>(std::index_sequence<Is...>) {
    ((oss << typeid(std::variant_alternative_t<Is, T>).name() << "\n"), ...);
  }(std::make_index_sequence<std::variant_size_v<T>>{});
  return oss.str();
}

#pragma region EnumVariant_Basic

TEST_CASE("Basic", "[EnumVariant]") {
  std::variant<std::monostate, int, char, std::string> v;
  list_variant_types<decltype(v)>();
  if (true) {
    QueryVariant qv;
    auto e = qv.index();
    CHECK(e == QueryType::None);
  }
  if (true) {
    QueryVariant qv{RetrievalKey{1, "test"}};
    auto e = qv.index();
    CHECK(e == QueryType::Retrieve);
  }
  if (true) {
    QueryVariant qv{in_place_enum<QueryType::OtherRange>, RangeKey{10, 20}};
    auto e = qv.index();
    CHECK(e == QueryType::OtherRange);
  }
  if (true) {
    QueryVariant qv{in_place_enum<QueryType::Status>};
    auto e = qv.index();
    CHECK(e == QueryType::Status);

    // The in_place ctors deduce the variant's own `enum_type`, so a foreign
    // scoped enum's tag is rejected at overload resolution.
    static_assert(std::is_constructible_v<QueryVariant,
        in_place_enum_t<QueryType::Status>>);
    static_assert(
        !std::is_constructible_v<QueryVariant, in_place_enum_t<test_id_t{0}>>);
  }
  if (true) {
    QueryVariant::underlying_type underlying_other_range_key{
        std::in_place_index<3>, RangeKey{10, 20}};
    CHECK((underlying_other_range_key.index()) ==
          ((size_t)QueryType::OtherRange));
    QueryVariant qv{in_place_enum<QueryType::OtherRange>, RangeKey{10, 20}};
    auto e = qv.index();
    CHECK(e == QueryType::OtherRange);
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
    CHECK(r.id == 1U);
    CHECK(r.name == "retrieve");
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
      CHECK(s == "None");
      break;
    case QueryType::Retrieve:
      CHECK(s == "RetrievalKey(id=1, name=retrieve)");
      break;
    case QueryType::Range: CHECK(s == "Main RangeKey(start=0, end=0)"); break;
    case QueryType::OtherRange:
      CHECK(s == "Other RangeKey(start=0, end=0)");
      break;
    case QueryType::Status: CHECK(s == "Status()"); break;
    }
    qv = QueryVariant::make<QueryType::OtherRange>(RangeKey{10, 20});
    s = visitor.visit(qv);
    CHECK(s == "Other RangeKey(start=10, end=20)");
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
    CHECK(s == "Some RangeKey(start=10, end=20)");
  }
  if (true) {
    // The callback helpers work on a plain std::variant, and the member
    // `visit` works with `indexed_callbacks`; both route through
    // `underlying_variant_type_t`, which must not hard-error on either shape.
    auto sv_visitor = indexed_callbacks( //
        [](std::monostate) { return "mono"s; },
        [](int n) { return format_args("int(", n, ")"); },
        [](const std::string& s) { return format_args("str(", s, ")"); });
    std::variant<std::monostate, int, std::string> sv{7};
    CHECK(sv_visitor.visit(sv) == "int(7)");

    auto qv_visitor = indexed_callbacks( //
        [](std::monostate) { return "None"s; },
        [](const RetrievalKey& rk) { return format_args("R(", rk.id, ")"); },
        [](const RangeKey&) { return "Range"s; },
        [](const RangeKey&) { return "OtherRange"s; },
        [](const std::string& s) { return format_args("S(", s, ")"); });
    QueryVariant qv{RetrievalKey{1, "retrieve"}};
    CHECK(qv.visit(qv_visitor) == "R(1)");

    // Rvalue paths move out through `get_underlying`.
    qv = QueryVariant::make<QueryType::Status>("status"s);
    auto moved =
        variant_get<static_cast<size_t>(QueryType::Status)>(std::move(qv));
    CHECK(moved == "status");
  }
}
#pragma endregion

enum class ThrowKind : std::uint8_t { value, thrower };

// The value constructor throws, to manufacture a valueless variant.
struct ThrowOnConstruct {
  ThrowOnConstruct() = default;
  explicit ThrowOnConstruct(int) { throw std::runtime_error{"boom"}; }
};

using ThrowVariant = enum_variant<ThrowKind, int, ThrowOnConstruct>;

#pragma region EnumVariant_BadIndex

TEST_CASE("BadIndex", "[EnumVariant]") {
  if (true) {
    // An out-of-range enum value fails the consteval constructor's constant
    // evaluation, so rejection is a compile error and can only be
    // demonstrated:
    // QueryVariant bad{static_cast<QueryType>(5)};
    //
    // The constexpr dodges an MSVC 14.51 bug materializing a consteval-built
    // variant with a non-zero index into a runtime object; see
    // crossplatform.md.
    constexpr QueryVariant good{QueryType::Range};
    CHECK(good.index() == QueryType::Range);
  }
  if (true) {
    // In-range enum assignment default-constructs that alternative. Out of
    // range is a precondition violation that terminates through the operator's
    // `noexcept` boundary, so only the in-range side is testable.
    QueryVariant qv{RetrievalKey{1, "x"}};
    qv = QueryType::Range;
    CHECK(qv.index() == QueryType::Range);
    static_assert(noexcept(qv = QueryType::Range));
  }
  if (true) {
    // A constructor that throws mid-emplace leaves the variant valueless.
    // Visiting it then throws `bad_variant_access`, mirroring `std::visit`,
    // on the indexed and overloaded paths alike.
    ThrowVariant tv{42};
    CHECK_THROWS_AS(tv.emplace<ThrowOnConstruct>(1), std::runtime_error);
    CHECK(tv.valueless_by_exception());
    CHECK(tv.index() == ThrowVariant::variant_npos);

    auto indexed = indexed_callbacks( //
        [](int) { return 1; }, [](const ThrowOnConstruct&) { return 2; });
    CHECK_THROWS_AS(indexed.visit(tv), std::bad_variant_access);
    CHECK_THROWS_AS(tv.visit(indexed), std::bad_variant_access);

    auto overloads = overloaded_callbacks( //
        [](int) { return 1; }, [](const ThrowOnConstruct&) { return 2; });
    CHECK_THROWS_AS(tv.visit(overloads), std::bad_variant_access);
  }
}
#pragma endregion

#pragma region EnumVector_Basic

TEST_CASE("Basic", "[EnumVector]") {
  using id_t = test_id_t;
  enum_vector<int, id_t> v;

  CHECK(v.empty());
  CHECK(v.size() == 0U);

  v.reserve(6);
  CHECK(v.capacity() >= 6U);

  v.resize(2, 5);
  v.resize(2);

  v[id_t{0}] = 10;
  v.at(id_t{1}) = 11;

  const auto& cv = v;
  CHECK(cv[id_t{0}] == 10);
  CHECK(cv.at(id_t{1}) == 11);

  auto& f = v.front();
  auto& b = v.back();
  f = 12;
  b = 13;
  CHECK(cv.front() == 12);
  CHECK(cv.back() == 13);

  auto* p = v.data();
  const auto* cp = cv.data();
  (void)p;
  (void)cp;

  auto it = v.begin();
  auto it_end = v.end();
  auto cit = v.cbegin();
  auto cit_end = v.cend();
  auto cit2 = cv.begin();
  auto cit3 = cv.end();
  (void)it;
  (void)it_end;
  (void)cit;
  (void)cit_end;
  (void)cit2;
  (void)cit3;

  int lval = 14;
  v.push_back(lval);
  v.push_back(15);
  v.emplace_back(16);
  v.pop_back();

  auto enum_size = v.size_as_enum();
  CHECK(*enum_size == v.size());

  // The capacity vocabulary is `size_t`, so the vector can outgrow a narrow
  // enum's underlying type and `size()` stays exact; `size_as_enum` is the
  // narrowing bridge and wraps when the size does not fit: a full-domain
  // 8-bit vector holds 256 but reports `small_id_t{0}`.
  enum_vector<int, small_id_t> sv;
  sv.resize(256);
  CHECK(sv.size() == 256U);
  CHECK(sv.size_as_enum() == small_id_t{0});
  sv.pop_back();
  CHECK(sv.size_as_enum() == small_id_t{255});

  auto& u = v.underlying();
  const auto& cu = cv.underlying();
  (void)u;
  (void)cu;

  auto& u2 = *v;
  const auto& u3 = *cv;
  (void)u2;
  (void)u3;

  // As a plain sequence, `enum_vector` formats for free through the std range
  // formatter (narrow and wide), with no formatter of its own.
  enum_vector<int, id_t> fv;
  fv.push_back(1);
  fv.push_back(2);
  fv.push_back(3);
  CHECK(std::format("{}", fv) == "[1, 2, 3]");
  CHECK(std::format("{:n}", fv) == "1, 2, 3");
  CHECK(std::format(L"{}", fv) == L"[1, 2, 3]");

  v.clear();
  CHECK(v.empty());
}
#pragma endregion

struct throwing_scoped_value_test {
  std::string value;
  bool throw_on_move{};

  throwing_scoped_value_test(std::string value_in,
      bool throw_on_move_in = false)
      : value(std::move(value_in)), throw_on_move(throw_on_move_in) {}

  throwing_scoped_value_test(const throwing_scoped_value_test&) = default;
  throwing_scoped_value_test& operator=(
      const throwing_scoped_value_test&) = default;

  throwing_scoped_value_test(throwing_scoped_value_test&& other) {
    if (other.throw_on_move) throw std::runtime_error("move failed");
    value = std::move(other.value);
    throw_on_move = other.throw_on_move;
  }

  throwing_scoped_value_test& operator=(throwing_scoped_value_test&& other) {
    if (other.throw_on_move) throw std::runtime_error("move failed");
    value = std::move(other.value);
    throw_on_move = other.throw_on_move;
    return *this;
  }
};

inline void swap(throwing_scoped_value_test& lhs,
    throwing_scoped_value_test& rhs) noexcept {
  using std::swap;
  swap(lhs.value, rhs.value);
  swap(lhs.throw_on_move, rhs.throw_on_move);
}

#pragma region ScopedValue_Basic

TEST_CASE("Basic", "[ScopedValue]") {
  if (true) {
    int x = 1;
    {
      scoped_value sv{x, 42};
      CHECK(x == 42);
    }
    CHECK(x == 1);
  }
  if (true) {
    // Nested scopes restore in reverse order.
    int x = 1;
    {
      scoped_value sv1{x, 10};
      CHECK(x == 10);
      {
        scoped_value sv2{x, 20};
        CHECK(x == 20);
      }
      CHECK(x == 10);
    }
    CHECK(x == 1);
  }
  if (true) {
    // Works with non-trivial types.
    std::string s = "original";
    {
      scoped_value sv{s, std::string{"temporary"}};
      CHECK(s == "temporary");
    }
    CHECK(s == "original");
  }
  if (true) {
    // Old value is captured at construction; direct mutations to the target
    // are overwritten on scope exit.
    int x = 5;
    {
      scoped_value sv{x, 99};
      x = 7; // Mutate target directly while scoped_value is active.
      CHECK(x == 7);
    }
    // Restored to 5 (captured at sv construction), not 7.
    CHECK(x == 5);
  }
  if (true) {
    // If materializing the replacement throws, the target stays untouched.
    throwing_scoped_value_test x{"original"};
    throwing_scoped_value_test replacement{"temporary", true};

    CHECK_THROWS_AS(
        (void)scoped_value<throwing_scoped_value_test>(x, replacement),
        std::runtime_error);
    CHECK(x.value == "original");
    CHECK_FALSE(x.throw_on_move);
  }
  if (true) {
    int x = 1;
    {
      scoped_value sv1{x, 10};
      scoped_value sv2{std::move(sv1)};
      CHECK(x == 10);
    }
    CHECK(x == 1);
  }
  if (true) {
    int x = 1;
    int y = 2;
    {
      scoped_value sv1{x, 10};
      scoped_value sv2{y, 20};
      sv2 = std::move(sv1);
      CHECK(x == 10);
      CHECK(y == 2);
    }
    CHECK(x == 1);
    CHECK(y == 2);
  }
  if (true) {
    int x = 1;
    {
      scoped_value sv{x, 10};
      sv.release();
      CHECK(x == 10);
    }
    CHECK(x == 10);
  }
}
#pragma endregion

#pragma region HashCombiner_Basic

TEST_CASE("Basic", "[HashCombiner]") {
  // Default seed is zero; explicit seed is respected.
  if (true) {
    hash_combiner h;
    CHECK(h.value() == 0u);
    CHECK(static_cast<size_t>(h) == 0u);
  }
  if (true) {
    hash_combiner h{42u};
    CHECK(h.value() == 42u);
  }

  // Combining a non-zero hash into seed 0 must produce a non-zero result.
  if (true) {
    hash_combiner h;
    h.combine_hash(1u);
    CHECK(h.value() != 0u);
  }

  // `combine` hashes a typed value and folds it in.
  if (true) {
    hash_combiner h;
    h.combine(123);
    CHECK(h.value() != 0u);
  }

  // Order of combination must matter.
  if (true) {
    hash_combiner h1;
    h1.combine(1);
    h1.combine(2);

    hash_combiner h2;
    h2.combine(2);
    h2.combine(1);

    CHECK(h1.value() != h2.value());
  }

  // `combine_all` must be equivalent to sequential `combine` calls.
  if (true) {
    hash_combiner h1;
    h1.combine_all(1, 2, 3);

    hash_combiner h2;
    h2.combine(1);
    h2.combine(2);
    h2.combine(3);

    CHECK(h1.value() == h2.value());
  }

  // `combined_hash` must match building a combiner manually.
  if (true) {
    auto expected = combined_hash(std::string{"hello"}, 42, true);

    hash_combiner h;
    h.combine(std::string{"hello"});
    h.combine(42);
    h.combine(true);

    CHECK(expected == h.value());
  }

  // Different argument values or orderings must produce different results.
  if (true) {
    CHECK(combined_hash(1, 2) != combined_hash(2, 1));
    CHECK(combined_hash(1, 2) != combined_hash(1, 3));
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
