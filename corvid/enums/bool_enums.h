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
#include "enums_shared.h"

namespace corvid { inline namespace enums { namespace bool_enums {

// Strongly-typed two-value enums intended to replace plain `bool` parameters.
// All have `bool` as their underlying type, enabling explicit conversion via
// `static_cast` and use as C++20 non-type template parameters.
//
// For naming, these typically have suffixes like "_scheme", "_mode",
// "_behavior", or "_policy", or they have prefixes like "on_".

// Whether to access values as const or as mutable.
enum class access : bool { as_const = false, as_mutable = true };

// Whether to enable generation counters for stale-reference detection or save
// memory by avoiding any versioning.
enum class generation_scheme : bool { unversioned = false, versioned = true };

// Reuse order for freed resources.
enum class reuse_order : bool { lifo = false, fifo = true };

// Whether to allocate eagerly (reserving capacity up front and prefilling as
// needed) or lazily (just-in-time).
enum class allocation_policy : bool { lazy = false, eager = true };

// Whether to release backing memory (and reset generation counters) on clear.
enum class deallocation_policy : bool { preserve = false, release = true };

// Whether to throw on failure or fail silently.
enum class on_failure : bool { ignore = false, raise = true };

// Whether the resource is owned exclusively or is shared.
enum class ownership : bool { unique = false, shared = true };

// Whether the resource should be preserved or removed immediately.
enum class removal_mode : bool { preserve = false, remove = true };

// Whether to perform a graceful close (e.g., via `close()`) or a forceful
// close (e.g., via `hangup()` with SO_LINGER disabled).
enum class close_mode : bool { graceful = false, forceful = true };

// Whether operations should block until progress is possible or return
// immediately when they would otherwise wait.
enum class execution : bool { nonblocking = false, blocking = true };

}}} // namespace corvid::enums::bool_enums
