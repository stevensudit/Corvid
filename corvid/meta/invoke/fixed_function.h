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
#include <cstddef>

#include "flexi_function.h"
#include "invocable_policy.h"

namespace corvid { inline namespace meta {
namespace flexi {

#pragma region fixed_function

// `fixed_function<RP(Args...), Size>` is a move-only, zero-allocation
// type-erased callable: like `std::move_only_function`, but with a fixed
// instance size `Size` and no dynamic allocation. It is the `inline_only`
// `flexi_function`, which is where the behavior is documented.
//
// This is also similar in principle to the proposed
// `stdext::inplace_function`, but a bit more specific.
//
// `Size` is the total instance size in bytes, rounded up to the storage
// alignment, `alignof(std::max_align_t)`, since a smaller instance would
// occupy the padded size anyway; `sizeof` reports the result. The stored
// callable must fit within `Size - 2*sizeof(void*)` bytes and have alignment
// <= `alignof(std::max_align_t)`. If it doesn't fit, a `static_assert` fires.
// Without `Size`, the instance is the default policy's size, which matches
// `std::function`'s small buffer.
//
// `fixed_function` instances that differ only in `Size` can be freely
// assigned, so long as the source fits in the target. A downsizing assignment
// that would not fit throws `std::length_error` and leaves both sides intact.
// A same-size or upsizing assignment always succeeds, transplanting the stored
// callable rather than nesting the wrapper.
template<class Sig, size_t Size = invocable_policy::fixed.size()>
using fixed_function =
    flexi_function<Sig, invocable_policy::fixed.with_size(Size)>;

// Determine whether `T` is a `fixed_function`, that is, a `flexi_function`
// whose policy is `inline_only`.
template<typename T>
constexpr inline bool is_fixed_function_v = false;

template<class Sig, invocable_policy Policy, class FunctionT>
requires(Policy.storage == storage_policy::inline_only)
constexpr inline bool
    is_fixed_function_v<flexi_function<Sig, Policy, FunctionT>> = true;

#pragma endregion
#pragma region fixed_function_of

// `fixed_function_of<Size>` pins the storage size and leaves the signature
// open, letting a single size constant be shared across a family of aliases.
//
// Example:
//   using my_fns     = fixed_function_of<64>;
//   using callback_t = my_fns::type<void(int)>;
//   using pred_t     = my_fns::type<bool(int)>;
template<size_t Size>
struct fixed_function_of {
  template<class Sig>
  using type = fixed_function<Sig, Size>;
};

#pragma endregion
} // namespace flexi

#pragma region Exports
using flexi::fixed_function;
using flexi::fixed_function_of;
using flexi::is_fixed_function_v;
#pragma endregion
}} // namespace corvid::meta
