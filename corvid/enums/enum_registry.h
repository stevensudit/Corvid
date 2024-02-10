// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
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

namespace corvid { inline namespace enums {

// Whether or not to limit the value, as by wrapping or clipping, when it
// exceeds the range of the enum.
enum class wrapclip { none, limit };

namespace registry {

// Base template for enum specifications. By default, this does not enable
// anything.
template<ScopedEnum E, E minseq = min_scoped_enum_v<E>,
    E maxseq = max_scoped_enum_v<E>, bool validseq = false,
    wrapclip wrapseq = wrapclip{}, uint64_t validbits = 0,
    wrapclip bitclip = wrapclip{}>
struct base_enum_spec {
  using U = as_underlying_t<E>;
  static constexpr E seq_min_v = minseq;
  static constexpr E seq_max_v = maxseq;
  static constexpr wrapclip seq_wrap_v = wrapseq;
  static constexpr bool seq_valid_v = validseq;
  static constexpr uint64_t valid_bits_v = validbits;
  static constexpr wrapclip bit_clip_v = bitclip;
};

// Base template for enum specifications.
//
// This is only intended to match non-enums in order to satisfy the compiler.
// Such a match does not enable the type as anything; further specialization is
// needed for that.
//
// Note that `std::byte` is used as a placeholder for the type `T` to satsify
// the compiler (because it counts as a ScopedEnum) and is not otherwise
// significant.
template<typename T, typename... Ts>
constexpr inline auto enum_spec_v = base_enum_spec<std::byte>();

namespace details {

// Enum lookup helper to handle the case of numeric values.
template<ScopedEnum E>
bool lookup_helper(E& v, std::string_view sv) {
  // Caller is responsible for ensuring `sv` is not empty.
  assert(!sv.empty());
  char first = sv.front();
  if ((first < '0' || first > '9') && first != '-') return false;
  std::underlying_type_t<E> t;
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), t);
  if (ec != std::errc{} || ptr != sv.end()) return false;
  v = static_cast<E>(t);
  return true;
}

} // namespace details

} // namespace registry
}} // namespace corvid::enums
