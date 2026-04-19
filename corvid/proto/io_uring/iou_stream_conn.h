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
#include "iou_loop.h"

namespace corvid { inline namespace proto { inline namespace iouring {
using namespace bool_enums;

// Fwd.
class iou_stream_conn;

// Move-only recv token delivered to `on_data`. Two consumption paths:
//   Inline: call `active_view()` + `consume(n)`. Destructor resubmits recv
//           into the remaining buffer (or a fresh one if fully consumed).
//   Async:  call `take()` to transfer buffer ownership for off-loop parsing.
//           Destructor posts a fresh-buffer recv immediately.
class iou_recv_view {
#pragma region Construction and assignment
public:
  iou_recv_view(iou_recv_view&&) noexcept = default;

  iou_recv_view(const iou_recv_view&) = delete;
  iou_recv_view& operator=(const iou_recv_view&) = delete;
  iou_recv_view& operator=(iou_recv_view&&) = delete;

  ~iou_recv_view() {
    if (!buf_) return; // moved-from
    resume_(std::move(buf_));
  }
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
#pragma endregion
#pragma region Internals
private:
  friend class iou_stream_conn;
  using resume_fn = std::function<void(iou_buf_pool::buffer&&)>;

  iou_recv_view(iou_buf_pool::buffer buf, resume_fn resume) noexcept
      : buf_{std::move(buf)}, resume_{std::move(resume)} {}
#pragma endregion
#pragma region Data members
private:
  iou_buf_pool::buffer buf_;
  resume_fn resume_;
#pragma endregion
};

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

// An `iou_stream_conn` is a non-blocking stream socket driven by an
// `iou_loop`. Instances are created, directly or indirectly, by
// `iou_stream_conn_ptr_with` factories, and registered with the loop.
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

  // True if the connection has not yet been closed.
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // The remote peer address supplied at construction. Safe to call from any
  // thread.
  [[nodiscard]] const net_endpoint& remote_endpoint() const noexcept {
    return remote_;
  }

  // The local address this socket is bound to. Useful after `listen` on
  // port 0 to discover the OS-assigned port. Safe to call from any thread.
  [[nodiscard]] net_endpoint local_endpoint() const noexcept {
    return net_endpoint{sock_};
  }

  // The `iou_loop` that drives this connection. Valid for the lifetime of
  // the connection. Loop-thread-only for mutation operations; reads are safe
  // from any thread, provided the connection is still alive.
  [[nodiscard]] iou_loop& loop() noexcept { return loop_; }

  // Return a weak pointer to the loop. The reason for this is to handle an
  // edge case that would lead to a crash during shutdown.
  //
  // Consider what happens if the connection is closed and the loop
  // is destroyed, but you have a callback running on some arbitrary thread
  // that kept this connection instance alive through its `shared_ptr`. If you
  // then call it and it tries to post to the loop, things will go badly.
  // Instead, you can attempt to upgrade the weak pointer, ensuring that the
  // loop is still alive.
  [[nodiscard]] std::weak_ptr<iou_loop> weak_loop() const noexcept {
    return weak_loop_;
  }

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

  // Start a close. If `coordination` is `unilateral` (the default), flushes
  // pending sends and then closes the socket. If `bilateral`, instead shuts
  // down the write side after flushing pending sends and discards incoming
  // data until the peer closes. Set the policy via `set_shutdown` before
  // calling this. Safe to call from any thread. Once called, destructing the
  // owning `iou_stream_conn_ptr_with` does not cause a forceful close.
  [[nodiscard]] bool close() {
    no_hangup_on_destruct_ = true;
    return loop_.execute_or_post([p = self()] { return p->do_close(); });
  }

  // The shutdown `coordination_policy` used by `close()`. `bilateral` shuts
  // down the write side after the send queue flushes, then discards incoming
  // data until the peer sends EOF. `unilateral` (the default) closes the
  // entire socket once the queue empties. Safe to call from any thread.
  [[nodiscard]] coordination_policy shutdown() const noexcept {
    return shutdown_;
  }

  // Set the shutdown coordination policy. `shutdown` defaults to `unilateral`.
  // Call before `close()`. Safe to call from any thread.
  void set_shutdown(coordination_policy shutdown) noexcept {
    shutdown_ = shutdown;
  }

  // Forceful close: cancel pending I/O and close immediately.
  // Safe from any thread.
  [[nodiscard]] bool hangup() {
    return loop_.execute_or_post([p = self()] { return p->do_close_now(); });
  }

private:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_stream_conn_ptr_with;
  template<typename>
  friend class iou_stream_conn_with_state;

public:
  // Public only for `std::make_shared`; use `iou_stream_conn_ptr_with`
  // factories instead.
  explicit iou_stream_conn(allow, const std::shared_ptr<iou_loop>& loop,
      net_socket sock, net_endpoint remote, iou_stream_conn_handlers h,
      std::optional<connection_role> role = {},
      coordination_policy shutdown = coordination_policy::unilateral)
      : loop_{*loop}, weak_loop_{loop}, shutdown_{shutdown},
        sock_{std::move(sock)}, remote_{remote}, own_handlers_{std::move(h)},
        open_{true}, connecting_{role == connection_role::client},
        listening_{role == connection_role::server} {}

