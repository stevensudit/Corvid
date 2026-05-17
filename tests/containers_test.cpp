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
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../corvid/containers.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using namespace corvid::internal;

// Enum type for testing enum_vector.
enum class test_id_t : size_t { invalid = std::numeric_limits<size_t>::max() };

template<>
constexpr auto corvid::enums::registry::enum_spec_v<test_id_t> =
    corvid::enums::sequence::make_sequence_enum_spec<test_id_t, "">();

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

#pragma region TransparentTest_General

TEST_CASE("TransparentTest_General", "[TransparentTest]") {
  const auto ks = "key"s;
  const auto ksv = "key"sv;
  if (true) {
    std::map<std::string, int> m;
    string_map<int> tm;
    CHECK((m.size()) == (0U));
    CHECK((tm.size()) == (0U));
    m[ks] = 42;
    tm[ks] = 42;
    // * tm[ksv] = 42; // error: no match for 'operator[]'
    int* p;
    p = find_opt(m, ks);
    CHECK((p));
    CHECK((*p) == (42));
    // * p = find_opt(m, ksv); // error: no known conversion
    p = find_opt(tm, ksv);
    CHECK((p));
    CHECK((*p) == (42));
  }
  if (true) {
    string_set tss;
    CHECK_FALSE((tss.contains(ks)));
    CHECK_FALSE((tss.contains(ksv)));
    tss.insert(ks);
    CHECK((tss.contains(ks)));
    CHECK((tss.contains(ksv)));
  }
  if (true) {
    string_unordered_map<int> tm;
    tm[ks] = 42;
    // * tm[ksv] = 42; // error: no known conversion
    int* p = find_opt(tm, ksv);
    CHECK((p));
    CHECK((*p) == (42));
  }
  if (true) {
    string_unordered_set tss;
    CHECK_FALSE((tss.contains(ks)));
    CHECK_FALSE((tss.contains(ksv)));
    tss.insert(ks);
    CHECK((tss.contains(ks)));
    CHECK((tss.contains(ksv)));
  }
}
#pragma endregion

#pragma region IndirectKey_Basic

TEST_CASE("IndirectKey_Basic", "[IndirectKey]") {
  using IHK = indirect_hash_key<std::string>;
  std::unordered_map<IHK, int> um;
  const auto key{"abc"s};
  um[key] = 42;
  CHECK((um[key]) == (42));

  using IMK = indirect_map_key<std::string>;
  std::map<IMK, int> m;
  m[key] = 42;
  CHECK((m[key]) == (42));
}
#pragma endregion

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
  D(D&&) noexcept { action = "move"sv; }
  void operator()(int* p) const {
    action = "delete"sv;
    delete p;
  };
};

#pragma region OwnPtrTest_Ctor

