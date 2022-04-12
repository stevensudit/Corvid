// Copyright 2022 Steven Sudit
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
#include "pch.h"

#include "OptionalPtr.h"

using namespace std::literals;
using namespace corvid;

TEST(optional_ptrTest, Construction) {
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
    //* optional_ptr qo{o};
    optional_ptr ro{std::move(o)};
    EXPECT_FALSE(o.has_value());
    EXPECT_TRUE(ro.has_value());
  }
}

TEST(optional_ptrTest, Access) {
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
    EXPECT_EQ(o->size(), 4);
  }
  if (true) {
    auto const test{"test"s};
    optional_ptr o = &test;
    auto p = o.get();
    //* o.get()->resize(test.size());
    p++;
    p[6];
    p = p + 1;
    //* o++;
    //* o[6];
    //* o + 1;
  }
}

TEST(optional_ptrTest, OrElse) {
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

TEST(optional_ptrTest, ConstOrPtr) {
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

TEST(optional_ptrTest, Smart) {
  if (true) {
    EXPECT_TRUE(optional_ptr<int*>::is_raw);
    EXPECT_FALSE(optional_ptr<std::unique_ptr<int>>::is_raw);

    auto test{"test"s};
    optional_ptr o = std::make_unique<std::string>(test);
    EXPECT_TRUE(o.has_value());
    EXPECT_EQ(o->size(), test.size());

    //* auto qo{o};
    auto qo{std::move(o)};
    EXPECT_FALSE(o.has_value());
    EXPECT_TRUE(qo.has_value());

    EXPECT_EQ(*qo, test);
    auto& q = *qo;
    q.resize(q.size());
    EXPECT_EQ(q, test);

    //* auto p{qo.get()};
    auto p{std::move(qo).get()};

    auto l = [&test]() {
      return optional_ptr{std::make_unique<std::string>(test)};
    };

    p = l().get();
    p.reset();

    bool f = o ? true : false;
    EXPECT_FALSE(f);

    // Does not compile because operator bool is marked as explicit and this is
    // not a predicate context.
    // * f = o;

    o.reset(std::make_unique<std::string>(test));
    EXPECT_EQ(o->size(), 4);
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

    EXPECT_EQ(o->size(), 4);
  }
}
