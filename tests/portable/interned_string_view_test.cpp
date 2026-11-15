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

#include <compare>
#include <concepts>
#include <format>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

#include "corvid/strings/interned_string_view.h"
#include "catch2_main.h"

using namespace corvid;
using namespace std::literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region Specialization path

// A classifier hands back one of a few constants. These are the only minting
// sites, so every "Frog" is a copy of the same constant and callers compare
// answers by identity without reading the characters.
using species_name = interned_string_view<decltype([] {})>;

namespace species {
inline constexpr auto frog = species_name::create("Frog");
inline constexpr auto toad = species_name::create("Toad");
inline constexpr auto newt = species_name::create("Newt");
} // namespace species

struct sighting {
  bool has_tail{};
  bool warty{};
};

constexpr species_name identify(sighting s) noexcept {
  if (s.has_tail) return species::newt;
  return s.warty ? species::toad : species::frog;
}

TEST_CASE("SpecializationPath", "[InternedStringViewTest]") {
  using namespace species;

  // Answers compare by identity, and the content is still there to read.
  static_assert(identify({}) == frog);
  static_assert(identify({.warty = true}) == toad);
  static_assert(identify({.has_tail = true}) == newt);
  static_assert(frog != toad);
  static_assert(frog.view() == "Frog");
  CHECK(identify({}) == frog);
  CHECK(identify({}) != toad);
  CHECK(std::format("{}", frog) == "Frog");

  // The tag type has no default instances, since the classifier always
  // answers.
  static_assert(!std::is_default_constructible_v<species_name>);

  // Unordered and ordered keys both work, by identity.
  std::unordered_map<species_name, int> census;
  for (const auto s : {sighting{}, sighting{.warty = true}, sighting{}})
    ++census[identify(s)];
  CHECK(census.size() == 2U);
  CHECK(census.at(frog) == 2);
  CHECK(census.at(toad) == 1);
  CHECK_FALSE(census.contains(newt));

  const std::set<species_name> seen{newt, frog, toad, frog};
  CHECK(seen.size() == 3U);
  CHECK(seen.contains(frog));
}

#pragma endregion
#pragma region Inheritance path

// A symbol table owns the storage and is the only minter. The node-based set
// keeps every string's address stable, and interning the same text twice hands
// back the same node, so two symbols are equal exactly when their text is.
class symbol: public interned_string_view_wrapper<symbol> {
  using base = interned_string_view_wrapper<symbol>;
  friend class symbol_table;
  explicit symbol(std::string_view sv) noexcept : base{sv} {}

public:
  constexpr symbol() noexcept : base{nullptr} {}
};

class symbol_table {
public:
  symbol intern(std::string_view text) {
    const auto [node, inserted] = names_.emplace(text);
    return symbol{*node};
  }
  [[nodiscard]] size_t size() const noexcept { return names_.size(); }

private:
  std::unordered_set<std::string> names_;
};

TEST_CASE("InheritancePath", "[InternedStringViewTest]") {
  symbol_table table;
  const std::string respelled{"alpha"};
  const auto alpha = table.intern("alpha");
  const auto again = table.intern(respelled);
  const auto beta = table.intern("beta");
  CHECK(table.size() == 2U);

  // Same text interns to the same node, so identity and content agree.
  CHECK(alpha == again);
  CHECK(alpha.data() == again.data());
  CHECK(alpha != beta);
  CHECK(alpha.view() == "alpha");

  // The child chose a null default, which is distinct from every symbol.
  const symbol none;
  CHECK(none.null());
  CHECK_FALSE(none);
  CHECK(none != alpha);
  CHECK(none == symbol{});
  CHECK(std::format("{}", none).empty());

  // Hashing by identity makes a symbol a cheap unordered key.
  std::unordered_map<symbol, int> uses;
  ++uses[alpha];
  ++uses[again];
  ++uses[beta];
  CHECK(uses.size() == 2U);
  CHECK(uses.at(alpha) == 2);
  CHECK(std::hash<symbol>{}(alpha) == std::hash<symbol>{}(again));
}

#pragma endregion
#pragma region Options

struct a_tag {};
struct b_tag {};

TEST_CASE("DefaultPolicies", "[InternedStringViewTest]") {
  // `no_default` is the default policy.
  static_assert(
      std::same_as<interned_string_view<a_tag>::default_type, no_default>);
  static_assert(!std::is_default_constructible_v<interned_string_view<a_tag>>);

  // `nullptr_default`: all defaults are null and identical.
  using nulled = interned_string_view<a_tag, nullptr_default>;
  nulled n;
  CHECK(n.null());
  CHECK(n == nulled{});
  n = nulled::create("x");
  CHECK(n.has_value());
  n.reset();
  CHECK(n.null());

  // `literal_default`: all defaults share one copy of the literal, even when
  // it is empty, which then differs from null.
  using lettered = interned_string_view<a_tag, literal_default<"blank">>;
  const lettered l1;
  const lettered l2;
  CHECK(l1 == l2);
  CHECK(l1.view() == "blank");
  using emptied = interned_string_view<a_tag, literal_default<"">>;
  const emptied e1;
  const emptied e2;
  CHECK(e1 == e2);
  CHECK(e1.empty());
  CHECK_FALSE(e1.null());

  // The wide alias takes a wide literal.
  using wide = interned_wstring_view<a_tag, literal_default<L"wide">>;
  const wide w;
  CHECK(w.view() == L"wide");
}

TEST_CASE("StrongTyping", "[InternedStringViewTest]") {
  using a = interned_string_view<a_tag>;
  using b = interned_string_view<b_tag>;
  static_assert(!std::is_convertible_v<a, b>);
  static_assert(!std::is_constructible_v<a, b>);

  // Nothing but `create` and the copy constructor builds one.
  static_assert(!std::is_constructible_v<a, std::string_view>);
  static_assert(!std::is_constructible_v<a, const char*>);
  static_assert(!std::is_constructible_v<a, std::string>);
  static_assert(std::is_copy_constructible_v<a>);

  // Conversion to a view is explicit, and content comparison against a raw
  // view does not exist, so identity cannot be bypassed by accident.
  static_assert(!std::is_convertible_v<a, std::string_view>);
  static_assert(std::is_constructible_v<std::string_view, a>);
  static_assert(!std::equality_comparable_with<a, std::string_view>);
  static_assert(!std::equality_comparable_with<a, const char*>);
}

TEST_CASE("IdentitySemantics", "[InternedStringViewTest]") {
  using view = interned_string_view<a_tag>;
  static constexpr std::string_view storage = "abcdef";

  // Same storage, different lengths: distinct, and the prefix sorts first.
  const auto whole = view::create(storage);
  const auto prefix = view::create(storage.substr(0, 3));
  CHECK(whole != prefix);
  CHECK(prefix < whole);
  static_assert(
      std::same_as<decltype(whole <=> prefix), std::strong_ordering>);

  // A copy is identical. A different constant is not, whatever it says.
  const auto copy = whole;
  CHECK(copy == whole);
  CHECK(std::hash<view>{}(copy) == std::hash<view>{}(whole));
  static constexpr std::string_view other = "xyz";
  CHECK(view::create(other) != whole);

  // The family concepts agree on what this is.
  static_assert(StringViewWrapperChild<view>);
  static_assert(IdentityCompared<view>);
  static_assert(IdentityCompared<symbol>);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
