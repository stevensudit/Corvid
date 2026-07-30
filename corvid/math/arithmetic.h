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
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <type_traits>

#include "../meta/concepts.h"
#include "../meta/crossplatform.h"

namespace corvid { inline namespace math { inline namespace arithmetic {

#pragma region Constants

// Mathematically meaningful constants that the standard's <numbers> does not
// provide, in the same variable-template form as std::numbers. General math
// only: a value tied to a particular domain (a hexagon's apothem-to-vertex
// ratio, say) does not belong here even when it derives from one of these.

// A full turn in radians, 2 pi.
template<std::floating_point T = float>
inline constexpr T two_pi_v = std::numbers::pi_v<T> * T{2};

// The cosine of 30 degrees, sqrt(3) / 2.
template<std::floating_point T = float>
inline constexpr T cos_30_v = std::numbers::sqrt3_v<T> / T{2};

#pragma endregion
#pragma region ceil_div

// Ceiling of the integer division `n / d`: the count of `d`-sized buckets
// needed to cover `n` items. Requires `d > 0`, and `n >= 0` for signed types.
// Computes without the overflow that the `(n + d - 1) / d` idiom suffers when
// `n` is near the type's maximum.
template<std::integral T, std::integral U>
[[nodiscard]] constexpr std::common_type_t<T, U> ceil_div(T n, U d) noexcept {
  using R = std::common_type_t<T, U>;
  assert(d > 0 && "ceil_div divisor must be positive");
  if constexpr (std::is_signed_v<T>)
    assert(n >= 0 && "ceil_div dividend must be non-negative");
  const auto rn = static_cast<R>(n);
  const auto rd = static_cast<R>(d);
  return (rn / rd) + static_cast<R>(rn % rd != 0);
}

#pragma endregion
#pragma region round_up_to_multiple

// Smallest multiple of `m` not less than `n`: rounds `n` up to a multiple of
// `m`. Requires `m > 0`, and `n >= 0` for signed types. Built on `ceil_div`,
// so the division does not overflow; the final multiply can still overflow if
// the rounded result (which is less than `n + m`) exceeds the common type's
// maximum.
template<std::integral T, std::integral U>
[[nodiscard]] constexpr std::common_type_t<T, U>
round_up_to_multiple(T n, U m) noexcept {
  using R = std::common_type_t<T, U>;
  return ceil_div(n, m) * static_cast<R>(m);
}

#pragma endregion
#pragma region Portable 128-bit integers

// TODO: Provide portable `uint128_t` and `int128_t` types.
//
// The 128-bit support here is conditional, so callers have to branch on
// `CORVID_HAS_INT128` and MSVC gets nothing at all. The fix is a struct that
// always exists, holding either the compiler's native 128-bit integer or a
// pair of 64-bit halves, so that the type and its behavior are identical
// everywhere and only the implementation varies.
//
// The native member could be `__uint128_t` and `__int128_t`, or the C23
// `_BitInt(128)` where a C++ compiler exposes it. Neither choice removes the
// need for the paired-halves fallback, since MSVC has no spelling for either.
// The struct would carry the full operator set plus user-defined literals, so
// that a 128-bit constant can be written directly. Division is the only
// genuinely awkward part of the fallback; the rest falls out of the halves.
//
// Once it exists, `meta`'s `UnsignedWord` collapses back to
// `std::unsigned_integral` plus the new type, `combine_result<void, 16>` stops
// being conditional, `CORVID_HAS_INT128` becomes an implementation detail of
// the wrapper rather than something callers see, and `proto/endian.h`'s
// `hton128` and `ntoh128` stop resting on a compiler extension. Worth settling
// at that point what `std::numeric_limits`, `std::format`, and the `charconv`
// conversions should do with it, since none of them handle a 128-bit integer
// today.

#pragma endregion
#pragma region extract_byte

// Byte `Ndx` of `v`, counting from the least significant: `Ndx == 0` is the
// low byte, `Ndx == 1` the one above it, and so on up to the width of `T`.
//
//    extract_byte<3>(addr), extract_byte<2>(addr), extract_byte<1>(addr),
//        extract_byte<0>(addr)
//
// Unsigned only, because the byte decomposition of a negative value depends
// on its representation rather than on its value. Asking for a byte wider
// than `T` is a compile error rather than a zero: the index is fixed at the
// call site, so a byte that cannot be there is a mistake in the caller, not a
// value worth reporting.
template<size_t Ndx = 0UZ>
[[nodiscard]] constexpr uint8_t extract_byte(UnsignedWord auto v) noexcept {
  // NOLINTNEXTLINE(bugprone-sizeof-expression): the width is the real bound.
  static_assert(Ndx < sizeof(decltype(v)),
      "extract_byte index exceeds the type's width");
  return static_cast<uint8_t>(v >> (Ndx * 8));
}

#pragma endregion
#pragma region combine_bytes

namespace details {

// Result type for `combine_bytes`: the caller's `R`, or, when that is `void`,
// the unsigned type exactly as wide as the supplied byte count. The primary
// handles a spelled-out `R`; the partial specialization rejects a count no
// standard width matches, and the full ones do the deducing.
template<typename R, size_t Count>
struct combine_result {
  using type = R;
};

// The widest deducible result: 16 bytes where the compiler has a 128-bit
// type, 8 otherwise.
inline constexpr size_t max_deducible_width = CORVID_HAS_INT128 ? 16 : 8;

template<size_t Count>
struct combine_result<void, Count> {
  static_assert(std::has_single_bit(Count) && Count <= max_deducible_width,
      "combine_bytes deduces its result from the byte count, which must "
      "therefore be a power of two no wider than the widest unsigned type");
  using type = uint64_t;
};

template<>
struct combine_result<void, 1> {
  using type = uint8_t;
};
template<>
struct combine_result<void, 2> {
  using type = uint16_t;
};
template<>
struct combine_result<void, 4> {
  using type = uint32_t;
};
template<>
struct combine_result<void, 8> {
  using type = uint64_t;
};
#if CORVID_HAS_INT128
template<>
struct combine_result<void, 16> {
  using type = __uint128_t;
};
#endif

template<typename R, size_t Count>
using combine_result_t = combine_result<R, Count>::type;

} // namespace details

// Assemble an unsigned value from its bytes, least significant first: the
// first argument becomes the low byte, the second the one above it, and so on.
//
//    combine_bytes(octets[1], octets[0])          // a uint16_t
//    combine_bytes<uint32_t>(lo, mid, hi, 0)      // spelled out
//
// The byte count determines the width, so the result type is deduced from it
// and need not be named. Spelling `R` anyway is allowed, and then the count
// must match its width; deducing instead requires a count of 1, 2, 4, or 8,
// or 16 where the compiler has `__uint128_t`, since those are the widths a
// type exists for.
//
// Each argument contributes only its low byte, which makes this the inverse
// of `extract_byte`. Because the count is exact, a partial value spells out
// its zero bytes: leaving one off is then a mistake the compiler catches
// rather than a silent zero-fill.
template<typename R = void>
[[nodiscard]] constexpr auto
combine_bytes(std::unsigned_integral auto... bytes) noexcept {
  // Both checks interrogate `R` rather than the deduced result, so that a
  // failed deduction reports only its own cause.
  static_assert(std::is_void_v<R> || UnsignedWord<R>,
      "combine_bytes assembles an unsigned value");
  using result_t = details::combine_result_t<R, sizeof...(bytes)>;
  // Deduction already fits the count exactly; only a spelled-out `R` can
  // disagree with it.
  if constexpr (!std::is_void_v<R>)
    // NOLINTNEXTLINE(bugprone-sizeof-expression): the width is the real bound.
    static_assert(sizeof...(bytes) == sizeof(R),
        "combine_bytes takes exactly one argument per byte of the type");
  result_t result{};
  size_t shift{};
  ((result |= result_t{extract_byte(bytes)} << shift, shift += 8), ...);
  return result;
}

#pragma endregion

}}} // namespace corvid::math::arithmetic
