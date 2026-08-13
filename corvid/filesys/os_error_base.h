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
#include <string>
#include <system_error>
#include <type_traits>

#include "../enums/bitmask_enum.h"
#include "../enums/sequence_enum.h"

namespace corvid { inline namespace filesys {

// Fix for MSVC cl bug.
//
// Using-declarations, not directives, because cl's two-phase lookup does not
// find the directive-injected operator inside template bodies. Declaring the
// sequence form alone would hide the directive-injected bitmask form from
// unqualified lookup in this namespace, so declare both.
using corvid::enums::sequence::ops::operator*;
using corvid::enums::bitmask::ops::operator*;

#pragma region os_error_base

// CRTP base defining the portable `os_error` interface.
//
// `os_error` is a value wrapper for an OS error code. The wrapped code is the
// platform's own vocabulary: `errno_code` on Linux and `win_error_code` on
// Windows. This is exposed as `code_t`, which is a scoped enum whose
// `ok` member means no error.
//
// A platform-specific implementation of `os_error` derives from this base,
// supplying only the `do_` workers that implement the platform-specific
// operations.
//
// Capture the error immediately after the failing call, as in:
//
//   if (!some_os_call())
//     if (auto err = os_error::last(); err.is_hard_error())
//       log("failed: {}", err);
template<typename Derived, typename CodeT>
class os_error_base {
  static_assert(enums::sequence::SequentialEnum<CodeT>);

public:
#pragma region Types

  using code_t = CodeT;

#pragma endregion
#pragma region Construction

  // Public so that `Derived` can expose them with `using base::base`; only
  // `Derived` may actually inherit from this class.
  // NOLINTBEGIN(bugprone-crtp-constructor-accessibility)

  // Holds `ok` by default.
  constexpr os_error_base() noexcept = default;

  constexpr os_error_base(code_t code) noexcept : code_{code} {}

  explicit constexpr os_error_base(std::underlying_type_t<CodeT> raw) noexcept
      : code_{raw} {}

  // NOLINTEND(bugprone-crtp-constructor-accessibility)

  // The calling thread's last error.
  //
  // Read it immediately after the failing call; any intervening OS call may
  // overwrite it.
  [[nodiscard]] static Derived last() noexcept {
    return Derived{Derived::do_last()};
  }

  // The calling thread's last socket error, for the platforms whose sockets
  // report errors through a separate channel.
  //
  // Use this for socket errors on either platform.
  [[nodiscard]] static Derived last_socket() noexcept {
    return Derived{Derived::do_last_socket()};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] constexpr code_t code() const noexcept { return code_; }

  [[nodiscard]] constexpr auto raw() const noexcept { return *code_; }

  [[nodiscard]] constexpr bool ok() const noexcept {
    return code_ == code_t::ok;
  }

  [[nodiscard]] constexpr explicit operator bool() const noexcept {
    return ok();
  }

  [[nodiscard]] constexpr code_t operator*() const noexcept { return code_; }

  constexpr bool operator==(const os_error_base&) const noexcept = default;

#pragma endregion
#pragma region Errors

  // Check whether this is a soft error.
  //
  // A soft error represents flow control conditions, such as `EAGAIN`, that
  // are expected to occur in normal operation and can be retried. An `ok`
  // value is not an error at all, so it is neither soft nor hard.
  [[nodiscard]] constexpr bool is_soft_error() const noexcept {
    return static_cast<const Derived&>(*this).do_is_soft_error();
  }

  // Check whether this is a hard error: an actual failure that should be
  // handled as such, rather than `ok` or a retriable soft error.
  [[nodiscard]] constexpr bool is_hard_error() const noexcept {
    return !ok() && !is_soft_error();
  }

  // Describe the error in the system locale, as by `strerror` or
  // `FormatMessage`.
  [[nodiscard]] std::string message() const {
    return std::system_category().message(static_cast<int>(raw()));
  }

#pragma endregion
#pragma region Data members
private:
  code_t code_ = code_t::ok;

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys
