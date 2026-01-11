// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include "./meta_shared.h"

namespace corvid { inline namespace meta { inline namespace forwarding {

#if defined(__GLIBCXX__) && !defined(__cpp_lib_forward_like)
// Fallback implementation of std::forward_like for older libstdc++.
template<class T, class U>
[[nodiscard]] constexpr decltype(auto) forward_like(U&& u) noexcept {
  using UT = std::remove_reference_t<U>;
  using Base = std::conditional_t<std::is_const_v<std::remove_reference_t<T>>,
      std::add_const_t<UT>, UT>;
  using CV = std::conditional_t<std::is_volatile_v<std::remove_reference_t<T>>,
      std::add_volatile_t<Base>, Base>;
  if constexpr (std::is_lvalue_reference_v<T>)
    return static_cast<CV&>(u);
  else
    return static_cast<CV&&>(std::forward<U>(u));
}
#else
using std::forward_like;
#endif

}}} // namespace corvid::meta::forwarding
