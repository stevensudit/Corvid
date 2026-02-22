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

// Read/write access. Replaces `bool MUTABLE` in row_wrapper, row_iterator,
// and iterator_t.
enum class access_t : bool { ro = false, rw = true };

// Whether IDs carry a generation counter for stale-reference detection.
// Replaces `bool UseGen` in stable_ids and entity_registry.
enum class gen_t : bool { unversioned = false, versioned = true };

// Recycled-ID reuse order. Replaces `bool UseFifo` in stable_ids.
enum class reuse_order_t : bool { lifo = false, fifo = true };

// Whether to pre-populate the free list during construction or reserve.
// Replaces `bool prefill` in stable_ids and entity_registry.
enum class prefill_t : bool { lazy = false, eager = true };

// Whether to release backing memory (and reset generation counters) on clear.
// Replaces `bool shrink` in entity_registry::clear.
enum class shrink_t : bool { preserve = false, shrink = true };

// Whether to pre-allocate storage capacity up to the limit at construction.
// Replaces `bool do_reserve` in archetype_storage, chunked_archetype_storage,
// and component_storage constructors.
enum class reserve_t : bool { on_demand = false, preallocate = true };

// Fast vs. full clear semantics for scene. Fast path drops all storage vectors
// and resets the registry wholesale (O(S)); full path erases entities one by
// one, preserving generation counters (O(N)). Replaces `bool fast` in
// scene::clear.
enum class clear_mode_t : bool { full = false, fast = true };

// Behavior when an insertion into stable_ids fails due to capacity exhaustion.
// Replaces `bool value` in stable_ids::throw_on_insert_failure.
enum class insert_fail_t : bool { silent = false, throws = true };

}}} // namespace corvid::enums::bool_enums
