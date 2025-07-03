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

// Base template.
template<typename T>
class appender {};

// CRTP base.
template<typename T>
class appender_crtp {
  using Base = appender_crtp<T>;
  using Child = appender<T>;

public:
  explicit appender_crtp(T& target) : target_{target} {}
  auto& child() { return *static_cast<Child*>(this); }

  auto& append(std::string_view sv) { return child().append_sv(sv); }
  auto& append(const char* ps, size_t len) { return append({ps, len}); }
  auto& append(char ch) { return child().append_ch(1, ch); }
  auto& append(size_t len, char ch) { return child().append_ch(len, ch); }

  auto& reserve(size_t) { return *this; }

  T& operator*() { return target_; }
  T* operator->() { return &target_; }

protected:
  T& target_;
};

// std::ostream specialization.
template<OStreamDerived T>
class appender<T>: public appender_crtp<T> {
public:
  explicit appender(T& target) : appender_crtp<T>{target} {}

private:
  friend appender_crtp<T>;
  auto& append_sv(std::string_view sv) {
    appender_crtp<T>::target_.write(sv.data(), sv.size());
    return *this;
  }
  auto& append_ch(size_t len, char ch) {
    while (len--) appender_crtp<T>::target_.put(ch);
    return *this;
  }
};

// String specialization.
template<StdString T>
class appender<T>: public appender_crtp<T> {
public:
  explicit appender(T& target) : appender_crtp<T>{target} {}

  auto& reserve(size_t len) {
    appender_crtp<T>::target_.reserve(appender_crtp<T>::target_.size() + len);
    return *this;
  }

private:
  friend appender_crtp<T>;
  auto& append_sv(std::string_view sv) {
    appender_crtp<T>::target_.append(sv);
    return *this;
  }
  auto& append_ch(size_t len, char ch) {
    if (len == 1)
      appender_crtp<T>::target_.push_back(ch);
    else
      appender_crtp<T>::target_.append(len, ch);
    return *this;
  }
};

// Deduction guide.
template<AppendTarget T>
appender(T) -> appender<T>;

}} // namespace corvid::strings::targeting
