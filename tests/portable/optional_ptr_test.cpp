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

#include "corvid/containers.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;
using corvid::internal::optional_ptr;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Construction

TEST_CASE("Construction", "[OptionalPtrTest]") {
  if (true) {
    optional_ptr<int*> o;
    CHECK_FALSE(o.has_value());
  }
  if (true) {
    optional_ptr<int*> o{nullptr};
    CHECK_FALSE(o.has_value());
  }
  if (true) {
    optional_ptr<int*> o{std::nullopt};
    CHECK_FALSE(o.has_value());
  }
  if (true) {
    int i{42};
    optional_ptr o{&i};
    CHECK(o.has_value());
    o.reset();
    CHECK_FALSE(o.has_value());
  }
  if (true) {
    optional_ptr o{new int{42}};
    CHECK(o.has_value());
    delete o.get();
    o.reset();
    CHECK_FALSE(o.has_value());
  }
  if (true) {
    int i{42};
    optional_ptr o{&i};
    // NOLINTNEXTLINE(performance-move-const-arg)
    optional_ptr qo{std::move(o)};
    // NOLINTBEGIN
    optional_ptr ro{o};
    CHECK(o.has_value());
    // NOLINTEND
    CHECK(qo.has_value());
    CHECK(ro.has_value());
  }
  if (true) {
    auto test{"test"s};
    optional_ptr o{std::make_unique<std::string>(test)};
    // * optional_ptr qo{o};
    optional_ptr ro{std::move(o)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(o.has_value());
    CHECK(ro.has_value());
  }
}

#pragma endregion
#pragma region Access

TEST_CASE("Access", "[OptionalPtrTest]") {
  if (true) {
    auto test{"test"s};
    optional_ptr o = &test;
    CHECK(o.has_value());
    CHECK(o.value() == test);
    std::string* p = o;
    CHECK(p == o);
    CHECK(o->size() == test.size());
    CHECK(p == o.get());
    o.reset();
    CHECK_THROWS_AS(o.value(), std::bad_optional_access);
    bool f = o ? true : false;
    CHECK_FALSE(f);

    // Raw pointers evaluate to bool in any context.
    f = o;
    (void)f;

    o.reset(&test);
    CHECK(o->size() == 4U);
  }
  if (true) {
    auto const test{"test"s};
    optional_ptr o = &test;
    auto p = o.get();
    // * o.get()->resize(test.size());
    p++;
    // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound): intentional UB
    (void)p[6];
    p = p + 1;
    (void)p;
    // * o++;
    // * o[6];
    // * o + 1;
  }
}

#pragma endregion
#pragma region OrElse

TEST_CASE("OrElse", "[OptionalPtrTest]") {
  if (true) {
    optional_ptr<std::string*> o;
    CHECK_FALSE(o.has_value());
    std::string empty{};
    auto test{"test"s};
    auto l = [&]() { return test; };
    CHECK(o.value_or(test) == test);
    CHECK(o.value_or() == empty);
    CHECK(o.value_or_ptr(&test) == test);
    CHECK(o.value_or_fn(l) == test);
  }
}

#pragma endregion
#pragma region ConstOrPtr

TEST_CASE("ConstOrPtr", "[OptionalPtrTest]") {
  if (true) {
    const auto test{"test"s};
    optional_ptr<const std::string*> o;
    auto& p = o.value_or_ptr(&test);
    auto b = std::is_same_v<decltype(p), const std::string&>;
    CHECK(b);
  }
  if (true) {
    const auto test{"test"s};
    optional_ptr<std::string*> o;
    auto& p = o.value_or_ptr(&test);
    CHECK((std::is_same_v<decltype(p), const std::string&>));
  }
  if (true) {
    auto test{"test"s};
    optional_ptr<const std::string*> o;
    auto& p = o.value_or_ptr(&test);
    CHECK((std::is_same_v<decltype(p), const std::string&>));
  }
}

#pragma endregion
#pragma region Smart

TEST_CASE("Smart", "[OptionalPtrTest]") {
  if (true) {
    auto test{"test"s};
    optional_ptr o = std::make_unique<std::string>(test);
    CHECK(o.has_value());
    CHECK(o->size() == test.size());

    // * auto qo{o};
    auto qo{std::move(o)};
    // NOLINTNEXTLINE(bugprone-use-after-move,clang-analyzer-cplusplus.Move)
    CHECK_FALSE(o.has_value());
    CHECK(qo.has_value());

    CHECK(*qo == test);
    auto& q = *qo;
    q.resize(q.size());
    CHECK(q == test);

    // * auto p{qo.get()};
    auto p{std::move(qo).get()};

    auto l = [&test]() {
      return optional_ptr{std::make_unique<std::string>(test)};
    };

    p = l().get();
    p.reset();

    // This was moved from, so it's empty.
    bool f = o ? true : false;
    CHECK_FALSE(f);

    // Does not compile because operator bool is marked as explicit and this is
    // not a predicate context.
    // * f = o;

    o.reset(std::make_unique<std::string>(test));
    CHECK(o->size() == 4U);
    o.reset();
    CHECK_FALSE(o.has_value());
  }
  if (true) {
    auto test{"test"s};
    optional_ptr o = std::make_shared<std::string>(test);
    CHECK(o.has_value());
    CHECK(o->size() == test.size());

    auto qo{o};
    CHECK(o.has_value());
    CHECK(qo.has_value());

    CHECK(*qo == test);
    auto& q = *qo;
    q.resize(q.size());
    CHECK(q == test);

    auto p{qo.get()};

    bool f = o ? true : false;
    CHECK(f);

    // Does not compile because operator bool is marked as explicit and this is
    // not a predicate context.
    // * f = o;

    CHECK(o->size() == 4U);

    p.reset();
    qo.reset();
  }
}

#pragma endregion
#pragma region Dumb

TEST_CASE("Dumb", "[OptionalPtrTest]") {
  using O = optional_ptr<int*>;

  if (true) {
    O o = nullptr;
    CHECK_FALSE(o);
    O p(nullptr);
    CHECK_FALSE(p);
    CHECK_FALSE(O{nullptr});
  }
  if (true) {
    int i;
    O o(&i);
    CHECK(o);
    auto& p = (o = nullptr);
    CHECK_FALSE(o);
    CHECK_FALSE(p);
  }
  if (true) {
    int i;
    O a(&i);
    O b;
    CHECK(a != b);
    CHECK(!(a == b));
    CHECK_FALSE(a == O());
    CHECK(b == O());
    CHECK_FALSE(a == nullptr);
    CHECK(b == nullptr);
    CHECK(a != O());
    CHECK_FALSE(b != O());
    CHECK(a != nullptr);
    CHECK_FALSE(b != nullptr);
  }
}

#pragma endregion
#pragma region Format

TEST_CASE("Format", "[OptionalPtrTest]") {
  int i{42};
  optional_ptr<int*> p{&i};
  optional_ptr<int*> n;

  // A present pointee forwards to its formatter (with its spec); a null
  // renders the unquoted `(null)` marker.
  CHECK(std::format("{}", p) == "42");
  CHECK(std::format("{:>4}", p) == "  42");
  CHECK(std::format("{}", n) == "(null)");

  // Narrow and wide both come along.
  CHECK(std::format(L"{}", p) == L"42");
  CHECK(std::format(L"{}", n) == L"(null)");

  // `{:?}` forwards to the pointee's debug format when present.
  std::string s{"hello"};
  optional_ptr<std::string*> sp{&s};
  optional_ptr<std::string*> sn;
  CHECK(std::format("{}", sp) == "hello");
  CHECK(std::format("{:?}", sp) == "\"hello\"");
  CHECK(std::format("{:?}", sn) == "(null)");
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
