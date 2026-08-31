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

#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "corvid/containers.h"
#include "corvid/strings/cases.h"
#include "catch2_main.h"

using namespace std::literals;
using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

using parsed = value_or_error<int, std::string>;

// Nothing leaks through from the variant underneath: no comparisons are
// generated.
static_assert(!std::equality_comparable<parsed>);

// Results assign like they construct. A const wing would silently delete
// this, which is one reason the class rejects cv-qualified wings outright
// (the rejection itself is a static_assert, so it cannot be probed here).
static_assert(std::is_copy_assignable_v<parsed>);
static_assert(std::is_move_assignable_v<parsed>);

#pragma region Construction

TEST_CASE("VoeConstruction", "[ValueOrErrorTest]") {
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
  // Default is a failure holding a value-initialized `E`, so `return {};`
  // reports a generic error. `T` need not be default-constructible.
  if (true) {
    parsed r;
    CHECK_FALSE(r.has_value());
    CHECK(r.as_error().empty());
  }
  if (true) {
    parsed r = {};
    CHECK_FALSE(r.has_value());
  }
  // Copies and moves preserve the alternative.
  if (true) {
    parsed r = 42;
    parsed q{r};
    CHECK(q.has_value());
    parsed m{std::move(q)};
    CHECK(*m == 42);
    parsed e = "oops"s;
    // NOLINTNEXTLINE(performance-unnecessary-copy-initialization)
    parsed f{e};
    CHECK(f.as_error() == "oops");
  }
}

#pragma endregion
#pragma region Access

TEST_CASE("VoeAccess", "[ValueOrErrorTest]") {
  if (true) {
    parsed r = 42;
    CHECK(static_cast<bool>(r));
    if (!r) FAIL("expected a value");
    CHECK(*r == 42);
  }
  if (true) {
    value_or_error<std::string, int> r = "text"s;
    CHECK(r->size() == 4);
  }
  // The value and the error each move out of an rvalue.
  if (true) {
    value_or_error<std::string, int> r = "payload"s;
    const auto taken = *std::move(r);
    CHECK(taken == "payload");
  }
  if (true) {
    parsed r = "gone"s;
    const auto taken = std::move(r).as_error();
    CHECK(taken == "gone");
  }
}

#pragma endregion
#pragma region Assignment

TEST_CASE("VoeAssignment", "[ValueOrErrorTest]") {
  // Assignment converts exactly as construction does, in both directions.
  parsed r = 42;
  r = "oops"s;
  CHECK_FALSE(r.has_value());
  CHECK(r.as_error() == "oops");
  r = 7;
  CHECK(*r == 7);
}

#pragma endregion
#pragma region Maybe access

