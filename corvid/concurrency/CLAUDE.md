# CLAUDE.md

The `concurrency` module provides thread-safety primitives.

- `sync_lock` -- `synchronizer`, `lock`, `reverse_lock`, and
  `breakable_synchronizer` for the attestation locking idiom.
- `notifiable<T>` -- value guarded by a mutex and condition variable;
  supports predicate waiting, timeouts, and `std::atomic<T>`.
- `tombstone_of<T>` / `tombstone` -- atomic value that can be set once to a
  final "dead" state; cannot be reverted.
- `timers` -- thread-safe priority-queue timer scheduler with one-shot and
  recurring events.
