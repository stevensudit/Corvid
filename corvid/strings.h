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
#include "strings/string_literals.h"
#include "strings/cstring_view.h"
#include "strings/fixed_string_utils.h"
#include "strings/cases.h"
#include "strings/locating.h"
#include "strings/targeting.h"
#include "strings/delimiting.h"
#include "strings/streaming.h"
#include "strings/concatenating.h"
#include "strings/trimming.h"
#include "strings/splitting.h"
#include "strings/expand_tabs.h"
#include "strings/textwrap.h"
#include "strings/fnmatch.h"
#include "strings/pure_path.h"
#include "strings/justification.h"
#include "strings/string_partition.h"
#include "strings/token_parser.h"
#include "strings/conversion.h"
#include "strings/no_zero.h"
#include "strings/enable_format.h"
#include "strings/any_strings.h"

// Recommendation: While you can import the entire `corvid::strings` namespace,
// you may not want to bring in all of these symbols, or you may wish to do so
// more selectively.
//
// The way to do that is to import just `corvid` and then reference symbols
// through the `strings` namespace, such as `strings::trim("a ")`. You can also
// choose to import the inline namespace for that group of symbols, such as
// `corvid::strings::trimming`.
//
// The module-style namespaces, `textwrap`, `fnmatch`, and `pure_path`, are
// deliberately not inline. Bring one in with a namespace alias, such as
// `namespace textwrap = corvid::strings::textwrap;`, and calls then read as
// they do in Python: `textwrap::dedent`, `fnmatch::filter`, and so on.
