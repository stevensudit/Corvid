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
#include <stdexcept>

#include "./sdl_common.h"

namespace corvid::sdl {

#pragma region SDL status

// Wrapper for the result of an SDL call.
//
// SDL reports failure as a false `bool` (or, for a pointer-returning call, a
// `nullptr`). On failure, we capture `SDL_GetError`.
//
// The captured message points into SDL's per-thread error buffer, which SDL
// reuses on its next call, so it is valid only until the next SDL call on this
// thread. Inspect it or `or_throw` before making further SDL calls; do not
// hold a `status` across them.
class sdl_status {
public:
#pragma region Construction

  sdl_status(bool ok) noexcept : error_{ok ? nullptr : SDL_GetError()} {}

#pragma endregion
#pragma region Status

  [[nodiscard]] bool ok() const noexcept { return !error_; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

#pragma endregion
#pragma region Errors

  [[nodiscard]] const char* get_error() const noexcept { return error_; }

  // NOLINTNEXTLINE(modernize-use-nodiscard)
  bool or_throw() const {
    if (error_) throw std::runtime_error{error_};
    return true;
  }

  template<typename T>
  [[nodiscard]]
  static T* or_throw(T* ptr) {
    if (!ptr) throw std::runtime_error{SDL_GetError()};
    return ptr;
  }

#pragma endregion
#pragma region Data members
private:
  const char* error_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::sdl
