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
#include <cassert>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "../../enums/bool_enums.h"
#include "../net_endpoint.h"
#include "iou_buf_pool.h"
#include "iou_loop.h"

namespace corvid { inline namespace proto { namespace iouring {
using namespace bool_enums;

// Fwd.
class iou_stream_conn;

#pragma region iou_recv_view
// Move-only recv token delivered to `on_data`. Two consumption paths:
//   Inline: call `active_view()` + `consume(n)`. Destructor resubmits recv
//           into the remaining buffer (or a fresh one if fully consumed).
//   Async:  call `take()` to transfer buffer ownership for off-loop parsing.
//           Destructor posts a fresh-buffer recv immediately.
class iou_recv_view {
#pragma region Construction
public:
  iou_recv_view(iou_recv_view&&) noexcept = default;

  iou_recv_view(const iou_recv_view&) = delete;
  iou_recv_view& operator=(const iou_recv_view&) = delete;
  iou_recv_view& operator=(iou_recv_view&&) = delete;

  // NOLINTBEGIN(bugprone-exception-escape)
  ~iou_recv_view() {
    if (!buf_) return; // moved-from
    resume_(std::move(buf_));
  }
  // NOLINTEND(bugprone-exception-escape)

#pragma endregion
#pragma region Buffer management

  // Current unconsumed payload.
  [[nodiscard]] std::string_view active_view() noexcept {
    return buf_.payload_view();
  }

  // Advance past `n` bytes, returning them as a view.
  auto consume(size_t n) noexcept { return buf_.consume_read(n); }

  // Transfer buffer to caller for async parsing. A fresh-buffer recv is posted
  // immediately; the caller returns the buffer to the pool via RAII.
  [[nodiscard]] iou_buf_pool::buffer take() {
    resume_(iou_buf_pool::buffer{}); // empty signals: start fresh recv
    return std::move(buf_);
  }

  // To apply backpressure, you can prevent further recvs. Note that this can
  // result in the connection lapsing if nothing is holding a strong pointer to
  // it. You can call `resume` to continue.
  //
  // TODO: With multishot recvs, this will have to be done somewhat
  // differently, as the `resume_` callback is basically a no-op. Instead, you
  // need to call `pause` on the connection.
  void stop_reading() { buf_.reset(); }

#pragma endregion
#pragma region Internals

private:
  friend class iou_stream_conn;
  using resume_fn = std::function<void(iou_buf_pool::buffer&&)>;

  iou_recv_view(iou_buf_pool::buffer buf, resume_fn resume) noexcept
      : buf_{std::move(buf)}, resume_{std::move(resume)} {}

private:
  iou_buf_pool::buffer buf_;
  resume_fn resume_;
#pragma endregion
};

#pragma endregion
#pragma region handlers

// User-supplied persistent callbacks for an `iou_stream_conn`.
struct iou_stream_conn_handlers {
  // Fired when data arrives. `view` is move-only; call `consume(n)` inline or
  // `take()` to move the buffer out for async parsing.
  std::function<bool(iou_stream_conn&, iou_recv_view)> on_data = nullptr;
  // Fired when all queued sends complete and the queue empties, or when an
  // async connect succeeds.
  std::function<bool(iou_stream_conn&)> on_drain = nullptr;
  // Fired once on graceful or error close.
  std::function<bool(iou_stream_conn&)> on_close = nullptr;
};

// Fwd.
template<typename STATE>
class iou_stream_conn_with_state;
template<typename T>
class iou_stream_conn_ptr_with;

#pragma endregion
#pragma region iou_stream_conn

// An `iou_stream_conn` is a non-blocking stream socket driven by an
// `iou_loop`. Instances are created, directly or indirectly, by
// `iou_stream_conn_ptr_with` factories.
//
/// Supports three creation paths:
//
// 1. Already-connected socket (whether from `connect` or `accept`): use the
//    `iou_stream_conn_ptr_with` constructor directly. The socket must be
//    non-blocking and connected.
//
// 2. Async connect: use the static `iou_stream_conn_ptr_with::connect`
//    factory. Fires `on_drain` on success, `on_close` on failure.
//
// 3. Listening: use the static `iou_stream_conn_ptr_with::listen` factory.
//    Accepts all incoming connections, each with a copy of the listener's
//    handlers.
//
// Send path: `send(buffer&&)` submits zero-copy directly. `send(string)`
// copies to a JIT-borrowed `buffer` to pack consecutive string items.
// One SQE in flight at a time for ordering; additional sends queue behind it.
// (Note: Soft-linked SQE's yield incorrect results on partial writes.)
//
// Recv path: one SQE in flight at a time. The `iou_recv_view` token delivered
// to `on_data` controls re-submission via inline `consume` or async `take`.
//
// Thread safety: `send`, `close`, and `hangup` are safe from any thread.
// All I/O and state mutation run on the loop thread.
class iou_stream_conn: public std::enable_shared_from_this<iou_stream_conn> {
public:
  using buffer = iou_loop::buffer;

#pragma region Accessors

