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
#include <format>
#include <stdexcept>
#include <string>
#include <system_error>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace corvid::win32 {

#pragma region hr_status

// Wrapper for `HRESULT`.
class hr_status {
public:
#pragma region Construction

  explicit hr_status(HRESULT hr) noexcept : hr_{hr} {}

  // The calling thread's last Win32 error, as an HRESULT. Read it immediately
  // after the failing call; any intervening Win32 call may overwrite it.
  [[nodiscard]] static hr_status last_error() noexcept {
    return hr_status{HRESULT_FROM_WIN32(GetLastError())};
  }

#pragma endregion
#pragma region Status

  [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(hr_); }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }

  [[nodiscard]] HRESULT value() const noexcept { return hr_; }

#pragma endregion
#pragma region Errors

  [[nodiscard]] std::string message() const {
    return std::format("{} (0x{:08X})", std::system_category().message(hr_),
        static_cast<unsigned long>(hr_));
  }

  // NOLINTNEXTLINE(modernize-use-nodiscard)
  bool or_throw() const {
    if (FAILED(hr_)) throw std::runtime_error{message()};
    return true;
  }

#pragma endregion
#pragma region Data members
private:
  HRESULT hr_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::win32
