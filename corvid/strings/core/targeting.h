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
#include "strings_shared.h"

namespace corvid::strings { inline namespace targeting {

//
// Appender target
//

// An appender target is a thin wrapper over a target stream or string. As its
// name suggests, it's used in the various append functions to support either
// type of target seamlessly.
//
// Note: Under clang and gcc, this is optimized away entirely. Under MSVC, not
// quite. But this is consistent with MSVC's overall pattern of underwhelming
// optimization.

#pragma region appender

// Base template forward declaration. Only specialized for supported targets.
template<typename T>
class appender;

// Base class with shared functionality using C++23 deducing this.
// This replaces the CRTP pattern - the `this auto&& self` parameter deduces
// the actual derived type, allowing base class methods to call derived
// class methods and return the correct type.
template<typename T, typename C>
class appender_base {
#pragma region Types
public:
  using char_t = C;
  using view_t = std::basic_string_view<char_t>;

#pragma endregion
#pragma region Construction
public:
  explicit appender_base(T& target) : target_{target} {}

#pragma endregion
#pragma region Appending

  // Deducing this: `self` deduces to the actual derived type (appender<T>).
  // All append overloads forward to append_sv or append_ch in the derived.
  auto& append(this auto&& self, view_t sv) { return self.append_sv(sv); }
  auto& append(this auto&& self, const char_t* ps, size_t len) {
    return self.append(view_t{ps, len});
  }
  auto& append(this auto&& self, char_t ch) { return self.append_ch(1, ch); }
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  auto& append(this auto&& self, size_t len, char_t ch) {
    return self.append_ch(len, ch);
  }

  // Default reserve is no-op; string specialization overrides.
  // Uses deducing this for return type consistency, not polymorphic dispatch.
  auto& reserve(this auto&& self, size_t) { return self; }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] T& operator*() { return target_; }
  [[nodiscard]] T* operator->() { return &target_; }

#pragma endregion
#pragma region Data members
protected:
  T& target_;

#pragma endregion
};

// `std::basic_ostream` specialization.
template<AnyOStreamDerived T>
class appender<T> final: public appender_base<T, typename T::char_type> {
  using char_t = typename T::char_type;
  using base = appender_base<T, char_t>;
  using base::target_;

#pragma region Construction
public:
  using base::base;

#pragma endregion
#pragma region Appending
private:
  friend base;
  auto& append_sv(std::basic_string_view<char_t> sv) {
    target_.write(sv.data(), sv.size());
    return *this;
  }
  auto& append_ch(size_t len, char_t ch) {
    while (len--) target_.put(ch);
    return *this;
  }

#pragma endregion
};

// `std::basic_string` specialization.
template<AnyStdString T>
class appender<T> final: public appender_base<T, typename T::value_type> {
  using char_t = typename T::value_type;
  using base = appender_base<T, char_t>;
  using base::target_;

#pragma region Construction
public:
  using base::base;

#pragma endregion
#pragma region Appending

  auto& reserve(size_t len) {
    target_.reserve(target_.size() + len);
    return *this;
  }

private:
  friend base;
  auto& append_sv(std::basic_string_view<char_t> sv) {
    target_.append(sv);
    return *this;
  }
  auto& append_ch(size_t len, char_t ch) {
    if (len == 1)
      target_.push_back(ch);
    else
      target_.append(len, ch);
    return *this;
  }

#pragma endregion
};

// Deduction guide.
template<AnyAppendTarget T>
appender(T) -> appender<T>;

#pragma endregion appender

}} // namespace corvid::strings::targeting
