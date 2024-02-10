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
#include "enum_registry.h"
#include "../strings/conversion.h"

namespace corvid { inline namespace enums { namespace registry {

// Default specification for a scoped enum. By default, opts out of both
// sequential and bitmask while providing a simple append method that outputs
// the underlying value as a number.
PRAGMA_CLANG_DIAG(push);
PRAGMA_CLANG_IGNORED("-Wenum-constexpr-conversion");
template<ScopedEnum E, E minseq = min_scoped_enum_v<E>,
    E maxseq = max_scoped_enum_v<E>, bool validseq = false,
    wrapclip wrapseq = wrapclip{}, uint64_t validbits = 0,
    wrapclip bitclip = wrapclip{}>
struct scoped_enum_spec
    : public base_enum_spec<E, std::min(minseq, maxseq),
          std::max(minseq, maxseq), validseq, wrapseq, validbits, bitclip> {
  auto& append(AppendTarget auto& target, E v) const {
    return strings::append_num(target, as_underlying(v));
  }

  bool lookup(E& v, std::string_view sv) const {
    if (sv.empty()) return false;
    if (details::lookup_helper(v, sv)) return true;
    return false;
  }
};
PRAGMA_CLANG_DIAG(pop);

// Registers generic support for all scoped enums. This allows outputting
// the value as its underlying integer but fails to qualify as either a
// bitmask or sequential enum. A further specialization is needed to mark a
// type as a bitmask or sequence enum.
template<ScopedEnum E>
constexpr inline auto enum_spec_v<E> = scoped_enum_spec<E>();

}}} // namespace corvid::enums::registry