  // True if the connection has not yet been closed. Safe to call from any
  // thread, but there's no guarantee that a connection will remain open, while
  // a closed one will remain closed.
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // The remote peer address. For accepted connections, computed lazily via
  // `getpeername` on first call. Safe to call from any thread.
  [[nodiscard]] const net_endpoint& remote_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (remote_.sockaddr.sockaddr.empty())
      remote_.sockaddr.sockaddr = net_endpoint::peer_of(sock_);
    return remote_.sockaddr.sockaddr;
  }

  // The local address this socket is bound to, computed lazily via
  // `getsockname` on first call. Useful after `listen` on port 0 to discover
  // the OS-assigned port. Safe to call from any thread.
  [[nodiscard]] const net_endpoint& local_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (local_.empty()) local_ = net_endpoint{sock_};
    return local_;
  }

  // The `iou_loop` that drives this connection. Valid for the lifetime of
  // the connection.
  [[nodiscard]] iou_loop& loop() noexcept { return loop_; }

  // Return a weak pointer to the loop. This is to handle an edge case that
  // would lead to a crash during shutdown.
  //
  // Consider what happens if the connection is closed and the loop is
  // destroyed, but you have a callback running on some arbitrary thread that
  // kept this connection instance alive through its `shared_ptr`. If you then
  // call it and it tries to post to the loop, things will go badly. Instead,
  // you can attempt to upgrade the weak pointer, ensuring that the loop is
  // still alive.
  [[nodiscard]] std::weak_ptr<iou_loop> weak_loop() const noexcept {
    return weak_loop_;
  }

  //
  // Block size for borrowed buffers. Thread-safe, but inherently racy.
  //

  [[nodiscard]] block_size recv_buf_size() const noexcept {
    return recv_buf_size_;
  }
  void set_recv_buf_size(block_size size) noexcept { recv_buf_size_ = size; }

  [[nodiscard]] block_size send_buf_size() const noexcept {
    return send_buf_size_;
  }
  void set_send_buf_size(block_size size) noexcept { send_buf_size_ = size; }

#pragma endregion
#pragma region send

  // Queue a string for sending. JIT-borrows a write buffer to pack consecutive
  // string items. Safe to call from any thread.
  [[nodiscard]] bool send(std::string&& data) {
    if (!open_ || data.empty()) return false;
    return loop_.execute_or_post(
        [p = self(), d = std::move(data)]() mutable -> bool {
          if (!p->open_) return false;
          p->send_queue_.emplace_back(std::move(d));
          if (!p->send_in_flight_) return p->do_submit_send();
          return true;
        });
  }

  // Queue a pre-filled registered buffer for zero-copy sending.
  // Safe to call from any thread.
  [[nodiscard]] bool send(buffer&& buf) {
    if (!open_ || !buf || buf.active_span().empty()) return false;
    return loop_.execute_or_post(
        [p = self(), b = std::move(buf)]() mutable -> bool {
          if (!p->open_) return false;
          p->send_queue_.emplace_back(std::move(b));
          if (!p->send_in_flight_) return p->do_submit_send();
          return true;
        });
  }

#pragma endregion
#pragma region close

  // Start a close.
  //
  // If `coordination` is `unilateral` (the default), flushes pending sends and
  // then closes the socket. If `bilateral`, instead shuts down the write side
  // after flushing pending sends and discards incoming data until the peer
  // closes. Set the policy via `set_shutdown` before calling this.
  //
  // Safe to call from any thread. Once called, destructing the owning
  // `iou_stream_conn_ptr_with` does not cause a forceful close.
  [[nodiscard]] bool close() {
    no_hangup_on_destruct_ = true;
    return loop_.execute_or_post([p = self()] { return p->do_close(); });
  }

