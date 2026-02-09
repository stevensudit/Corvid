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

#include "../enums/sequence_enum.h"

namespace corvid { inline namespace ecs { namespace id_enums {

// These are the default ID types for various ECS concepts. You can use them
// as-is, or define your own (typically, smaller ones) and ignore these. If you
// do used them, you will want to inject `id_enums` into your namespace.

// These have to be declared up front so that we can place `enum_spec_v` in the
// global namespace. Note that "sys/types.h" also defines a type named `id_t`
// and rudely injects it into the global namespace.
enum class id_t : size_t { invalid = std::numeric_limits<size_t>::max() };
enum class entity_id_t : size_t {
  invalid = std::numeric_limits<size_t>::max()
};
enum class component_id_t : size_t {
  invalid = std::numeric_limits<size_t>::max()
};
enum class archetype_id_t : size_t {
  invalid = std::numeric_limits<size_t>::max()
};
enum class store_id_t : size_t {
  invalid = std::numeric_limits<size_t>::max()
};

}}} // namespace corvid::ecs::id_enums

template<>
constexpr auto
    corvid::enums::registry::enum_spec_v<corvid::ecs::id_enums::id_t> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::ecs::id_enums::id_t, "">();

template<>
constexpr auto
    corvid::enums::registry::enum_spec_v<corvid::ecs::id_enums::entity_id_t> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::ecs::id_enums::entity_id_t, "">();

template<>
constexpr auto corvid::enums::registry::enum_spec_v<
    corvid::ecs::id_enums::component_id_t> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::ecs::id_enums::component_id_t, "">();

template<>
constexpr auto corvid::enums::registry::enum_spec_v<
    corvid::ecs::id_enums::archetype_id_t> =
    corvid::enums::sequence::make_sequence_enum_spec<
        corvid::ecs::id_enums::archetype_id_t, "">();

template<>
constexpr auto
    corvid::enums::registry::enum_spec_v<corvid::ecs::id_enums::store_id_t> =
        corvid::enums::sequence::make_sequence_enum_spec<
            corvid::ecs::id_enums::store_id_t, "">();
