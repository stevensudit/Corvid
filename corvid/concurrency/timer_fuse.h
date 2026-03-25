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
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

#include "timing_wheel.h"

namespace corvid { inline namespace concurrency {

// Copyable liveness token for a per-operation timeout on a shared resource.
//
// `T` is the resource type (e.g., a connection class), which must be managed
// via `std::shared_ptr` / `std::weak_ptr`.
//
// Each independently timed operation keeps a `std::atomic_uint64_t`
// sequencer in the resource's state. Arming a timeout does an atomic
// pre-increment of that sequencer and snapshots the new value as `target_`.
// A fuse triggers only when the sequencer still holds `target_` at fire time;
// any earlier fuse sees a mismatch and fizzles. Wraparound is harmless because
// equality is the only comparison performed.
//
// `timer_fuse<T>` is copyable and is captured by value into wheel and executor
// callbacks so that liveness checks work correctly across thread boundaries.
//
// Typical payload pattern (double-check across two threads):
//
//   // Fires on the wheel thread:
//   [](const timer_fuse<MyConn>& fuse) -> bool {
//     auto resource = fuse.get_if_armed();  // pre-check
//     if (!resource) return true;
//     // Deliver to the resource's event loop or executor.
//     return some_loop.post([fuse]() -> bool {
//       auto r = fuse.get_if_armed();  // definitive check
//       if (!r) return true;
//       // ... perform the timeout action on `r` ...
//       return true;
//     });
//   }
//
// The two checks are needed because `disarm` can race with the inner callback
// executing. The payload is responsible for obtaining the executor (e.g., via
// a method on `T` or a captured reference), since `timer_fuse` does not hold
// one.
template<typename T>
class timer_fuse {
public:
  using resource_t = T;
  using resource_ptr_t = std::shared_ptr<T>;
  using resource_weak_ptr_t = std::weak_ptr<T>;

  // Default-constructed fuse is permanently unarmed: `get_if_armed` always
  // returns `nullptr`. It exists only to be copied over.
  timer_fuse() = default;

  // Return the live resource if the fuse is still armed, `nullptr` otherwise.
  //
  // Fails if:
  //   - the resource has been destroyed (`std::weak_ptr` expired), or
  //   - the sequencer no longer holds `target_` (disarmed or re-armed).
  [[nodiscard]] resource_ptr_t get_if_armed() const {
    auto r = resource_.lock();
    if (!r) return nullptr;
    if (seq_->load(std::memory_order::relaxed) != target_) return nullptr;
    return r;
  }

  // Arm (or re-arm) a timeout on `seq` for `resource`. Schedules
  // `payload(fuse)` on `wheel` after `delay`. Atomically increments `seq` so
  // that any previously armed fuse on that sequencer fizzles.
  //
  // `payload` must be invocable as `bool(const timer_fuse<T>&)`.
  //
  // Returns false if `delay` exceeds the wheel's schedulable range; the
  // caller should use `timers` instead for delays above ~60 s.
  [[nodiscard]] static bool set_timeout(timing_wheel& wheel,
      std::atomic_uint64_t& seq, resource_weak_ptr_t resource_weak,
      timing_wheel::duration_t delay, auto&& payload) {
    static_assert(
        std::is_invocable_r_v<bool,
            const std::remove_reference_t<decltype(payload)>&,
            const timer_fuse<T>&>,
        "payload must be invocable with `const timer_fuse<T>&` and return "
        "`bool`");

    const auto target = ++seq;
    timer_fuse<T> fuse{std::move(resource_weak), seq, target};
    return wheel.schedule(
        [fuse, p = std::forward<decltype(payload)>(payload)]() -> bool {
          return p(fuse);
        },
        delay);
  }

  // Stale any pending callback on `seq` without scheduling a new one.
  static void disarm(std::atomic_uint64_t& seq) { ++seq; }

private:
  // Due to global treaties against war crimes, only `set_timeout` may
  // construct an armed fuse.
  timer_fuse(resource_weak_ptr_t resource, std::atomic_uint64_t& seq,
      uint64_t target) noexcept
      : resource_{std::move(resource)}, seq_{&seq}, target_{target} {}

  resource_weak_ptr_t resource_;
  std::atomic_uint64_t* seq_{};
  uint64_t target_{};
};

}} // namespace corvid::concurrency
