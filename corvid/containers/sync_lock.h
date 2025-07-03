// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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
#include <atomic>
#include <mutex>

namespace corvid { inline namespace container { inline namespace sync_lock {

// Synchronization object for use with containers.
//
// Not copyable or moveable.
//
// TODO: Consider adding debug-only lock counts so that deadlocks throw.
class synchronizer {
public:
  void lock() const { mutex_.lock(); }
  void unlock() const { mutex_.unlock(); }

private:
  mutable std::mutex mutex_;
};

// Breakable synchronization object. Once the guarded resource is frozen, you
// call `disable` and the conversion to `synchronizer` returns `nullptr`, so
// that no actual locking is done anymore.
//
// If you don't want to allow someone outside the class to call `disable`, make
// this object private and expose it through a function that returns a `const
// synchronizer*`.
class breakable_synchronizer {
public:
  operator const synchronizer*() const { return sync_; };
  void disable() const { sync_ = nullptr; };
  bool is_disabled() const { return !sync_; }

private:
  synchronizer actual_sync_;
  mutable std::atomic<const synchronizer*> sync_ = &actual_sync_;
};

// Attestation of a lock on a sync object.
//
// This is not a drop-in replacement for `std::lock_guard` or
// `std::unique_lock`. Instead, it's intended to be used with the attestation
// idiom so as to allow nested calls to public methods. The alternative is
// either expensive recursive mutexes or shadowing each public method with a
// private one that lacks the lock on top. This method is as fast as the
// latter, without the code duplication.
//
// The way it works is that you add `const lock& attestation = {}` to the end
// of the method, and then call `attestation(sync)` at the top. The `sync` is
// the `synchronizer` member for that instance (which should be directly or
// indirectly public). A caller can reuse an attestation across multiple calls,
// maintaining a single lock across them all.
//
// Within a method that takes `attestation`, when calling other methods of the
// same instance, pass that `attestation` instead of allowing it to be
// defaulted. Note that if allow it to be defaulted, you'll deadlock. If your
// method doesn't access any data, it can skip the attestation sync call at
// top, just passing along the `attestation` without calling it.
//
// You can use a `breakable_synchronizer` if you want the ability to disable
// locking once the object is frozen.
//
// All methods are `const` and all members are `mutable` because thread
// safety is needed regardless of constness.
//
// class thread_safe_container {
//   synchronizer sync;
// public:
//   void do_something(int x, int y, const lock& attestation = {}) {
//     attestation(sync);
//     // Do something with x and y, but use the same lock.
//     do_something_else(x + 2, y - 2, attestation);
//     [...]
//
// Note again how, in the above case, the caller could make their own `lock`
// object and reuse it across multiple calls, maintaining a lock. They could
// even construct it on the instance's `sync` member.
class lock {
public:
  lock() = default;

  explicit lock(const synchronizer& sync) : sync_{&sync} { sync_->lock(); }
  explicit lock(const synchronizer* sync) : sync_{sync} {
    if (sync_) sync_->lock();
  }

  lock(const lock&) = delete;
  lock& operator=(const lock&) = delete;

  lock(lock&& r) : sync_(r.release()) {}
  lock& operator=(lock&& r) {
    sync_ = r.release();
    return *this;
  }

  ~lock() {
    if (sync_) sync_->unlock();
  }

  // Call this at top of method.
  void operator()(const synchronizer& sync) const {
    assert(!sync_ || sync_ == &sync);
    if (sync_) return;
    sync_ = &sync;
    sync_->lock();
  }
  void operator()(const synchronizer* sync) const {
    if (sync) (*this)(*sync);
  }

  const synchronizer* release() const {
    const auto old = sync_;
    sync_ = nullptr;
    return old;
  }

private:
  mutable const synchronizer* sync_{};
};

// TODO: Consider whether we need an `empty_t` trick to make it easier to
// specialize the synchronizer away.

}}} // namespace corvid::container::sync_lock
