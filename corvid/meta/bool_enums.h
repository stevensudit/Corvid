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
#pragma once

namespace corvid { inline namespace meta {

// Strongly typed two-value enums that the foundation band needs. The main
// catalog is `enums/bool_enums.h`, which re-exports these into
// `bool_enums` so the public API stays flat.
//
// This header exists only because `meta` may not include `enums`.

#pragma region Boolean enums

// Whether to throw on failure or fail silently.
enum class on_failure : bool { ignore = false, raise = true };

// Whether a type-erased owner stores its target in its own inline buffer or
// in a dynamic allocation it points to.
enum class allocation_mode : bool { inlined = false, dynamic = true };

#pragma endregion
}} // namespace corvid::meta
