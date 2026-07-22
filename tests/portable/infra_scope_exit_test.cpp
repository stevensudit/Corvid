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

#include "corvid/infra/scope_exit.h"

#include "catch2_main.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

using namespace corvid;

// The exit and fail kinds call their source directly if storing it throws, so
// a source that merely converts to the stored callable (a null `std::function`
// here) is constrained away for them; the success kind never makes that call
// and still accepts it.
static_assert(!std::is_constructible_v<scope_exit<std::function<void()>>,
    std::nullptr_t>);
static_assert(!std::is_constructible_v<scope_fail<std::function<void()>>,
    std::nullptr_t>);
static_assert(std::is_constructible_v<scope_success<std::function<void()>>,
    std::nullptr_t>);

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region ScopeExit_Basic

TEST_CASE("Basic", "[ScopeExit]") {
  if (true) {
    bool exited = false;
    {
      scope_exit guard{[&]() noexcept { exited = true; }};
      CHECK_FALSE(exited);
    }
    CHECK(exited);
  }
  if (true) {
    int value = 0;
    {
      auto guard = scope_exit{[&]() noexcept { value = 42; }};
      (void)guard;
      CHECK(value == 0);
    }
    CHECK(value == 42);
  }
  if (true) {
    bool exited = false;
    {
      auto guard = scope_exit{[&]() noexcept { exited = true; }};
      guard.release();
    }
    CHECK_FALSE(exited);
  }
  if (true) {
    int calls = 0;
    {
      auto guard1 = scope_exit{[&]() noexcept { ++calls; }};
      {
        auto guard2 = std::move(guard1);
        CHECK(calls == 0);
        (void)guard2;
      }
      CHECK(calls == 1);
    }
    CHECK(calls == 1);
  }
  if (true) {
    int value = 0;
    {
      auto payload = std::make_unique<int>(7);
      auto guard = scope_exit{[owned = std::move(payload), &value]() noexcept {
        value = *owned;
      }};
      CHECK_FALSE(payload);
      (void)guard;
    }
    CHECK(value == 7);
  }
}
#pragma endregion
#pragma region ScopeFail_Basic

TEST_CASE("Basic", "[ScopeFail]") {
  // Normal exit does not run the exit function.
  if (true) {
    bool failed = false;
    {
      scope_fail guard{[&]() noexcept { failed = true; }};
      CHECK_FALSE(failed);
    }
    CHECK_FALSE(failed);
  }
  // Exceptional exit runs the exit function.
  if (true) {
    bool failed = false;
    try {
      scope_fail guard{[&]() noexcept { failed = true; }};
      throw std::runtime_error("boom");
    }
    catch (const std::runtime_error&) {
      (void)failed;
    }
    CHECK(failed);
  }
  // A guard constructed while an exception is already in flight runs only if a
  // further exception unwinds it, not on the pre-existing one.
  if (true) {
    int calls = 0;
    try {
      auto outer = scope_exit{[&]() noexcept {
        // Unwinding the outer exception: a scope_fail created here sees no
        // *new* exception, so it must not fire on normal destruction.
        scope_fail inner{[&]() noexcept { ++calls; }};
      }};
      (void)outer;
      throw std::runtime_error("boom");
    }
    catch (const std::runtime_error&) {
      (void)calls;
    }
    CHECK(calls == 0);
  }
  // release() disarms it even on the failure path.
  if (true) {
    bool failed = false;
    try {
      scope_fail guard{[&]() noexcept { failed = true; }};
      guard.release();
      throw std::runtime_error("boom");
    }
    catch (const std::runtime_error&) {
      (void)failed;
    }
    CHECK_FALSE(failed);
  }
  // Moving transfers ownership; the moved-from guard is inert.
  if (true) {
    int calls = 0;
    try {
      auto guard1 = scope_fail{[&]() noexcept { ++calls; }};
      auto guard2 = std::move(guard1);
      (void)guard2;
      throw std::runtime_error("boom");
    }
    catch (const std::runtime_error&) {
      (void)calls;
    }
    CHECK(calls == 1);
  }
}
#pragma endregion
#pragma region ScopeSuccess_Basic

TEST_CASE("Basic", "[ScopeSuccess]") {
  // Normal exit runs the exit function.
  if (true) {
    bool succeeded = false;
    {
      scope_success guard{[&]() noexcept { succeeded = true; }};
      CHECK_FALSE(succeeded);
    }
    CHECK(succeeded);
  }
  // Exceptional exit does not run the exit function.
  if (true) {
    bool succeeded = false;
    try {
      scope_success guard{[&]() noexcept { succeeded = true; }};
      throw std::runtime_error("boom");
    }
    catch (const std::runtime_error&) {
      (void)succeeded;
    }
    CHECK_FALSE(succeeded);
  }
  // release() disarms it on the success path.
  if (true) {
    bool succeeded = false;
    {
      auto guard = scope_success{[&]() noexcept { succeeded = true; }};
      guard.release();
    }
    CHECK_FALSE(succeeded);
  }
  // Moving transfers ownership; the moved-from guard is inert.
  if (true) {
    int calls = 0;
    {
      auto guard1 = scope_success{[&]() noexcept { ++calls; }};
      auto guard2 = std::move(guard1);
      (void)guard2;
    }
    CHECK(calls == 1);
  }
  // On the success path, a throwing exit function propagates rather than
  // terminating: this is what distinguishes scope_success from the others.
  if (true) {
    auto fire = [] {
      scope_success guard{[] { throw std::runtime_error("flush failed"); }};
    };
    CHECK_THROWS_AS(fire(), std::runtime_error);
  }
}
#pragma endregion
#pragma region ThrowingConstruction

namespace {
// Callable whose copy constructor throws, exercising a guard whose
// construction fails.
struct throwing_copy_fn {
  int& calls;
  explicit throwing_copy_fn(int& c) noexcept : calls{c} {}
  throwing_copy_fn(const throwing_copy_fn& other) : calls{other.calls} {
    throw std::runtime_error("copy failed");
  }
  void operator()() const noexcept { ++calls; }
};
} // namespace

TEST_CASE("Throwing construction", "[ScopeExit][ScopeFail][ScopeSuccess]") {
  // Matching `std::experimental`, when storing the callable throws,
  // `scope_exit` and `scope_fail` still invoke it, so the cleanup is not lost.
  // `scope_success` does not, since the scope did not succeed.
  if (true) {
    int calls = 0;
    throwing_copy_fn fn{calls};
    CHECK_THROWS_AS(scope_exit{fn}, std::runtime_error);
    CHECK(calls == 1);
  }
  if (true) {
    int calls = 0;
    throwing_copy_fn fn{calls};
    CHECK_THROWS_AS(scope_fail{fn}, std::runtime_error);
    CHECK(calls == 1);
  }
  if (true) {
    int calls = 0;
    throwing_copy_fn fn{calls};
    CHECK_THROWS_AS(scope_success{fn}, std::runtime_error);
    CHECK(calls == 0);
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
