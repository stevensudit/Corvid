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
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace corvid { inline namespace meta {

#pragma region address_forwarder

// CRTP base for objects that need to remain addressable after being moved out
// of visibility, such as by being captured into a lambda.
//
// A child instance maintains a non-owning `Derived**` that is updated on each
// move of that instance to track its new location.
//
// While it can technically be used on its own, it is intended for use with the
// matching `forwarded_address` RAII handle, below. This object registers
// itself on construction, unregisters on destruction, and reads as null once
// the `forwarded_address` dies, so neither side can dangle. Note that there is
// mutual awareness, but no ownership.
template<typename Derived>
class address_forwarder {
public:
  // Tag required for direct access. This could have been made private, but the
  // back door has been left open in case it's needed.
  enum class raw_forwarding : std::uint8_t { allow };

#pragma region Accessors

  // Access to the current forwarding address, or null when the handle has died
  // or been displaced.
  [[nodiscard]] Derived** forwarding_address_ptr() const noexcept {
    return forwarding_address_;
  }

  // Reference to the raw tracking slot.
  //
  // Set to `&ptr` to have `ptr` follow this object across moves; assign
  // `nullptr` to stop tracking.
  //
  // Prefer `forwarded_address`, which manages the registration safely.
  [[nodiscard]] Derived**& forwarding_address_ptr(raw_forwarding) noexcept {
    return forwarding_address_;
  }

#pragma endregion
#pragma region Construction

  // The clang-tidy warning is generally good advice, but following it would
  // block compilation, so maybe it's not so good here.
  //
  // NOLINTBEGIN(bugprone-crtp-constructor-accessibility)

  address_forwarder() = default;

protected:
  // Move, but forward the address.
  address_forwarder(address_forwarder&& o) noexcept
      : forwarding_address_{std::exchange(o.forwarding_address_, nullptr)} {
    update_registered();
  }

  // Move assign, but forward the address.
  address_forwarder& operator=(address_forwarder&& o) noexcept {
    if (this == &o) return *this;
    reset(raw_forwarding::allow,
        std::exchange(o.forwarding_address_, nullptr));
    return update_registered();
  }

  // Throws on copy so that it can be used in `std::function`, which requires
  // copyability even if you always move.
  address_forwarder(const address_forwarder&) {
    throw std::logic_error{"address_forwarder is not copyable"};
  }
  address_forwarder& operator=(const address_forwarder& o) {
    if (this == &o) return *this;
    throw std::logic_error{"address_forwarder is not copyable"};
  }
  // NOLINTEND(bugprone-crtp-constructor-accessibility)

  // Update registered address to point at the current location.
  address_forwarder& update_registered() {
    if (forwarding_address_)
      *forwarding_address_ = static_cast<Derived*>(this);
    return *this;
  }

public:
  ~address_forwarder() { reset(); }

  // Unregister from the handle, if any, and go null.
  void reset() noexcept { reset(raw_forwarding::allow, nullptr); }

  // Unregister from the handle, if any, and switch to new forwarding address.
  //
  // Prefer `forwarded_address`, which manages the registration safely.
  void reset(raw_forwarding, Derived** forwarding_address) noexcept {
    if (forwarding_address_) *forwarding_address_ = nullptr;
    forwarding_address_ = forwarding_address;
  }

#pragma endregion
#pragma region Data members
private:
  Derived** forwarding_address_{};

#pragma endregion
};

#pragma endregion
#pragma region AddressForwarder

template<typename T>
concept AddressForwarder = std::derived_from<std::remove_cvref_t<T>,
    address_forwarder<std::remove_cvref_t<T>>>;

#pragma endregion
#pragma region forwarded_address

// RAII handle that tracks an `address_forwarder` across moves.
//
// Construction registers the handle with the forwarder. The forwarder's moves
// then update the handle to point at the current location of the forwarder,
// and the forwarder's destruction nulls the handle.
//
// Destroying, resetting, or reassigning the handle unregisters it. The two
// ends repair each other, so neither can dangle; this is the safe front door
// to address forwarding, with the raw `forwarding_address_ptr` protocol behind
// it for whatever a scoped handle cannot express.
//
// Registration is exclusive, and the last one wins: constructing a handle
// for an already-tracked forwarder displaces the previous handle, which reads
// as null from then on, just like a handle whose forwarder has died.
//
// Usage:
//   class Foo : public address_forwarder<Foo> { ... };
//
//   Foo f{42};
//   forwarded_address fa{f};
//   CHECK(fa.get() == &f);
//   Foo g{std::move(f)};
//   CHECK(fa.get() == &g);
//  // f dies; fa goes null.
template<AddressForwarder Derived>
class forwarded_address {
public:
#pragma region Construction

  forwarded_address() = default;

  // Register with `forwarder`, displacing any previous handle.
  explicit forwarded_address(Derived& forwarder) noexcept
      : forwarder_{&forwarder} {
    update_registration();
  }

  forwarded_address(const forwarded_address&) = delete;
  forwarded_address& operator=(const forwarded_address&) = delete;

  // Move re-registers the tracking at the new location, leaving RHS null.
  forwarded_address(forwarded_address&& o) noexcept
      : forwarder_{std::exchange(o.forwarder_, nullptr)} {
    update_registration();
  }

  forwarded_address& operator=(forwarded_address&& o) noexcept {
    if (this == &o) return *this;
    reset();
    forwarder_ = std::exchange(o.forwarder_, nullptr);
    return update_registration();
  }

  ~forwarded_address() { reset(); }

  // Unregister from the target, if any, and go null.
  void reset() noexcept {
    if (forwarder_) forwarder_->reset(Derived::raw_forwarding::allow, nullptr);
  }

#pragma endregion
#pragma region Accessors

  // The forwarder's current address, or null when the forwarder has died, the
  // handle was displaced by a newer one, or the handle was reset or moved
  // from.
  [[nodiscard]] Derived* get() const noexcept { return forwarder_; }
  [[nodiscard]] Derived* operator->() const noexcept { return forwarder_; }
  [[nodiscard]] Derived& operator*() const noexcept { return *forwarder_; }
  [[nodiscard]] explicit operator bool() const noexcept { return forwarder_; }

#pragma endregion
#pragma region Data members
private:
  Derived* forwarder_{};

  forwarded_address& update_registration() {
    if (forwarder_)
      forwarder_->reset(Derived::raw_forwarding::allow, &forwarder_);
    return *this;
  }

#pragma endregion
};

#pragma endregion
}} // namespace corvid::meta