  virtual ~iou_stream_conn() = default;

  [[nodiscard]] std::shared_ptr<iou_stream_conn> self() {
    return std::static_pointer_cast<iou_stream_conn>(shared_from_this());
  }

protected:
  iou_loop& loop_;
  std::weak_ptr<iou_loop> weak_loop_;

  // When `bilateral`, `close()` shuts down the write side after the send
  // queue flushes, then discards incoming data until the peer closes. When
  // `unilateral` (the default), `close()` closes the socket immediately once
  // the queue empties.
  relaxed_atomic<coordination_policy> shutdown_{
      coordination_policy::unilateral};

  // Produce a new `iou_stream_conn` for each accepted connection. Override in
  // `iou_stream_conn_with_state` to return a richer type. Returning `nullptr`
  // skips registration for that connection (connection-limiting hook).
  [[nodiscard]] virtual std::shared_ptr<iou_stream_conn>
  accept_clone(net_socket&& sock, const net_endpoint& remote,
      iou_stream_conn_handlers h) const {
    auto lp = weak_loop_.lock();
    if (!lp) return nullptr;
    return std::make_shared<iou_stream_conn>(allow::ctor, std::move(lp),
        std::move(sock), remote, std::move(h), std::nullopt, shutdown_);
  }

private:
  net_socket sock_;
  net_endpoint remote_;
  iou_stream_conn_handlers own_handlers_;

  relaxed_atomic_bool open_;
  bool connecting_{};
  bool listening_{};
  bool close_requested_{};
  bool close_notified_{};
  relaxed_atomic_bool no_hangup_on_destruct_;

  // Recv state: one SQE in flight at a time.
  bool recv_in_flight_{};

  // Send state: one SQE in flight at a time; queue holds strings and buffers.
  std::deque<std::variant<std::string, buffer>> send_queue_;
  bool send_in_flight_{};

  // Accept state (listener only). Must remain valid until the accept SQE
  // fires.
  net_endpoint_target accept_peer_target_;

  // Connect address. Must remain valid until the connect SQE fires.
  sockaddr_storage connect_addr_{};

private:
  bool do_submit_recv() {
    assert(loop_.is_loop_thread() && !recv_in_flight_);
    auto buf = loop_.borrow_read_buffer();
    if (!buf) return false;
    recv_in_flight_ = true;
    return loop_.submit_recv_buffer(sock_, std::move(buf),
        [p = self()](buffer& b) mutable -> bool {
          return p->on_recv_complete(b);
        });
  }

  // Resubmit recv using an existing buffer from a prior `on_recv_complete`.
  // If the buffer has no remaining active_span (completely full), re-delivers
  // any remaining payload first, then gets a fresh buffer.
  bool do_continue_recv(buffer&& buf) {
    assert(loop_.is_loop_thread() && !recv_in_flight_);
    // Space remains in this buffer; resubmit for more data.
    if (!buf.active_span().empty()) {
      recv_in_flight_ = true;
      return loop_.submit_recv_buffer(sock_, std::move(buf),
          [p = self()](buffer& b) mutable -> bool {
            return p->on_recv_complete(b);
          });
    }

    // Buffer fully consumed and reset to initial read state: fresh recv.
    if (buf.payload_span().empty()) return do_submit_recv();

    // Buffer is completely full but has remaining unconsumed payload.
    // Re-deliver it; when the handler consumes, the buffer resets and the view
    // destructor calls do_continue_recv again, which then falls through to
    // do_submit_recv.
    auto resume = make_resume();
    iou_recv_view view{std::move(buf), std::move(resume)};
    if (own_handlers_.on_data)
      return own_handlers_.on_data(*this, std::move(view));
    return true;
  }

  bool on_recv_complete(buffer& buf) {
    recv_in_flight_ = false;
    if (!open_) return true;

    const iou_res res = buf.result();
    if (res.value() == 0) {
      // EOF from peer.
      (void)notify_close_once();
      if (close_requested_ && !send_in_flight_ && send_queue_.empty())
        return do_close_now();
      return true;
    }
    if (!res.ok()) {
      if (res.is_soft_error()) return do_submit_recv();
      (void)notify_close_once();
      return do_close_now();
    }

    iou_recv_view view{std::move(buf), make_resume()};
    if (own_handlers_.on_data)
      return own_handlers_.on_data(*this, std::move(view));
    return true; // view destructs and resubmits recv
  }

