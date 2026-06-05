# Error-Handling Policy (C++23)

House style for this library. Each directive is a **Rule** (no known exceptions)
or a **Preference** (a default, override only with a stated reason). Reasons are
given so cases not covered here can be judged on principle.

## Foundational assumption: the process can die at any instant

The OOM killer, `SIGKILL`, power loss, and hardware faults do not unwind the
stack or run destructors. Therefore:

- **Rule:** Never rely on orderly destruction or stack unwinding to protect
  persistent or shared state. Anything that outlives the process (files, shared
  memory, on-disk structures, peers) must be designed to survive abrupt death
  (atomic writes, journaling, idempotent operations).
- **Rule:** Given the above, calling `std::terminate` on a detected bad state is
  always acceptable. Continuing in an unknown or invalid state is never
  acceptable. Keeping the process alive is not an obligation; correctness is.
- **Rule:** A branch reachable only via contract violation (kernel bug, memory
  corruption, a broken invariant) must not try to recover state. A leak or no-op
  is safer than a recovery path that assumes the violated contract still held.

## Classify every function by how it fails

1. **Always succeeds** (e.g. `empty`). Mark `noexcept`.
2. **Succeeds if its precondition holds** (e.g. `front` requires non-empty).
   Narrow contract: assert the precondition, treat violation as UB. Should
   generally **not** be marked `noexcept` (Lakos rule: a checked build may want
   the precondition check to throw).
3. **Can fail under normal conditions** (lookup misses, parse fails, most I/O).
   Not exceptional: signal it through the return value (see below).
4. **Can fail unrecoverably** (allocation and the like). Throw.

## `noexcept`

- A `noexcept` function is trivially exception-safe from the caller's side: a
  violation terminates, which is acceptable per the foundational assumption.
  `noexcept` is **not** a substitute for making durable state crash-safe: that
  obligation exists regardless of how the function is marked.
- **Rule:** Mark a function `noexcept` only when it genuinely cannot throw,
  including transitively. A C-style function that reports failure through a
  return code and cannot throw is `noexcept`.
- **Preference:** Generally, do not mark narrow-contract functions
  `noexcept` (category 2).
- **Rule:** Mark move constructor and move assignment `noexcept` when
  they are. A throwing move makes `std::vector` growth (`move_if_noexcept`)
  fall back to  copies and costs move-only types the strong guarantee. A
  precondition on the moved-from source is **not** sufficient reason to
  drop `noexcept`.
- **Rule:** Destructors are implicitly `noexcept`; never let one throw.

## Return values: signaling recoverable failure

- **Preference:** Default failure-prone calls (I/O and anything whose far end can
  vanish) to `bool` or a similar value, marked `[[nodiscard]]`. The caller must
  consciously decide; the usual failure is to abandon the operation and return
  a failure in turn.  Void it out explicitly only when failure genuinely changes
  nothing (e.g. an idempotent `close` during cleanup); the explicit `void`
  documents that choice.
- **Preference (house style):** Avoid `void`. Prefer returning a cheap value even
  when usually unchecked: e.g. `clear` returning whether any work was done. This
  is deliberate, not an oversight; unit tests should check it.
- **Rule:** Mark `[[nodiscard]]` when calling without using the result is
  pointless, such as `empty`.
- **Reviewing or adding a `(void)` cast on a `[[nodiscard]]` result:** before
  *adding* one, prefer propagating, logging, or handling the error; voiding is
  acceptable when intentional. Before *flagging* an existing one, verify the
  callee can actually fail in practice and that the caller should do something
  different on failure. Some functions have a nominal `bool` return that is
  always `true` given internal invariants (e.g. `execute_or_post_with_retry`
  retries until success); treat the cast as deliberate unless you can identify a
  concrete failure path.
- To carry "no value", choose by how easy the missing-value path is to forget:
  - A natural sentinel that is never valid in context (`nullptr`, empty string,
    `npos`) is fine and often leaner than wrapping. But it gives no type-level
    prompt to check: `[[nodiscard]]` forces *using* the return, not comparing it
    to the sentinel (`find() == npos` is the classic miss, which is why we have
    `find_opt`).
  - Reach for `std::optional`, or `std::expected` when failure must carry *why*,
    when forgetting the check is plausible. Both are options; **neither is
    mandated.** This is not Go, and even `optional` is often excessive.
- Error codes: Officially,`std::error_code` is the non-throwing *return* vehicle;
  `std::system_error` is the *exception* that carries one. Bridge an `errno` with
  `std::error_code(errno, std::generic_category())`. These are not much used.

## Exceptions

- Throwing for genuinely exceptional failure is legitimate, and sometimes
  cheapest: checking every call would be a nuisance and slower.
- **Rule:** Every function provides at least the **basic guarantee**: on an
  exception, no leaks and no invalid state. Offer the **strong guarantee**
  (commit-or-rollback) where the operation warrants it. If a function cannot meet
  even the basic guarantee, mark it `noexcept` so a fault terminates rather than
  corrupts.
- Recover only from exceptions known to be recoverable. `std::system_error`
  usually is: read `.code()` and act on it as you would on `errno`.
  `std::logic_error`, and some `std::runtime_error`, means the code can no
  longer be trusted; do not "recover", terminate or propagate.
- **Rule:** Never blindly swallow. A `catch(...)` that silently returns is
  forbidden, including across C-library callback boundaries. Log, and almost
  always propagate.
  - **Exception: `std::bad_alloc` you intend to recover from:** do not log it.
    Either you retry with a smaller allocation (the log is noise) or you are
    shutting down (allocating in order to log is counterproductive). If anything,
    free a pre-reserved "parachute" block (via `std::set_new_handler` or on the
    caught `bad_alloc`) to make room to exit cleanly.
- **Limit blast radius.** `main` has a top-level try/catch to report the reason.
  A server wraps each entry point and converts an escaped exception to a 500 (or
  equivalent), shutting down the offending connection, not the process, so long
  as it remains in a well-defined state.
- **Rule:** When a resource is tainted by a fatal error (e.g. failed I/O), close
  it and give up on it. That is the minimum blast radius.