  // Get the shutdown `coordination_policy` used by `close()`. `bilateral`
  // shuts down the write side after the send queue flushes, then discards
  // incoming data until the peer sends EOF. `unilateral` (the default) closes
  // the entire socket once the queue empties. Safe to call from any thread.
  [[nodiscard]] coordination_policy get_shutdown() const noexcept {
    return shutdown_;
  }

  // Set the shutdown coordination policy. `shutdown` defaults to `unilateral`.
  // Call before `close()`. Safe to call from any thread.
  void set_shutdown(coordination_policy shutdown) noexcept {
    shutdown_ = shutdown;
  }

  // Forceful close: cancel pending I/O and close immediately with RST.
  // Safe to call from any thread. By default, destructing the owning
  // `iou_stream_conn_ptr_with` causes a forceful close.
  [[nodiscard]] bool hangup() {
    return loop_.execute_or_post([p = self()] { return p->do_hangup_now(); });
  }

#pragma endregion
#pragma region Internals
private:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_stream_conn_ptr_with;
  template<typename>
  friend class iou_stream_conn_with_state;

#pragma endregion
#pragma region Construction
public:
  // Public only for `std::make_shared`; use `iou_stream_conn_ptr_with`
  // factories instead.
  explicit iou_stream_conn(allow, const std::shared_ptr<iou_loop>& loop,
      net_socket sock, bound_endpoint_with_timeout remote,
      iou_stream_conn_handlers h, std::optional<connection_role> role,
      coordination_policy shutdown, block_size recv_buf_size,
      block_size send_buf_size)
      : loop_{*loop}, weak_loop_{loop}, sock_{std::move(sock)},
        remote_{std::move(remote)}, own_handlers_{std::move(h)},
        connecting_{role == connection_role::client},
        listening_{role == connection_role::server}, shutdown_{shutdown},
        recv_buf_size_{recv_buf_size}, send_buf_size_{send_buf_size} {}

  virtual ~iou_stream_conn() = default;

  [[nodiscard]] std::shared_ptr<iou_stream_conn> self() {
    return std::static_pointer_cast<iou_stream_conn>(shared_from_this());
  }

#pragma endregion
#pragma region Child access
protected:
  iou_loop& loop_;
  std::weak_ptr<iou_loop> weak_loop_;

  // Produce a new `iou_stream_conn` for each accepted connection. Override in
  // `iou_stream_conn_with_state` to return a richer type.
  [[nodiscard]] virtual std::shared_ptr<iou_stream_conn>
  accept_clone(net_socket&& sock, const net_endpoint& remote) const {
    auto lp = weak_loop_.lock();
    if (!lp) return nullptr;
    bound_endpoint_with_timeout remote_ep{};
    remote_ep.sockaddr.sockaddr = remote;
    remote_ep.sockaddr.len = remote.sockaddr_size();

    return std::make_shared<iou_stream_conn>(allow::ctor, std::move(lp),
        std::move(sock), std::move(remote_ep), own_handlers_, std::nullopt,
        shutdown_, recv_buf_size_, send_buf_size_);
  }

#pragma endregion
#pragma region Data members
private:
  net_socket sock_;

  std::mutex endpoint_mutex_; // protects lazy initialization of endpoints.
  bound_endpoint_with_timeout remote_; // Use `remote_endpoint()`.
  net_endpoint local_; // Always access through `local_endpoint()` JIT.

  iou_stream_conn_handlers own_handlers_;

  relaxed_atomic_bool open_{true}; // Cleared once close starts.
  bool connecting_{};              // True when an async connect is in-flight.
  bool listening_{};               // True if this is a listener.
  bool close_requested_{};         // True once `close` is called.
  bool close_notified_{};          // True once `on_close` fires.
  bool write_open_{true};          // False after SHUT_WR in bilateral close.
  bool peer_eof_{};                // True once peer sends EOF.
  relaxed_atomic<coordination_policy> shutdown_{
      coordination_policy::unilateral};
  relaxed_atomic_bool no_hangup_on_destruct_; // Set by `close`.

  // Size of borrowed buffers.
  relaxed_atomic<block_size> recv_buf_size_{block_size::small};
  relaxed_atomic<block_size> send_buf_size_{block_size::small};