  // Build the resume callback captured by an `iou_recv_view`.
  iou_recv_view::resume_fn make_resume() {
    return [wp = weak_loop_, p = self()](buffer&& b) mutable {
      auto lk = wp.lock();
      if (!lk) return;
      (void)lk->execute_or_post([p, b = std::move(b)]() mutable -> bool {
        if (!p->open_) return false;
        if (!b) return p->do_submit_recv();       // take() path: fresh recv
        return p->do_continue_recv(std::move(b)); // inline path
      });
    };
  }

  bool do_submit_send() {
    assert(loop_.is_loop_thread());
    assert(!send_in_flight_);
    if (send_queue_.empty()) return true;
    send_in_flight_ = true;

    if (std::holds_alternative<std::string>(send_queue_.front())) {
      // JIT-borrow a write buffer; pack consecutive string items.
      auto buf = loop_.borrow_write_buffer();
      if (!buf) {
        send_in_flight_ = false;
        return false;
      }
      while (!send_queue_.empty() &&
             std::holds_alternative<std::string>(send_queue_.front()))
      {
        if (!buf.append(std::get<std::string>(send_queue_.front()))) break;
        send_queue_.pop_front();
      }
      return loop_.submit_send_buffer(sock_, std::move(buf),
          [p = self()](buffer& b) mutable -> bool {
            return p->on_send_complete(b);
          });
    }

    // Direct buffer send.
    auto buf = std::move(std::get<buffer>(send_queue_.front()));
    send_queue_.pop_front();
    return loop_.submit_send_buffer(sock_, std::move(buf),
        [p = self()](buffer& b) mutable -> bool {
          return p->on_send_complete(b);
        });
  }

  bool on_send_complete(buffer& buf) {
    assert(loop_.is_loop_thread());
    send_in_flight_ = false;
    if (!open_) return true;

    if (!buf.result().ok()) return do_close_now();

    // Partial send: remaining bytes in active_span.
    if (!buf.active_span().empty()) {
      send_in_flight_ = true;
      return loop_.submit_send_buffer(sock_, std::move(buf),
          [p = self()](buffer& b) mutable -> bool {
            return p->on_send_complete(b);
          });
    }

    if (close_requested_ && send_queue_.empty()) return do_close_now();
    if (!send_queue_.empty()) return do_submit_send();
    return notify_drained();
  }

  bool do_submit_connect() {
    assert(loop_.is_loop_thread());
    assert(connecting_);
    return loop_.submit_connect(sock_, remote_,
        [p = self()](iou_res res) mutable -> bool {
          return p->handle_connect_complete(res);
        });
  }

  bool handle_connect_complete(iou_res res) {
    assert(loop_.is_loop_thread());
    connecting_ = false;
    if (!open_) return true;
    if (!res.ok()) {
      (void)notify_close_once();
      return do_close_now();
    }
    if (!do_submit_recv()) {
      (void)notify_close_once();
      return do_close_now();
    }
    if (send_queue_.empty()) return notify_drained();
    return do_submit_send();
  }

  bool do_submit_accept() {
    assert(loop_.is_loop_thread());
    assert(listening_);
    return loop_.submit_accept(sock_, accept_peer_target_,
        [p = self()](iou_res res) mutable -> bool {
          return p->handle_accept_complete(res);
        });
  }

  bool handle_accept_complete(iou_res res) {
    assert(loop_.is_loop_thread());
    if (!open_) return true;
    if (!res.ok()) {
      if (res.is_soft_error()) return do_submit_accept();
      (void)notify_close_once();
      return do_close_now();
    }
    net_socket new_sock{os_file{res.value()}};
    net_endpoint& remote = accept_peer_target_.sockaddr;
    auto peer = accept_clone(std::move(new_sock), remote, own_handlers_);
    if (peer) {
      auto lp = weak_loop_.lock();
      if (lp) (void)lp->post([p = peer] { return p->register_with_loop(); });
    }
    return do_submit_accept(); // re-arm for next connection
  }

  bool register_with_loop() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    if (listening_) return do_submit_accept();
    if (connecting_) return do_submit_connect();
    return do_submit_recv();
  }

  [[nodiscard]] bool notify_close_once() {
    assert(loop_.is_loop_thread());
    if (close_notified_) return false;
    close_notified_ = true;
    if (own_handlers_.on_close) return own_handlers_.on_close(*this);
    return true;
  }

  [[nodiscard]] bool notify_drained() {
    assert(loop_.is_loop_thread());
    if (own_handlers_.on_drain) return own_handlers_.on_drain(*this);
    return true;
  }

