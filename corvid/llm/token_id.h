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

#include <cstdint>

#include "../enums/sequence_enum.h"

// The token ID type shared by the tokenizer and the model ops.
namespace corvid::llm {

#pragma region token_id

// Index of a piece in a tokenizer's vocabulary.
enum class token_id : uint32_t {};
consteval auto corvid_enum_spec(token_id*) {
  return corvid::enums::sequence::make_sequence_enum_spec<token_id, "">();
}

#pragma endregion

} // namespace corvid::llm
