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
- `owner_thread_dispatcher` -- cross-thread dispatch primitive: post a
  callback (in stored form) from any thread, run it on the bound owner
  thread. Provides `post`, `execute_or_post`, `post_and_wait`, and
  `is_loop_thread`. Owns the wake `event_fd` exposed via `wake_fd()`. The
  instance must be constructed and destructed on the owner thread, and only
  one such instance can live on a given thread. Used as the base for I/O
  loops (`iou_basic_loop`, `epoll_loop`), which register the wake fd with
  their poller so posts interrupt blocked waits.
- `tombstone_of<T>` / `tombstone` -- atomic value that can be set once to a
  final "dead" state; cannot be reverted.
- `timers` -- thread-safe priority-queue timer scheduler with one-shot and
  recurring events.
- `timer_fuse<T>` -- copyable liveness token for per-operation timeouts on a
  `shared_ptr`-managed resource. Each distinct timeout keeps one
  `std::atomic_uint64_t` sequencer in the resource's state; arming a fuse
  pre-increments it and snapshots the new value as the trigger target. Any
  earlier fuse fizzles on mismatch. `get_if_armed` returns the live resource or
  nullptr. `set_timeout` schedules a payload on a `timing_wheel`; `disarm`
  increments without scheduling. Payload signature: `bool(const timer_fuse<T>&)`.
- `timing_wheel` / `timing_wheel_runner` -- single-level, O(1) schedule,
  100ms-precision timing wheel; callbacks own all metadata (IDs, targets,
  delivery channels); `timing_wheel_runner` drives the wheel from its own
  thread. Constructor requires `slot_count >= 2` and
  `tick_interval >= 500000ns`; throws `std::invalid_argument` otherwise.
- `timeout_sweeper` -- min-heap of (`expiration`, `callback`) pairs, swept
  by an external driver that calls `tick(now)` periodically. Each callback
  receives the expiration it was registered at and returns either zero
  (drop the entry) or a new expiration (rearm at that time). No
  cancellation API: a callback self-cancels by returning zero on a later
  invocation. `schedule` is thread-safe and re-entrant from inside a
  callback; `tick` is intended to be called from a single driver thread.
  Callbacks fire outside the heap lock so other threads may `schedule`
  concurrently. Common types and the `paused_expiration` sentinel live in
  `timeout_sweeper_base`. Compared to `timing_wheel`, it trades O(1)
  schedule for unbounded delay range and per-entry rearm logic chosen by
  the callback; compared to `timers`, a much smaller API surface (just
  register, tick, return-value contract).
- `idle_timeout` -- per-direction idle-timeout helper bound to a
  `timeout_sweeper`. Templated on `Owner` (must inherit from
  `std::enable_shared_from_this`) and `Sweeper`. The sweeper callback
  captures a `weak_ptr` aliased to the owner, so the owner can be
  destroyed freely; the next sweep then drops the entry. The cancel
  action runs through a one-shot atomic slot, so the sweeper-driven fire
  and a manual `expire` cannot both invoke it; `reset_expiration` rearms
  the slot. Public API: `configure`, `postpone`, `start`, `stop`, `pause`,
  `set_mode` (`stopped` / `paused` / `running`), `get_mode`, `expire`,
  `reset_expiration`, plus read-only accessors.
- `jthread_stoppable_sleep` -- interruptible deadline sleep for `std::jthread`;
  workaround for the missing stop-token overload of
  `std::condition_variable_any::wait_until` in libc++.
- `relaxed_atomic<T>` -- thin wrapper around `std::atomic<T>` whose implicit
  conversion and assignment operator both use relaxed memory ordering. Use
  `operator->()` to access the underlying `std::atomic<T>` for stronger
  ordering when needed. Named aliases (`relaxed_atomic_bool`,
  `relaxed_atomic_int`, `relaxed_atomic_size_t`, etc.) mirror the
  `std::atomic_*` typedef family.
