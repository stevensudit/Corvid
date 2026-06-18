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

#ifndef NOMINMAX
#define NOMINMAX
#endif

// Common Windows graphics includes for the Direct3D 11 wrappers: the D3D11
// device, context, and resource API, plus the DXGI 1.2 surface that supplies
// `IDXGIFactory2::CreateSwapChainForHwnd` and the flip-model
// `DXGI_SWAP_CHAIN_DESC1`.

#include <d3d11.h>
#include <dxgi1_2.h>

namespace corvid::d3d {

#pragma region hr_status

// Wrapper for `HRESULT`.
class hr_status {
public:
#pragma region Construction

  explicit hr_status(HRESULT hr) noexcept : hr_{hr} {}

#pragma endregion
#pragma region Status

  [[nodiscard]] bool ok() const noexcept { return SUCCEEDED(hr_); }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] bool operator!() const noexcept { return !ok(); }
  [[nodiscard]] HRESULT value() const noexcept { return hr_; }

#pragma endregion
#pragma region Errors

  [[nodiscard]] std::string message() const {
    return std::format("HRESULT 0x{:08X}", static_cast<unsigned long>(hr_));
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

} // namespace corvid::d3d
