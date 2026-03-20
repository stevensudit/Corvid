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

#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdint>

namespace corvid { inline namespace concurrency {
inline namespace relaxed_atomic_ns {

// Thin wrapper around `std::atomic<T>`, providing only implicit conversion to
// `T` and assignment from `T` while using relaxed memory ordering.  All other
// `std::atomic<T>` operations are accessed using `operator->`.
//
// Intended for values where adjacent loads and stores need no ordering
// guarantees relative to other memory operations. For example: statistics
// counters, hot-path flags, or hardware registers polled in a tight loop.
//
// In these cases, the default sequentially consistent ordering of
// `std::atomic<T>` is unnecessarily expensive, and using `std::atomic<T>` with
// explicit relaxed ordering is verbose.
//
// You can still use the underlying `std::atomic<T>` directly when you need
// stronger ordering guarantees.
template<typename T>
class relaxed_atomic {
public:
  using value_type = T;
  using underlying_t = std::atomic<value_type>;

  constexpr relaxed_atomic() noexcept(
      std::is_nothrow_default_constructible_v<T>)
  requires std::is_default_constructible_v<T>
  = default;

  constexpr explicit relaxed_atomic(value_type desired) noexcept
      : value_{desired} {}

  relaxed_atomic(const relaxed_atomic&) = delete;
  relaxed_atomic& operator=(const relaxed_atomic&) = delete;

  // Conversion to `value_type` with relaxed semantics.
  operator value_type(this auto& self) noexcept {
    return self.value_.load(std::memory_order::relaxed);
  }

  // Assignment from `value_type` with relaxed semantics. Returns `value_type`,
  // matching the return type of `std::atomic<value_type>::operator=`.
  value_type operator=(this auto& self, value_type desired) noexcept {
    self.value_.store(desired, std::memory_order::relaxed);
    return desired;
  }

  // Extended functionality.

  // Direct access to the underlying `std::atomic<T>`.
  [[nodiscard]] auto& underlying(this auto& self) noexcept {
    return self.value_;
  }

  // Dereference to explicitly get the underlying atomic.
  [[nodiscard]] auto& operator*(this auto& self) noexcept {
    return self.value_;
  }

  // Access underlying atomic methods.
  [[nodiscard]] auto* operator->(this auto& self) noexcept {
    return &self.value_;
  }

private:
  underlying_t value_{};
};

// Aliases matching the `std::atomic_*` typedef family.

// Character and bool types.
using relaxed_atomic_bool = relaxed_atomic<bool>;
using relaxed_atomic_char = relaxed_atomic<char>;
using relaxed_atomic_schar = relaxed_atomic<signed char>;
using relaxed_atomic_uchar = relaxed_atomic<unsigned char>;
using relaxed_atomic_short = relaxed_atomic<short>;
using relaxed_atomic_ushort = relaxed_atomic<unsigned short>;
using relaxed_atomic_int = relaxed_atomic<int>;
using relaxed_atomic_uint = relaxed_atomic<unsigned int>;
using relaxed_atomic_long = relaxed_atomic<long>;
using relaxed_atomic_ulong = relaxed_atomic<unsigned long>;
using relaxed_atomic_llong = relaxed_atomic<long long>;
using relaxed_atomic_ullong = relaxed_atomic<unsigned long long>;
using relaxed_atomic_char8_t = relaxed_atomic<char8_t>; // C++20
using relaxed_atomic_char16_t = relaxed_atomic<char16_t>;
using relaxed_atomic_char32_t = relaxed_atomic<char32_t>;
using relaxed_atomic_wchar_t = relaxed_atomic<wchar_t>;

// Fixed-width types (optional -- provided when the underlying typedef exists).
using relaxed_atomic_int8_t = relaxed_atomic<std::int8_t>;
using relaxed_atomic_uint8_t = relaxed_atomic<std::uint8_t>;
using relaxed_atomic_int16_t = relaxed_atomic<std::int16_t>;
using relaxed_atomic_uint16_t = relaxed_atomic<std::uint16_t>;
using relaxed_atomic_int32_t = relaxed_atomic<std::int32_t>;
using relaxed_atomic_uint32_t = relaxed_atomic<std::uint32_t>;
using relaxed_atomic_int64_t = relaxed_atomic<std::int64_t>;
using relaxed_atomic_uint64_t = relaxed_atomic<std::uint64_t>;

// Least-width types.
using relaxed_atomic_int_least8_t = relaxed_atomic<std::int_least8_t>;
using relaxed_atomic_uint_least8_t = relaxed_atomic<std::uint_least8_t>;
using relaxed_atomic_int_least16_t = relaxed_atomic<std::int_least16_t>;
using relaxed_atomic_uint_least16_t = relaxed_atomic<std::uint_least16_t>;
using relaxed_atomic_int_least32_t = relaxed_atomic<std::int_least32_t>;
using relaxed_atomic_uint_least32_t = relaxed_atomic<std::uint_least32_t>;
using relaxed_atomic_int_least64_t = relaxed_atomic<std::int_least64_t>;
using relaxed_atomic_uint_least64_t = relaxed_atomic<std::uint_least64_t>;

// Fast types.
using relaxed_atomic_int_fast8_t = relaxed_atomic<std::int_fast8_t>;
using relaxed_atomic_uint_fast8_t = relaxed_atomic<std::uint_fast8_t>;
using relaxed_atomic_int_fast16_t = relaxed_atomic<std::int_fast16_t>;
using relaxed_atomic_uint_fast16_t = relaxed_atomic<std::uint_fast16_t>;
using relaxed_atomic_int_fast32_t = relaxed_atomic<std::int_fast32_t>;
using relaxed_atomic_uint_fast32_t = relaxed_atomic<std::uint_fast32_t>;
using relaxed_atomic_int_fast64_t = relaxed_atomic<std::int_fast64_t>;
using relaxed_atomic_uint_fast64_t = relaxed_atomic<std::uint_fast64_t>;

// Pointer-sized and other platform types (optional -- provided when the
// underlying typedef exists).
using relaxed_atomic_intptr_t = relaxed_atomic<std::intptr_t>;
using relaxed_atomic_uintptr_t = relaxed_atomic<std::uintptr_t>;
using relaxed_atomic_size_t = relaxed_atomic<std::size_t>;
using relaxed_atomic_ptrdiff_t = relaxed_atomic<std::ptrdiff_t>;
using relaxed_atomic_intmax_t = relaxed_atomic<std::intmax_t>;
using relaxed_atomic_uintmax_t = relaxed_atomic<std::uintmax_t>;

}}} // namespace corvid::concurrency::relaxed_atomic_ns
