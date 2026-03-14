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
#include "strings_shared.h"

namespace corvid::strings {
inline namespace non_zero_resize {

// Support for resizing strings without zero-initialization.
//
// C++23 provides `std::string::resize_and_overwrite`, but it's awkward to use
// directly and doesn't have the semantics needed for buffer use.
//
// Note that, quite intentionally, the contents of the string buffer are
// indeterminate after any of these calls.
struct no_zero {
  // Clear out string, actually releasing the buffer. Capacity drops to
  // whatever the small string optimization allows.
  static constexpr auto& clear_out(std::string& s) {
    s.clear();
    s.shrink_to_fit();
    return s;
  }

  // Direct wrapper of `resize_and_overwrite`, hiding the lambda away. Note
  // that, on enlargement, this still copies the old contents to the new
  // buffer, which is wasteful. Instead, use `enlarge_to` to avoid this.
  static constexpr auto& resize_to(std::string& s, std::size_t new_size) {
    s.resize_and_overwrite(new_size, [new_size](char*, std::size_t) noexcept {
      return new_size;
    });
    return s;
  }

  // Enlarge string to make room for at least `minimum_size` characters.
  // Does not copy old string contents.
  static constexpr auto& enlarge_to(std::string& s, size_t minimum_size) {
    // If we can satisfy the requirement using the current buffer, expand size
    // to match its capacity.
    if (minimum_size <= s.capacity()) return resize_to(s, s.capacity());

    // Since we're going to need to enlarge, we don't want to preserve the old
    // contents.
    resize_to(clear_out(s), minimum_size);

    // If there's slack, use all of the available capacity.
    if (s.capacity() > minimum_size) resize_to(s, s.capacity());

    return s;
  }

  // Right-size string to be between `minimum_size` and `maximum_size`,
  // inclusive. This allows the string to grow above `minimum_size`, but puts a
  // limit on how bloated it can get.
  static constexpr auto&
  rightsize_to(std::string&& s, size_t minimum_size, size_t maximum_size) {
    // If current capacity exceeds `maximum_size`, shrink to fit and then
    // resize to `minimum_size`.
    if (s.capacity() > maximum_size)
      return resize_to(clear_out(s), minimum_size);

    // Otherwise, use `enlarge_to`.
    return enlarge_to(s, minimum_size);
  }
} // namespace corvid::strings::non_zero_resize
