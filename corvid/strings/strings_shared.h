// Corvid20: A general-purpose C++20 library extending std.
// https://github.com/stevensudit/Corvid20
//
// Copyright 2022-2023 Steven Sudit
//
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
#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdlib>
#include <concepts>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <ranges>
#include <vector>
#include <stdexcept>
#include <optional>

#include "../meta.h"

namespace corvid::strings {

// Import.
using namespace std::literals;

} // namespace corvid::strings
