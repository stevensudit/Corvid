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
#include "strings/core/strings_shared.h"
#include "strings/core/cstring_view.h"
#include "strings/core/fixed_string.h"
#include "strings/core/fixed_string_utils.h"
#include "strings/core/cases.h"
#include "strings/core/locating.h"
#include "strings/core/targeting.h"
#include "strings/core/delimiting.h"
#include "strings/core/streaming.h"
#include "strings/core/trimming.h"
#include "strings/core/splitting.h"
#include "strings/core/token_parser.h"
#include "strings/core/conversion.h"
#include "strings/utils/enum_conversion.h"
#include "strings/core/no_zero.h"
#include "strings/core/any_strings.h"

// Recommendation: While you can import the entire `corvid::strings` namespace,
// you may not want to bring in all of these symbols, or you may wish to do so
// more selectively.
//
// The way to do that is to import just `corvid` and then reference symbols
// through the `strings` namespace, such as `strings::trim("a ")`. You can also
// choose to import the inline namespace for that group of symbols, such as
// `corvid::trimming`.
