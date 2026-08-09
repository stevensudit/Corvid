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
#include "os_error.h"

namespace corvid { inline namespace filesys {

#pragma region os_file_base

// CRTP base defining the shared `os_file` interface.
//
// `os_file` is an RAII wrapper around an OS file handle: a file descriptor on
// Linux and a `HANDLE` on Windows, exposed as `file_handle_t` with the
// invalid sentinel `invalid_file_handle`. An `os_file` owns a single file and
// closes it on destruction; it is movable and non-copyable. The platform
// implementation derives from this base, supplying the `do_` workers for
// adoption and closing, the I/O operations (`write`, `read`, `write_all`,
// `read_exact`, whose semantics are documented on each platform), and any
// extras, such as the `fcntl` helpers on Linux.
//
//   auto data = read_request();
//   while (!data.empty())
//     if (!file.write(data)) return fail("write: {}", os_error::last());
template<typename Derived, typename HandleT, HandleT Invalid>
class os_file_base {
public:
#pragma region Types

  using file_handle_t = HandleT;
  static constexpr file_handle_t invalid_file_handle = Invalid;

#pragma endregion
#pragma region Construction

  // Public so that `Derived` can expose it with `using base::base`; only
  // `Derived` may actually inherit from this class.
  // NOLINTBEGIN(bugprone-crtp-constructor-accessibility)

  // Adopt an existing handle, normalizing any alternate invalid sentinel to
  // `invalid_file_handle`. Defaults to an invalid (closed) file.
  explicit os_file_base(file_handle_t h = invalid_file_handle) noexcept
      : handle_{Derived::do_adopt(h)} {}

  os_file_base(const os_file_base&) = delete;
  os_file_base& operator=(const os_file_base&) = delete;

  os_file_base(os_file_base&& other) noexcept : handle_{other.release()} {}

  os_file_base& operator=(os_file_base&& other) noexcept {
    if (this != &other) {
      close();
      handle_ = other.release();
    }
    return *this;
  }

  // NOLINTEND(bugprone-crtp-constructor-accessibility)

  ~os_file_base() { close(); }

#pragma endregion
#pragma region Accessors

  // True if the handle is valid (i.e., the file is open).
  [[nodiscard]] bool is_open() const noexcept {
    return handle_ != invalid_file_handle;
  }

  [[nodiscard]] explicit operator bool() const noexcept { return is_open(); }
  [[nodiscard]] bool operator!() const noexcept { return !is_open(); }

  // Return the raw platform handle.
  [[nodiscard]] file_handle_t handle() const noexcept { return handle_; }
  [[nodiscard]] file_handle_t operator*() const noexcept { return handle_; }

#pragma endregion
#pragma region Close and release

  // Close the file. Idempotent. Returns true when the file was open and is
  // now closed, false if it could not be closed (likely because it already
  // was). Note that, on failure, the file is left in a closed state to avoid
  // potential reuse of a stale handle.
  bool close() noexcept {
    if (!is_open()) return false;
    const auto old_handle = handle_;
    handle_ = invalid_file_handle;
    return Derived::do_close(old_handle);
  }

  // Release ownership and return the handle without closing it.
  [[nodiscard]] file_handle_t release() noexcept {
    const auto h = handle_;
    handle_ = invalid_file_handle;
    return h;
  }

#pragma endregion
#pragma region Errors

  // Check whether the last error was a hard error (true) or a soft error
  // (false); see `os_error::is_hard_error`.
  //
  // Note that the last error is only meaningful immediately after a failing
  // OS call and may be overwritten by the next one.
  static bool is_hard_error(os_error err = os_error::last()) noexcept {
    return err.is_hard_error();
  }

#pragma endregion
#pragma region Data members
private:
  file_handle_t handle_ = invalid_file_handle;

#pragma endregion
};

#pragma endregion
}} // namespace corvid::filesys