  // Recv state: one SQE in flight at a time.
  bool recv_in_flight_{};

  // Send state: one SQE in flight at a time; queue holds strings and
  // buffers.
  std::deque<std::variant<std::string, buffer>> send_queue_;
  bool send_in_flight_{};

#pragma endregion
#pragma region Helpers
private:
  // Submit buffer for receiving, borrowing a read buffer if needed.
  bool do_submit_recv(buffer buf = {}) {
    assert(loop_.is_loop_thread() && !recv_in_flight_);
    if (!buf) buf = loop_.borrow_read_buffer(recv_buf_size_);
    if (!buf) return false;
    recv_in_flight_ = true;
    // TODO: Save the token.
    const auto token = loop_.submit_read_buffer(sock_, std::move(buf),
        [p = self()](completion_id, buffer& b) {
          (void)p->on_recv_complete(b);
          return slot_retention{};
        });
    return token.is_valid();
  }

  // Resubmit recv using an existing buffer from a prior `on_recv_complete`.
  // If the buffer has no remaining `active_span` (completely full),
  // re-delivers any remaining payload first, then gets a fresh buffer.
  bool do_continue_recv(buffer&& buf) {
    assert(loop_.is_loop_thread() && !recv_in_flight_);
    // Space remains in this buffer, perhaps because the contents have been
    // fully consumed and the buffer has reset itself.
    if (!buf.active_span().empty()) return do_submit_recv(std::move(buf));

    // Buffer is completely full but has remaining unconsumed payload.
    // Re-deliver it; when the handler consumes, the buffer resets and the view
    // destructor calls `do_continue_recv` again, which then falls through to
    // `do_submit_recv`.
    auto resume = make_resume();
    iou_recv_view view{std::move(buf), std::move(resume)};
    if (own_handlers_.on_data)
      return own_handlers_.on_data(*this, std::move(view));
    return true;
  }

  // Build the resume callback captured by an `iou_recv_view`.
  iou_recv_view::resume_fn make_resume() {
    return [conn = self()](buffer&& buf) {
      auto loop = conn->weak_loop_.lock();
      if (!loop) return;
      (void)loop->execute_or_post(
          [conn, buf = std::move(buf)]() mutable -> bool {
            if (!conn->open_) return false;
            if (!buf) return conn->do_submit_recv(); // take() path: fresh recv
            return conn->do_continue_recv(std::move(buf)); // inline path
          });
    };
  }

  // Send the next buffer in the send queue.
  bool do_submit_send() {
    assert(loop_.is_loop_thread() && !send_in_flight_);
    if (send_queue_.empty()) return true;

    buffer buf;
    if (std::holds_alternative<buffer>(send_queue_.front())) {
      buf = std::move(std::get<buffer>(send_queue_.front()));
      send_queue_.pop_front();
    } else {
      buf = loop_.borrow_write_buffer(send_buf_size_);
      while (!send_queue_.empty() &&
             std::holds_alternative<std::string>(send_queue_.front()))
      {
        if (!buf.append(std::get<std::string>(send_queue_.front())))
          return false;
        send_queue_.pop_front();
      }
    }

    return do_submit_send_buffer(std::move(buf));
  }

  // Helper for sending specific buffer.
  bool do_submit_send_buffer(buffer&& buf) {
    assert(loop_.is_loop_thread() && !send_in_flight_);
    if (!buf) return false;
    send_in_flight_ = true;
    // TODO: Save the token.
    const auto token = loop_.submit_write_buffer(sock_, std::move(buf),
        [p = self()](completion_id, buffer& b) -> slot_retention {
          (void)p->on_send_complete(b);
          return {};
        });
    return token.is_valid();
  }

  // Attempt to connect. On success, fires `on_drain` and transitions to recv
  // state. On failure, fires `on_close`.
  bool do_submit_connect() {
    assert(loop_.is_loop_thread() && connecting_);
    // TODO: Store cancelation token.
    bound_endpoint_with_timeout ep;
    if (std::scoped_lock lock{endpoint_mutex_}; true) ep = remote_;
    auto token = loop_.submit_connect(sock_, std::move(ep),
        [p = self()](completion_id, iou_res res,
            iou_cqe_flags flags) -> slot_retention {
          (void)p->on_connect_complete(res, flags);
          return {};
        });
    return token.is_valid();
  }

