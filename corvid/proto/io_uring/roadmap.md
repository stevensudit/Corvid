We need to improve iou_buf_pool::buffer (in @io_buff_pool.h)  to make it more flexible and handle partial reads and writes. Here's the plan.

The buffer has a flag for whether it's a reader or writer at the moment, and it has an `iou_res` that's updated after the completion of the I/O operation (this updating also affects the spans,
as explained below).
	
The buffer is defined by a few spans:
- full_span: the entire block; this never changes and is used for bounds-checking and for
  allocation bookkeeping.
- payload_span: the part that contains actual data. (Does not always start at the start of
  full_span.)
- active_span: the part that is active in the next I/O operation. (Depends heavily on
  read-vs-write semantics, see below.)
- tail_span: the part after payload_span, up to the end of full_span. (Not an
  actual std::span, but derived on the fly. And see below for some wrinkles on how it's calcuated.)
	
These differ in semantics for reader and writer buffers.

For readers:
- payload_span starts off empty length but pointing to the start of the full_span, and active_span starts off as the entire full_span. This means that a read can fill the entire buffer.
- On completion, `update` is called with the `iou_res`, and the end of the payload_span is extended by the number of bytes read (from `iou_res.bytes()`, when `iou_res.ok()`), while the active_span is set to the tail_span. This means that the payload_span points to all the bytes a user can now read.
- If the buffer is reused for subsequent read operations, the new bytes go into the active_span, so when `update` is called after the read, the end of the payload_span is pointed past the end of the new bytes read, and the active_span is updated to the new tail_span. This ensures that multiple reads are concatenated.
- The user can call `payload_span()` on the completed read buffer to see the payload all at once. They may also also call `read_consume`, passing in a byte count. This retrieves span of the payload with a length up to that requested size. It also has the side-effect of pushing the front of the payload_span past what was returned, so that subsequent calls consume subsequent bytes of the payload until none remain.
- The logic of `read_consume` ensures that a read buffer that has been fully consumed has its payload_span initialized to empty and its active_span set to the entire full_span. This is equivalent to its initial state, allowing further reads to use the entire buffer.
- A read buffer may be promoted to a write buffer, as when doing proxy work, with `promote_to_write`. This keeps the payload_span as-is, but sets active_span to payload_span (as opposed to the tail_span). Note that, underneath the hood, we also decrement `in_flight_read_bytes_` in  exactly the way we do now in `do_return`. This way, we don't mess up our bookkeeping.
- A write buffer can be demoted to a read buffer with `demote_to_read`. This keeps the payload_span as-is, but sets the active_span to the tail_span. If the payload_span was empty, then this is equivalent to the initial state of a read buffer.

For writers:
- payload_span starts off zero-length but pointing to the start of the full span, and so
  does the active_span. As it is empty, it cannot be used to write until it is prepared with some payload. (I mean, you could call with it, but you'd be doing a 0-byte write, which is probably not what you want.)
- One preparation flow is for the caller to get the tail_span by calling `tail_span()`, write some number of bytes to it, adjust the end of their copy of the span to reflect the new length, and then call `update_payload` with it. This sets the end of payload_span to point past the end of the span passed in. It also sets the end of the active_span to point to the end of the payload_span. This means that the tail_span will now point to the unused tail of the buffer, allowing this process to be repeated. This process can be repeated.
- The other preparation flow is to call `append` with a span or string_view, one or more times. This appends the bytes to the end of payload_span (aka, the start of the tail_span), and extends the end of the payload_span and the end of the active_span to point past it. There is no need for an `update_payload` call as each `append` effectively does so.
- On completion, the system calls `update` with the `iou_res`. This is used to set the start of active_span past the last written byte of the payload, while the end of the active_span continues to point to the end of the payload_span. This allows the user to look at the payload that had just been sent.
- If a write buffer is fully written, it is considered consumed. Its `active_span` will wind up zero length with its start pointing past the end of the payload_span. In this state, the next call to `append` will first reset the payload_span so that it points to the start of full_span and is of zero length, and reset the active_span to point to the payload_span. This allows the buffer to be reused from the  start. Similarly, in this state, a call to `tail_span()` will return the entire full_span, and a call to `update_payload` will set the payload_span and active_span to the passed-in span, allowing you to treat the buffer as though it has started off empty. Essentially, this fully-consumed state acts like empty when we write to it, but gives the user a window of opportunity to examine the payload_span before writing.
- If a write buffer is partially consumed and then reused for subsequent writes without further changes, then it is already prepared. Its payload_span points to the entire portion with data, but the active_span points to the unwritten portion only. The active_span is the part that will actually get written.
- Before using a partially-consumed write buffer for subsequent writes, the user is also free to call `append` on it, which extends the payload_span to include the new bytes while also extending active_span to include them. Similarly, the user could use the `upload_payload` flow. Note that this behavior just append, which is different from the special case for a fully-consumed write buffer.

We're also going to need thorough unit tests that exercise the buffer without actually doing real I/O, because there are all sorts of edge cases (like partial reads and writes) that are very difficult to induce. The test cases should also include `iou_res` values that signal error, to show that we can handle these cleanly.
