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

// Memory-layout utilities.

namespace corvid { inline namespace meta {

#pragma region padded_size

// Round an inline-storage size up to its alignment, claiming as usable
// capacity what the compiler would otherwise waste as padding.
//
// A size-parameterized buffer, such as `fixed_function`'s `SZ` or a proxy
// policy's `sbo_size`, occupies a multiple of its alignment regardless of the
// size requested, so a request between multiples would pay for the padding
// without getting to use it. Those classes therefore reject such sizes, and
// this helper turns a byte budget into a conforming one. The alignment
// defaults to that of `std::max_align_t`, matching the default alignment of
// such buffers.
[[nodiscard]] consteval size_t
padded_size(size_t sz, size_t align = alignof(std::max_align_t)) noexcept {
  return ((sz / align) + (sz % align != 0)) * align;
}

#pragma endregion

}} // namespace corvid::meta
