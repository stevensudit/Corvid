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
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>

#include "concepts.h"

namespace corvid { inline namespace meta { inline namespace bit_casting {

// `std::bit_cast` between an object and a run of bytes.
//
// `std::bit_cast` reinterprets one object as another of the same size, but
// wire and file formats hold values as bytes inside a larger buffer, at any
// offset and alignment. The standard's answer is `std::memcpy`, which also
// serves for copying arbitrary blocks of memory, so a call site does not say
// which of the two it means. These functions name the reinterpretation.
//
// `bit_cast_from` mirrors `std::bit_cast` for reading a value out of bytes.
// `bit_cast_to` writes a value into bytes, reads one out into an existing
// object, or reinterprets a run of elements as another, and requires that the
// destination fit. The `try_bit_cast_to` forms check the fit and return false
// instead.
//
// Byte order is the caller's concern; see "../math/endian.h".

namespace details {

// `R` must not be known at compile time to hold fewer than `Bytes` bytes.
template<typename R, size_t Bytes>
concept MayHoldBytes =
    (static_size_v<R> == std::dynamic_extent) ||
    (static_size_v<R> * sizeof(std::ranges::range_value_t<R>) >= Bytes);

// The number of whole `T` elements that both `to` and `from` can hold.
template<typename T, size_t Extent>
[[nodiscard]] constexpr size_t
castable_count(const std::span<T, Extent>& to, const auto& from) noexcept {
  return std::min(to.size_bytes(), std::span{from}.size_bytes()) / sizeof(T);
}

} // namespace details

#pragma region bit_cast_from

// Replacement for `std::bit_cast` when the source is a run of bytes.
//
// Reinterprets the leading `sizeof(T)` bytes of `from` as a `T`, which must
// be trivially copyable.
//
// The range must hold at least that many bytes, which is asserted at runtime
// and, when the size is static, checked at compile time. For a runtime check,
// use `try_bit_cast_from` or `try_bit_cast_to`.
template<typename T>
requires std::is_trivially_copyable_v<T>
[[nodiscard]] T bit_cast_from(ByteRange auto&& from) noexcept {
  static_assert(details::MayHoldBytes<decltype(from), sizeof(T)>);
  assert(std::ranges::size(from) >= sizeof(T));
  T value{};
  std::memcpy(&value, std::ranges::data(from), sizeof(T));
  return value;
}

#pragma endregion
#pragma region try_bit_cast_from

// Reinterpret the leading `sizeof(T)` bytes of `from` as a `T`, which must be
// trivially copyable.
//
// Returns `std::nullopt` if the range is too short.
template<typename T>
requires std::is_trivially_copyable_v<T>
[[nodiscard]] std::optional<T>
try_bit_cast_from(ByteRange auto&& from) noexcept {
  if (std::ranges::size(from) < sizeof(T)) return std::nullopt;
  return bit_cast_from<T>(from);
}

#pragma endregion
#pragma region bit_cast_to

// Write the object representation of `from`, which must be trivially
// copyable, over the leading `sizeof(T)` bytes of `to`. The range must hold
// at least that many bytes.
//
// Always returns true, so prefer `try_bit_cast_to` unless you have already
// ensured fit.
template<typename T>
requires std::is_trivially_copyable_v<T> && (!ContiguousSizedRange<T>)
bool bit_cast_to(MutableByteRange auto&& to, const T& from) noexcept {
  static_assert(details::MayHoldBytes<decltype(to), sizeof(T)>);
  assert(std::ranges::size(to) >= sizeof(T));
  std::memcpy(std::ranges::data(to), &from, sizeof(T));
  return true;
}

// Reinterpret the leading `sizeof(T)` bytes of `from` as a `T`, which must be
// trivially copyable, storing it in `to`. The range must hold at least that
// many bytes.
//
// Always returns true, so prefer `try_bit_cast_to` unless you have already
// ensured fit.
template<typename T>
requires std::is_trivially_copyable_v<T> && (!ContiguousSizedRange<T>) &&
         (!std::is_const_v<T>)
bool bit_cast_to(T& to, ByteRange auto&& from) noexcept {
  static_assert(details::MayHoldBytes<decltype(from), sizeof(T)>);
  assert(std::ranges::size(from) >= sizeof(T));
  std::memcpy(&to, std::ranges::data(from), sizeof(T));
  return true;
}

// There is intentionally no version of `bit_cast_to` that takes two spans;
// use the `try_bit_cast_to` overload for this. The reason is that a non-try
// version still has to do all the same work, but loses the ability to cleanly
// signal failure.

#pragma endregion
#pragma region try_bit_cast_to

// Write the object representation of `from`, which must be trivially
// copyable, over the leading `sizeof(T)` bytes of `to`.
//
// Returns false, leaving `to` untouched, if the range is too short.
template<typename T>
requires std::is_trivially_copyable_v<T> && (!ContiguousSizedRange<T>)
[[nodiscard]] bool
try_bit_cast_to(MutableByteRange auto&& to, const T& from) noexcept {
  if (std::ranges::size(to) < sizeof(T)) return false;
  return bit_cast_to(to, from);
}

// Reinterpret the leading `sizeof(T)` bytes of `from` as a `T`, which must be
// trivially copyable, storing it in `to`.
//
// Returns false, leaving `to` untouched, if the range is too short.
template<typename T>
requires std::is_trivially_copyable_v<T> && (!ContiguousSizedRange<T>) &&
         (!std::is_const_v<T>)
[[nodiscard]] bool try_bit_cast_to(T& to, ByteRange auto&& from) noexcept {
  if (std::ranges::size(from) < sizeof(T)) return false;
  return bit_cast_to(to, from);
}

// Reinterpret the leading bytes of `from` as elements of `to`, copying as
// many whole elements as can fit in both, and shrinking a dynamic-extent `to`
// to the elements written. Both element types must be trivially copyable.
//
// Returns count of elements written to `to`, resizing `to` to that size as
// well. For a static extent, returns 0 if the span would not be filled
// exactly.
template<typename T, size_t Extent>
requires std::is_trivially_copyable_v<T> && (!std::is_const_v<T>)
[[nodiscard]] size_t try_bit_cast_to(std::span<T, Extent>& to,
    TriviallyCopyableRange auto&& from) noexcept {
  const auto count = details::castable_count(to, from);
  if constexpr (Extent == std::dynamic_extent) {
    to = to.first(count);
    if (count == 0) return 0;
  } else {
    if (count != Extent) return 0;
  }
  std::memcpy(to.data(), std::ranges::data(from), count * sizeof(T));
  return count;
}

#pragma endregion

}}} // namespace corvid::meta::bit_casting
