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
#include <cstdint>
#include <exception>
#include <type_traits>
#include <utility>

#include "../meta/maybe.h"

namespace corvid { inline namespace infra {

// Scope guards
//
// RAII helpers that run a callable on scope exit, either always
// (`scope_exit`), only on an exceptional exit (`scope_fail`), or only on a
// normal exit (`scope_success`).
//
//  These are placeholders for the `std::experimental::scope_exit`,
//  `std::experimental::scope_fail`, and `std::experimental::scope_success`,
//  which are not only experimental, but currently unavailable in libcpp.
//
// https://en.cppreference.com/cpp/experimental/scope_success.

namespace details {

#pragma region scope_kind

// Which scope-exit policy a `scope_guard` implements: run always, only on an
// exceptional exit, or only on a normal exit.
enum class scope_kind : std::uint8_t { exit, fail, success };

#pragma endregion
#pragma region scope_guard

// Combined implementation for the three scope guards, parameterized on the
// exit-function type `EF` and the policy `Kind`. The public names (scope_exit,
// scope_fail, scope_success) are thin derived classes that pin `Kind`, so all
// of the policy-dependent logic lives here in one place.
template<typename EF, scope_kind Kind>
class scope_guard {
#pragma region Policy

  static_assert(std::is_object_v<EF>,
      "scope guard requires EF to be an object type");
  static_assert(!std::is_reference_v<EF>,
      "scope guard requires EF to be stored by value");
  static_assert(std::is_invocable_v<EF&>,
      "scope guard requires EF to be invocable");
  static constexpr bool counts_exceptions_v = (Kind != scope_kind::exit);
  using exception_count_t = maybe_t<int, counts_exceptions_v>;

#pragma endregion
#pragma region Construction
public:
  template<typename Fn>
  requires(!std::is_same_v<std::remove_cvref_t<Fn>, scope_guard> &&
           std::is_constructible_v<EF, Fn>)
  explicit scope_guard(Fn&& fn) noexcept(
      std::is_nothrow_constructible_v<EF, Fn>)
      : exit_function_(std::forward<Fn>(fn)) {
    if constexpr (counts_exceptions_v)
      uncaught_on_entry_ = std::uncaught_exceptions();
  }

  scope_guard(const scope_guard&) = delete;
  scope_guard& operator=(const scope_guard&) = delete;
  scope_guard& operator=(scope_guard&&) = delete;

  scope_guard(scope_guard&& other) noexcept(
      std::is_nothrow_move_constructible_v<EF> ||
      std::is_nothrow_copy_constructible_v<EF>)
      : exit_function_(std::move_if_noexcept(other.exit_function_)),
        active_(std::exchange(other.active_, false)),
        uncaught_on_entry_(other.uncaught_on_entry_) {}

#pragma endregion
#pragma region Destruction

  // The `exit` and `fail` cases may run while an exception is unwinding, so a
  // throw there terminates. success runs only on the normal path, so it is
  // conditionally noexcept and lets a throwing exit function propagate.
  // NOLINTNEXTLINE(bugprone-exception-escape)
  ~scope_guard() noexcept(
      Kind != scope_kind::success || noexcept(std::declval<EF&>()())) {
    if (!active_) return;
    if constexpr (Kind == scope_kind::exit)
      exit_function_();
    else if constexpr (Kind == scope_kind::fail) {
      if (std::uncaught_exceptions() > uncaught_on_entry_) exit_function_();
    } else {
      if (std::uncaught_exceptions() <= uncaught_on_entry_) exit_function_();
    }
  }

#pragma endregion
#pragma region Accessors

  // Disarm the guard so the exit function will not run.
  void release() noexcept { active_ = false; }

#pragma endregion
#pragma region Data members
private:
  EF exit_function_;
  bool active_{true};
  [[no_unique_address]] exception_count_t uncaught_on_entry_;

#pragma endregion
};

#pragma endregion

} // namespace details

#pragma region scope_exit

// Always invokes on leaving current scope.
template<typename EF>
class [[nodiscard]]
scope_exit: public details::scope_guard<EF, details::scope_kind::exit> {
public:
  using details::scope_guard<EF, details::scope_kind::exit>::scope_guard;
};

template<typename EF>
scope_exit(EF) -> scope_exit<EF>;

#pragma endregion
#pragma region scope_fail

// Only invokes on leaving current scope via an exception.
template<typename EF>
class [[nodiscard]]
scope_fail: public details::scope_guard<EF, details::scope_kind::fail> {
public:
  using details::scope_guard<EF, details::scope_kind::fail>::scope_guard;
};

template<typename EF>
scope_fail(EF) -> scope_fail<EF>;

#pragma endregion
#pragma region scope_success

// Only invokes on leaving current scope normally.
//
// Note that the destructor is only conditionally noexcept.
template<typename EF>
class [[nodiscard]]
scope_success: public details::scope_guard<EF, details::scope_kind::success> {
public:
  using details::scope_guard<EF, details::scope_kind::success>::scope_guard;
};

template<typename EF>
scope_success(EF) -> scope_success<EF>;

#pragma endregion

}} // namespace corvid::infra
