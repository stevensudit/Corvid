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
    // TODO: If it's a Provided Buffer, as shown by its `bid`, then it must be
    // fully consumed, since we can't continue appending to it.
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
  // it.
  void stop_reading() { buf_.deactivate(); }

#pragma endregion
#pragma region Internals
private:
  friend class iou_stream_conn;
  using resume_fn = fixed_function<default_fixed_function::capacity,
      void(iou_buf_pool::buffer&&)>;

  iou_recv_view(iou_buf_pool::buffer buf, resume_fn resume) noexcept
      : buf_{std::move(buf)}, resume_{std::move(resume)} {}

private:
  iou_buf_pool::buffer buf_;
  resume_fn resume_;
#pragma endregion
};

#pragma endregion
#pragma region Handlers

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
// Attempts to use `submit_send_buffer` (`IORING_OP_SEND_ZC`), which may
// deliver two CQEs per send: one for the send completion and a notification
// CQE when the kernel releases the buffer. For sockets that do not support ZC,
// such as UDS pairs, it will fall back to `submit_write_buffer`, which
// generates a single CQE.
//
// Recv path: one SQE in flight at a time. The `iou_recv_view` token delivered
// to `on_data` controls re-submission via inline `consume` or async `take`.
//
// Thread safety: `send`, `close`, and `hangup` are safe from any thread.
// All I/O and state mutation run on the loop thread.
class iou_stream_conn: public std::enable_shared_from_this<iou_stream_conn> {
public:
  using buffer = iou_loop::buffer;
  using completion_token = iou_loop::completion_token;

#pragma region Accessors

