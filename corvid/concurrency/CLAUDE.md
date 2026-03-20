# CLAUDE.md

The `concurrency` module provides thread-safety primitives.

- `sync_lock` -- `synchronizer`, `lock`, `reverse_lock`, and
  `breakable_synchronizer` for the attestation locking idiom.
- `notifiable<T>` -- value guarded by a mutex and condition variable;
  supports predicate waiting, timeouts, and `std::atomic<T>`. For
  `wait_until_changed` / `wait_for_changed`, prefer the overloads that take an
  `expected_old` value captured via `get()` before spawning the notifying
  thread; the zero-argument forms capture the "before" value inside the lock
  and will hang if the thread has already notified by then.
- `tombstone_of<T>` / `tombstone` -- atomic value that can be set once to a
  final "dead" state; cannot be reverted.
- `timers` -- thread-safe priority-queue timer scheduler with one-shot and
  recurring events.
- `relaxed_atomic<std::atomic<T>>` / `relaxed_atomic_t<T>` -- thin wrapper
  around `std::atomic<T>` whose implicit conversion and assignment operator
  both default to relaxed memory ordering.
