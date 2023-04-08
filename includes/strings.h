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
#include "strings/strings_shared.h"
#include "strings/cstring_view.h"
#include "strings/fixed_string.h"
#include "strings/cases.h"
#include "strings/search_and.h"
#include "strings/targeting.h"
#include "strings/delimiting.h"
#include "strings/streaming.h"
#include "strings/delimiting.h"
#include "strings/trimming.h"
#include "strings/splitting.h"
#include "strings/conversion.h"
#include "strings/concat_join.h"

// Recommendation: While you can import the entire `corvid::strings` namespace,
// you may not want to bring in all of these symbols, or you may wish to do so
// more selectively.
//
// The way to do that is to import just `corvid` and then reference symbols
// through the `strings` namespace, such as `strings::trim("a ")`. You can also
// choose to import the inline namespace for that group of symbols, such as
// `corvid::trimming`.

//
// TODO
//

// TODO: Get extract_num to work with cstring_view cleanly. It's safe because
// we only remove the prefix, never the suffix. The brute-force solution is to
// add overloads for the extract methods. Maybe add extract_num and such to
// cstring, so as not to pollute this.

// TODO: Maybe add a replace_any that replaces any matching chars with the
// destination value. Maybe supplement replace with remove and remove_any.

// TODO: Consider offering a version of `replace` that returns a new value,
// avoiding multiple resizes.

// TODO: Wacky idea: overload unary `operator+` for `std::string_view` to mean
// non-empty. And `-` for empty.
