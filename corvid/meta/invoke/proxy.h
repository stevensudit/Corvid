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

// Registration-based runtime polymorphism ("proxy") system.
//
// Type-erased handles (`proxy`, `proxy_view`, `const_proxy_view`,
// `shared_proxy`, `const_shared_proxy`, `weak_proxy`, `const_weak_proxy`)
// over an interface definition (a facade),
// without inheritance, vtable pointers in the target type, or macros.
//
// The family splits by handle, with the machinery in its own header:
//
// - proxy_common.h: facades, registration, and dispatch.
// - owning_proxy.h: `proxy`.
// - proxy_view.h: `proxy_view` and `const_proxy_view`.
// - shared_proxy.h: `shared_proxy` and `const_shared_proxy`, with their
//   observers `weak_proxy` and `const_weak_proxy`.
//
// Does not include `proxy_codegen.h`.
//
// See "proxy.md" for the design.

#include "proxy_common.h"
#include "owning_proxy.h"
#include "proxy_view.h"
#include "shared_proxy.h"
