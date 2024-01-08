// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
//
// Licensed under the Apache License, Version 2.0(the "License");
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
#include <array>
#include <span>
#include <vector>

namespace corvid { inline namespace adapters {

// Circular buffer adapter over any container that supports std::span. Allows
// pushing to back and front, popping from back and front, and random access.
template<typename T>
class circular_buffer {
public:
  // Construct from any container that converts to std::span.
  template<typename U>
  explicit circular_buffer(U&& u) : range_(std::forward<U>(u)) {}

  // Push the value to the front of the buffer, overwriting the back-most if
  // full.
  auto& push_front(T&& value) {
    if (full()) pop_back();
    return range_.begin()[--wrap_front()] = std::move(value);
  }
  template<class... Args>
  auto& emplace_front(Args&&... args) {
    if (full()) pop_back();
    return range_.begin()[--wrap_front()] = T(std::forward<Args>(args)...);
  }

  // Try to push the value to the front of the buffer, returning nullptr if
  // full.
  auto* try_push_front(T&& value) {
    if (full()) return nullptr;
    return &range_.begin()[--wrap_front()] = std::move(value);
  }
  template<class... Args>
  auto& try_emplace_front(Args&&... args) {
    if (full()) return nullptr;
    return &range_.begin()[--wrap_front()] = T(std::forward<Args>(args)...);
  }

  // Push the value to the back of the buffer, overwriting the front-most if
  // full.
  auto& push_back(T&& value) {
    if (full()) pop_front();
    return range_.begin()[wrap_back()++] = std::move(value);
  }
  template<class... Args>
  auto& emplace_back(Args&&... args) {
    if (full()) pop_front();
    return range_.begin()[wrap_back()++] = T(std::forward<Args>(args)...);
  }

  // Try to push the value to the back of the buffer, returning nullptr if
  // full.
  auto* try_push_back(const T& value) {
    if (full()) return nullptr;
    return &range_.begin()[wrap_back()++] = value;
  }
  template<class... Args>
  auto* try_emplace_back(Args&&... args) {
    if (full()) return nullptr;
    return &range_.begin()[wrap_back()++] = T(std::forward<Args>(args)...);
  }

  // Remove front or back element.
  auto&& pop_front() {
    assert(!empty());
    auto&& result = range_.begin()[front_];
    if (++front_ == range_.size()) front_ = 0;
    return result;
  }
  auto&& pop_back() {
    assert(!empty());
    auto&& result = range_.begin()[last_index()];
    if (back_ == 0) back_ = range_.size();
    --back_;
    return result;
  }

  // Capacity is always one less than the original range in order to
  // distinguish between empty and full.
  size_t capacity() const { return range_.size() - 1; }

  // Size is how many elements are inserted, up to capacity.
  size_t size() const {
    if (back_ >= front_) return back_ - front_;
    return back_ + range_.size() - front_;
  }

  bool empty() const { return front_ == back_; }
  // TODO: Make this more efficient by comparing front with adjusted back.
  bool full() const { return size() == capacity(); }

  auto& front() const { return range_.data()[front_]; }
  auto& front() { return range_.data()[front_]; }

  auto& back() const { return range_.data()[last_index()]; }
  auto& back() { return range_.data()[last_index()]; }

  auto& operator[](size_t index) const { return range_[index_at(index)]; }
  auto& operator[](size_t index) { return range_[index_at(index)]; }

  auto& at(size_t index) const { return range_[index_at_checked(index)]; }
  auto& at(size_t index) { return range_[index_at_checked(index)]; }

  // TODO: Begin/end iterators. This will be tricky because they need a pointer
  // and an index.

private:
  std::span<T> range_;
  size_t front_{};
  size_t back_{};

  size_t last_index() const {
    if (back_ == 0) return range_.size() - 1;
    return back_ - 1;
  }

  size_t index_at(size_t offset) const {
    // if (offset >= range_.size()) offset %= range_.size();
    return (front_ + offset) % range_.size();
  }

  size_t index_at_checked(size_t offset) const {
    if (offset >= size()) throw std::out_of_range("index out of range");
    return index_at(offset);
  }

  auto& wrap_front() {
    if (front_ == 0) front_ = back_;
    return front_;
  }

  auto& wrap_back() {
    if (back_ == range_.size()) back_ = 0;
    return back_;
  }
};

template<typename T>
circular_buffer(std::span<T>&) -> circular_buffer<T>;

template<typename T>
circular_buffer(std::vector<T>&) -> circular_buffer<T>;

template<typename T, std::size_t N>
circular_buffer(std::array<T, N>&) -> circular_buffer<T>;

}} // namespace corvid::adapters
