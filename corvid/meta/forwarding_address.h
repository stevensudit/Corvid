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
#include <concepts>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace corvid { inline namespace meta {

// CRTP base for objects that need to remain addressable after being moved out
// of visibility, such as by being captured into a `std::function` and/or a
// lambda. Maintains a non-owning `Derived**` that is updated on each move to
// track the new location, and nulled on destruction to prevent wild writes.
//
// It is your responsibility to clear the forwarding address of the
// currently-active instance before what it points to leaves scope. Otherwise,
// it will do a wild write on destruction or move. Also note that the pointer
// is not thread-safe.
//
// Warning: While this class exhibits well-defined behavior and can be used
// safely and correctly, it is not for the faint of heart. If you need it, you
// need it. Otherwise, keep your distance.
//
// Usage:
//   class Foo : public address_forwarder<Foo> { ... };
//
//   Foo* ptr = nullptr;
//   Foo f;
//   f.forwarding_address() = &ptr;  // ptr tracks f
//   auto g = std::move(f);          // ptr now tracks g
//   g.forwarding_address() = nullptr; // clear once stable
template<typename Derived>
class address_forwarder {
public:
  // Reference to the external tracking pointer. Set to `&ptr` to have `ptr`
  // follow this object across moves; assign `nullptr` to stop tracking.
  [[nodiscard]] Derived**& forwarding_address() noexcept {
    return forwarding_address_;
  }

  // The clang-tidy warning is generally good advice, but it also blocks
  // compilation.
  // NOLINTNEXTLINE(bugprone-crtp-constructor-accessibility)
  address_forwarder() = default;

protected:
  // NOLINTBEGIN(bugprone-crtp-constructor-accessibility)

  address_forwarder(address_forwarder&& o) noexcept
      : forwarding_address_{std::exchange(o.forwarding_address_, nullptr)} {
    if (forwarding_address_)
      *forwarding_address_ = static_cast<Derived*>(this);
  }

  address_forwarder& operator=(address_forwarder&& o) noexcept {
    if (this != &o) {
      if (forwarding_address_) *forwarding_address_ = nullptr;
      forwarding_address_ = std::exchange(o.forwarding_address_, nullptr);
      if (forwarding_address_)
        *forwarding_address_ = static_cast<Derived*>(this);
    }
    return *this;
  }

  address_forwarder(const address_forwarder&) {
    throw std::logic_error{"address_forwarder is not copyable"};
  }
  address_forwarder& operator=(const address_forwarder&) = default;

public:
  ~address_forwarder() {
    if (forwarding_address_) *forwarding_address_ = nullptr;
  }

  address_forwarder&& as_base_move() noexcept { return std::move(*this); }

  // NOLINTEND(bugprone-crtp-constructor-accessibility)
private:
  Derived** forwarding_address_{};
};

template<typename T>
concept AddressForwarder = std::derived_from<std::remove_cvref_t<T>,
    address_forwarder<std::remove_cvref_t<T>>>;

}} // namespace corvid::meta