TEST_CASE("VoeMaybe", "[ValueOrErrorTest]") {
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

TEST_CASE("VoePropagation", "[ValueOrErrorTest]") {
  // Propagate a failure out of a function whose result has a different value
  // type: the failed result itself converts.
  auto to_double = [](const parsed& p) -> value_or_error<double, std::string> {
    if (!p) return p;
    return *p * 2.0;
  };

  CHECK(*to_double(parsed{21}) == 42.0);
  const auto r = to_double(parsed{"missing digits"});
  REQUIRE_FALSE(r.has_value());
  CHECK(r.as_error() == "missing digits");

  // An rvalue moves the error across.
  parsed failed{"moved"};
  value_or_error<double, std::string> taken{std::move(failed)};
  CHECK(taken.as_error() == "moved");
}

#pragma endregion
#pragma region Success propagation

TEST_CASE("VoeSuccessPropagation", "[ValueOrErrorTest]") {
  // The opposite flow: a successful result converts across error types, so
  // crossing an error-domain boundary with a good value is just returning
  // it.
  using wire_error = error_value<struct WireTag>;
  using wired = value_or_error<int, wire_error>;
  auto transmit = [](const parsed& p) -> wired {
    if (p) return p;
    return wire_error{"lost: " + p.as_error()};
  };

  CHECK(*transmit(parsed{7}) == 7);
  const auto r = transmit(parsed{"corrupt"});
  REQUIRE_FALSE(r.has_value());
  CHECK(r.as_error().reason == "lost: corrupt");

  // An rvalue moves the value across.
  parsed ok = 9;
  wired taken{std::move(ok)};
  CHECK(*taken == 9);
}

#pragma endregion
#pragma region error_value

TEST_CASE("VoeErrorValue", "[ValueOrErrorTest]") {
  // `error_value` distinguishes error types nominally, so a failure cannot
  // cross between the result specializations built on them. A good value
  // still can: that is success propagation, working as intended.
  using parse_error = error_value<struct ParseTag>;
  using io_error = error_value<struct IoTag>;
  static_assert(!std::is_convertible_v<parse_error, io_error>);
  using parse_result = value_or_error<int, parse_error>;
  using io_result = value_or_error<int, io_error>;
  static_assert(std::is_convertible_v<parse_result, io_result>);

  // Results sharing neither `T` nor `E` do not convert at all.
  static_assert(
      !std::is_convertible_v<parse_result, value_or_error<double, io_error>>);

  parse_result r = parse_error{"bad digit"};
  CHECK_FALSE(r.has_value());
  CHECK(r.as_error().reason == "bad digit");

  // The minimal spelling names only the domain; the reason defaults to
  // "Unknown error", which composes with the result's default constructor:
  // `return {};` is a self-describing generic failure.
  using generic = value_or_error<int, error_value<struct GenericTag>>;
  generic g = {};
  CHECK_FALSE(g.has_value());
  CHECK(g.as_error().reason == "Unknown error");

  // A domain with its own reason type supplies its own default: anything
  // explicitly convertible to that type.
  enum class wire_errc : uint8_t { unknown, timeout };
  using wire_result = value_or_error<int,
      error_value<struct WireDefaultTag, wire_errc, wire_errc::unknown>>;
  wire_result w = {};
  CHECK(w.as_error().reason == wire_errc::unknown);
}

TEST_CASE("VoeErrorValueUsage", "[ValueOrErrorTest]") {
  // A miniature pipeline with two error domains. Each stage wraps its reason
  // in its own `error_value`, so a failure cannot drift between domains
  // unnoticed, while a good value flows freely.
  using parse_error = error_value<struct ParseTag>;
  using math_error = error_value<struct MathTag>;

  auto parse_digit =
      [](std::string_view s) -> value_or_error<int, parse_error> {
    if (s.size() != 1 || !strings::is_digit(s[0]))
      return parse_error{"not a digit: " + std::string{s}};
    return s[0] - '0';
  };

  // An error is spelled by naming its type; "VoeAnonymousErrorLimits" shows
  // why the anonymous spelling is shunned.
  auto halve = [](int n) -> value_or_error<int, math_error> {
    if (n % 2) return math_error{"odd: " + std::to_string(n)};
    return n / 2;
  };

  // Crossing domains: a good value crosses on its own (success propagation);
  // a failure is translated, deliberately.
  auto to_math = [](const value_or_error<int, parse_error>& n)
      -> value_or_error<int, math_error> {
    if (n) return n;
    return math_error{"bad input: " + n.as_error().reason};
  };

  // Within a domain, failure propagation is just returning what you have.
  auto half_of = [&](std::string_view s) -> value_or_error<int, math_error> {
    auto n = to_math(parse_digit(s));
    if (!n) return n;
    return halve(*n);
  };

  CHECK(*half_of("4") == 2);
  CHECK(half_of("3").as_error().reason == "odd: 3");
  CHECK(half_of("x").as_error().reason == "bad input: not a digit: x");
}

// Whether the anonymous double-brace error spelling compiles for a result. A
// concept keeps the failure in a substitution context so it can be asserted.
template<typename R>
concept AcceptsAnonymousBraces = requires(R r) { r = {{"bad digit"}}; };

TEST_CASE("VoeAnonymousErrorLimits", "[ValueOrErrorTest]") {
  // Why the anonymous double-brace error spelling is an anti-pattern: it
  // works by coincidence until `T` can also absorb the inner list, and then
  // it fails silently rather than loudly.
  using math_error = error_value<struct MathTag>;
  using named = value_or_error<std::string, math_error>;

  // When both wings can absorb the inner list, the spelling is ambiguous
  // outright and fails to compile.
  static_assert(!AcceptsAnonymousBraces<named>);

  // When only the value wing can absorb it, it compiles, and silently
  // constructs a value where an error was meant.
  using numeric_error = error_value<struct NumericTag, int, 0>;
  value_or_error<std::string, numeric_error> silent = {{"bad digit"}};
  CHECK(silent.has_value());

  // A bare string is a value, unambiguously.
  named val = "payload"s;
  CHECK(val.has_value());

  // The anti-pattern in action: this compiles, but as a VALUE. A
  // single-element list whose element is already a `T` converts at
  // exact-match rank, beating the error aggregate's user-defined rank.
  named trap{{"went to the value"s}};
  CHECK(trap.has_value());

  // A designator restores safety, since a designated list can only
  // initialize an aggregate, filtering `T` out; its worst case is honest
  // ambiguity. But it still hides which error domain is entered, so it is
  // not the house spelling either.
  named halfway{{.reason = "went to the error"s}};
  CHECK_FALSE(halfway.has_value());

  // The sanctioned spelling names the domain: always correct, and it says
  // which error is being made.
  named err = math_error{"I hate you"s};
  CHECK_FALSE(err.has_value());
  CHECK(err.as_error().reason == "I hate you");
}

#pragma endregion
#pragma region Monostate

TEST_CASE("VoeMonostate", "[ValueOrErrorTest]") {
  // A value-less result: did it work, and if not, why not. Success is
  // spelled explicitly, since default construction is a failure.
  using outcome = value_or_error<std::monostate, std::string>;
  outcome ok = std::monostate{};
  CHECK(ok.has_value());
  outcome failed = "no permission"s;
  CHECK_FALSE(failed.has_value());
  CHECK(failed.maybe_error().value_or("none") == "no permission");
}

TEST_CASE("VoeVoidWing", "[ValueOrErrorTest]") {
  // A `void` wing is the spelling for "nothing to carry": it is stored as a
  // `std::monostate`, so success is returned as `std::monostate{}`.
  using outcome = value_or_error<void, std::string>;
  static_assert(std::same_as<outcome::value_type, std::monostate>);
  outcome ok = std::monostate{};
  CHECK(ok.has_value());
  outcome failed = "no permission"s;
  CHECK_FALSE(failed.has_value());
  CHECK(failed.as_error() == "no permission");

  // A `void` error fits a result whose failure needs no explanation, and it
  // composes with default construction being a failure.
  using attempt = value_or_error<std::string, void>;
  static_assert(std::same_as<attempt::error_type, std::monostate>);
  attempt made = "payload"s;
  CHECK(*made == "payload");
  attempt refused = {};
  CHECK_FALSE(refused.has_value());
  refused = "second try"s;
  CHECK(refused.has_value());
  refused = std::monostate{}; // An `E` constructs an error.
  CHECK_FALSE(refused.has_value());

  // The type follows the specialization, not the storage: the `void` and
  // `std::monostate` spellings name distinct result types.
  static_assert(
      !std::same_as<outcome, value_or_error<std::monostate, std::string>>);
}

#pragma endregion
#pragma region References

TEST_CASE("VoeReferences", "[ValueOrErrorTest]") {
  using lookup_error = error_value<struct LookupTag>;
  using name_ref = value_or_error<const std::string&, lookup_error>;

  // A reference result hands back the caller's own object, not a copy.
  const auto name =
      "a name long enough to defeat the small-string optimization"s;
  // NOLINTNEXTLINE(performance-no-automatic-move): binds a reference; no move.
  auto say_my_name = [&]() -> name_ref { return name; };
  auto r = say_my_name();
  REQUIRE(r.has_value());
  CHECK(&*r == &name);
  CHECK(r->size() == name.size());
  CHECK(r.maybe_value().value_or("none") == name);

  // The error wing is unaffected.
  auto no_name = [] -> name_ref { return lookup_error{"unknown"}; };
  CHECK(no_name().as_error().reason == "unknown");

  // Construction from a temporary is deleted, including a temporary
  // materialized by conversion, so a dangling reference cannot form.
  static_assert(!std::is_constructible_v<name_ref, std::string>);
  static_assert(!std::is_constructible_v<name_ref, const char*>);

  // Success propagation carries the reference itself across error domains.
  using other_ref =
      value_or_error<const std::string&, error_value<struct OtherLookupTag>>;
  other_ref crossed = r;
  CHECK(&*crossed == &name);

  // A mutable reference writes through to the referent.
  auto count = 0;
  value_or_error<int&, lookup_error> c = count;
  *c += 1;
  CHECK(count == 1);
}

#pragma endregion
#pragma region Pointers

TEST_CASE("VoePointerWings", "[ValueOrErrorTest]") {
  using open_error = error_value<struct OpenTag>;

  // A move-only smart pointer works as a value wing, and the result becomes
  // move-only along with it.
  using handle_result = value_or_error<std::unique_ptr<int>, open_error>;
  static_assert(!std::is_copy_constructible_v<handle_result>);
  static_assert(std::is_move_constructible_v<handle_result>);
  static_assert(std::is_move_assignable_v<handle_result>);

  auto open = [](bool ok) -> handle_result {
    if (!ok) return open_error{"denied"};
    return std::make_unique<int>(42);
  };

  auto r = open(true);
  REQUIRE(r.has_value());
  CHECK(**r == 42);

  // The payload moves out of an rvalue result; the husk stays on the value
  // wing, now holding an empty pointer.
  auto owned = *std::move(r);
  CHECK(*owned == 42);
  // NOLINTNEXTLINE(bugprone-use-after-move): inspecting the husk is the point.
  CHECK(r.has_value());
  CHECK(!*r);

  // The error wing is unaffected.
  CHECK(open(false).as_error().reason == "denied");

  // Success propagation moves the pointer across error domains.
  using stash_result =
      value_or_error<std::unique_ptr<int>, error_value<struct StashTag>>;
  stash_result crossed = open(true);
  CHECK(**crossed == 42);

  // A raw pointer wing holds the address without owning it, and writes
  // through.
  auto n = 7;
  value_or_error<int*, open_error> praw = &n;
  REQUIRE(praw.has_value());
  CHECK(*praw == &n);
  **praw += 1;
  CHECK(n == 8);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
