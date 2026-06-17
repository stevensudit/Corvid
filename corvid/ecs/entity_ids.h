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

#include "../enums.h"
#include "../enums/bool_enums.h"

namespace corvid { inline namespace ecs {
using namespace bool_enums;

// MSVC cl's two-phase name lookup does not reach the corvid-scope using-
// directive for `corvid::enums::sequence::ops` from inside a class-template
// body (a static_assert or static data member), so the sequence-enum
// `operator*` applied to ECS ids there (e.g. `*id_t::invalid`) goes unfound. A
// using- declaration in this namespace is honored where the directive is not.
using corvid::enums::sequence::ops::operator*;

namespace id_enums {

// These are the default ID types for various ECS concepts. You can use them
// as-is, or define your own (typically, smaller ones) and ignore these. If you
// do use them, you will want to inject `id_enums` into your namespace.

// These have to be declared up front so that we can place `enum_spec_v` in the
// global namespace. Note that "sys/types.h" also defines a type named `id_t`
// and rudely injects it into the global namespace.

#pragma region id_t

enum class id_t : size_t { invalid = std::numeric_limits<size_t>::max() };
consteval auto corvid_enum_spec(id_t*) {
  return corvid::enums::sequence::make_sequence_enum_spec<id_t, "">();
}

#pragma endregion
#pragma region entity_id_t

enum class entity_id_t : size_t {
  invalid = std::numeric_limits<size_t>::max()
};
consteval auto corvid_enum_spec(entity_id_t*) {
  return corvid::enums::sequence::make_sequence_enum_spec<entity_id_t, "">();
}

#pragma endregion
#pragma region component_id_t

enum class component_id_t : size_t {
  invalid = std::numeric_limits<size_t>::max()
};
consteval auto corvid_enum_spec(component_id_t*) {
  return corvid::enums::sequence::make_sequence_enum_spec<component_id_t,
      "">();
}

#pragma endregion
#pragma region archetype_id_t

enum class archetype_id_t : size_t {
  invalid = std::numeric_limits<size_t>::max()
};
consteval auto corvid_enum_spec(archetype_id_t*) {
  return corvid::enums::sequence::make_sequence_enum_spec<archetype_id_t,
      "">();
}

#pragma endregion
#pragma region store_id_t

enum class store_id_t : size_t {
  invalid = std::numeric_limits<size_t>::max()
};
consteval auto corvid_enum_spec(store_id_t*) {
  return corvid::enums::sequence::make_sequence_enum_spec<store_id_t, "">();
}

#pragma endregion
} // namespace id_enums
}} // namespace corvid::ecs
