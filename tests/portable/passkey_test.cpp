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

#include <memory>
#include <type_traits>

#include "corvid/meta/passkey.h"
#include "catch2_main.h"

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity)

namespace {

// Self-minted: the class is its own factory.
class locker {
public:
  using mint = passkey<locker>;
  explicit locker(mint) noexcept {}

  static std::shared_ptr<locker> open() {
    return std::make_shared<locker>(mint{});
  }
};

// Minted by another class: only `ledger` can post an entry.
class ledger;
class entry {
public:
  using mint = passkey<ledger>;
  constexpr entry(mint, int amount) noexcept : amount{amount} {}
  int amount;
};

class ledger {
public:
  static constexpr entry post(int amount) noexcept {
    return entry{entry::mint{}, amount};
  }
};

// Whether an empty brace list stands in for the key, which is the hole an
// enum key leaves open.
template<typename T>
concept BraceKeyed = requires { T{{}}; };
template<typename T>
concept BraceKeyedWithArg = requires { T{{}, 0}; };

} // namespace

TEST_CASE("Passkey", "[PasskeyTest]") {
  static_assert(std::is_empty_v<locker::mint>);
  static_assert(std::is_trivially_copyable_v<locker::mint>);

  // Nothing outside the friend can produce a key, by any spelling.
  static_assert(!std::is_default_constructible_v<locker::mint>);
  static_assert(!std::is_default_constructible_v<entry::mint>);
  static_assert(!BraceKeyed<locker>);
  static_assert(!BraceKeyedWithArg<entry>);

  CHECK(locker::open() != nullptr);
  static_assert(ledger::post(7).amount == 7);
}

// NOLINTEND(readability-function-cognitive-complexity)