  // Submit a multishot accept operation.
  bool do_submit_accept() {
    assert(loop_.is_loop_thread() && listening_);
    // Set up a callback that resubmits itself, and use it too bootstrap the
    // initial submission.
    auto raw_cb =
        [p = self()](completion_id cbid, iou_res res, iou_cqe_flags flags,
            combined_endpoint& endpoint) -> slot_retention {
      (void)p->on_accept_complete(res, flags, endpoint);
      if (bitmask::has(flags, iou_cqe_flags::more))
        return slot_retention::automatic;
      if (!p->listening_) return slot_retention::release;
      if (!p->loop_.submit_accept_multishot(p->sock_, endpoint,
              iou_loop::completion_token{cbid}))
        throw std::runtime_error("Failed to re-arm multishot accept");
      return slot_retention::retain;
    };

    auto [cbtoken, endpoint_ptr] =
        loop_.wrap_completion_fn_and_ptr(std::move(raw_cb), bound_endpoint{});

    // Pretend a CQE arrived, with a soft error that will be ignored. This will
    // get it to submit itself.
    auto borrowed = loop_.borrow(cbtoken);
    (void)borrowed(cbtoken.as_int(), iou_res{*EC::again}, iou_cqe_flags{});
    loop_.detach(std::move(borrowed));

    // TODO: Store cbtoken for cancelation.

    return true;
  }

  // Close immediately without flushing. For listening sockets, a cancel is
  // submitted first to abort the in-flight multishot accept before the fd is
  // handed to the kernel for closing.
  [[nodiscard]] bool do_close_now() {
    assert(loop_.is_loop_thread());
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    send_queue_.clear();
    if (sock_)
      (void)loop_.submit_close(std::move(sock_),
          [p = self()](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });

    return notify_close_once();
  }

  // Close immediately with RST, canceling all pending I/O first.
  [[nodiscard]] bool do_hangup_now() {
    assert(loop_.is_loop_thread());
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    send_queue_.clear();
    if (sock_) {
      (void)sock_.set_option(socket_option::linger,
          linger{.l_onoff = 1, .l_linger = 0});
      (void)loop_.submit_close(std::move(sock_),
          [p = self()](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });
      (void)loop_.immediate_submit();
    }
    return notify_close_once();
  }

  // Finalize a requested close once the send queue is empty. With
  // `unilateral` (or if the peer already sent EOF), closes immediately. With
  // `bilateral`, shuts down the write side via `SHUT_WR` and keeps the recv
  // loop running to discard incoming data until the peer sends EOF.
  [[nodiscard]] bool do_finish_close() {
    assert(loop_.is_loop_thread());
    if (shutdown_ == coordination_policy::unilateral || peer_eof_)
      return do_close_now();
    const auto token = loop_.submit_shutdown(sock_, shutdown_how::wr,
        [p = self()](completion_id, iou_res res, iou_cqe_flags) {
          if (!res.ok()) (void)p->do_close_now();
          return slot_retention{};
        });
    if (!token.is_valid()) return do_close_now();
    write_open_ = false;
    if (!recv_in_flight_) return do_submit_recv();
    return true;
  }

  // Close after flushing.
  [[nodiscard]] bool do_close() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    close_requested_ = true;
    if (send_queue_.empty() && !send_in_flight_) return do_finish_close();
    return true;
  }

#pragma endregion
#pragma region Event handlers

  // Handle completion of a send operation. On success, if the buffer is
  // fully sent, pops the next buffer from the send queue and submits it. If
  // the buffer is only partially sent or there's a soft error, resubmits the
  // remaining active span. On error, initiates close.
  bool on_send_complete(buffer& buf) {
    assert(loop_.is_loop_thread());
    send_in_flight_ = false;
    if (!open_) return true;

    // Error.
    if (!buf.result().ok()) {
      if (buf.result().is_soft_error())
        return do_submit_send_buffer(std::move(buf));
      return do_close_now();
    }

    // Partial send: remaining bytes in active_span.
    if (!buf.active_span().empty())
      return do_submit_send_buffer(std::move(buf));

    // Full send.
    if (!send_queue_.empty()) return do_submit_send();

    //  Close.
    if (close_requested_ && send_queue_.empty()) return do_finish_close();

    return notify_drained();
  }

