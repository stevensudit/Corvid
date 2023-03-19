// Corvid20: A general-purpose C++ 20 library extending std.
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
#include <charconv>
#include <concepts>
#include <iostream>
#include <limits>
#include <ranges>
#include <vector>

// String headers that do not depend upon enums.
// TODO: Trim to minimum required.
#include "../strings/cases.h"
#include "../strings/search_and.h"
#include "../strings/targeting.h"
#include "../strings/delimiting.h"
#include "../strings/streaming.h"
#include "../strings/delimiting.h"
#include "../strings/trimming.h"
#include "../strings/splitting.h"
#include "../strings/conversion.h"
