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
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace corvid { inline namespace concurrency { inline namespace sync_lock {

#pragma region synchronizer

// Synchronization object for use with containers.
//
// Not copyable or moveable.
class synchronizer {
public:
  void lock() const { mutex_.lock(); }
  void unlock() const { mutex_.unlock(); }

private:
  mutable std::mutex mutex_;
};

#pragma endregion
#pragma region breakable_synchronizer

// Breakable synchronization object. Once the guarded resource is frozen, you
// call `disable` and the conversion to `synchronizer` returns `nullptr`, so
// that no actual locking is done anymore.
//
// The disable transition is a data handoff: a reader that observes `nullptr`
// skips the mutex entirely and reads the guarded data unlocked, so the null
// store must pair with the null load as release/acquire to publish every
// write made before freezing. The mutex cannot provide that ordering to a
// reader who never takes it, and relaxed ordering would not suffice.
//
// If you don't want to allow someone outside the class to call `disable`, make
// this object private and expose it through a function that returns a `const
// synchronizer*`.
class breakable_synchronizer final {
public:
  operator const synchronizer*() const noexcept {
    return sync_.load(std::memory_order_acquire);
  }
  void disable() const noexcept {
    sync_.store(nullptr, std::memory_order_release);
  }
  bool is_disabled() const noexcept {
    return !sync_.load(std::memory_order_acquire);
  }

private:
  synchronizer actual_sync_;
  mutable std::atomic<const synchronizer*> sync_ = &actual_sync_;
};

#pragma endregion
#pragma region reverse_lock

// Reversed lock. Unlocks on construction, relocks on destruction.
//
// This class is largely internal. The normal way to use it is to call
// `lock_reverse` on a `lock`.
class [[nodiscard]] reverse_lock final {
public:
#pragma region Construction

  explicit reverse_lock(const synchronizer* sync) : sync_{sync} {
    if (sync_) sync_->unlock();
  }

  reverse_lock(reverse_lock&& r) noexcept : sync_{r.release()} {}
  reverse_lock(const reverse_lock&) = delete;
  reverse_lock& operator=(const reverse_lock&) = delete;
  reverse_lock& operator=(reverse_lock&&) = delete;

  ~reverse_lock() {
    if (sync_) sync_->lock();
  }

#pragma endregion
#pragma region Operations

  [[nodiscard]] const synchronizer* release() const noexcept {
    const auto old = sync_;
    sync_ = nullptr;
    return old;
  }

  // Whether a `synchronizer` is associated.
  [[nodiscard]] explicit operator bool() const noexcept { return sync_; }

#pragma endregion
#pragma region Data members
private:
  mutable const synchronizer* sync_{};

#pragma endregion
};

#pragma endregion
#pragma region lock

// Attestation of a lock on a sync object.
//
// While this does create a scope within which a lock is held, it is not a
// drop-in replacement for `std::lock_guard` or `std::unique_lock`. Instead,
// it's intended to be used with the attestation idiom so as to allow nested
// calls to public methods.
//
// The alternative is to either use expensive recursive mutexes or to
// rigorously shadow each public method with a private one that lacks the lock.
// This idiom is as fast as the latter, without the code duplication and
// complexity.
//
// The way it works is that you add `const lock& attestation = {}` to the end
// of the method, and then call `attestation(sync)` at the top. The `sync` is
// the `synchronizer` member for that instance (which should be directly or
// indirectly public). A caller can reuse an attestation across multiple calls,
// maintaining a single lock across them all.
//
// Within a method that takes `attestation`, when you call other methods of the
// same instance, then you pass that `attestation` instead of allowing it to be
// defaulted. Note that if you allow it to be defaulted, you'll deadlock. If
// your method doesn't access any data, it can skip the attestation sync call
// at top, just passing along the `attestation` without calling it.
//
// There is an additional pattern where you pass in a `lock&` that is either
// already associated with the synchronizer, or will become associated.
//
// You can use a `breakable_synchronizer` if you want the ability to disable
// locking once the object is frozen. And if you need to reverse the lock
// within a scope, use `lock_reverse`.
//
// All methods are `const` and all members are `mutable` because thread
// safety is needed regardless of constness.
//
// This example is kept compiling and passing as the `LedeExample` case in
// "sync_lock_test.cpp"; change the two together.
//
//   class thread_safe_container {
//   public:
//     synchronizer sync;
//
//     void do_something(int x, int y, const lock& attestation = {}) {
//       attestation(sync);
//       // Work with x and y under the lock, then call a sibling method,
//       // passing the same attestation instead of letting it default.
//       do_something_else(x + 2, y - 2, attestation);
//     }
//
//     void do_something_else(int x, int y, const lock& attestation = {}) {
//       attestation(sync);
//       total_ += x + y;
//     }
//
//     int total(const lock& attestation = {}) const {
//       attestation(sync);
//       return total_;
//     }
//
//   private:
//     int total_{};
//   };
//
// Note again how, in the above case, the caller could make a named `lock`
// object, constructing it on the instance's public `sync` member. This lets
// them not only reuse the `lock` across multiple calls, but also hold a
// single lock across all of them.
class [[nodiscard]] lock final {
public:
#pragma region Construction

  // Default construct with no associated synchronizer. The first call to
  // `operator()` will associate it with a synchronizer and lock it.
  constexpr lock() noexcept = default;

  // Construct with an associated synchronizer and lock it.
  explicit lock(const synchronizer& sync) : sync_{&sync} { sync_->lock(); }
  explicit lock(const synchronizer* sync) : sync_{sync} {
    if (sync_) sync_->lock();
  }

  lock(const lock&) = delete;
  lock& operator=(const lock&) = delete;

  lock(lock&& r) noexcept : sync_{r.release()} {}
  lock& operator=(lock&& r) noexcept {
    if (sync_ == r.sync_) return *this;
    if (sync_) sync_->unlock();
    sync_ = r.release();
    return *this;
  }

  ~lock() {
    if (sync_) sync_->unlock();
  }

#pragma endregion
#pragma region Operations

  // Whether a `synchronizer` is associated.
  [[nodiscard]] explicit operator bool() const noexcept { return sync_; }

  // Call this at top of method to acquire a lock on the synchronizer. Performs
  // a no-op if already locked, but throws if locks are mixed.
  void operator()(const synchronizer& sync) const {
    if (sync_ && (sync_ != &sync)) throw std::logic_error{"cannot mix locks"};
    // Already locked, so no-op.
    if (sync_) return;

    // Bind and lock.
    sync_ = &sync;
    sync_->lock();
  }
  void operator()(const synchronizer* sync) const {
    if (sync) (*this)(*sync);
  }

  // Release ownership of the synchronizer, but does not unlock it. The caller
  // now owns the existing lock.
  [[nodiscard]] const synchronizer* release() const noexcept {
    const auto old = sync_;
    sync_ = nullptr;
    return old;
  }

  // Reverse the lock, so that it unlocks the synchronizer when destroyed.
  [[nodiscard]] reverse_lock lock_reverse() const noexcept {
    return reverse_lock{sync_};
  }

#pragma endregion
#pragma region Data members
private:
  mutable const synchronizer* sync_{};

#pragma endregion
};

#pragma endregion
}}} // namespace corvid::concurrency::sync_lock