  // Handle completion of connect operation. On success, fires `on_drain` and
  // transitions to recv state. On failure, fires `on_close` and initiates
  // close.
  bool on_connect_complete(iou_res res, iou_cqe_flags) {
    assert(loop_.is_loop_thread() && connecting_);
    connecting_ = false;
    if (!open_) return true;

    // Error.
    if (!res.ok()) return do_close_now();

    // Start listening for data.
    if (!do_submit_recv()) return do_close_now();

    // In principle, we could have writes queued.
    if (!send_queue_.empty()) return do_submit_send();

    return notify_drained();
  }

  // Handle completion of a multishot accept. On success, creates a new
  // `iou_stream_conn` for the accepted socket and registers it with the loop.
  // On error, initiates close.
  bool on_accept_complete(iou_res res, iou_cqe_flags flags,
      const combined_endpoint& remote) {
    // TODO: Use flags.
    (void)flags;
    assert(loop_.is_loop_thread() && listening_);
    if (!open_) return true;

    // Error. If it's an ECANCELED, then we're either shutting down or pausing:
    // either way, there's nothing for us to do here.
    if (!res.ok()) {
      if (res.is_soft_error() || res.err() == EC::canceled) return true;
      return do_close_now();
    }

    net_socket accepted_sock{os_file{res.value()}};
    auto peer = accept_clone(std::move(accepted_sock), remote.sockaddr);
    if (peer) (void)peer->start_reading();
    return true;
  }

  // Handle completion of a receive operation. If successful, delivers the
  // buffer to `on_data`. On error or EOF, initiates close.
  bool on_recv_complete(buffer& buf) {
    assert(loop_.is_loop_thread() && recv_in_flight_);
    recv_in_flight_ = false;
    if (!open_) return true;

    const auto res = buf.result();
    // EOF from peer.
    if (res.value() == 0) {
      peer_eof_ = true;
      // Bilateral drain: peer sent EOF as expected, so close now.
      if (close_requested_ && !write_open_) return do_close_now();
      (void)notify_close_once();
      if (close_requested_ && !send_in_flight_ && send_queue_.empty())
        return do_finish_close();
      return true;
    }
    // Error.
    if (!res.ok()) {
      if (res.is_soft_error()) return do_submit_recv(std::move(buf));
      return do_close_now();
    }

    // Note how `make_resume` captures a strong pointer to this connection,
    // keeping it alive between receives.
    //
    // TODO: For multishot recvs, things are different. There's no way to reuse
    // the same buffer, and our `buffer` object has to be created over the
    // Provided Buffer, with the destructor cleaning it up by returning it to
    // the provider-buffer ring, not our own pool.
    //
    // This means that the caller should just process the `iou_recv_view` and
    // consume all of its bytes. If it destructs the view with bytes left, the
    // `resume_` callback should shut down the connection immediately because
    // programmer error has led to lost bytes.
    //
    // In the normal case, the `resume_` callback provided by
    // `make_multishot_resume` doesn't have to do anything. However, if the
    // `iou_cqe_flags::more` flag in the buffer (which comes from the CQE) is
    // not set, (and we're not actually trying to `pause`) then we need to
    // start a new multishot recv.
    //
    // The most likely reason for this is that the  kernel has filled up the
    // provided-buffer ring. In that case, the fact that we've freed up a
    // provided buffer should allow us to continue.
    //
    // A possible edge case is when the `buffer` was taken, in which case we
    // ought not start a new multishot recv but we also don't ever resume, even
    // after the `buffer` is eventually freed. This means that if you use
    // `take` for multishot, the user has to manually call `resume`.
    //
    // It's also possible to force this situation before we run out of buffers
    // by actively canceling the multishot recv. This does not risk losing
    // in-flight data, but it does require the user to call `resume` when
    // they're ready.

    // Bilateral drain: discard data and resubmit recv until peer sends EOF.
    if (close_requested_ && !write_open_) return do_submit_recv();

    iou_recv_view view{std::move(buf), make_resume()};
    if (own_handlers_.on_data)
      return own_handlers_.on_data(*this, std::move(view));

    return true;
  }

