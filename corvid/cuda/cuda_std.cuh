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

// The pieces of libcu++ that the device code uses, included safely.
//
// libcu++ is the device twin of the standard library, shipped under the
// toolkit's include/cccl, which the build names as a SYSTEM include for every
// .cu target and for clangd. Its `cuda::std` namespace holds the vocabulary
// that kernels can use in place of `std` (`numeric_limits`, `plus`, `max`),
// and `cuda` itself holds extras such as `maximum` and the `cuda::device`
// warp shuffles.
//
// Include this header rather than any <cuda/...> header directly. On Windows,
// <cuda/functional> reaches libcu++'s driver wrapper, which includes
// <windows.h> bare, and a bare <windows.h> pulls in the old <winsock.h>, which
// then clashes with the <winsock2.h> that "windows_os_enums.h" includes. The
// guards below are the ones it uses, so the two agree whichever comes first.
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <cuda/functional>
#include <cuda/std/algorithm>
#include <cuda/std/functional>
#include <cuda/std/limits>
#include <cuda/warp>
