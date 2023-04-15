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
#include "./meta_shared.h"

namespace corvid {
inline namespace meta {
inline namespace pragmas {

// Glue to silence overeager warnings.
#if defined(__clang__)
#define PRAGMA_clang_push _Pragma("clang diagnostic push")
#define PRAGMA_clang_pop _Pragma("clang diagnostic pop")
#define PRAGMA_clang_enum_constexpr_conversion                                \
  _Pragma("clang diagnostic ignored \"-Wenum-constexpr-conversion\"")
#define PRAGMA_gcc_push
#define PRAGMA_gcc_pop
#define PRAGMA_gcc_waddress
#define PRAGMA_gcc_non_nullcompare
#elif defined(__GNUC__) || defined(__GNUG__)
#define PRAGMA_clang_push
#define PRAGMA_clang_pop
#define PRAGMA_clang_enum_constexpr_conversion
#define PRAGMA_gcc_push _Pragma("GCC diagnostic push")
#define PRAGMA_gcc_pop _Pragma("GCC diagnostic pop");
#define PRAGMA_gcc_waddress _Pragma("GCC diagnostic ignored \"-Waddress\"")
#define PRAGMA_gcc_non_nullcompare                                            \
  _Pragma("GCC diagnostic ignored \"-Wnonnull-compare\"")
#endif

// TODO: Consider going meta by making a PRAGMA_clang that takes an arbitrary
// string.

} // namespace pragmas
} // namespace meta
} // namespace corvid