TEST_CASE("OwnPtrTest_Ctor", "[OwnPtrTest]") {
  {
    own_ptr<int> p;
    own_ptr<int, DefaultIntDeleter> q;

    // If there's no deleter, it points to the object itself.
    CHECK((((const void*)&p.get_deleter())) == (((const void*)&p)));

    // Requires defaultable constructor.
    //* own_ptr<int, SpecialIntDeleter> r;

    own_ptr<int, SpecialIntDeleter> r{nullptr, SpecialIntDeleter{42}};
    CHECK((sizeof(r)) > (sizeof(int*)));

    CHECK((sizeof(p)) == (sizeof(int*)));
    CHECK((sizeof(q)) == (sizeof(int*)));
    auto p2 = std::move(p);

    CHECK((r.get_deleter().x_) == (42));
    auto r2 = std::move(r);
    CHECK((r2.get_deleter().x_) == (42));
  }
  {
    using P0 = own_ptr<int>;
    CHECK((P0::is_deleter_non_reference_v));
    CHECK_FALSE((P0::is_deleter_lvalue_reference_v));
    CHECK_FALSE((P0::is_deleter_const_lvalue_reference_v));
    using P1 = own_ptr<int, D>;
    CHECK((P1::is_deleter_non_reference_v));
    CHECK_FALSE((P1::is_deleter_lvalue_reference_v));
    CHECK_FALSE((P1::is_deleter_const_lvalue_reference_v));
    using P2 = own_ptr<int, D&>;
    CHECK_FALSE((P2::is_deleter_non_reference_v));
    CHECK((P2::is_deleter_lvalue_reference_v));
    CHECK_FALSE((P2::is_deleter_const_lvalue_reference_v));
    using P3 = own_ptr<int, const D&>;
    CHECK_FALSE((P3::is_deleter_non_reference_v));
    CHECK_FALSE((P3::is_deleter_lvalue_reference_v));
    CHECK((P3::is_deleter_const_lvalue_reference_v));
  }

  // Cases from https://en.cppreference.com/w/cpp/memory/unique_ptr.
  {
    // Example constructor(1).
    using P = own_ptr<int>;
    P p;
    P q{nullptr};
    CHECK((p.get()) == (nullptr));
    CHECK((q.get()) == (nullptr));
  }
  {
    // Example constructor(2)
    using P = own_ptr<int>;
    P{new int};
  }
  D d;
  CHECK((D::action) == ("ctor"sv));
  {
    // Example constructor(3a)
    // Non-reference is copied when lvalue.
    using P = own_ptr<int, D>;
    P p{new int, d}; // Copy of d
    CHECK((D::action) == ("copy"sv));
  }
  CHECK((D::action) == ("delete"sv));
  {
    // Example constructor(3b)
    // Reference is held when lvalue.
    using P = own_ptr<int, D&>;
    D::action = "referenced"sv;
    P p{new int, d}; // Reference to d
    CHECK((D::action) == ("referenced"sv));
  }
  CHECK((D::action) == ("delete"sv));
  {
    // Example constructor(4)
    // Non-reference is moved when rvalue.
    using P = own_ptr<int, D>;
    P p{new int, D{}}; // Move of D
    CHECK((D::action) == ("move"sv));
  }
  CHECK((D::action) == ("delete"sv));
  {
    // Example constructor(5)
    // Ownership transfer.
    using P = own_ptr<int>;
    P p{new int};
    P q{std::move(p)};
  }
  CHECK((D::action) == ("delete"sv));
  {
    // Example constructor(6ab)
    // Non-reference is copied when lvalue.
    using P = own_ptr<int, D>;
    P p{new int, d}; // Copy of d
    CHECK((D::action) == ("copy"sv));
    P q{std::move(p)}; // Move of d
    CHECK((D::action) == ("move"sv));
  }
  CHECK((D::action) == ("delete"sv));
  {
    // Example constructor(6cd)
    // Non-reference is copied when lvalue.
    using P = own_ptr<int, D&>;
    using Q = own_ptr<int, D>;
    D::action = "referenced"sv;
    // It cannot be moved. Implicitly deleted.
    //* P q(new int, D{});
    P p{new int, d}; // Copy of d
    CHECK((D::action) == ("referenced"sv));
    Q q{std::move(p)}; // Move of d
    CHECK((D::action) == ("non-const copy"sv));
    // This correctly fails.
    //* P r{new int, D{}};
  }
  CHECK((D::action) == ("delete"sv));
  {
    using P = own_ptr<int, const D&>;
    D::action = "referenced"sv;
    P p{new int, d}; // Reference to d
    CHECK((D::action) == ("referenced"sv));
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

  //  CHECK_FALSE((p));
  //  std::unique_ptr<int> up;

  {
    auto p = own_ptr<int>::make(42);
    CHECK((*p) == (42));
  }
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

TEST_CASE("DeductionTest_Experimental", "[DeductionTest]") {
  int i = 42;
  Holder<int> h0{&i};
  //  Holder h1{&i};
  //  Holder h2{42.0};
}
#pragma endregion

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

#pragma region CustomHandleTest_Basic

TEST_CASE("CustomHandleTest_Basic", "[CustomHandleTest]") {
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
    CHECK((fd_deleter::close_count) == (0U));
    P p;
    CHECK((sizeof(p)) == (sizeof(int)));
    P q{FileDescriptor{42}};
    auto* x = (int*)&p;
    p.reset(FileDescriptor{49});
    auto y = *p;
    CHECK((*p) == (FileDescriptor{42}));
    p.reset(q.release());
    q = std::move(p);
    p.reset(FileDescriptor{43});
    CHECK((fd_deleter::close_count) == (0U));
    FileDescriptor i{49};
    p.reset(i);
    CHECK((fd_deleter::close_count) == (1U));
    CHECK((i) == (FileDescriptor{42}));
    p = unique_fd{std::move(i)};
    CHECK((fd_deleter::close_count) == (2U));
    CHECK((i) == (FileDescriptor::invalid));
    const FileDescriptor j{42};
    p = unique_fd{j};
    CHECK((fd_deleter::close_count) == (3U));
    CHECK((j) == (FileDescriptor{42}));
    // * p = unique_fd{std::move(j)};
    p.reset();
    CHECK((fd_deleter::close_count) == (4U));
    i = FileDescriptor{46};
    p.reset(i);
    CHECK((*p) == (FileDescriptor{46}));
    CHECK((i) == (FileDescriptor{46}));
    i = FileDescriptor{47};

    // Proof that 0 is not the nullptr.
    p = unique_fd{FileDescriptor{0}};
    CHECK((*p.get()) == (FileDescriptor{0}));
    CHECK((*p) == (FileDescriptor{0}));
    bool is_present = p ? true : false;
    CHECK((is_present) == (true));
    p.reset();
    CHECK((fd_deleter::close_count) == (6U));
  }
  CHECK((fd_deleter::close_count) == (8U));
#endif
}
#pragma endregion

#pragma region NoInitResize_Basic

TEST_CASE("NoInitResize_Basic", "[NoInitResize]") {
  std::vector<int> v;
  v.resize(2);
  std::string s;
  // s.resize_and_overwrite(2);
  (void)s;
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

TEST_CASE("StrongType_Basic", "[StrongType]") {
  FirstName fn{"John"};
  LastName ln{"Smith"};
  CHECK((fn.value()) == ("John"));
  CHECK((ln.value()) == ("Smith"));
  CHECK((fn) == ("John"s));
  CHECK((fn) == (fn));
  CHECK((fn) == (FirstName{"John"}));
  CHECK((fn) == (FirstName{"John"}));
  CHECK((fn) != (FirstName{"Jane"}));
  CHECK((fn) != (FirstName{"Jane"}));
  // Does not compile, giving clean error message.
  //* CHECK((fn) != (ln));
  //++fn;
  //* fn = fn * fn;
  //* fn = fn * 2;
  CHECK((fn) == (FirstName{"John"}));
  std::map<FirstName, LastName> m;
  m[fn] = ln;
  std::unordered_map<FirstName, LastName> um;
  um[fn] = ln;
  PersonAge age{42};
  CHECK((age.value()) == (42));
  CHECK((age) == (42));
  CHECK((age) == (PersonAge{42}));
  CHECK((age) != (PersonAge{43}));
  ++age;
  CHECK((age) == (43));
  age = age + 1L;
  CHECK((age) == (44));
  age = age - 1;
  CHECK((age) == (43));
  age = age << 1;
}
#pragma endregion

#pragma region StrongType_Extended

TEST_CASE("StrongType_Extended", "[StrongType]") {
  // Comprehensive test of all methods and operators for FirstName.

  if (true) {
    // Default ctor.
    FirstName fn;
    CHECK((fn) == (""));
    fn = "John";
    CHECK((fn) == ("John"));
    // Copy ctor.
    FirstName fn_copy{fn};
    CHECK((fn_copy) == ("John"));
    // Move ctor.
    FirstName fn_moved{std::move(fn_copy)};
    CHECK((fn_moved) == ("John"));
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK((fn_copy) == (""));
    // Copy conversion from string.
    std::string name{"Jane"};
    FirstName fn_copy_from_string{name};
    CHECK((fn_copy_from_string) == ("Jane"));
    // Move conversion from string.
    FirstName fn_move_from_string{std::move(name)};
    CHECK((fn_move_from_string) == ("Jane"));
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK((name) == (""));
    // Conversion from char[].
    char name2[]{"Jim"};
    FirstName fn_from_char_array{name2};
    CHECK((fn_from_char_array) == ("Jim"));
  }

  if (true) {
    FirstName fn{"Jane"};
    CHECK((fn) == ("Jane"));
    // Homogeneous copy assignment.
    FirstName fn_copy;
    fn_copy = fn;
    CHECK((fn_copy) == ("Jane"));
    // Homogeneous move assignment.
    FirstName fn_move;
    fn_move = std::move(fn_copy);
    CHECK((fn_move) == ("Jane"));
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK((fn_copy) == (""));
    // Copy from char[].
    char namearray[]{"John"};
    fn = namearray;
    CHECK((fn) == ("John"));
    // Copy from string.
    auto name = "Jane"s;
    fn = name;
    CHECK((fn) == ("Jane"));
    // Move from string.
    fn->clear();
    fn = std::move(name);
    CHECK((fn) == ("Jane"));
    // NOLINTNEXTLINE(bugprone-use-after-move)
    CHECK((name) == (""));
  }

  if (true) {
    // Access and iteration.
    FirstName fn{"John"};
    CHECK((fn.value()) == ("John"));
    CHECK((fn->size()) == (4U));
    CHECK((fn->at(0)) == ('J'));
    CHECK((fn->front()) == ('J'));
    CHECK((fn->back()) == ('n'));
    CHECK((std::string_view{fn->data()}) == ("John"));
    CHECK((std::string_view{fn->c_str()}) == ("John"));
    std::string s;
    for (auto c : fn) s += c;
    CHECK((s) == ("John"));
    // You can move through get.
    s.clear();
    s = std::move(*fn);
    CHECK((s) == ("John"));
    CHECK((fn) == (""));
  }

  if (true) {
    // Relational ops.
    FirstName fn{"John"};
    FirstName fn2{"Jane"};
    CHECK((fn) == ("John"s));
    CHECK((fn) == (fn));
    // Test spaceship, both heterogeneous and homogeneous.
    CHECK(((fn <=> fn) == (std::strong_ordering::equal)));
    CHECK(((fn <=> fn2) == (std::strong_ordering::greater)));
    CHECK(((fn2 <=> fn) == (std::strong_ordering::less)));
    CHECK(((fn <=> "John"s) == (std::strong_ordering::equal)));
    CHECK(((fn <=> "Zoe"s) == (std::strong_ordering::less)));
    CHECK((("Zoe"s <=> fn) == (std::strong_ordering::greater)));
    CHECK((("John"s <=> fn) == (std::strong_ordering::equal)));
    // Test homogeneous comparisons.
    CHECK_FALSE((fn == fn2));
    CHECK((fn != fn2));
    CHECK_FALSE((fn < fn2));
    CHECK_FALSE((fn <= fn2));
    CHECK((fn > fn2));
    CHECK((fn >= fn2));
    // Test heterogeneous comparisons.
    CHECK((fn == "John"s));
    CHECK(((fn <=> "John"s) == (std::strong_ordering::equal)));
    CHECK_FALSE((fn != "John"s));
    CHECK((fn < "Zoe"s));
    CHECK((fn <= "John"s));
    CHECK((fn > "Adam"s));
    CHECK((fn >= "John"s));
    CHECK((("John"s <=> fn) == (std::strong_ordering::equal)));
    CHECK(("John"s == fn));
    CHECK_FALSE(("John"s != fn));
    CHECK(("Zoe"s > fn));
    CHECK(("John"s >= fn));
    CHECK(("Adam"s < fn));
    CHECK(("John"s <= fn));
  }

  // Test unary operators.
  if (true) {
    PersonAge age{42};
    CHECK((+age) == (42));
    CHECK((-age) == (-42));
    CHECK((!age) == (false));
    CHECK((!!age) == (true));
    CHECK((~age) == (-43));
    CHECK((++age) == (43));
    CHECK((age++) == (43));
    CHECK((age) == (44));
    CHECK((--age) == (43));
    CHECK((age--) == (43));
    CHECK((age) == (42));
    // Test bitwise and bool.
    CHECK((age & 1) == (0));
    CHECK((age | 1) == (43));
    CHECK((age ^ 1) == (43));
    CHECK((age ? true : false));
    CHECK((~age) == (-43));
    CHECK((age) == (static_cast<long>(age)));
  }

  // Test binary arithmetic operators.
  if (true) {
    PersonAge age{42};
    CHECK((age + 1) == (43));
    CHECK((age - 1) == (41));
    CHECK((age * 2) == (84));
    CHECK((age / 2) == (21));
    CHECK((age % 5) == (2));
    CHECK((1 + age) == (43));
    CHECK((1 - age) == (-41));
    CHECK((2 * age) == (84));
    CHECK((2 / age) == (0));
    CHECK((5 % age) == (5));
    CHECK((age + age) == (84));
    CHECK((age - age) == (0));
    CHECK((age * age) == (1764));
    CHECK((age / age) == (1));
    CHECK((age % age) == (0));
  }

  // Test binary bitwise operators.
  if (true) {
    PersonAge age{42};
    CHECK((age & 1) == (0));
    CHECK((age | 1) == (43));
    CHECK((age ^ 1) == (43));
    CHECK((age << 1) == (84));
    CHECK((age >> 1) == (21));
    CHECK((1 & age) == (0));
    CHECK((1 | age) == (43));
    CHECK((1 ^ age) == (43));
    CHECK((2 & age) == (2));
    CHECK((2 | age) == (42));
    CHECK((2 ^ age) == (40));
    // Does not compile:
    //* CHECK((2 << age) == (16834));
    //* CHECK((2 >> age) == (0));
    CHECK((age & age) == (42));
    CHECK((age | age) == (42));
    CHECK((age ^ age) == (0));
    age = 1;
    CHECK((age << age) == (2));
    CHECK((age >> age) == (0));
  }

  // Test arithmetic assignment operators.
  if (true) {
    PersonAge age{42};
    age += 1;
    CHECK((age) == (43));
    age -= 1;
    CHECK((age) == (42));
    age *= 2;
    CHECK((age) == (84));
    age /= 2;
    CHECK((age) == (42));
    age %= 5;
    CHECK((age) == (2));
    long i = 1;
    // Does not compile.
    //* i += age;
    i += *age;
    CHECK((i) == (3));
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
    CHECK(((pf(FirstName{"John"}, LastName{"Smith"}))) == (PersonAge{42}));
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
    CHECK((m[fn].value()) == ("Smith"));
    std::unordered_map<FirstName, LastName> um;
    um[fn] = ln;
    CHECK((um[fn].value()) == ("Smith"));

    using StrongMap =
        strong_type<std::map<FirstName, LastName>, struct StrongMapTag>;
    StrongMap sm;
    sm[fn] = ln;
    CHECK((sm[fn].value()) == ("Smith"));
  }

  // Assorted tests.
  FirstName fn{"John"};
  LastName ln{"Smith"};
  PersonAge age{42};

  // Test `get` method.
  CHECK((fn.value()) == ("John"));
  CHECK((ln.value()) == ("Smith"));
  CHECK((age.value()) == (42));

  // Test equality and inequality operators.
  CHECK((fn) == ("John"s));
  CHECK((fn) == (fn));
  CHECK((fn) == (FirstName{"John"}));
  CHECK((fn) != (FirstName{"Jane"}));
  // Does not compile.
  //*  CHECK((fn) != (ln)); // Different strong types.

  // Test copy and move constructors.
  FirstName fn_copy{fn};
  CHECK((fn_copy) == (fn));
  FirstName fn_moved{std::move(fn_copy)};
  CHECK((fn_moved) == (fn));

  // Test copy and move assignment operators.
  FirstName fn_assigned = fn;
  CHECK((fn_assigned) == (fn));
  FirstName fn_move_assigned = std::move(fn_assigned);
  CHECK((fn_move_assigned) == (fn));

  // Test arithmetic operators.
  CHECK((age + 1) == (PersonAge{43}));
  CHECK((age - 1) == (PersonAge{41}));
  CHECK((age * 2) == (PersonAge{84}));
  CHECK((age / 2) == (PersonAge{21}));
  CHECK((age % 5) == (PersonAge{2}));

  // Test arithmetic assignment operators.
  age += 1;
  CHECK((age) == (PersonAge{43}));
  age -= 1;
  CHECK((age) == (PersonAge{42}));
  age *= 2;
  CHECK((age) == (PersonAge{84}));
  age /= 2;
  CHECK((age) == (PersonAge{42}));
  age %= 5;
  CHECK((age) == (PersonAge{2}));

  // Test increment and decrement operators.
  ++age;
  CHECK((age) == (PersonAge{3}));
  age++;
  CHECK((age) == (PersonAge{4}));
  --age;
  CHECK((age) == (PersonAge{3}));
  age--;
  CHECK((age) == (PersonAge{2}));

  // Test bitwise operators.
  CHECK((age & 1) == (PersonAge{0}));
  CHECK((age | 1) == (PersonAge{3}));
  CHECK((age ^ 1) == (PersonAge{3}));
  CHECK((age << 1) == (PersonAge{4}));
  CHECK((age >> 1) == (PersonAge{1}));

  // Test bitwise assignment operators.
  age &= 1;
  CHECK((age) == (PersonAge{0}));
  age |= 3;
  CHECK((age) == (PersonAge{3}));
  age ^= 1;
  CHECK((age) == (PersonAge{2}));
  age <<= 1;
  CHECK((age) == (PersonAge{4}));
  age >>= 1;
  CHECK((age) == (PersonAge{2}));

  // Test heterogeneous comparisons.
  CHECK((fn == "John"s));
  CHECK_FALSE((fn != "John"s));
  CHECK((fn < "Zoe"s));
  CHECK((fn <= "John"s));
  CHECK((fn > "Adam"s));
  CHECK((fn >= "John"s));

  // Test map and unordered_map compatibility.
  std::map<FirstName, LastName> m;
  m[fn] = ln;
  CHECK((m[fn].value()) == ("Smith"));

  std::unordered_map<FirstName, LastName> um;
  um[fn] = ln;
  CHECK((um[fn].value()) == ("Smith"));

  // Test stream output (if implemented).
  std::ostringstream oss;
  oss << fn;
  CHECK((oss.str()) == ("John"));
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
  [&]<std::size_t... Is>(std::index_sequence<Is...>) {
    ((oss << typeid(std::variant_alternative_t<Is, T>).name() << "\n"), ...);
  }(std::make_index_sequence<std::variant_size_v<T>>{});
  return oss.str();
}

#pragma region EnumVariant_Basic

TEST_CASE("EnumVariant_Basic", "[EnumVariant]") {
  std::variant<std::monostate, int, char, std::string> v;
  list_variant_types<decltype(v)>();
  if (true) {
    QueryVariant qv;
    auto e = qv.index();
    CHECK((e) == (QueryType::None));
  }
  if (true) {
    QueryVariant qv{RetrievalKey{1, "test"}};
    auto e = qv.index();
    CHECK((e) == (QueryType::Retrieve));
  }
  if (true) {
    QueryVariant qv{in_place_enum<QueryType::OtherRange>, RangeKey{10, 20}};
    auto e = qv.index();
    CHECK((e) == (QueryType::OtherRange));
  }
  if (true) {
    QueryVariant qv{in_place_enum<QueryType::Status>};
    auto e = qv.index();
    CHECK((e) == (QueryType::Status));
  }
  if (true) {
    QueryVariant::underlying_type underlying_other_range_key{
        std::in_place_index<3>, RangeKey{10, 20}};
    CHECK((underlying_other_range_key.index()) ==
          ((size_t)QueryType::OtherRange));
    QueryVariant qv{in_place_enum<QueryType::OtherRange>, RangeKey{10, 20}};
    auto e = qv.index();
    CHECK((e) == (QueryType::OtherRange));
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
    CHECK((r.id) == (1U));
    CHECK((r.name) == ("retrieve"));
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
      CHECK((s) == ("None"));
      break;
    case QueryType::Retrieve:
      CHECK((s) == ("RetrievalKey(id=1, name=retrieve)"));
      break;
    case QueryType::Range:
      CHECK((s) == ("Main RangeKey(start=0, end=0)"));
      break;
    case QueryType::OtherRange:
      CHECK((s) == ("Other RangeKey(start=0, end=0)"));
      break;
    case QueryType::Status: CHECK((s) == ("Status()")); break;
    }
    qv = QueryVariant::make<QueryType::OtherRange>(RangeKey{10, 20});
    s = visitor.visit(qv);
    CHECK((s) == ("Other RangeKey(start=10, end=20)"));
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
    CHECK((s) == ("Some RangeKey(start=10, end=20)"));
  }
}
#pragma endregion

#pragma region EnumVector_Basic

TEST_CASE("EnumVector_Basic", "[EnumVector]") {
  using id_t = test_id_t;
  enum_vector<int, id_t> v;

  CHECK((v.empty()));
  CHECK((v.size()) == (0U));

  v.reserve(6);
  CHECK((v.capacity() >= 6U));

  v.resize(2, 5);
  v.resize(2);

  v[id_t{0}] = 10;
  v.at(id_t{1}) = 11;

  const auto& cv = v;
  CHECK((cv[id_t{0}]) == (10));
  CHECK((cv.at(id_t{1})) == (11));

  auto& f = v.front();
  auto& b = v.back();
  f = 12;
  b = 13;
  CHECK((cv.front()) == (12));
  CHECK((cv.back()) == (13));

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
  CHECK((*enum_size) == (v.size()));

  auto& u = v.underlying();
  const auto& cu = cv.underlying();
  (void)u;
  (void)cu;

  auto& u2 = *v;
  const auto& u3 = *cv;
  (void)u2;
  (void)u3;

  v.clear();
  CHECK((v.empty()));
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

TEST_CASE("ScopedValue_Basic", "[ScopedValue]") {
  if (true) {
    int x = 1;
    {
      scoped_value sv{x, 42};
      CHECK((x) == (42));
    }
    CHECK((x) == (1));
  }
  if (true) {
    // Nested scopes restore in reverse order.
    int x = 1;
    {
      scoped_value sv1{x, 10};
      CHECK((x) == (10));
      {
        scoped_value sv2{x, 20};
        CHECK((x) == (20));
      }
      CHECK((x) == (10));
    }
    CHECK((x) == (1));
  }
  if (true) {
    // Works with non-trivial types.
    std::string s = "original";
    {
      scoped_value sv{s, std::string{"temporary"}};
      CHECK((s) == ("temporary"));
    }
    CHECK((s) == ("original"));
  }
  if (true) {
    // Old value is captured at construction; direct mutations to the target
    // are overwritten on scope exit.
    int x = 5;
    {
      scoped_value sv{x, 99};
      x = 7; // Mutate target directly while scoped_value is active.
      CHECK((x) == (7));
    }
    // Restored to 5 (captured at sv construction), not 7.
    CHECK((x) == (5));
  }
  if (true) {
    // If materializing the replacement throws, the target stays untouched.
    throwing_scoped_value_test x{"original"};
    throwing_scoped_value_test replacement{"temporary", true};

    CHECK_THROWS_AS(
        (void)scoped_value<throwing_scoped_value_test>(x, replacement),
        std::runtime_error);
    CHECK((x.value) == ("original"));
    CHECK_FALSE((x.throw_on_move));
  }
  if (true) {
    int x = 1;
    {
      scoped_value sv1{x, 10};
      scoped_value sv2{std::move(sv1)};
      CHECK((x) == (10));
    }
    CHECK((x) == (1));
  }
  if (true) {
    int x = 1;
    int y = 2;
    {
      scoped_value sv1{x, 10};
      scoped_value sv2{y, 20};
      sv2 = std::move(sv1);
      CHECK((x) == (10));
      CHECK((y) == (2));
    }
    CHECK((x) == (1));
    CHECK((y) == (2));
  }
  if (true) {
    int x = 1;
    {
      scoped_value sv{x, 10};
      sv.release();
      CHECK((x) == (10));
    }
    CHECK((x) == (10));
  }
}
#pragma endregion

#pragma region ScopeExit_Basic

TEST_CASE("ScopeExit_Basic", "[ScopeExit]") {
  if (true) {
    bool exited = false;
    {
      scope_exit guard{[&]() noexcept { exited = true; }};
      CHECK_FALSE((exited));
    }
    CHECK((exited));
  }
  if (true) {
    int value = 0;
    {
      auto guard = make_scope_exit([&]() noexcept { value = 42; });
      (void)guard;
      CHECK((value) == (0));
    }
    CHECK((value) == (42));
  }
  if (true) {
    bool exited = false;
    {
      auto guard = make_scope_exit([&]() noexcept { exited = true; });
      guard.release();
    }
    CHECK_FALSE((exited));
  }
  if (true) {
    int calls = 0;
    {
      auto guard1 = make_scope_exit([&]() noexcept { ++calls; });
      {
        auto guard2 = std::move(guard1);
        CHECK((calls) == (0));
        (void)guard2;
      }
      CHECK((calls) == (1));
    }
    CHECK((calls) == (1));
  }
  if (true) {
    int value = 0;
    {
      auto payload = std::make_unique<int>(7);
      auto guard = make_scope_exit(
          [owned = std::move(payload), &value]() noexcept { value = *owned; });
      CHECK_FALSE((payload));
      (void)guard;
    }
    CHECK((value) == (7));
  }
}
#pragma endregion

#pragma region HashCombiner_Basic

TEST_CASE("HashCombiner_Basic", "[HashCombiner]") {
  // Default seed is zero; explicit seed is respected.
  if (true) {
    hash_combiner h;
    CHECK((h.value()) == (0u));
    CHECK((static_cast<size_t>(h)) == (0u));
  }
  if (true) {
    hash_combiner h{42u};
    CHECK((h.value()) == (42u));
  }

  // Combining a non-zero hash into seed 0 must produce a non-zero result.
  if (true) {
    hash_combiner h;
    h.combine_hash(1u);
    CHECK((h.value()) != (0u));
  }

  // `combine` hashes a typed value and folds it in.
  if (true) {
    hash_combiner h;
    h.combine(123);
    CHECK((h.value()) != (0u));
  }

  // Order of combination must matter.
  if (true) {
    hash_combiner h1;
    h1.combine(1);
    h1.combine(2);

    hash_combiner h2;
    h2.combine(2);
    h2.combine(1);

    CHECK((h1.value()) != (h2.value()));
  }

  // `combine_all` must be equivalent to sequential `combine` calls.
  if (true) {
    hash_combiner h1;
    h1.combine_all(1, 2, 3);

    hash_combiner h2;
    h2.combine(1);
    h2.combine(2);
    h2.combine(3);

    CHECK((h1.value()) == (h2.value()));
  }

  // `combined_hash` must match building a combiner manually.
  if (true) {
    auto expected = combined_hash(std::string{"hello"}, 42, true);

    hash_combiner h;
    h.combine(std::string{"hello"});
    h.combine(42);
    h.combine(true);

    CHECK((expected) == (h.value()));
  }

  // Different argument values or orderings must produce different results.
  if (true) {
    CHECK((combined_hash(1, 2)) != (combined_hash(2, 1)));
    CHECK((combined_hash(1, 2)) != (combined_hash(1, 3)));
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
