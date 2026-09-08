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
#include <limits>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../../enums/sequence_enum.h"
#include "interval.h"

namespace corvid { inline namespace container { inline namespace enum_spans {

using corvid::enums::sequence::ops::operator*;

#pragma region enum_span

// Wrapper for span where the index is a class enum.
//
// Provides full access to the underlying span, but avoids casting. It is the
// caller's responsibility not to view more elements than the enum can index.
//
// Subspans shift the index origin, as they do for `std::span`. That is right
// for an enum that is an index, such as a row number, and meaningless for one
// that is a key, such as a color.
template<typename T, sequence::SequentialEnum E,
    size_t Extent = std::dynamic_extent>
class enum_span {
public:
#pragma region Types

  using enum_t = E;
  static constexpr size_t extent = Extent;
  using span_type = std::span<T, extent>;

  using element_type = T;
  using value_type = std::remove_cv_t<T>;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using iterator = span_type::iterator;
  using const_iterator = span_type::const_iterator;
  using reverse_iterator = span_type::reverse_iterator;
  using const_reverse_iterator = span_type::const_reverse_iterator;

#pragma endregion
#pragma region Construction

  constexpr enum_span() noexcept = default;

  // Construct a view of `count` elements from `first`. Explicit for a static
  // extent, as for `std::span`.
  constexpr explicit(extent != std::dynamic_extent)
      enum_span(pointer first, size_type count) noexcept
      : data_(first, count) {}

  // Construct a view of whatever the underlying span views: a contiguous
  // container, an array, a span, or another `enum_span`. Implicit exactly when
  // the span's own conversion is.
  template<typename R>
  requires std::constructible_from<span_type, R&&>
  constexpr explicit(!std::is_convertible_v<R&&, span_type>) enum_span(
      R&& range) noexcept(std::is_nothrow_constructible_v<span_type, R&&>)
      : data_(std::forward<R>(range)) {}

#pragma endregion
#pragma region Capacity

  [[nodiscard]] constexpr bool empty() const noexcept { return data_.empty(); }
  [[nodiscard]] constexpr size_type size() const noexcept {
    return data_.size();
  }
  [[nodiscard]] constexpr size_type size_bytes() const noexcept {
    return data_.size_bytes();
  }
  // See also: `size_as_enum()`.

#pragma endregion
#pragma region Element access

  [[nodiscard]] constexpr reference operator[](enum_t ndx) const noexcept {
    return data_[*ndx];
  }

  // The element at `ndx`, throwing `std::out_of_range` when it is not below
  // `size()`.
  [[nodiscard]] constexpr reference at(enum_t ndx) const {
    if (static_cast<size_type>(*ndx) >= size())
      throw std::out_of_range{"enum_span::at"};
    return data_[*ndx];
  }

  [[nodiscard]] constexpr reference front() const { return data_.front(); }
  [[nodiscard]] constexpr reference back() const { return data_.back(); }
  [[nodiscard]] constexpr pointer data() const noexcept {
    return data_.data();
  }

#pragma endregion
#pragma region Subviews

  // The first `count` elements, the last `count` elements, or the `count`
  // elements from `offset` on, where `std::dynamic_extent` means the rest.
  // Each must lie within the span, as for `std::span`.
  [[nodiscard]] constexpr enum_span<T, E> first(size_type count) const {
    return enum_span<T, E>{data_.first(count)};
  }
  [[nodiscard]] constexpr enum_span<T, E> last(size_type count) const {
    return enum_span<T, E>{data_.last(count)};
  }
  [[nodiscard]] constexpr enum_span<T, E>
  subspan(enum_t offset, size_type count = std::dynamic_extent) const {
    return enum_span<T, E>{data_.subspan(*offset, count)};
  }

  // The same with static counts, whose extents follow `std::span`'s rules.
  template<size_type Count>
  [[nodiscard]] constexpr auto first() const {
    return as_enum_span(data_.template first<Count>());
  }
  template<size_type Count>
  [[nodiscard]] constexpr auto last() const {
    return as_enum_span(data_.template last<Count>());
  }
  template<enum_t Offset, size_type Count = std::dynamic_extent>
  [[nodiscard]] constexpr auto subspan() const {
    return as_enum_span(data_.template subspan<*Offset, Count>());
  }

#pragma endregion
#pragma region Iterators

  [[nodiscard]] constexpr iterator begin() const noexcept {
    return data_.begin();
  }
  [[nodiscard]] constexpr iterator end() const noexcept { return data_.end(); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
    return data_.cbegin();
  }
  [[nodiscard]] constexpr const_iterator cend() const noexcept {
    return data_.cend();
  }
  [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept {
    return data_.rbegin();
  }
  [[nodiscard]] constexpr reverse_iterator rend() const noexcept {
    return data_.rend();
  }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
    return data_.crbegin();
  }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
    return data_.crend();
  }

#pragma endregion
#pragma region Additional methods

  // Return the size as an enum value.
  //
  // Warning: this is only meaningful when the size fits the enum's underlying
  // type. A span covering the enum's full domain has a size one past the
  // largest representable value, so it wraps. As a result, for an 8-bit enum,
  // 256 elements report as `enum_t{0}`.
  [[nodiscard]] constexpr enum_t size_as_enum() const noexcept {
    // The explicit cast keeps the conversion legal for enums narrower than
    // `size_t`, which brace-init alone would reject as narrowing.
    return enum_t{static_cast<as_underlying_t<enum_t>>(data_.size())};
  }

  // Access underlying type.
  [[nodiscard]] constexpr auto& underlying(this auto&& self) noexcept {
    return self.data_;
  }

  [[nodiscard]] constexpr auto& operator*(this auto&& self) noexcept {
    return self.data_;
  }

  [[nodiscard]] constexpr auto range_interval() const noexcept {
    return interval<enum_t>::iota(data_.size());
  }

#pragma endregion
#pragma region Workers
private:
  // `span` as the `enum_span` with the same static extent.
  template<size_type N>
  [[nodiscard]] static constexpr auto
  as_enum_span(std::span<T, N> span) noexcept {
    return enum_span<T, E, N>{span};
  }

#pragma endregion
#pragma region Data members

  span_type data_;

#pragma endregion
};

#pragma endregion
}}} // namespace corvid::container::enum_spans

// NOLINTBEGIN(bugprone-std-namespace-modification)

// Like `std::span`, an `enum_span` is a borrowed range and a view.
template<typename T, corvid::sequence::SequentialEnum E, size_t Extent>
inline constexpr bool
    std::ranges::enable_borrowed_range<corvid::enum_span<T, E, Extent>> = true;
template<typename T, corvid::sequence::SequentialEnum E, size_t Extent>
inline constexpr bool
    std::ranges::enable_view<corvid::enum_span<T, E, Extent>> = true;

// NOLINTEND(bugprone-std-namespace-modification)
