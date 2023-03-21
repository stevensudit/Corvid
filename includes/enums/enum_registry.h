// Corvid20: A general-purpose C++ 20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
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
#include "enums_shared.h"
#include "../strings/targeting.h"

// Note: This does not need to be directly included by the user, because it's
// in both "bitmask_enum.h" and "sequence_enum.h". It does get included by
// "strings/conversion.h" for access to  `enum_spec_v`.

namespace corvid::enums {
namespace registry {

// TODO: Replace wrap (and maybe validity) bools with custom two-value enums.

// Base template for enum specifications. By default, this doees not enable
// anything.
template<ScopedEnum E, E minseq = E{}, E maxseq = E{}, bool validseq = false,
    bool wrapseq = false, as_underlying_t<E> bitcount = 0,
    bool bitclip = false>
struct base_spec {
  using U = as_underlying_t<E>;
  static constexpr E seq_min_v = minseq;
  static constexpr E seq_max_v = maxseq;
  static constexpr bool seq_wrap_v = wrapseq;
  static constexpr bool seq_valid_v = validseq;
  static constexpr U bit_count_v = bitcount;
  static constexpr bool bit_clip_v = bitclip;
};

// Base template for enum specifications.
//
// This is only intended to match non-enums in order to satisfy the compiler.
// Such a match does not enable the type as anything; further specialization is
// needed for that.
//
// Note that `std::byte` is used as a placeholder for the type `T` because it
// counts as a ScopedEnum, and is not otherwise significant.
template<typename T, typename... Ts>
constexpr auto enum_spec_v = base_spec<std::byte>();

} // namespace registry
} // namespace corvid::enums
