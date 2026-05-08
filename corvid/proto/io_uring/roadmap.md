# Needed enhancements

## iou_dgram_router / iou_dgram_session

Replaces the old `iou_dgram_conn`. The router owns the UDP socket and demuxes
incoming datagrams onto per-key sessions via a user-supplied extractor (default:
peer endpoint; QUIC: connection ID). Multishot recvmsg with provided buffers,
singleshot fallback on `EC::nobufs`. Sends are independent and atomic; each
completion returns the buffer to the originating session's `on_sent` so the
caller decides whether to retry.

# Possible enhancements

## Fixed File Table.
We can use io_uring_register_files to register an array of FDs whose lifespan we need to manage. We then pass the IOSQE_FIXED_FILE flag when making an SQE, and this lets us use the index of the FD in the registered array instead of the FD. This bypasses OS-level locking of the file structure, which is a win. However, we need to use io_uring_register_files_sparse to register many slots up front and io_uring_register_files_update to add or subtract an FD without recreating the whole register. We can then use IORING_OP_ACCEPT with the IORING_ACCEPT_DIRECT flag to tell the kernel that new connections automatically get placed in the fixed-file slot. However, we need to keep track of which slots are free, likely with a parallel array with a linked free list. There are also timing risks, such as needing to delay unregistering until after the OS has actually closed the underlying file.

## Pre-allocated Connections.
Instead of a shared_ptr to keep each iou_stream_conn alive, we could allocate a massive array of connections in advance and reuse them. Essentially, it's an object pool. To reference it, we'd have a handle with the index and generation (32 bits each). We'd then store this in the `user_data` of the SQE. This avoids reference counting, adds locality, and offers a relatively simple lifetime management approach. However, it doesn't go well with supporting both streams and datagrams, and it's fundamentally based on limited connections.

## Typed flags for prep_cancel_fd.
`prep_cancel_fd` accepts a raw `unsigned flags` parameter. The `IORING_ASYNC_CANCEL_*` constants should be wrapped into a typed enum (analogous to `iou_sqe_flags`) so callers do not pass bare integers.

## epoll cleanup.
Perhaps prefix some of the classes with epoll. For example, epoll_stream_conn.

# io_uring Preparation Operations Matrix

| io_uring prep operation                  | TCP   | UDP   | UDS stream   | UDS datagram / seqpacket   | Zero Copy   | Fixed Buffers                                           |
|:-----------------------------------------|:------|:------|:-------------|:---------------------------|:------------|:--------------------------------------------------------|
| io_uring_prep_read                       | Yes*  | Yes*  | Yes          | Yes*                       | No          | No                                                      |
| io_uring_prep_readv                      | Yes*  | Yes*  | Yes          | Yes*                       | No          | No                                                      |
| io_uring_prep_readv2                     | Yes*  | Yes*  | Yes          | Yes*                       | No          | No                                                      |
| io_uring_prep_read_fixed                 | Yes*  | Yes*  | Yes          | Yes*                       | No          | Yes: registered buffer                                  |
| io_uring_prep_read_multishot             | Yes*  | Yes*  | Maybe / Yes* | Yes*                       | No          | Provided buffers required, not fixed registered buffers |
| io_uring_prep_recv                       | Yes   | Yes   | Yes          | Yes                        | No          | No                                                      |
| io_uring_prep_recv + IOSQE_BUFFER_SELECT | Yes   | Yes   | Yes          | Yes                        | No          | Provided buffers, not fixed registered buffers          |
| io_uring_prep_recv_multishot             | Yes   | Yes   | Yes          | Yes                        | No          | Provided buffers required, not fixed registered buffers |
| io_uring_prep_recvmsg                    | Yes   | Yes   | Yes          | Yes                        | No          | No                                                      |

read is for generic files. It doesn't consume zero-size datagrams, unlike recv.
But read_fixed does use fixed buffers, whereas multishots effectively do.


# io_uring Network Operations Matrix

| io_uring prep operation                  | TCP   | UDP               | UDS stream   | UDS datagram / seqpacket   | Zero Copy   | Fixed Buffers                                      |
|:-----------------------------------------|:------|:------------------|:-------------|:---------------------------|:------------|:---------------------------------------------------|
| io_uring_prep_write                      | Yes*  | Yes* if connected | Yes          | Yes* if connected          | No          | No                                                 |
| io_uring_prep_writev                     | Yes*  | Yes* if connected | Yes          | Yes* if connected          | No          | No                                                 |
| io_uring_prep_writev2                    | Yes*  | Yes* if connected | Yes          | Yes* if connected          | No          | No                                                 |
| io_uring_prep_write_fixed                | Yes*  | Yes* if connected | Yes          | Yes* if connected          | No          | Yes: registered buffer                             |
| io_uring_prep_send                       | Yes   | Yes* if connected | Yes          | Yes* if connected          | No          | No                                                 |
| io_uring_prep_send + IOSQE_BUFFER_SELECT | Yes   | Yes* if connected | Yes          | Yes* if connected          | No          | No: provided buffers, not registered fixed buffers |
| io_uring_prep_sendto                     | Yes*  | Yes               | Yes*         | Yes                        | No          | No                                                 |
| io_uring_prep_sendmsg                    | Yes   | Yes               | Yes          | Yes                        | No          | No                                                 |
| io_uring_prep_send_zc                    | Yes   | Yes               | No           | No                         | Yes         | No                                                 |
| io_uring_prep_send_zc_fixed              | Yes   | Yes               | No           | No                         | Yes         | Yes: registered buffer                             |
| io_uring_prep_sendmsg_zc                 | Yes   | Yes               | No           | No                         | Yes         | No                                                 |
| io_uring_prep_sendmsg_zc_fixed           | Yes   | Yes               | No           | No                         | Yes         | Yes: registered buffer                             |
| io_uring_prep_send_bundle                | Yes   | Yes* if connected | Maybe / Yes* | Yes* if connected          | No          | No: provided buffers, not registered fixed buffers |
