// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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

#pragma once
#include "containers_shared.h"

namespace corvid { inline namespace ranges {

// Reversing view over container.
template<typename T>
class reversed_range {
public:
  constexpr reversed_range(T& t) noexcept : t_{t} {}

  constexpr auto begin() noexcept { return t_.rbegin(); }
  constexpr auto end() noexcept { return t_.rend(); }

  constexpr auto cbegin() const noexcept { return t_.crbegin(); }
  constexpr auto cend() const noexcept { return t_.crend(); }

private:
  T& t_;
};

// TODO: We may need a deduction rule to infer T.

}} // namespace corvid::ranges
