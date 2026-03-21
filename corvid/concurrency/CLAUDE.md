# CLAUDE.md

The `concurrency` module provides thread-safety primitives.

- `sync_lock` -- `synchronizer`, `lock`, `reverse_lock`, and
  `breakable_synchronizer` for the attestation locking idiom.
- `notifiable<T>` -- value guarded by a mutex and condition variable;
  supports predicate waiting, timeouts, `std::atomic<T>`, and
  `relaxed_atomic<T>`. For `wait_until_changed` / `wait_for_changed`, prefer
  the overloads that take an `expected_old` value captured via `get()` before
  spawning the notifying thread; the zero-argument forms capture the "before"
  value inside the lock and will hang if the thread has already notified by
  then.
- `tombstone_of<T>` / `tombstone` -- atomic value that can be set once to a
  final "dead" state; cannot be reverted.
- `timers` -- thread-safe priority-queue timer scheduler with one-shot and
  recurring events.
- `timing_wheel` / `timing_wheel_runner` -- single-level, O(1) schedule,
  100ms-precision timing wheel; callbacks own all metadata (IDs, targets,
  delivery channels); `timing_wheel_runner` drives the wheel from its own
  thread. Constructor requires `slot_count >= 2` and
  `tick_interval >= 500000ns`; throws `std::invalid_argument` otherwise.
- `jthread_stoppable_sleep` -- interruptible deadline sleep for `std::jthread`;
  workaround for the missing stop-token overload of
  `std::condition_variable_any::wait_until` in libc++.
- `relaxed_atomic<T>` -- thin wrapper around `std::atomic<T>` whose implicit
  conversion and assignment operator both use relaxed memory ordering. Use
  `operator->()` to access the underlying `std::atomic<T>` for stronger
  ordering when needed. Named aliases (`relaxed_atomic_bool`,
  `relaxed_atomic_int`, `relaxed_atomic_size_t`, etc.) mirror the
  `std::atomic_*` typedef family.
