# Edge cases

## Stop listening.
We've never tried to stop listening on a connection (whether epoll or io_uring). Presumably, closing the listening port would work, but that's not tested.

# Needed enhancements

## iou_buf_pool coalesce.
We put off writing the code to glue slabs back together. As it stands, the system could get into a state where only 4k buffers were available. What we probably want to do is serve smaller buffers from the bottom of the memory area and bigger ones from the top.

## Cancellation token tracking in iou_stream_conn.
`do_submit_recv`, `do_submit_send_buffer`, `do_submit_connect`, and `do_submit_accept` all obtain a completion token but do not store it anywhere on the connection. Without storing these tokens, there is no way to cancel individual in-flight operations (e.g., to implement a send timeout or to abort a pending connect). Each should be saved as a member so that `do_close` and related paths can cancel them explicitly.

## Skip submit_now when ring is empty.
`submit_now` calls `ring_.submit()` unconditionally. It should first check whether any SQEs are actually pending (via `io_uring_sq_ready`) and skip the syscall if the ring is empty. The current approach wastes a system call on every `run_once` tick when there is no work to submit.

## Shut down the loop on fatal submit errors.
`submit_now` returns `false` on a fatal ring error, propagating it up the call chain. A fatal ring error means the loop cannot continue; returning `false` only defers the problem. Instead, `submit_now` should initiate a graceful loop shutdown directly rather than relying on callers to handle the return value correctly.

# Possible enhancements

## Provide Buffers and Multishot.
We can use IORING_OP_PROVIDE_BUFFERS and IORING_OP_PBUF_RING features in conjunction with the IOSQE_BUFFER_SELECT, to do multishot IORING_OP_RECV_MULTISHOT reads. This avoids the need to keep sending recv SQE's and also lets the OS manage buffers. This is a higher-throughput option, and lower latency, so there's a lot to like. However, we'd need a separate pool of fixed-sized buffers that the OS will write to. We also need to release these buffers back into the pool once we're done with them. The nice thing is that we can mix this approach with our current one. Note that we'd need to handle timeouts with a repeating timer. On completion, we check the timestamp of the last received data and close the connection if it's been too long. In theory, we should schedule the timeout operation to repeat more often than the actual timeout value, for higher precision.
When Provided Buffers are in use, `iou_recv_view::stop_reading` and `iou_buffer::promote_to_write`/`demote_to_read` need to detect this and behave differently, since the OS owns those buffers.

## Fixed File Table.
We can use io_uring_register_files to register an array of FDs whose lifespan we need to manage. We then pass the IOSQE_FIXED_FILE flag when making an SQE, and this lets us use the index of the FD in the registered array instead of the FD. This bypasses OS-level locking of the file structure, which is a win. However, we need to use io_uring_register_files_sparse to register many slots up front and io_uring_register_files_update to add or subtract an FD without recreating the whole register. We can then use IORING_OP_ACCEPT with the IORING_ACCEPT_DIRECT flag to tell the kernel that new connections automatically get placed in the fixed-file slot. However, we need to keep track of which slots are free, likely with a parallel array with a linked free list. There are also timing risks, such as needing to delay unregistering until after the OS has actually closed the underlying file.

## Pre-allocated Connections.
Instead of a shared_ptr to keep each iou_stream_conn alive, we could allocate a massive array of connections in advance and reuse them. Essentially, it's an object pool. To reference it, we'd have a handle with the index and generation (32 bits each). We'd then store this in the `user_data` of the SQE. This avoids reference counting, adds locality, and offers a relatively simple lifetime management approach. However, it doesn't go well with supporting both streams and datagrams, and it's fundamentally based on limited connections.

## Reducing submissions.
`maybe_submit_pending` currently submits immediately every time; the deferred-batching logic is not implemented. The intended design: track the count of pending SQEs and the time since the last submit, and only call `submit_now` when either a count threshold or a time threshold is exceeded. A repeating timer SQE could serve as the time-based flush. Note that we need to decrement the number of submissions based on the return code from submit, not assume they were all consumed. We may need to enable IORING_SETUP_SUBMIT_ALL. (As an alternative to the repeating timer, we could do a maybe_submit at the end of each run_once loop.)

## Typed flags for prep_cancel_fd.
`prep_cancel_fd` accepts a raw `unsigned flags` parameter. The `IORING_ASYNC_CANCEL_*` constants should be wrapped into a typed enum (analogous to `iou_sqe_flags`) so callers do not pass bare integers.

## Timeout config interning.
`prep_timeout` and `prep_link_timeout` take separate `duration`, `flags`, and `cqe_count` arguments. There are a finite number of meaningful configurations. Combining them into a small struct and interning or pooling instances would avoid repeated stack allocations and make it easier to register timeouts as fixed resources.

## CQE flags in on_accept_complete.
`on_accept_complete` ignores its `flags` argument. The `IORING_CQE_F_MORE` flag indicates the multishot accept is still armed; other flags may carry socket metadata. The handler should at minimum assert that unexpected flag combinations do not occur, and eventually act on any that are relevant.

## epoll cleanup.
Perhaps prefix some of the classes with epoll. For example, epoll_stream_conn.


