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
#pragma once
#include <cstddef>

#include "flexi_function.h"
#include "invocable_policy.h"
#include "padding.h"

namespace corvid { inline namespace meta {

#pragma region fixed_function

namespace fn_details {

// Not defined: naming it in a constant expression is how `fixed_function`
// rejects an `SZ` too small to hold the two thunk pointers.
void fixed_function_SZ_must_exceed_two_pointers();

// The policy behind `fixed_function<SZ, Sig>`: an `inline_only` buffer filling
// out an instance of exactly `SZ` bytes.
consteval invocable_policy fixed_function_policy(size_t sz) noexcept {
  constexpr auto pointer_pair_size = 2 * sizeof(void*);
  if (sz <= pointer_pair_size) fixed_function_SZ_must_exceed_two_pointers();
  return invocable_policy{.inline_size = sz - pointer_pair_size,
      .alloc = invocable_alloc::inline_only};
}

} // namespace fn_details

// `fixed_function<SZ, RP(Args...)>` is a move-only, zero-allocation
// type-erased callable: like `std::move_only_function`, but with a fixed
// inline storage size `SZ` and no dynamic allocation. It is the `inline_only`
// `flexi_function`, which is where the behavior is documented.
//
// This is also similar in principle to the proposed
// `stdext::inplace_function`, but a bit more specific.
//
// `SZ` is the total instance size in bytes. The stored callable must fit
// within `SZ - 2*sizeof(void*)` bytes and have alignment <=
// `alignof(std::max_align_t)`. If it doesn't fit, a `static_assert` fires.
//
// `SZ` must be a multiple of the storage alignment,
// `alignof(std::max_align_t)`, because a smaller value would occupy the padded
// size anyway and waste the difference. Instead of hardcoding a number that
// might only be valid on a particular platform, you should pass the size
// through `padded_size` to get a conforming value.
//
// `fixed_function` instances that differ only in `SZ` can be freely assigned,
// so long as the source fits in the target. A downsizing assignment that would
// not fit throws `std::length_error` and leaves both sides intact. A same-size
// or upsizing assignment always succeeds, transplanting the stored callable
// rather than nesting the wrapper.
template<size_t SZ, class Sig>
using fixed_function =
    flexi_function<fn_details::fixed_function_policy(SZ), Sig>;

// Determine whether `T` is a `fixed_function`, that is, a `flexi_function`
// whose policy is `inline_only`.
template<typename T>
constexpr inline bool is_fixed_function_v = false;

template<invocable_policy Policy, class Sig, class FunctionT>
requires(Policy.alloc == invocable_alloc::inline_only)
constexpr inline bool
    is_fixed_function_v<flexi_function<Policy, Sig, FunctionT>> = true;

#pragma endregion
#pragma region fixed_function_of

// `fixed_function_of<SZ>` pins the storage size and leaves the signature open,
// letting a single size constant be shared across a family of aliases.
//
// Example:
//   using my_fns     = fixed_function_of<64>;
//   using callback_t = my_fns::type<void(int)>;
//   using pred_t     = my_fns::type<bool(int)>;
template<size_t SZ>
struct fixed_function_of {
  template<class Sig>
  using type = fixed_function<SZ, Sig>;
};

#pragma endregion
}} // namespace corvid::meta