  // Send out SQEs for new connection so that we're ready to receive either a
  // connection completion, an accepted socket, or data. Without this,
  // nothing is keeping this instance alive.
  bool start_reading() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    if (listening_) return do_submit_accept();
    if (connecting_) return do_submit_connect();
    return do_submit_recv();
  }

  // Notify `on_drain`.
  [[nodiscard]] bool notify_drained() {
    assert(loop_.is_loop_thread());
    if (own_handlers_.on_drain) return own_handlers_.on_drain(*this);
    return true;
  }

  // Notify `on_close` exactly once.
  [[nodiscard]] bool notify_close_once() {
    assert(loop_.is_loop_thread());
    if (close_notified_) return false;
    close_notified_ = true;
    if (own_handlers_.on_close) return own_handlers_.on_close(*this);
    return true;
  }
};

#pragma endregion
#pragma region ptr_with

// RAII handle that owns an `iou_stream_conn` (or a derived class).
// Destruction calls `hangup` unless `close` was called first.
//
// `T` defaults to `iou_stream_conn`. Use `iou_stream_conn_ptr_with<MyConn>`
// for a typed handle whose `operator->` returns `MyConn*`.
template<typename T = iou_stream_conn>
class iou_stream_conn_ptr_with {
  static_assert(std::derived_from<T, iou_stream_conn>,
      "iou_stream_conn_ptr_with<T>: T must derive from iou_stream_conn");

public:
  using conn_t = T;
  using shared_ptr_t = std::shared_ptr<conn_t>;

  iou_stream_conn_ptr_with() noexcept = default;
  iou_stream_conn_ptr_with(iou_stream_conn_ptr_with&&) noexcept = default;
  iou_stream_conn_ptr_with(const iou_stream_conn_ptr_with&) = delete;
  iou_stream_conn_ptr_with& operator=(
      iou_stream_conn_ptr_with&&) noexcept = default;
  iou_stream_conn_ptr_with& operator=(
      const iou_stream_conn_ptr_with&) = delete;

  // Implicit upcast: `iou_stream_conn_ptr_with<Derived>` ->
  // `iou_stream_conn_ptr_with<Base>`.
  template<typename U>
  requires std::derived_from<U, conn_t>
  iou_stream_conn_ptr_with(iou_stream_conn_ptr_with<U>&& other) noexcept
      : conn_{std::move(other.conn_)} {}

  // Perform `hangup` on destruction.  If you want to close cleanly, you must
  // call `close` before the instance is destructed.
  // NOLINTBEGIN(bugprone-exception-escape)
  ~iou_stream_conn_ptr_with() {
    if (!conn_ || conn_->no_hangup_on_destruct_) return;
    (void)conn_->loop_.execute_or_post([p = std::move(conn_)] {
      return p->do_hangup_now();
    });
  }
  // NOLINTEND(bugprone-exception-escape)

  [[nodiscard]] const shared_ptr_t& pointer() const noexcept { return conn_; }
  [[nodiscard]] shared_ptr_t release() noexcept { return std::move(conn_); }

  // Adopt an already-connected socket. `sock` must be non-blocking.
  [[nodiscard]] static iou_stream_conn_ptr_with
  adopt(const std::shared_ptr<iou_loop>& loop, net_socket sock,
      net_endpoint remote, iou_stream_conn_handlers h = {}) {
    return do_make(loop, std::move(sock), std::move(remote), std::move(h),
        std::nullopt);
  }

  // Initiate an async connect to `remote`. `on_drain` fires on success;
  // `on_close` fires on failure. Returns an empty handle on socket creation
  // failure.
  [[nodiscard]] static iou_stream_conn_ptr_with
  connect(const std::shared_ptr<iou_loop>& loop, const net_endpoint& remote,
      iou_stream_conn_handlers h = {}) {
    auto sock = net_socket::create_for(remote);
    if (!sock.is_open()) return {};
    return do_make(loop, std::move(sock), remote, std::move(h),
        connection_role::client);
  }

  // Create a listening socket bound to `local`. Each accepted connection
  // gets a copy of `h`. Returns an empty handle on failure.
  [[nodiscard]] static iou_stream_conn_ptr_with
  listen(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      iou_stream_conn_handlers h = {}) {
    auto sock = net_socket::create_for(local);
    if (!sock.is_open()) return {};
    if (!sock.set_reuse_addr()) return {};
    if (!sock.bind(local)) return {};
    if (!sock.listen()) return {};
    return do_make(loop, std::move(sock), net_endpoint::invalid, std::move(h),
        connection_role::server);
  }

