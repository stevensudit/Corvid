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
#include <cassert>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "../../meta/concepts.h"
#include "../../enums/sequence_enum.h"

namespace corvid { inline namespace container {
inline namespace string_arenas {

#pragma region string_arena

// An append-only list of strings packed into one buffer, indexed by `I`.
//
// Each appended string takes the next index, counting from zero, and is read
// back as a view into the buffer. Nothing is removed or reordered. A view is
// invalidated by the next `append`, like a reference to a `std::vector`
// element.
//
// `I` is the index type, which is a plain integer or an enum such as a
// registered sequence enum, so that a vocabulary can be indexed by its own ID
// type without casts at the call site.
template<CharType CharT, IntegerOrEnum I = size_t>
class basic_string_arena {
public:
#pragma region Types

  using char_t = CharT;
  using id_t = I;
  using view_t = std::basic_string_view<CharT>;
  using string_t = std::basic_string<CharT>;

#pragma endregion
#pragma region string_arena_iterator

  class string_arena_iterator {
  public:
    using value_type = view_t;
    using difference_type = ptrdiff_t;
    // `operator*` returns a prvalue, so this models the C++20 bidirectional
    // concept via `iterator_concept`, while the Cpp17 category honestly caps
    // at input.
    using iterator_concept = std::bidirectional_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using reference = view_t;
    using pointer = void;

    constexpr string_arena_iterator() noexcept = default;
    constexpr string_arena_iterator(const basic_string_arena& arena,
        size_t ndx) noexcept
        : arena_{&arena}, ndx_{ndx} {}

    constexpr view_t operator*() const noexcept {
      return arena_->view_at(ndx_);
    }

    constexpr bool operator==(
        const string_arena_iterator& r) const noexcept = default;

    constexpr auto& operator++() noexcept {
      ++ndx_;
      return *this;
    }

    constexpr auto operator++(int) noexcept {
      auto o = *this;
      operator++();
      return o;
    }

    constexpr auto& operator--() noexcept {
      --ndx_;
      return *this;
    }

    constexpr auto operator--(int) noexcept {
      auto o = *this;
      operator--();
      return o;
    }

  private:
    const basic_string_arena* arena_{};
    size_t ndx_{};
  };

  using iterator = string_arena_iterator;
  using const_iterator = string_arena_iterator;

#pragma endregion
#pragma region Capacity

  // The number of strings, which is one past the largest index.
  [[nodiscard]] size_t size() const noexcept { return starts_.size() - 1; }

  [[nodiscard]] bool empty() const noexcept { return !size(); }

  // Reserve room for `count` strings whose lengths add up to `total_length`.
  void reserve(size_t count, size_t total_length) {
    starts_.reserve(count + 1);
    text_arena_.reserve(total_length);
  }

#pragma endregion
#pragma region Element access

  // The string at `id`.
  [[nodiscard]] view_t operator[](id_t id) const noexcept {
    return view_at(static_cast<size_t>(id));
  }

  // The string at `ndx`.
  [[nodiscard]] view_t view_at(size_t ndx) const noexcept {
    assert(ndx < size());
    return view_t{text_arena_}.substr(starts_[ndx],
        starts_[ndx + 1] - starts_[ndx]);
  }

#pragma endregion
#pragma region Iterators

  [[nodiscard]] iterator begin() const noexcept { return {*this, 0}; }
  [[nodiscard]] iterator end() const noexcept { return {*this, size()}; }
  [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] const_iterator cend() const noexcept { return end(); }

#pragma endregion
#pragma region Modifiers

  // Append `text` as the next string, returning its ID.
  id_t append(view_t text) {
    const auto ndx = size();
    text_arena_ += text;
    starts_.push_back(text_arena_.size());
    return static_cast<id_t>(ndx);
  }

#pragma endregion
#pragma region Data members
private:
  // Every string, concatenated in ID order.
  string_t text_arena_;

  // Where each string starts, plus one past the last, so the sentinel is
  // present even while empty.
  std::vector<size_t> starts_{0};

#pragma endregion
};

// Specialized aliases.
template<IntegerOrEnum I = size_t>
using string_arena = basic_string_arena<char, I>;
template<IntegerOrEnum I = size_t>
using wstring_arena = basic_string_arena<wchar_t, I>;
template<IntegerOrEnum I = size_t>
using u8string_arena = basic_string_arena<char8_t, I>;
template<IntegerOrEnum I = size_t>
using u16string_arena = basic_string_arena<char16_t, I>;
template<IntegerOrEnum I = size_t>
using u32string_arena = basic_string_arena<char32_t, I>;

#pragma endregion

}}} // namespace corvid::container::string_arenas
