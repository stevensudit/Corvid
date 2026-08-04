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

#include <string>
#include <type_traits>
#include <utility>

#include "corvid/containers.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

using parsed = expected<int, std::string>;

#pragma region Construction

TEST_CASE("ExpectedConstruction", "[ExpectedTest]") {
  // A `T` constructs a value; an `E` constructs an error.
  if (true) {
    parsed r = 42;
    CHECK(r.has_value());
    CHECK(*r == 42);
  }
  if (true) {
    parsed r = "missing digits"s;
    CHECK_FALSE(r.has_value());
    CHECK(r.as_error() == "missing digits");
  }
  // A string literal is unambiguous when only `E` can absorb it.
  if (true) {
    parsed r{"missing digits"};
    CHECK_FALSE(r.has_value());
  }
  // Default is a value-initialized `T`.
  if (true) {
    parsed r;
    CHECK(r.has_value());
    CHECK(*r == 0);
  }
  // Copies and moves preserve the alternative.
  if (true) {
    parsed r = 42;
    parsed q{r};
    CHECK(q.has_value());
    parsed m{std::move(q)};
    CHECK(*m == 42);
    parsed e = "oops"s;
    parsed f{e};
    CHECK(f.as_error() == "oops");
  }
}

#pragma endregion
#pragma region Access

TEST_CASE("ExpectedAccess", "[ExpectedTest]") {
  if (true) {
    parsed r = 42;
    CHECK(static_cast<bool>(r));
    if (!r) FAIL("expected a value");
    CHECK(*r == 42);
  }
  if (true) {
    expected<std::string, int> r = "text"s;
    CHECK(r->size() == 4);
  }
  // The error moves out of an rvalue.
  if (true) {
    parsed r = "gone"s;
    const auto taken = std::move(r).as_error();
    CHECK(taken == "gone");
  }
}

#pragma endregion
#pragma region Maybe access

TEST_CASE("ExpectedMaybe", "[ExpectedTest]") {
  // `maybe_value` and `maybe_error` return an `optional_ptr`, empty when the
  // other alternative is engaged; its adapters do the rest.
  if (true) {
    parsed r = 42;
    CHECK(r.maybe_value().value_or(-1) == 42);
    CHECK_FALSE(r.maybe_error().has_value());
    CHECK(r.maybe_error().value_or("none") == "none");
  }
  if (true) {
    parsed r = "missing digits"s;
    CHECK(r.maybe_value().value_or(-1) == -1);
    CHECK(r.maybe_error().value_or("none") == "missing digits");
    CHECK(r.maybe_value().value_or_fn([] { return 7; }) == 7);
  }
  // The pointer semantics bind naturally in a condition.
  if (true) {
    parsed r = 42;
    if (const auto v = r.maybe_value())
      CHECK(*v == 42);
    else
      FAIL("expected a value");
  }
}

#pragma endregion
#pragma region Error propagation

TEST_CASE("ExpectedPropagation", "[ExpectedTest]") {
  // Propagate a failure out of a function whose `expected` has a different
  // value type: the failed `expected` itself converts.
  auto to_double = [](const parsed& p) -> expected<double, std::string> {
    if (!p) return p;
    return *p * 2.0;
  };

  CHECK(*to_double(parsed{21}) == 42.0);
  const auto r = to_double(parsed{"missing digits"});
  REQUIRE_FALSE(r.has_value());
  CHECK(r.as_error() == "missing digits");

  // An rvalue moves the error across.
  parsed failed{"moved"};
  expected<double, std::string> taken{std::move(failed)};
  CHECK(taken.as_error() == "moved");
}

#pragma endregion
#pragma region Tags

TEST_CASE("ExpectedTags", "[ExpectedTest]") {
  // A tag makes otherwise-identical specializations distinct types, so
  // neither values nor errors cross between them.
  using apples = expected<int, std::string, struct AppleTag>;
  using oranges = expected<int, std::string, struct OrangeTag>;
  static_assert(!std::is_convertible_v<apples, oranges>);
  static_assert(!std::is_convertible_v<oranges, apples>);

  // Error propagation converts across both value types and tags, gated only
  // on an identical `E`.
  using peeled = expected<double, std::string, struct OrangeTag>;
  static_assert(std::is_convertible_v<apples, peeled>);
  apples a = "unripe"s;
  peeled p = a;
  CHECK(p.as_error() == "unripe");
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
