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
#include <array>
#include <span>
#include <vector>

namespace corvid { inline namespace adapters {

// Circular buffer adapter over any container that supports `std::span`. Allows
// access to the full range, with pushing to back and front, popping from back
// and front, and random access. Does not own the underlying container.
//
// As an optimization, you may specialize on a SZ smaller than size_t, such as
// uint32_t, if you know that your buffer will never be larger than that.
template<typename T, typename SZ = size_t>
class circular_buffer {
public:
  using value_type = T;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using size_type = SZ;
  static_assert(std::is_unsigned_v<size_type>);

  // Default.
  circular_buffer() noexcept = default;

  // Move-only.
  circular_buffer(circular_buffer&& other) noexcept {
    steal(std::move(other));
  }
  circular_buffer& operator=(circular_buffer&& other) noexcept {
    if (this != &other) steal(std::move(other));
    return *this;
  }

  // Construct from any container that converts to std::span. Starts off empty.
  template<typename U>
  explicit circular_buffer(U&& u) noexcept
  requires std::convertible_to<U, std::span<T>>
      : range_(std::forward<U>(u)), back_(last_index()) {
    assert(range_.size() <= std::numeric_limits<size_type>::max());
  }

  // Construct with an initial size, from container.
  template<typename U>
  explicit circular_buffer(U&& u, size_type size) noexcept
      : range_(std::forward<U>(u)), back_(size ? size - 1 : last_index()),
        size_(size) {
    assert(range_.size() <= std::numeric_limits<size_type>::max());
    assert(size <= capacity());
  }

  // Clear the buffer. Does not affect underlying container.
  void clear() noexcept {
    front_ = size_ = 0u;
    back_ = last_index();
  }

  // Push the value to the front of the buffer, overwriting the backmost
  // value if full. Returns reference to the new element.
  auto& push_front(const value_type& value) noexcept(
      noexcept(*data() = value)) {
    adjust_size_for_front();
    return (add_front() = value);
  }
  auto& push_front(value_type&& value) noexcept(
      noexcept(*data() = std::move(value))) {
    adjust_size_for_front();
    return (add_front() = std::move(value));
  }
  template<class... Args>
  auto& emplace_front(Args&&... args) noexcept(
      noexcept(*data() = value_type{std::forward<Args>(args)...})) {
    adjust_size_for_front();
    return (add_front() = value_type{std::forward<Args>(args)...});
  }

  // Try to push the value to the front of the buffer. Returns pointer to the
  // new element or nullptr if full.
  auto* try_push_front(const value_type& value) noexcept(
      noexcept(*data() = value)) {
    if (full()) return pointer{};
    ++size_;
    return &(add_front() = value);
  }
  auto* try_push_front(value_type&& value) noexcept(
      noexcept(*data() = std::move(value))) {
    if (full()) return pointer{};
    ++size_;
    return &(add_front() = std::move(value));
  }
  template<class... Args>
  auto* try_emplace_front(Args&&... args) noexcept(
      noexcept(*data() = value_type{std::forward<Args>(args)...})) {
    if (full()) return pointer{};
    ++size_;
    return &(add_front() = value_type{std::forward<Args>(args)...});
  }

  // Push the value to the back of the buffer, overwriting the frontmost
  // value if full. Returns reference to the new element.
  auto& push_back(const value_type& value) noexcept(
      noexcept(*data() = value)) {
    adjust_size_for_back();
    return (add_back() = value);
  }
  auto& push_back(value_type&& value) noexcept(
      noexcept(*data() = std::move(value))) {
    adjust_size_for_back();
    return (add_back() = std::move(value));
  }
  template<class... Args>
  auto& emplace_back(Args&&... args) noexcept(
      noexcept(*data() = value_type{std::forward<Args>(args)...})) {
    adjust_size_for_back();
    return (add_back() = value_type{std::forward<Args>(args)...});
  }

  // Try to push the value to the back of the buffer. Returns pointer to the
  // new element or nullptr if full.
  auto* try_push_back(const value_type& value) noexcept(
      noexcept(*data() = value)) {
    if (full()) return pointer{};
    ++size_;
    return &(add_back() = value);
  }
  auto* try_push_back(value_type&& value) noexcept(
      noexcept(*data() = std::move(value))) {
    if (full()) return pointer{};
    ++size_;
    return &(add_back() = std::move(value));
  }
  template<class... Args>
  auto* try_emplace_back(Args&&... args) noexcept(
      noexcept(*data() = value_type{std::forward<Args>(args)...})) {
    if (full()) return pointer{};
    ++size_;
    return &(add_back() = value_type{std::forward<Args>(args)...});
  }

  // Remove front or back element, returning a reference to it. Must not be
  // empty.
  auto& pop_front() noexcept {
    auto& result = front();
    drop_front();
    --size_;
    return result;
  }
  auto& pop_back() noexcept {
    auto& result = back();
    drop_back();
    --size_;
    return result;
  }

  // Size accessors. Note that capacity is full size of the underlying
  // range.
  [[nodiscard]] size_type capacity() const noexcept { return range_.size(); }
  [[nodiscard]] size_type size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return !size_; }
  [[nodiscard]] bool full() const noexcept { return size() == capacity(); }

  // Front and back accessors. Must not be empty.
  [[nodiscard]] const auto& front() const noexcept { return data(front_); }
  [[nodiscard]] auto& front() noexcept { return data(front_); }
  [[nodiscard]] const auto& back() const noexcept { return data(back_); }
  [[nodiscard]] auto& back() noexcept { return data(back_); }

