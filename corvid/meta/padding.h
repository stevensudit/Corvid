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
#include <cstdint>
#include <utility>

// Padding utilities, whether the padding is memory (rounding a buffer size up
// to its alignment) or text (splitting fill around content in a fixed-width
// field).

namespace corvid { inline namespace meta {

#pragma region padded_size

// Round an inline-storage size up to its alignment, claiming as usable
// capacity what the compiler would otherwise waste as padding.
//
// A size-parameterized buffer, such as `fixed_function`'s `Size` or a proxy
// policy's `inline_size`, occupies a multiple of its alignment regardless of
// the size requested, so a request between multiples would pay for the padding
// without getting to use it. Those classes therefore reject such sizes, and
// this helper turns a byte budget into a conforming one. The alignment
// defaults to that of `std::max_align_t`, matching the default alignment of
// such buffers.
[[nodiscard]] consteval size_t
padded_size(size_t sz, size_t align = alignof(std::max_align_t)) noexcept {
  return ((sz / align) + (sz % align != 0)) * align;
}

#pragma endregion
#pragma region calc_padding

// Alignment of content within a fixed-width field.
enum class aligned : std::uint8_t { left, right, center };

// Calculate the left and right padding counts for `content_width` in a field
// of `total_width`, based on `alignment`. When centering leaves an odd count,
// the extra unit goes to the trailing side, matching `std::format`'s `^`.
[[nodiscard]] constexpr std::pair<size_t, size_t> calc_padding(
    aligned alignment, size_t content_width, size_t total_width) noexcept {
  size_t lead{};
  size_t trail{};
  if (total_width > content_width) {
    const auto pad = total_width - content_width;
    lead =
        (alignment == aligned::right) ? pad
        : (alignment == aligned::center)
            ? pad / 2
            : 0;
    trail = pad - lead;
  }
  return {lead, trail};
}

#pragma endregion

}} // namespace corvid::meta