  // True if the connection has not yet been closed. Safe to call from any
  // thread, but there's no guarantee that a connection will remain open, while
  // a closed one will remain closed.
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // The remote peer address. For accepted connections, computed lazily via
  // `getpeername` on first call. Safe to call from any thread.
  [[nodiscard]] const net_endpoint& remote_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (remote_.empty()) remote_ = net_endpoint::peer_of(sock_);
    return remote_;
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
        [conn = self(), d = std::move(data)]() mutable -> bool {
          if (!conn->open_) return false;
          conn->send_queue_.emplace_back(std::move(d));
          if (!conn->send_token_) return conn->do_submit_send();
          return true;
        });
  }

  // Queue a pre-filled registered buffer for zero-copy sending.
  // Safe to call from any thread.
  [[nodiscard]] bool send(buffer&& buf) {
    if (!open_ || !buf || buf.active_span().empty()) return false;
    return loop_.execute_or_post(
        [conn = self(), b = std::move(buf)]() mutable -> bool {
          if (!conn->open_) return false;
          conn->send_queue_.emplace_back(std::move(b));
          if (!conn->send_token_) return conn->do_submit_send();
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
    return loop_.execute_or_post([conn = self()] { return conn->do_close(); });
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
    return loop_.execute_or_post([conn = self()] {
      return conn->do_hangup_now();
    });
  }

  // Resume receiving after `stop_reading()`. Safe to call from any thread.
  [[nodiscard]] bool resume_recv() {
    return loop_.execute_or_post([conn = self()]() -> bool {
      if (!conn->open_ || !conn->recv_paused_ || conn->recv_token_)
        return false;
      conn->recv_paused_ = false;
      return conn->do_submit_recv();
    });
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
      net_socket sock, const net_endpoint* remote, iou_stream_conn_handlers h,
      std::optional<connection_role> role, coordination_policy shutdown,
      block_size recv_buf_size, block_size send_buf_size,
      shot_type recv_shot = shot_type::single)
      : loop_{*loop}, weak_loop_{loop}, sock_{std::move(sock)},
        remote_{remote ? *remote : net_endpoint{}},
        own_handlers_{std::move(h)},
        connecting_{role == connection_role::client},
        listening_{role == connection_role::server}, shutdown_{shutdown},
        recv_buf_size_{recv_buf_size}, send_buf_size_{send_buf_size},
        recv_intended_shot_{recv_shot}, recv_active_shot_{recv_shot} {}

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
  accept_clone(net_socket&& sock, const net_endpoint* remote = {}) const {
    auto lp = weak_loop_.lock();
    if (!lp) return nullptr;
    return std::make_shared<iou_stream_conn>(allow::ctor, std::move(lp),
        std::move(sock), remote, own_handlers_, std::nullopt, shutdown_,
        recv_buf_size_, send_buf_size_, recv_intended_shot_);
  }

#pragma endregion
#pragma region Data members
private:
  net_socket sock_;

  std::mutex endpoint_mutex_; // protects lazy initialization of endpoints.
  net_endpoint remote_;       // Always access through `remote_endpoint()` JIT.
  net_endpoint local_;        // Always access through `local_endpoint()` JIT.

  iou_stream_conn_handlers own_handlers_;

  relaxed_atomic_bool open_{true}; // Cleared once close starts.
  bool connecting_{};      // True when socket starts off trying to connect.
  bool listening_{};       // True when socket starts off listening.
  bool close_requested_{}; // True once `close` is called.
  bool close_notified_{};  // True once `on_close` fires.
  bool write_open_{true};  // False after SHUT_WR in bilateral close.
  bool peer_eof_{};        // True once peer sends EOF.
  relaxed_atomic_bool no_hangup_on_destruct_; // Set by `close`.
  relaxed_atomic<coordination_policy> shutdown_{
      coordination_policy::unilateral};

  // Size of borrowed buffers.
  relaxed_atomic<block_size> recv_buf_size_{block_size::kb004};
  relaxed_atomic<block_size> send_buf_size_{block_size::kb004};

  // Recv state: whether it's paused, whether we're trying for single or
  // multishot, and whether the current shot is single or multi. The
  // distinction is needed because we can be forced by buffer pressure to
  // downgrade from multi to single.
  bool recv_paused_{};
  shot_type recv_intended_shot_{shot_type::single};
  shot_type recv_active_shot_{shot_type::single};

  // When receiving, token of callback. Can be used for cancelation.
  completion_token recv_token_;

  // Send state: one SQE in flight at a time; queue holds strings and
  // buffers.
  std::deque<std::variant<std::string, buffer>> send_queue_;
  bool send_zc_supported_{true}; // Cleared on first EOPNOTSUPP.
  // When sending, token of callback. Can be used for cancelation.
  completion_token send_token_;

  // Connect/listen token.
  completion_token connect_token_;

#pragma endregion
#pragma region Helpers
private:
  // Submit buffer for recv.
  [[nodiscard]] bool do_submit_recv() {
    if (recv_active_shot_ == shot_type::single) return do_submit_single_recv();
    return do_submit_multi_recv();
  }

  // Submit buffer for singleshot recv, borrowing a read buffer if needed.
  // `allow_upgrade` prevents re-entering `do_submit_multi_recv` when multishot
  // submission itself fails.
  [[nodiscard]] bool
  do_submit_single_recv(buffer* bufptr = {}, bool allow_upgrade = true) {
    assert(loop_.is_loop_thread());
    if (recv_token_) return false;
    recv_active_shot_ = shot_type::single;

    if (allow_upgrade && recv_intended_shot_ == shot_type::multi &&
        (!bufptr || bufptr->payload_view().empty()) &&
        loop_.free_tcp_block_count() > 16)
      return do_submit_multi_recv();

    // Reuse buffer if possible.
    buffer buf;
    if (bufptr && *bufptr)
      buf = std::move(*bufptr);
    else
      buf = loop_.borrow_read_buffer(recv_buf_size_);

    if (!buf) return false;

    recv_token_ = loop_.submit_read_buffer(sock_, std::move(buf),
        [conn = self()](completion_id, buffer& b) {
          conn->recv_token_ = {};
          (void)conn->on_recv_complete(b);
          return slot_retention{};
        });
    return recv_token_.is_valid();
  }

  // Submit a multishot recv using the loop's TCP provided-buffer ring.
  // Automatically handles `has_more` failure.
  [[nodiscard]] bool do_submit_multi_recv() {
    assert(loop_.is_loop_thread() && !recv_token_);
    if (recv_paused_) return false;

    recv_active_shot_ = shot_type::multi;
    recv_token_ = loop_.submit_recv_buffer_multi(sock_,
        [conn = self()](completion_id cbid,
            buffer& buf) mutable -> slot_retention {
          if (!conn->open_) return slot_retention::release;
          const auto result = buf.result();
          bool has_more = buf.has_more();

          // Normal case.
          if (has_more) {
            (void)conn->on_recv_complete(buf);
            return slot_retention::automatic;
          }

          // Multishot has stopped. Decide what to do about it.
          conn->recv_token_ = {};

          // If it's an intentional cancelation, do not resuscitate.
          if (result.err() == EC::canceled) {
            conn->recv_paused_ = true;
            return slot_retention::release;
          }

          // If we ran out of buffers, downgrade to singleshot for now.
          if (result.err() == EC::nobufs) {
            (void)conn->do_submit_single_recv();
            return slot_retention::release;
          }

          // Not EOF or an error, so probably just a glitch. Retry.
          if (result.value() > 0 && !conn->recv_paused_) {
            conn->recv_token_ = completion_token{cbid};
            has_more = conn->loop_.submit_recv_buffer_multi(conn->sock_,
                completion_token{cbid});
            (void)conn->on_recv_complete(buf);
            if (has_more) return slot_retention::retain;
            conn->recv_token_ = {};
            conn->recv_paused_ = true;
            return slot_retention::release;
          }

          // Pass error on.
          (void)conn->on_recv_complete(buf);
          return slot_retention::release;
        });

    // If we can't start a multishot, try singleshot without upgrading again.
    if (!recv_token_) return do_submit_single_recv({}, false);

    return true;
  }

  // Pause recv, canceling any in-flight operations. Call `resume_recv()`
  // to restart.
  [[nodiscard]] bool stop_receiving() {
    assert(loop_.is_loop_thread());
    if (recv_paused_) return false;

    recv_paused_ = true;
    if (recv_token_) return loop_.submit_cancel(std::move(recv_token_));
    return true;
  }

  // Continue receiving after `stop_receiving`.
  [[nodiscard]] bool do_continue_recv() {
    assert(loop_.is_loop_thread() && !recv_token_);
    if (!recv_paused_) return false;

    recv_paused_ = false;
    return do_submit_recv();
  }

  // Build the resume callback captured by an `iou_recv_view`.
  [[nodiscard]] iou_recv_view::resume_fn make_resume() {
    return [conn = self()](buffer&& buf) {
      auto loop = conn->weak_loop_.lock();
      if (!loop) return;
      (void)loop->execute_or_post(
          [conn, buf = std::move(buf)]() mutable -> bool {
            if (!conn->open_) return false;

            // If backpressure, stop receiving.
            if (buf.result().err() == EC::notsock)
              return conn->stop_receiving();

            // If in singleshot mode, need to explicitly ask for more.
            if (!conn->recv_paused_ &&
                conn->recv_active_shot_ == shot_type::single)
            {
              // Buffer is full (no space for a kernel recv): re-deliver to
              // on_data without a round-trip to the kernel.
              if (buf && buf.active_span().empty())
                return conn->on_recv_complete(buf);
              return conn->do_submit_single_recv(&buf);
            }

            return true;
          });
    };
  }

  // Send the next buffer in the send queue.
  [[nodiscard]] bool do_submit_send() {
    assert(loop_.is_loop_thread() && !send_token_);
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
  //
  // Prefers `submit_send_buffer` (ZC). On first `EOPNOTSUPP` (such as with
  // Unix domain sockets), `on_send_complete` clears `send_zc_supported_` and
  // this helper falls back to `submit_write_buffer` for all subsequent sends.
  [[nodiscard]] bool do_submit_send_buffer(buffer&& buf) {
    assert(loop_.is_loop_thread() && !send_token_);
    if (!buf) return false;

    auto fn =
        [conn = self()](completion_id cbhandle, buffer& b) -> slot_retention {
      if (bitmask::has(b.cqe_flags(), iou_cqe_flags::notif))
        return b.pending_releases_decision();

      return conn->on_send_complete(cbhandle, b);
    };

    if (send_zc_supported_)
      send_token_ =
          loop_.submit_send_buffer(sock_, std::move(buf), std::move(fn));
    else
      send_token_ =
          loop_.submit_write_buffer(sock_, std::move(buf), std::move(fn));

    return send_token_.is_valid();
  }

  // Attempt to connect. On success, fires `on_drain` and transitions to recv
  // state. On failure, fires `on_close`.
  [[nodiscard]] bool do_submit_connect() {
    assert(loop_.is_loop_thread() && connecting_ && !connect_token_);

    const auto& remote = remote_endpoint();
    bound_endpoint_with_timeout ep;
    ep.when.ts = iou_timespec{10s};
    ep.sockaddr = {remote, remote.sockaddr_size()};

    connect_token_ = loop_.submit_connect(sock_, std::move(ep),
        [conn = self()](completion_id, iou_res res,
            iou_cqe_flags) -> slot_retention {
          (void)conn->on_connect_complete(res);
          return {};
        });
    return connect_token_.is_valid();
  }

  // Submit a multishot accept operation.
  [[nodiscard]] bool do_submit_accept() {
    assert(loop_.is_loop_thread() && listening_ && !connect_token_);
    // Set up a callback that resubmits itself, and use it to bootstrap the
    // initial submission.
    auto raw_cb = [conn = self()](completion_id cbid, iou_res res,
                      iou_cqe_flags flags) -> slot_retention {
      (void)conn->on_accept_complete(res);
      if (bitmask::has(flags, iou_cqe_flags::more))
        return slot_retention::automatic;
      if (!conn->listening_) return slot_retention::release;
      if (!conn->loop_.submit_accept_multishot(conn->sock_,
              completion_token{cbid}))
        throw std::runtime_error("Failed to re-arm multishot accept");
      return slot_retention::retain;
    };

    const auto cbtoken = loop_.tokenize(std::move(raw_cb));

    // Pretend a CQE arrived, with a soft error that will be ignored. This will
    // get it to submit itself.
    auto borrowed = loop_.borrow(cbtoken);
    (void)borrowed(cbtoken.as_int(), iou_res{*EC::again}, iou_cqe_flags{});
    loop_.detach(std::move(borrowed));

    connect_token_ = cbtoken;
    return connect_token_.is_valid();
  }

  // Close immediately without flushing.
  [[nodiscard]] bool do_close_now() {
    assert(loop_.is_loop_thread());
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    send_queue_.clear();
    if (sock_)
      (void)loop_.submit_close(std::move(sock_),
          [conn = self()](completion_id, iou_res, iou_cqe_flags) {
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
          [conn = self()](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });
      (void)loop_.immediate_submit();
    }
    return notify_close_once();
  }

  // Finalize a requested close once the send queue is empty. With `unilateral`
  // (or if the peer already sent EOF), closes immediately. With `bilateral`,
  // shuts down the write side via `SHUT_WR` and keeps the recv loop running to
  // discard incoming data until the peer sends EOF.
  [[nodiscard]] bool do_finish_close() {
    assert(loop_.is_loop_thread());
    if (shutdown_ == coordination_policy::unilateral || peer_eof_)
      return do_close_now();
    const auto token = loop_.submit_shutdown(sock_, shutdown_how::wr,
        [conn = self()](completion_id, iou_res res, iou_cqe_flags) {
          if (!res.ok()) (void)conn->do_close_now();
          return slot_retention{};
        });
    if (!token.is_valid()) return do_close_now();
    write_open_ = false;
    if (!recv_token_) return do_submit_recv();
    return true;
  }

  // Close after flushing.
  [[nodiscard]] bool do_close() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    close_requested_ = true;
    if (send_queue_.empty() && !send_token_) return do_finish_close();
    return true;
  }

#pragma endregion
#pragma region Event handlers

  // Resubmit `buf` using the same callback. This may be used for retriable
  // failures or partial sends.
  [[nodiscard]] bool do_resubmit_send(completion_id cbhandle, buffer& buf) {
    assert(loop_.is_loop_thread());
    assert(!buf.active_span().empty());
    send_token_ = completion_token{cbhandle};
    if (send_zc_supported_)
      return loop_.submit_send_buffer(sock_, buf, send_token_,
          slot_retention::automatic);
    else
      return loop_.submit_write_buffer(sock_, buf, send_token_,
          slot_retention::automatic);
  }

  // Handle a failed send CQE. Returns the slot retention decision.
  [[nodiscard]] slot_retention
  do_handle_send_error(completion_id cbhandle, buffer& buf) {
    assert(loop_.is_loop_thread());
    // Retry if possible.
    if (buf.result().is_soft_error() || buf.result().err() == EC::opnotsupp) {
      if (buf.result().err() == EC::opnotsupp) send_zc_supported_ = false;
      (void)do_resubmit_send(cbhandle, buf);
      return slot_retention::retain;
    }
    // Give up and close on hard errors.
    (void)do_close_now();
    return buf.pending_releases_decision();
  }

  // Handle completion of a send CQE (not the optional ZC notification CQE).
  //
  // For partial sends and retriable errors, resubmit using the same callback
  // so the buffer stays in the closure. A new slot is only used when advancing
  // to the next buffer. `pending_releases_` (maintained by
  // `iou_buffer::update`) tracks all outstanding ZC pins; we retain the
  // callback slot until it reaches zero.
  [[nodiscard]] slot_retention
  on_send_complete(completion_id cbhandle, buffer& buf) {
    assert(loop_.is_loop_thread());
    send_token_ = {};
    if (!open_) return buf.pending_releases_decision();

    // Retry on soft errors.
    if (!buf.result().ok()) return do_handle_send_error(cbhandle, buf);

    // Continue partial sends.
    if (!buf.active_span().empty()) {
      (void)do_resubmit_send(cbhandle, buf);
      return slot_retention::retain;
    }

    // Send complete. Start the next send if queued, or close if requested,
    // else notify drain.
    if (!send_queue_.empty())
      (void)do_submit_send();
    else if (close_requested_)
      (void)do_finish_close();
    else
      (void)notify_drained();

    return buf.pending_releases_decision();
  }

  // Handle completion of connect operation. On success, fires `on_drain` and
  // transitions to recv state. On failure, fires `on_close` and initiates
  // close.
  [[nodiscard]] bool on_connect_complete(iou_res res) {
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
  [[nodiscard]] bool on_accept_complete(iou_res res) {
    assert(loop_.is_loop_thread() && listening_);
    if (!open_) return true;

    // Error. If it's an `ECANCELED`, then we're either shutting down or
    // pausing: either way, there's nothing for us to do here.
    if (!res.ok()) {
      if (res.is_soft_error() || res.err() == EC::canceled) return true;
      return do_close_now();
    }

    net_socket accepted_sock{os_file{res.value()}};
    auto peer = accept_clone(std::move(accepted_sock));
    if (peer) (void)peer->start_reading();
    return true;
  }

  // Handle completion of a receive operation. If successful, delivers the
  // buffer to `on_data`. On error or EOF, initiates close.
  [[nodiscard]] bool on_recv_complete(buffer& buf) {
    assert(loop_.is_loop_thread());
    if (!open_) return true;

    // EOF from peer.
    const auto res = buf.result();
    if (res.value() == 0) {
      peer_eof_ = true;
      // Bilateral drain: peer sent EOF as expected, so close now.
      if (close_requested_ && !write_open_) return do_close_now();
      (void)notify_close_once();
      if (close_requested_ && !send_token_ && send_queue_.empty())
        return do_finish_close();
      return true;
    }

    // Fail on hard errors.
    if (!res && !res.is_soft_error()) return do_close_now();

    // Otherwise, keep reading (when there's a soft error or a drain).
    if (!res || (close_requested_ && !write_open_)) {
      if (recv_active_shot_ == shot_type::multi) {
        recv_token_ = {};
        return do_submit_recv();
      }
      return true;
    }

    // Process buffer and count on view destructor to continue receiving.
    iou_recv_view view{std::move(buf), make_resume()};
    if (own_handlers_.on_data)
      return own_handlers_.on_data(*this, std::move(view));

    return true;
  }

  // Send out SQEs for new connection so that we're ready to receive either a
  // connection completion, an accepted socket, or data. Without this, nothing
  // is keeping this instance alive.
  [[nodiscard]] bool start_reading() {
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
    (void)conn_->loop_.execute_or_post([conn = std::move(conn_)] {
      return conn->do_hangup_now();
    });
  }
  // NOLINTEND(bugprone-exception-escape)

  [[nodiscard]] const shared_ptr_t& pointer() const noexcept { return conn_; }
  [[nodiscard]] shared_ptr_t release() noexcept { return std::move(conn_); }

  // Adopt an already-connected socket. `sock` must be non-blocking.
  [[nodiscard]] static iou_stream_conn_ptr_with
  adopt(const std::shared_ptr<iou_loop>& loop, net_socket sock,
      net_endpoint remote, iou_stream_conn_handlers h = {},
      shot_type shot_recv = shot_type::single) {
    return do_make(loop, std::move(sock), &remote, std::move(h), std::nullopt,
        coordination_policy::unilateral, block_size::kb004, block_size::kb004,
        shot_recv);
  }

  // Initiate an async connect to `remote`. `on_drain` fires on success;
  // `on_close` fires on failure. Returns an empty handle on socket creation
  // failure.
  [[nodiscard]] static iou_stream_conn_ptr_with
  connect(const std::shared_ptr<iou_loop>& loop, const net_endpoint& remote,
      iou_stream_conn_handlers h = {},
      shot_type shot_recv = shot_type::single) {
    auto sock = net_socket::create_for(remote);
    if (!sock.is_open()) return {};
    return do_make(loop, std::move(sock), &remote, std::move(h),
        connection_role::client, coordination_policy::unilateral,
        block_size::kb004, block_size::kb004, shot_recv);
  }

  // Create a listening socket bound to `local`. Each accepted connection
  // gets a copy of `h`. Returns an empty handle on failure.
  [[nodiscard]] static iou_stream_conn_ptr_with
  listen(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      iou_stream_conn_handlers h = {},
      shot_type shot_recv = shot_type::single) {
    auto sock = net_socket::create_for(local);
    if (!sock.is_open()) return {};
    if (!sock.set_reuse_addr()) return {};
    if (!sock.bind(local)) return {};
    if (!sock.listen()) return {};
    return do_make(loop, std::move(sock), nullptr, std::move(h),
        connection_role::server, coordination_policy::unilateral,
        block_size::kb004, block_size::kb004, shot_recv);
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
      const net_endpoint* remote, iou_stream_conn_handlers h,
      std::optional<connection_role> role,
      coordination_policy shutdown = coordination_policy::unilateral,
      block_size recv_buf_size = block_size::kb004,
      block_size send_buf_size = block_size::kb004,
      shot_type shot_recv = shot_type::single) {
    assert(loop.get());
    auto conn = std::make_shared<T>(iou_stream_conn::allow::ctor, loop,
        std::move(sock), remote, std::move(h), role, shutdown, recv_buf_size,
        send_buf_size, shot_recv);
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
      const net_endpoint* remote, iou_stream_conn_handlers h,
      std::optional<connection_role> role = {},
      coordination_policy shutdown = coordination_policy::unilateral,
      block_size recv_buf_size = block_size::kb004,
      block_size send_buf_size = block_size::kb004,
      shot_type recv_shot = shot_type::single)
      : iou_stream_conn(a, loop, std::move(sock), remote, std::move(h), role,
            shutdown, recv_buf_size, send_buf_size, recv_shot) {}

  [[nodiscard]] state_t& state() noexcept { return state_; }
  [[nodiscard]] const state_t& state() const noexcept { return state_; }

  // Debug-safe downcast from `iou_stream_conn&`.
  [[nodiscard]] static iou_stream_conn_with_state& from(
      iou_stream_conn& c) noexcept {
    assert(dynamic_cast<iou_stream_conn_with_state*>(&c) != nullptr);
    return static_cast<iou_stream_conn_with_state&>(c);
  }

protected:
  [[nodiscard]] std::shared_ptr<iou_stream_conn> accept_clone(
      net_socket&& sock, const net_endpoint* remote = nullptr) const override {
    auto loop = weak_loop_.lock();
    if (!loop) return nullptr;
    return std::make_shared<iou_stream_conn_with_state<state_t>>(allow::ctor,
        std::move(loop), std::move(sock), remote, own_handlers_, std::nullopt,
        shutdown_, recv_buf_size_, send_buf_size_, recv_intended_shot_);
  }

private:
  state_t state_{};
};

#pragma endregion
}}} // namespace corvid::proto::iouring
