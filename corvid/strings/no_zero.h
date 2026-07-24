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
#include <concepts>
#include <cstddef>
#include <string>

#include "../meta/concepts.h"

namespace corvid::strings { inline namespace nozerostruct {

#pragma region no_zero

// Resizer of strings without zero-initialization.
//
// C++23 provides `std::string::resize_and_overwrite`, but it's awkward to use
// directly and doesn't have the semantics needed for buffer use. This wraps a
// reference to the string, of any code unit, and exposes the buffer-friendly
// operations as chainable methods. Like `appender`, dereference to get the
// wrapped string back, as in:
//
//   recv(*no_zero{buf}.enlarge_to(max_bytes));
//
// Note that, quite intentionally, the contents of the string buffer are
// indeterminate after any of these calls.
template<CharType Char>
struct no_zero {
  using string_t = std::basic_string<Char>;

  constexpr explicit no_zero(string_t& s) noexcept : s_{s} {}

  // Clear out the string, actually releasing the buffer. Capacity drops to
  // whatever the small string optimization allows.
  constexpr auto& clear_out() {
    s_.clear();
    s_.shrink_to_fit();
    return *this;
  }

  // Direct wrapper of `resize_and_overwrite`, hiding the lambda away. This is
  // probably not the method you want to call.
  //
  // On enlargement, this still copies the old contents to the new buffer,
  // which is wasteful. Instead, use `enlarge_to` to avoid this. It also
  // doesn't get rid of unwanted capacity, as `rightsize_to` does. Nor is it
  // ideal for trimming after filling, as `trim_to` is.
  constexpr auto& resize_to(std::size_t new_size) {
    s_.resize_and_overwrite(new_size, [new_size](Char*, std::size_t) noexcept {
      return new_size;
    });
    return *this;
  }

  // Trim size down to `new_size`, but cannot enlarge and does not affect
  // capacity. This is the method to call after filling a buffer partially.
  // Note that it handles the case of negative signed values by clamping them
  // to zero.
  constexpr auto& trim_to(std::integral auto new_size) {
    if constexpr (std::signed_integral<decltype(new_size)>) {
      if (new_size < 0) new_size = 0;
    }
    const auto cast_size = static_cast<std::size_t>(new_size);
    if (cast_size < s_.size()) resize_to(cast_size);
    return *this;
  }

  // Resize up to the current capacity.
  constexpr auto& enlarge_to_cap() { return resize_to(s_.capacity()); }

  // Enlarge string to make room for at least `minimum_size` characters.
  // Does not copy old string contents. Does not reduce capacity.
  constexpr auto& enlarge_to(size_t minimum_size) {
    // If we can satisfy the requirement using the current buffer, expand size
    // to match its capacity.
    if (minimum_size <= s_.capacity()) return enlarge_to_cap();

    // Since we're going to need to enlarge, we don't want to preserve the old
    // contents.
    clear_out().resize_to(minimum_size);

    // If there's slack, use all of the available capacity.
    if (s_.capacity() > minimum_size) enlarge_to_cap();

    return *this;
  }

  // Right-size string to be between `minimum_size` and `maximum_size`,
  // inclusive. This allows the string to grow above `minimum_size`, but puts
  // a limit on how bloated it can get.
  //
  // Note: `maximum_size` must be substantially larger than `minimum_size`,
  // otherwise the capacity for `minimum_size` could exceed `maximum_size`,
  // leading to churn. Typically, `maximum_size` should be at least 2x
  // `minimum_size`.
  constexpr auto& rightsize_to(size_t minimum_size, size_t maximum_size) {
    // If current capacity exceeds `maximum_size`, shrink to fit and then
    // resize to `minimum_size`.
    if (s_.capacity() > maximum_size)
      return clear_out().resize_to(minimum_size);

    // Otherwise, use `enlarge_to`.
    return enlarge_to(minimum_size);
  }

  // The wrapped string.
  [[nodiscard]] constexpr string_t& operator*() const noexcept { return s_; }
  [[nodiscard]] constexpr string_t* operator->() const noexcept { return &s_; }

private:
  string_t& s_;
};

#pragma endregion

}} // namespace corvid::strings::nozerostruct
