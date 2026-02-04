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

// Base template forward declaration. Only specialized for supported targets.
template<typename T>
class appender;

// Base class with shared functionality using C++23 deducing this.
// This replaces the CRTP pattern - the `this auto&& self` parameter deduces
// the actual derived type, allowing base class methods to call derived
// class methods and return the correct type.
template<typename T>
class appender_base {
public:
  explicit appender_base(T& target) : target_{target} {}

  // Deducing this: `self` deduces to the actual derived type (appender<T>).
  // All append overloads forward to append_sv or append_ch in the derived.
  auto& append(this auto&& self, std::string_view sv) {
    return self.append_sv(sv);
  }
  auto& append(this auto&& self, const char* ps, size_t len) {
    return self.append(std::string_view{ps, len});
  }
  auto& append(this auto&& self, char ch) { return self.append_ch(1, ch); }
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  auto& append(this auto&& self, size_t len, char ch) {
    return self.append_ch(len, ch);
  }

  // Default reserve is no-op; string specialization overrides.
  // Uses deducing this for return type consistency, not polymorphic dispatch.
  auto& reserve(this auto&& self, size_t) { return self; }

  [[nodiscard]] T& operator*() { return target_; }
  [[nodiscard]] T* operator->() { return &target_; }

protected:
  T& target_;
};

// std::ostream specialization.
template<OStreamDerived T>
class appender<T> final: public appender_base<T> {
  using appender_base<T>::target_;

public:
  using appender_base<T>::appender_base;

private:
  friend appender_base<T>;
  auto& append_sv(std::string_view sv) {
    target_.write(sv.data(), sv.size());
    return *this;
  }
  auto& append_ch(size_t len, char ch) {
    while (len--) target_.put(ch);
    return *this;
  }
};

// String specialization.
template<StdString T>
class appender<T> final: public appender_base<T> {
  using appender_base<T>::target_;

public:
  using appender_base<T>::appender_base;

  auto& reserve(size_t len) {
    target_.reserve(target_.size() + len);
    return *this;
  }

private:
  friend appender_base<T>;
  auto& append_sv(std::string_view sv) {
    target_.append(sv);
    return *this;
  }
  auto& append_ch(size_t len, char ch) {
    if (len == 1)
      target_.push_back(ch);
    else
      target_.append(len, ch);
    return *this;
  }
};

// Deduction guide.
template<AppendTarget T>
appender(T) -> appender<T>;

}} // namespace corvid::strings::targeting
