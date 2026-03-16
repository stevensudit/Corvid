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
#include "containers_shared.h"
#include <utility>

namespace corvid { inline namespace container {
inline namespace value_scoping {

// RAII helper for temporarily changing a value and restoring it on scope exit.
template<typename T>
class scoped_value {
public:
  explicit scoped_value(T& target, T newValue)
      : m_target(target), m_oldValue(std::move(target)) {
    m_target = std::move(newValue);
  }

  ~scoped_value() { m_target = std::move(m_oldValue); }

  scoped_value(const scoped_value&) = delete;
  scoped_value(scoped_value&&) = delete;
  scoped_value& operator=(const scoped_value&) = delete;
  scoped_value& operator=(scoped_value&&) = delete;

private:
  T& m_target;
  T m_oldValue;
};

}}} // namespace corvid::container::value_scoping