  [[nodiscard]] bool do_close_now() {
    assert(loop_.is_loop_thread());
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    send_queue_.clear();
    (void)sock_.close();
    close_requested_ = false;
    return notify_close_once();
  }

  [[nodiscard]] bool do_close() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    close_requested_ = true;
    if (send_queue_.empty() && !send_in_flight_) return do_close_now();
    return true;
  }
};

// RAII handle that owns an `iou_stream_conn` (or a derived class). Destruction
// calls `hangup` unless `close` was called first.
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

  // NOLINTBEGIN(bugprone-exception-escape)
  ~iou_stream_conn_ptr_with() {
    if (!conn_ || conn_->no_hangup_on_destruct_) return;
    (void)conn_->loop_.execute_or_post([p = std::move(conn_)] {
      return p->do_close_now();
    });
  }
  // NOLINTEND(bugprone-exception-escape)

  [[nodiscard]] const shared_ptr_t& pointer() const noexcept { return conn_; }
  [[nodiscard]] shared_ptr_t release() noexcept { return std::move(conn_); }

  // Adopt an already-connected socket. `sock` must be non-blocking.
  [[nodiscard]] static iou_stream_conn_ptr_with
  adopt(std::shared_ptr<iou_loop> loop, net_socket sock, net_endpoint remote,
      iou_stream_conn_handlers h = {}) {
    return do_make(std::move(loop), std::move(sock), std::move(remote),
        std::move(h), std::nullopt);
  }

  // Initiate an async connect to `remote`. `on_drain` fires on success;
  // `on_close` fires on failure. Returns an empty handle on socket creation
  // failure.
  [[nodiscard]] static iou_stream_conn_ptr_with
  connect(std::shared_ptr<iou_loop> loop, const net_endpoint& remote,
      iou_stream_conn_handlers h = {}) {
    auto sock = net_socket::create_for(remote);
    if (!sock.is_open()) return {};
    return do_make(std::move(loop), std::move(sock), remote, std::move(h),
        connection_role::client);
  }

  // Create a listening socket bound to `local`. Each accepted connection gets
  // a copy of `h`. Returns an empty handle on failure.
  [[nodiscard]] static iou_stream_conn_ptr_with
  listen(std::shared_ptr<iou_loop> loop, const net_endpoint& local,
      iou_stream_conn_handlers h = {}) {
    auto sock = net_socket::create_for(local);
    if (!sock.is_open()) return {};
    if (!sock.set_reuse_addr()) return {};
    if (!sock.bind(local)) return {};
    if (!sock.listen()) return {};
    return do_make(std::move(loop), std::move(sock), net_endpoint::invalid,
        std::move(h), connection_role::server);
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

  static iou_stream_conn_ptr_with do_make(std::shared_ptr<iou_loop> loop,
      net_socket sock, net_endpoint remote, iou_stream_conn_handlers h,
      std::optional<connection_role> role) {
    assert(loop.get());
    auto conn = std::make_shared<T>(iou_stream_conn::allow::ctor, loop,
        std::move(sock), std::move(remote), std::move(h), role);
    if (!loop->post([p = conn] { return p->register_with_loop(); })) return {};
    return iou_stream_conn_ptr_with{conn};
  }

  shared_ptr_t conn_;
};

// Untyped alias for the common case.
using iou_stream_conn_ptr = iou_stream_conn_ptr_with<>;

// Extends `iou_stream_conn` with a typed per-connection state value.
// `STATE` must be default-constructible.
//
// In callbacks (which receive `iou_stream_conn&`), recover the concrete type
// and state via `iou_stream_conn_with_state<STATE>::from(conn)`.
//
// Connections accepted by a listening
// `iou_stream_conn_ptr_with<iou_stream_conn_with_state<STATE>>` also have type
// `iou_stream_conn_with_state<STATE>` with a fresh default-constructed
// `STATE`.
template<typename STATE>
class iou_stream_conn_with_state: public iou_stream_conn {
public:
  using state_t = STATE;

  explicit iou_stream_conn_with_state(allow a, std::shared_ptr<iou_loop> loop,
      net_socket sock, net_endpoint remote, iou_stream_conn_handlers h,
      std::optional<connection_role> role = {},
      coordination_policy shutdown = coordination_policy::unilateral)
      : iou_stream_conn(a, std::move(loop), std::move(sock), std::move(remote),
            std::move(h), role, shutdown) {}

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
  accept_clone(net_socket&& sock, const net_endpoint& remote,
      iou_stream_conn_handlers h) const override {
    auto lp = weak_loop_.lock();
    if (!lp) return nullptr;
    return std::make_shared<iou_stream_conn_with_state<state_t>>(allow::ctor,
        std::move(lp), std::move(sock), remote, std::move(h), std::nullopt,
        shutdown_);
  }

private:
  state_t state_{};
};

}}} // namespace corvid::proto::iouring
