// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2024 Steven Sudit
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

#include "containers_shared.h"

namespace corvid { inline namespace custhandle {

// A `custom_handle` is a wrapper for a raw pointer, or for a resource ID that
// is the moral equivalent of one, such as a file descriptor. It fullfills
// https://en.cppreference.com/w/cpp/named_req/NullablePointer.
//
// It is not intended for general use and is not, in itself, a smart pointer,
// in that it does not free the object in its destructor. Instead, it's made to
// be used as the `pointer` alias in a custom deleter for `std::unique_ptr`,
// replacing the raw pointer that it normally stores and manages. In this
// context, it's considered a "custom handle type".
//
// To use it, you define your own deleter that exposes an appropriate
// `custom_handle` as `pointer` and then offers an `operator()` that closes it.
// In all cases, you ensure the uniqueness of the type by specializing with a
// `tag_type`; the custom deleter itself makes for a good choice.
//
// Likewise, in all cases, you can use an rvalue when constructing or assigning
// to the `std::unique_ptr`, and this will cause the parameter to be set to
// `null_v`. Finally, whatever you choose as `null_v` will be used to evaluate
// `std::unique_ptr::operator bool`.
//
// 1. For wrapping something like a file descriptor, specialize `element_type`
// and `resource_id_type` to the same type and set `null_v` to the desired
// default value, which is something like -1 or INVALID_HANDLE_VALUE. The
// `std::unique_ptr` will then store the value as a `resource_id_type` but
// expose it as an `element_type` when you dereference it.
//
// Note that, unlike `std::unique_ptr<int, fd_deleter>`, it does not involve
// dynamically allocating an `int`. Also note that you can use same-sized class
// enums and underlying integral types for the `element_type` and
// `resource_id_type` and they'll be converted.
//
// 2. For replacing the default value of an actual pointer, you specialize
// `resource_id_type` to a raw pointer to `element_type`, and set `null_v` to
// the desired value. https://en.wikipedia.org/wiki/Null_object_pattern and
// https://en.wikipedia.org/wiki/Sentinel_node are two examples that work well
// with this.
//
// Note that, even if you leave the default value as `nullptr`, you can
// still benefit from the move semantics and the ability to coerce the
// `resource_id_type` to a pointer to `element_type`, even when the former is
// `void*`.
//
// Usage:
// ```
// struct fd_deleter {
//   using pointer = custom_handle<fd_deleter, int, int, -1>;
//   void operator()(pointer p) const { if (p) ::close(*p); }
// };
//
// using unique_fd = std::unique_ptr<int, fd_deleter>;
//
// unique_fd make_fd(const char* filename, int oflag) {
//    return ::open(filename, oflag); }
// ```
template<typename TAG, typename T, typename TPtr = T*, TPtr N = nullptr>
class custom_handle {
public:
  using element_type = T;
  using resource_id_type = TPtr;
  using tag_type = TAG;
  using reference_type = element_type&;
  static constexpr resource_id_type null_v = N;

  custom_handle() = default;
  custom_handle(std::nullptr_t) {}
  custom_handle(const custom_handle&) = default;

  template<typename RT>
  requires std::convertible_to<RT, resource_id_type>
  custom_handle(RT&& resource) : resource_(resource) {
    if constexpr (std::is_rvalue_reference_v<RT&&>) resource = null_v;
  }

  custom_handle& operator=(std::nullptr_t) {
    resource_ = null_v;
    return *this;
  }
  custom_handle& operator=(const custom_handle&) = default;

  template<typename RT>
  requires std::convertible_to<RT, resource_id_type>
  custom_handle& operator=(RT&& resource) {
    resource_ = resource;
    if constexpr (std::is_rvalue_reference_v<RT&&>) resource = null_v;
    return *this;
  }

  reference_type operator*() const {
    if constexpr (std::is_pointer_v<resource_id_type>)
      return *reinterpret_cast<element_type*>(resource_);
    else
      return static_cast<reference_type>(resource_);
  }

  friend bool operator==(const custom_handle& p, std::nullptr_t) {
    return p.resource_ == null_v;
  }
  friend bool operator==(std::nullptr_t, const custom_handle& p) {
    return p.resource_ == null_v;
  }
  friend bool operator==(const custom_handle& p, const custom_handle& q) {
    return p.resource_ == q.resource_;
  }

  explicit operator bool() const { return resource_ != null_v; }

  // Note: Mutable because a const pointer to a mutable resource allows mutable
  // access and this is just as true for resource ID's.
  mutable resource_id_type resource_ = null_v;
};

}} // namespace corvid::custhandle
