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

namespace corvid::enums {
namespace registry {

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

// Default specification for a scoped enum. By default, opts out of both
// sequential and bitmask while providing a simple append method that outputs
// the underlying value as a number.
template<ScopedEnum E, E minseq = E{}, E maxseq = E{}, bool validseq = false,
    bool wrapseq = false, as_underlying_t<E> bitcount = 0,
    bool bitclip = false>
struct scoped_enum_spec
    : base_spec<E, minseq, maxseq, validseq, wrapseq, bitcount, bitclip> {
  auto& append(AppendTarget auto& target, E v) const {
    return strings::append_num(target, as_underlying(v));
  }
};

// Base template for enum specifications.
//
// This is only intended to match non-enums in order to satisfy the compiler.
// Such a match does not enable the type as anything, and does not mean that
// the `append` will be used.
// Note that `std::byte` is used as a placeholder for the type `T` because it
// counts as a ScopedEnum.
template<typename T, typename... Ts>
constexpr auto enum_spec_v = base_spec<std::byte>();

// Generic support for all scoped enums. This allows outputting the value as
// its underlying integer but fails to qualify as either a bitmask or
// sequential enum. A further specialization is needed to mark a type as a
// bitmask or sequence enum.
template<ScopedEnum E>
constexpr auto enum_spec_v<E> = scoped_enum_spec<E>();

// Conversion.

// TODO: Move this into strings/conversion.h, replacing the previous version.

// Append enum to `target`.
constexpr auto& append_enumXXX(AppendTarget auto& target, ScopedEnum auto t) {
  return enum_spec_v<decltype(t)>.append(target, t);
}

// Return enum as string.
constexpr std::string enum_as_stringXXX(ScopedEnum auto t) {
  std::string target;
  return append_enumXXX(target, t);
}

} // namespace registry
} // namespace corvid::enums
