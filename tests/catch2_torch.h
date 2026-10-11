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

// Catch2 beside ATen, for the torch tests. Include this instead of
// "catch2_main.h", before any other header that reaches ATen.
//
// c10's logging shim defines a glog-style `CHECK`, which Catch2's must
// replace. Torch's own throwing check is `TORCH_CHECK`, which stays.
#include <ATen/ATen.h>
#undef CHECK
#include "catch2_main.h"
