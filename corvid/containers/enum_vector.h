// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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

#include <vector>

#include "../enums/sequence_enum.h"

namespace corvid { inline namespace container { inline namespace enum_vectors {

// Wrapper for vector where the index is a class enum.
//
// Provides full access to underlying type, but avoids casting.
template<typename T, sequence::SequentialEnum E,
    class Allocator = std::allocator<T>>
class enum_vector {
public:
  using value_type = T;
  using allocator_type = Allocator;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = typename std::allocator_traits<Allocator>::pointer;
  using const_pointer =
      typename std::allocator_traits<Allocator>::const_pointer;
  using iterator = typename std::vector<T, Allocator>::iterator;
  using const_iterator = typename std::vector<T, Allocator>::const_iterator;
  using enum_t = E;

  enum_vector() = default;
  explicit enum_vector(size_type count, const T& value = T(),
      const Allocator& alloc = Allocator())
      : data_(count, value, alloc) {}
  explicit enum_vector(const Allocator& alloc) : data_(alloc) {}
  enum_vector(std::initializer_list<T> init,
      const Allocator& alloc = Allocator())
      : data_(init, alloc) {}

  [[nodiscard]] size_type size() const noexcept { return data_.size(); }
  [[nodiscard]] bool empty() const noexcept { return data_.empty(); }
  void reserve(size_type new_cap) { data_.reserve(new_cap); }
  [[nodiscard]] size_type capacity() const noexcept {
    return data_.capacity();
  }
  void resize(size_type count) { data_.resize(count); }
  void resize(size_type count, const T& value) { data_.resize(count, value); }
  void clear() noexcept { data_.clear(); }

  [[nodiscard]] reference operator[](enum_t ndx) noexcept {
    return data_[*ndx];
  }
  [[nodiscard]] const_reference operator[](enum_t ndx) const noexcept {
    return data_[*ndx];
  }
  [[nodiscard]] reference at(enum_t ndx) { return data_.at(*ndx); }
  [[nodiscard]] const_reference at(enum_t ndx) const { return data_.at(*ndx); }

  [[nodiscard]] reference front() { return data_.front(); }
  [[nodiscard]] const_reference front() const { return data_.front(); }
  [[nodiscard]] reference back() { return data_.back(); }
  [[nodiscard]] const_reference back() const { return data_.back(); }

  [[nodiscard]] T* data() noexcept { return data_.data(); }
  [[nodiscard]] const T* data() const noexcept { return data_.data(); }

  iterator begin() noexcept { return data_.begin(); }
  const_iterator begin() const noexcept { return data_.begin(); }
  const_iterator cbegin() const noexcept { return data_.cbegin(); }
  iterator end() noexcept { return data_.end(); }
  const_iterator end() const noexcept { return data_.end(); }
  const_iterator cend() const noexcept { return data_.cend(); }

  void push_back(const T& value) { data_.push_back(value); }
  void push_back(T&& value) { data_.push_back(std::move(value)); }
  template<class... Args>
  reference emplace_back(Args&&... args) {
    return data_.emplace_back(std::forward<Args>(args)...);
  }
  void pop_back() { data_.pop_back(); }

  // Additional methods.

  enum_t size_as_enum() const noexcept { return enum_t{data_.size()}; }

  // Access underlying type.
  auto& underlying() noexcept { return data_; }
  const auto& underlying() const noexcept { return data_; }

  auto& operator*() { return data_; }
  const auto& operator*() const { return data_; }

private:
  std::vector<T, Allocator> data_{};
};

}}} // namespace corvid::container::enum_vectors
