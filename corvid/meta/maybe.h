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
#include "./meta_shared.h"

namespace corvid { inline namespace meta { inline namespace maybe_types {

// Maybe types go away entirely depending on template parameters. These work
// with [[no_unique_address]] inside a struct or class to avoid any space
// overhead.

// Empty type used when maybe_t is disabled.
struct empty_t {};

// Maybe bool type: T if Enabled is true, otherwise empty.
//
// Usage:
//   [[no_unique_address]] maybe_t<int, Enabled> int_or_missing;
template<typename T, bool Enabled = false>
using maybe_t = std::conditional_t<Enabled, T, empty_t>;

// Maybe void type: T if T is not void, otherwise empty.
//
// Usage:
//   [[no_unique_address]] maybe_void_t<int> int_or_missing;
template<typename T = void>
using maybe_void_t = maybe_t<T, !std::is_void_v<T>>;

}}} // namespace corvid::meta::maybe_types
