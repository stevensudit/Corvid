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
#pragma once
#include <type_traits>

namespace corvid { inline namespace meta { inline namespace passkeys {

#pragma region passkey

// Construction key that only `T` can mint.
//
// A constructor taking a `passkey<T>` stays public, so `std::make_unique`,
// `std::make_shared`, and inherited constructors can call it, yet only `T` can
// produce the argument. A class names its key with `using mint = passkey<T>;`,
// where `T` is itself or the factory class that constructs it, and derived
// constructors forward the key they were given rather than minting a fresh
// one.
//
// A key with several minters awaits C++26 variadic friends (P2893,
// `__cpp_variadic_friend`), when a `passkey<Ts...>` can befriend each of them.
// A minter that is every specialization of a class template stays out of reach
// even then, since a friend pack names types, not templates. Such a key is
// hand-written with a friend template.
template<typename T>
class passkey {
  friend T;

  // User-provided and not `= default`, so the class is never an aggregate.
  // This is what keeps `{}` from bypassing the private constructor.
  // NOLINTNEXTLINE(modernize-use-equals-default)
  constexpr passkey() noexcept {}
};

// The key costs nothing to pass by value.
static_assert(
    std::is_empty_v<passkey<int>> &&
    std::is_trivially_copyable_v<passkey<int>>);

#pragma endregion

}}} // namespace corvid::meta::passkeys
