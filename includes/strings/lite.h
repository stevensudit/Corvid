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
#include "strings_shared.h"
#include "cases.h"
#include "locating.h"
#include "targeting.h"
#include "delimiting.h"
#include "streaming.h"
#include "delimiting.h"
#include "trimming.h"
#include "splitting.h"
#include "conversion.h"
#include "fixed_string.h"
#include "fixed_string_utils.h"

// This header contains the includes for the "light" part of the strings
// namespace, with minimal dependencies. In specific, it does not include
// "concat_join.h".
