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

namespace corvid { inline namespace meta { inline namespace pragmas {

// Glue to silence overeager warnings.
#define PRAGMA_DIAG_HELPER(action) _Pragma(#action)
#if defined(__clang__)
#define PRAGMA_CLANG_DIAG(action) PRAGMA_DIAG_HELPER(clang diagnostic action)
#define PRAGMA_CLANG_IGNORED(quoted) PRAGMA_CLANG_DIAG(ignored quoted)
#define PRAGMA_GCC_DIAG(action)
#define PRAGMA_GCC_IGNORED(quoted)
#define PRAGMA_DIAG(action) PRAGMA_CLANG_DIAG(action)
#define PRAGMA_IGNORED(quoted) PRAGMA_CLANG_IGNORED(quoted)
#elif defined(__GNUC__) || defined(__GNUG__)
#define PRAGMA_CLANG_DIAG(action)
#define PRAGMA_CLANG_IGNORED(action)
#define PRAGMA_GCC_DIAG(action) PRAGMA_DIAG_HELPER(GCC diagnostic action)
#define PRAGMA_GCC_IGNORED(quoted) PRAGMA_GCC_DIAG(ignored quoted)
#define PRAGMA_DIAG(action) PRAGMA_GCC_DIAG(action)
#define PRAGMA_IGNORED(quoted)
#endif

}}} // namespace corvid::meta::pragmas
