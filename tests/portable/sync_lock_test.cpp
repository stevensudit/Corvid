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

#include <stdexcept>
#include <thread>
#include <utility>

// The umbrella, not just sync_lock.h: on Windows this doubles as a compile
// pin that the Linux-only dispatcher include stays properly guarded.
#include "corvid/concurrency.h"

#include "catch2_main.h"

using namespace corvid::concurrency;

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region LedeExample

namespace {
// The container from the `lock` class comment's example. This test keeps the
// code sample in "sync_lock.h" compiling and passing; change the two
// together.
class thread_safe_container {
public:
  synchronizer sync;

  void do_something(int x, int y, const lock& attestation = {}) {
    attestation(sync);
    // Work with x and y under the lock, then call a sibling method,
    // passing the same attestation instead of letting it default.
    do_something_else(x + 2, y - 2, attestation);
  }

  void do_something_else(int x, int y, const lock& attestation = {}) {
    attestation(sync);
    total_ += x + y;
  }

  int total(const lock& attestation = {}) const {
    attestation(sync);
    return total_;
  }

private:
  int total_{};
};
} // namespace

TEST_CASE("LedeExample", "[SyncLock]") {
  thread_safe_container c;

  // Defaulted attestation: each outer call takes its own lock; the nested
  // call reuses it instead of deadlocking.
  c.do_something(1, 2);
  CHECK(c.total() == 3);

  // A caller-held lock spans multiple calls.
  lock att{c.sync};
  c.do_something(10, 20, att);
  CHECK(c.total(att) == 33);
}

#pragma endregion
#pragma region LockAttestation

TEST_CASE("LockAttestation", "[SyncLock]") {
  synchronizer sync;

  // Default-constructed lock is unbound; the first attestation call binds
  // and locks, and repeating it is a no-op.
  lock att;
  CHECK_FALSE(static_cast<bool>(att));
  att(sync);
  CHECK(static_cast<bool>(att));
  att(sync);
  CHECK(static_cast<bool>(att));

  // Attesting a different synchronizer under the same lock throws.
  synchronizer other;
  CHECK_THROWS_AS(att(other), std::logic_error);

  // The failed mix leaves the original association intact.
  CHECK(static_cast<bool>(att));
}

#pragma endregion
#pragma region LockMoveAndRelease

TEST_CASE("LockMoveAndRelease", "[SyncLock]") {
  synchronizer sync;

  lock att{sync};
  lock moved{std::move(att)};
  // Moved-from lock is defined unbound.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  CHECK_FALSE(static_cast<bool>(att));
  CHECK(static_cast<bool>(moved));

  lock target;
  target = std::move(moved);
  // Moved-from lock is defined unbound.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  CHECK_FALSE(static_cast<bool>(moved));
  CHECK(static_cast<bool>(target));

  // `release` detaches ownership without unlocking; the caller now owns the
  // held lock and must unlock through the returned synchronizer.
  const auto* released = target.release();
  CHECK(released == &sync);
  CHECK_FALSE(static_cast<bool>(target));
  released->unlock();
}

#pragma endregion
#pragma region ReverseLock

TEST_CASE("ReverseLock", "[SyncLock]") {
  synchronizer sync;
  lock att{sync};

  {
    auto rev = att.lock_reverse();
    CHECK(static_cast<bool>(rev));

    // The reverse genuinely unlocked: another thread can take the lock and
    // finish. Join-based, no timing assumptions.
    bool acquired = false;
    std::jthread probe{[&] {
      lock inner{sync};
      acquired = true;
    }};
    probe.join();
    CHECK(acquired);
  }

  // `rev` relocked on destruction; `att` still attests and unlocks at scope
  // end.
  CHECK(static_cast<bool>(att));
}

#pragma endregion
#pragma region ReverseLockMoveAndRelease

TEST_CASE("ReverseLockMoveAndRelease", "[SyncLock]") {
  synchronizer sync;

  // Reversing an unbound lock is a clean no-op.
  {
    lock unbound;
    auto rev = unbound.lock_reverse();
    CHECK_FALSE(static_cast<bool>(rev));
  }

  lock att{sync};
  auto rev = att.lock_reverse();
  auto moved = std::move(rev);
  // Moved-from reverse_lock is defined empty.
  // NOLINTNEXTLINE(bugprone-use-after-move)
  CHECK_FALSE(static_cast<bool>(rev));
  CHECK(static_cast<bool>(moved));

  // `release` detaches, so nobody relocks on destruction; restore the lock
  // by hand to keep `att`'s eventual unlock balanced.
  const auto* released = moved.release();
  CHECK(released == &sync);
  released->lock();
}

#pragma endregion
#pragma region BreakableSynchronizer

TEST_CASE("BreakableSynchronizer", "[SyncLock]") {
  breakable_synchronizer bs;

  // Live: converts to a real synchronizer, which a lock can take.
  CHECK_FALSE(bs.is_disabled());
  const synchronizer* live = bs;
  CHECK(live);
  {
    lock att{live};
    CHECK(static_cast<bool>(att));
  }

  // Disabled: converts to null, and locking through it is a clean no-op.
  bs.disable();
  CHECK(bs.is_disabled());
  const synchronizer* frozen = bs;
  CHECK_FALSE(frozen);
  lock att{frozen};
  CHECK_FALSE(static_cast<bool>(att));
  lock late;
  late(frozen);
  CHECK_FALSE(static_cast<bool>(late));
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
