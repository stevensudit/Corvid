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
#include "containers_shared.h"

#include <type_traits>
#include <utility>

namespace corvid { inline namespace container { inline namespace scope_guards {

// RAII helper that invokes a callable when leaving the current scope.
//
// Placeholder for
// https://www.en.cppreference.com/w/cpp/experimental/scope_exit.html.
template<typename EF>
class [[nodiscard]] scope_exit {
public:
  static_assert(std::is_object_v<EF>,
      "scope_exit requires EF to be an object type");
  static_assert(!std::is_reference_v<EF>,
      "scope_exit requires EF to be stored by value");
  static_assert(std::is_invocable_v<EF&>,
      "scope_exit requires EF to be invocable");

  template<typename Fn>
    requires (!std::is_same_v<std::remove_cvref_t<Fn>, scope_exit> &&
              std::is_constructible_v<EF, Fn>)
  explicit scope_exit(Fn&& fn) noexcept(
      std::is_nothrow_constructible_v<EF, Fn>)
      : exit_function_(std::forward<Fn>(fn)) {}

  ~scope_exit() noexcept(noexcept(std::declval<EF&>()())) {
    if (active_) exit_function_();
  }

  scope_exit(const scope_exit&) = delete;
  scope_exit& operator=(const scope_exit&) = delete;
  scope_exit& operator=(scope_exit&&) = delete;

  scope_exit(scope_exit&& other) noexcept(
      std::is_nothrow_move_constructible_v<EF> ||
      std::is_nothrow_copy_constructible_v<EF>)
      : exit_function_(std::move_if_noexcept(other.exit_function_)),
        active_(std::exchange(other.active_, false)) {}

  void release() noexcept { active_ = false; }

private:
  EF exit_function_;
  bool active_{true};
};

template<typename EF>
[[nodiscard]] auto make_scope_exit(EF&& fn) noexcept(
    noexcept(scope_exit<std::remove_cvref_t<EF>>(std::forward<EF>(fn)))) {
  return scope_exit<std::remove_cvref_t<EF>>(std::forward<EF>(fn));
}

template<typename EF>
scope_exit(EF) -> scope_exit<EF>;

}}} // namespace corvid::container::scope_guards