  // Begin graceful close. Safe from any thread.
  [[nodiscard]] bool close() {
    if (!conn_) return false;
    return conn_->close();
  }

  [[nodiscard]] conn_t* operator->() noexcept { return conn_.get(); }
  [[nodiscard]] const conn_t* operator->() const noexcept {
    return conn_.get();
  }
  [[nodiscard]] explicit operator bool() const noexcept {
    return conn_ != nullptr;
  }

private:
  template<typename>
  friend class iou_stream_conn_ptr_with;

  explicit iou_stream_conn_ptr_with(shared_ptr_t conn)
      : conn_{std::move(conn)} {}

  static iou_stream_conn_ptr_with
  do_make(const std::shared_ptr<iou_loop>& loop, net_socket sock,
      net_endpoint remote, iou_stream_conn_handlers h,
      std::optional<connection_role> role,
      coordination_policy shutdown = coordination_policy::unilateral,
      block_size recv_buf_size = block_size::small,
      block_size send_buf_size = block_size::small) {
    assert(loop.get());
    bound_endpoint_with_timeout ep;
    ep.sockaddr.sockaddr = remote;
    ep.sockaddr.len = remote.sockaddr_size();
    auto conn = std::make_shared<T>(iou_stream_conn::allow::ctor, loop,
        std::move(sock), std::move(ep), std::move(h), role, shutdown,
        recv_buf_size, send_buf_size);
    if (!loop->post([p = conn] { return p->start_reading(); })) return {};
    return iou_stream_conn_ptr_with{conn};
  }

  shared_ptr_t conn_;
};

// Untyped alias for the common case.
using iou_stream_conn_ptr = iou_stream_conn_ptr_with<>;

#pragma endregion
#pragma region with_state

// Extends `iou_stream_conn` with a typed per-connection state value.
// `STATE` must be default-constructible.
//
// In callbacks (which receive `iou_stream_conn&`), recover the concrete type
// and state via `iou_stream_conn_with_state<STATE>::from(conn)`.
//
// Connections accepted by a listening
// `iou_stream_conn_ptr_with<iou_stream_conn_with_state<STATE>>` also have
// type `iou_stream_conn_with_state<STATE>` with a fresh default-constructed
// `STATE`.
template<typename STATE>
class iou_stream_conn_with_state: public iou_stream_conn {
public:
  using state_t = STATE;

  explicit iou_stream_conn_with_state(allow a,
      const std::shared_ptr<iou_loop>& loop, net_socket sock,
      bound_endpoint_with_timeout remote, iou_stream_conn_handlers h,
      std::optional<connection_role> role = {},
      coordination_policy shutdown = coordination_policy::unilateral,
      block_size recv_buf_size = block_size::small,
      block_size send_buf_size = block_size::small)
      : iou_stream_conn(a, loop, std::move(sock), std::move(remote),
            std::move(h), role, shutdown, recv_buf_size, send_buf_size) {}

  [[nodiscard]] state_t& state() noexcept { return state_; }
  [[nodiscard]] const state_t& state() const noexcept { return state_; }

  // Debug-safe downcast from `iou_stream_conn&`.
  [[nodiscard]] static iou_stream_conn_with_state& from(
      iou_stream_conn& c) noexcept {
    assert(dynamic_cast<iou_stream_conn_with_state*>(&c) != nullptr);
    return static_cast<iou_stream_conn_with_state&>(c);
  }

protected:
  [[nodiscard]] std::shared_ptr<iou_stream_conn>
  accept_clone(net_socket&& sock, const net_endpoint& remote) const override {
    auto loop = weak_loop_.lock();
    if (!loop) return nullptr;
    bound_endpoint_with_timeout ep;
    ep.sockaddr.sockaddr = remote;
    ep.sockaddr.len = remote.sockaddr_size();
    return std::make_shared<iou_stream_conn_with_state<state_t>>(allow::ctor,
        std::move(loop), std::move(sock), std::move(ep), own_handlers_,
        std::nullopt, shutdown_, recv_buf_size_, send_buf_size_);
  }

private:
  state_t state_{};
};

#pragma endregion
}}} // namespace corvid::proto::iouring