  // Array operators allow circular access, while `at` throws on
  // out-of-range.
  [[nodiscard]] const auto& operator[](size_type index) const noexcept {
    return data(index_at(index));
  }
  [[nodiscard]] auto& operator[](size_type index) noexcept {
    return data(index_at(index));
  }
  [[nodiscard]] const auto& at(size_type index) const noexcept {
    return data(index_at_checked(index));
  }
  [[nodiscard]] auto& at(size_type index) noexcept {
    return data(index_at_checked(index));
  }

  [[nodiscard]] auto begin() const noexcept { return iterator_t(*this, 0); }
  [[nodiscard]] auto begin() noexcept { return iterator_t(*this, 0); }
  [[nodiscard]] auto cbegin() const noexcept { return iterator_t(*this, 0); }

  [[nodiscard]] auto end() const noexcept { return iterator_t(*this, size()); }
  [[nodiscard]] auto end() noexcept { return iterator_t(*this, size()); }
  [[nodiscard]] auto cend() const noexcept {
    return iterator_t(*this, size());
  }

private:
  // Templated so that it can const or mutable.
  template<typename CB>
  class iterator_t {
  public:
    using iterator_category = std::forward_iterator_tag;
    using raw_value_type = circular_buffer::value_type;
    using value_type = std::conditional_t<std::is_const_v<CB>,
        const raw_value_type, raw_value_type>;
    using const_type = std::conditional_t<std::is_const_v<CB>,
        const circular_buffer, circular_buffer>;

    using difference_type = std::ptrdiff_t;
    using pointer = value_type*;
    using reference = value_type&;

    iterator_t(CB& buf, size_type index) noexcept
        : buf_(&buf), index_(index) {}

    [[nodiscard]] reference operator*() { return buf_->at(index_); }
    [[nodiscard]] pointer operator->() { return &(buf_->at(index_)); }

    // Prefix increment.
    auto& operator++() noexcept {
      ++index_;
      return *this;
    }

    // Postfix increment.
    auto operator++(int) noexcept {
      auto tmp = *this;
      ++(*this);
      return tmp;
    }

    [[nodiscard]] friend bool
    operator==(const iterator_t& a, const iterator_t& b) noexcept {
      return a.index_ == b.index_ && a.buf_ == b.buf_;
    }
    [[nodiscard]] friend bool
    operator!=(const iterator_t& a, const iterator_t& b) noexcept {
      return !(a == b);
    }

  private:
    CB* buf_;
    size_type index_;
  };

  // Deduction guide.
  template<typename CB>
  iterator_t(CB&, size_type) -> iterator_t<CB>;

public:
  using iterator = iterator_t<circular_buffer>;
  using const_iterator = iterator_t<const circular_buffer>;

private:
  // Implementation details:
  // For any non-empty buffer, the `front_` and `back_` indexes point
  // directly to the front() and back() elements. An empty buffer looks
  // like a full buffer, with `back_` pointing to the element before
  // `front_` (modulo capacity). What distinguishes the two is the value of
  // `size_`. Storing this explicitly allows us to use all the elements and
  // avoid doing modulo arithmetic just to calculate the size.
  std::span<T> range_;
  size_type front_{};
  size_type back_{};
  size_type size_{};

  // Note: Size must be adjusted before calling these, due to assert.
  auto* data() noexcept {
    assert(!empty());
    return &*range_.begin();
  }
  const auto* data() const noexcept {
    assert(!empty());
    return &*range_.begin();
  }
  auto& data(size_type index) noexcept { return data()[index]; }
  auto& data(size_type index) const noexcept { return data()[index]; }

  size_type last_index() const noexcept { return capacity() - 1; }

  // Note: Size must be adjusted before calling these, due to offset modulo.
  size_type index_at(size_type offset) const noexcept {
    offset %= size();
    return (front_ + offset) % capacity();
  }
  size_type index_at_checked(size_type offset) const {
    if (offset >= size_) throw std::out_of_range("index out of range");
    return (front_ + offset) % capacity();
  }

  // Note: Size must be adjusted before calling these, due to data().
  auto& add_front() noexcept {
    if (front_ == 0) front_ = capacity();
    return data(--front_);
  }
  auto& add_back() noexcept {
    if (++back_ == capacity()) back_ = 0;
    return data(back_);
  }

  void drop_front() noexcept {
    if (++front_ == capacity()) front_ = 0;
  }
  void drop_back() noexcept {
    if (back_ == 0) back_ = capacity();
    --back_;
  }

  void adjust_size_for_front() noexcept {
    if (full())
      drop_back();
    else
      ++size_;
  }
  void adjust_size_for_back() noexcept {
    if (full())
      drop_front();
    else
      ++size_;
  }

  void steal(circular_buffer&& other) {
    range_ = std::exchange(other.range_, {});
    front_ = std::exchange(other.front_, {});
    back_ = std::exchange(other.back_, {});
    size_ = std::exchange(other.size_, {});
  }
};

// Deduction guides.
template<typename T>
circular_buffer(std::span<T>&) -> circular_buffer<T>;

template<typename T, typename SZ>
circular_buffer(std::span<T>&, SZ) -> circular_buffer<T>;

template<typename T>
circular_buffer(std::vector<T>&) -> circular_buffer<T>;

template<typename T, typename SZ>
circular_buffer(std::vector<T>&, SZ) -> circular_buffer<T>;

template<typename T, std::size_t N>
circular_buffer(std::array<T, N>&) -> circular_buffer<T>;

template<typename T, std::size_t N, typename SZ>
circular_buffer(std::array<T, N>&, SZ) -> circular_buffer<T>;
}} // namespace corvid::adapters
