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
#include "meta_shared.h"

namespace corvid { inline namespace meta { inline namespace crossplatform {

#pragma region Suppression

// Glue to silence overeager warnings.
#define PRAGMA_DIAG_HELPER(action) _Pragma(#action)
#ifdef __clang__
#define PRAGMA_CLANG_DIAG(action) PRAGMA_DIAG_HELPER(clang diagnostic action)
#define PRAGMA_CLANG_IGNORED(quoted) PRAGMA_CLANG_DIAG(ignored quoted)
#define PRAGMA_GCC_DIAG(action)
#define PRAGMA_GCC_IGNORED(quoted)
#define PRAGMA_DIAG(action) PRAGMA_CLANG_DIAG(action)
#define PRAGMA_IGNORED(quoted) PRAGMA_CLANG_IGNORED(quoted)
// Removed in clang 20+; guard with __has_warning to stay forward-compatible.
#if __has_warning("-Wenum-constexpr-conversion")
#define PRAGMA_CLANG_IGNORED_ENUM_CONSTEXPR_CONV                              \
  PRAGMA_CLANG_IGNORED("-Wenum-constexpr-conversion")
#else
#define PRAGMA_CLANG_IGNORED_ENUM_CONSTEXPR_CONV
#endif
#elif defined(__GNUC__) || defined(__GNUG__)
#define PRAGMA_CLANG_DIAG(action)
#define PRAGMA_CLANG_IGNORED(quoted)
#define PRAGMA_GCC_DIAG(action) PRAGMA_DIAG_HELPER(GCC diagnostic action)
#define PRAGMA_GCC_IGNORED(quoted) PRAGMA_GCC_DIAG(ignored quoted)
#define PRAGMA_DIAG(action) PRAGMA_GCC_DIAG(action)
#define PRAGMA_IGNORED(quoted) PRAGMA_GCC_IGNORED(quoted)
#define PRAGMA_CLANG_IGNORED_ENUM_CONSTEXPR_CONV
#else
// Other compilers - define empty macro stubs
#define PRAGMA_CLANG_DIAG(action)
#define PRAGMA_CLANG_IGNORED(quoted)
#define PRAGMA_GCC_DIAG(action)
#define PRAGMA_GCC_IGNORED(quoted)
#define PRAGMA_DIAG(action)
#define PRAGMA_IGNORED(quoted)
#define PRAGMA_CLANG_IGNORED_ENUM_CONSTEXPR_CONV
#endif

#pragma endregion

#pragma region Attributes

// `[[no_unique_address]]` is silently ignored under the MSVC ABI, which spells
// the attribute `[[msvc::no_unique_address]]` instead. clang-cl and cl both
// target that ABI and define `_MSC_VER`; every other toolchain takes the
// standard spelling. Without this, types that rely on empty-member elision
// silently grow on Windows.
#ifdef _MSC_VER
#define CORVID_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
#define CORVID_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif

#pragma endregion
}}} // namespace corvid::meta::crossplatform
