# Edge cases

## Stop listening.
We've never tried to stop listening on a connection (whether epoll or io_uring). Presumably, closing the listening port would work, but that's not tested.

# Needed enhancements

## iou_buf_pool coalesce.
We put off writing the code to glue slabs back together. As it stands, the system could get into a state where only 4k buffers were available. What we probably want to do is serve smaller buffers from the bottom of the memory area and bigger ones from the top.

# Possible enhancements

## Provide Buffers and Multishot.
We can use IORING_OP_PROVIDE_BUFFERS and IORING_OP_PBUF_RING features in conjunction with the IOSQE_BUFFER_SELECT, to do multishot IORING_OP_RECV_MULTISHOT reads. This avoids the need to keep sending recv SQE's and also lets the OS manage buffers. This is a higher-throughput option, and lower latency, so there's a lot to like. However, we'd need a separate pool of fixed-sized buffers that the OS will write to. We also need to release these buffers back into the pool once we're done with them. The nice thing is that we can mix this approach with our current one.

## Fixed File Table.
We can use io_uring_register_files to register an array of FDs whose lifespan we need to manage. We then pass the IOSQE_FIXED_FILE flag when making an SQE, and this lets us use the index of the FD in the registered array instead of the FD. This bypasses OS-level locking of the file structure, which is a win. However, we need to use io_uring_register_files_sparse to register many slots up front and io_uring_register_files_update to add or subtract an FD without recreating the whole register. We can then use IORING_OP_ACCEPT with the IORING_ACCEPT_DIRECT flag to tell the kernel that new connections automatically get placed in the fixed-file slot. However, we need to keep trackof which slots are free, likely with a parallel array with a linked free  list. There are also timing risks, such as needing to delay unregistering until after the OS has actually closed the underlying file.

## Pre-allocated Connections.
Instead of a shared_ptr to keep each iou_stream_conn alive, we could allocate a massive array of connections in advance and reuse them. Essentially, it's an object pool. To reference it, we'd have a handle with the index and generation (32 bits each). We'd then store this in the `user_data` of the SQE. This avoids reference counting, adds locality, and offers a relatively simple lifetime management approach. However, it doesn't go well with supporting both streams and datagrams, and it's fundamentally based on limited connections.
