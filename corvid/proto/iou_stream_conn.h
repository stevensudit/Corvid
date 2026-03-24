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
#include <atomic>
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <unistd.h>
#include <fcntl.h>

#include "iouring_loop.h"
#include "net_endpoint.h"
#include "recv_buffer.h"
#include "../concurrency/relaxed_atomic.h"
#include "../strings/no_zero.h"

// NOTICE: This is purely vibe-coded. It is not ready for production.

// `iou_stream_conn` is the `io_uring`-backed equivalent of `stream_conn`.
//
// Established connections use `iouring_loop` completion mode:
//   - Receives via multi-shot `IORING_OP_RECV` with `IOSQE_BUFFER_SELECT`
//     (the kernel picks a pool buffer; `on_recv_complete` copies to the
//     per-connection `recv_buffer` and returns the pool buffer immediately).
//   - Sends via one-shot `IORING_OP_SEND`; `on_send_complete` handles short
//     sends and advances the send queue.
//
// Listener connections use poll mode (`IORING_OP_POLL_ADD`) to detect
// incoming connections via `on_readable` -> `accept4`.
//
// Outbound connections use poll mode briefly (EPOLLOUT to detect connect
// completion), then `handle_connect` cancels the poll subscription and
// switches to completion mode.

namespace corvid { inline namespace proto {

using namespace corvid::strings::no_zero_funcs;

// Forward declaration.
class iou_stream_conn;

// User-supplied persistent callbacks for an `iou_stream_conn`.
struct iou_stream_conn_handlers {
  std::function<bool(iou_stream_conn&, recv_buffer_view)> on_data = nullptr;
  std::function<bool(iou_stream_conn&)> on_drain = nullptr;
  std::function<bool(iou_stream_conn&)> on_close = nullptr;
};

// Forward declaration so `iou_stream_conn` can friend it.
template<typename STATE>
class iou_stream_conn_with_state;

// `iou_stream_conn` is a non-blocking stream socket driven by an
// `iouring_loop`. See `stream_conn` in `stream_conn.h` for the full design
// rationale; this class is API-compatible with `stream_conn`.
class iou_stream_conn: public iou_io_conn {
public:
  static constexpr size_t default_recv_buf_size = 16384;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] bool set_recv_buf_size(size_t size) {
    if (size == 0) return false;
    recv_buf_.min_capacity =
        std::min(size, std::numeric_limits<std::size_t>::max() / 2);
    return true;
  }

  [[nodiscard]] size_t recv_buf_size() const noexcept {
    return recv_buf_.min_capacity;
  }

  [[nodiscard]] bool can_read() const noexcept { return read_open_; }
  [[nodiscard]] bool can_write() const noexcept { return write_open_; }
  [[nodiscard]] bool is_mutual_close() const noexcept { return mutual_close_; }
  void set_mutual_close(bool on = true) noexcept { mutual_close_ = on; }

  [[nodiscard]] const net_endpoint& remote_endpoint() const noexcept {
    return remote_;
  }

  [[nodiscard]] net_endpoint local_endpoint() const noexcept {
    return net_endpoint{sock()};
  }

  [[nodiscard]] bool send(std::string&& buf) {
    if (buf.empty()) return false;
    if (!open_) return false;
    if (!write_open_) return false;
    return execute_or_post([p = self(), b = std::move(buf)]() mutable {
      return p->enqueue_send(std::move(b));
    });
  }

  [[nodiscard]] bool close() {
    no_hangup_on_destruct_ = true;
    return loop_.post([p = self()] { return p->do_close(); });
  }

  [[nodiscard]] bool hangup() {
    return execute_or_post([p = self()] { return p->do_hangup(); });
  }

  [[nodiscard]] bool shutdown_read(execution exec = execution::blocking) {
    if (!open_) return false;
    return exec_lambda(exec, [p = self()] { return p->do_shutdown_read(); });
  }

  [[nodiscard]] bool shutdown_write(execution exec = execution::blocking) {
    if (!open_) return false;
    return exec_lambda(exec, [p = self()] { return p->do_shutdown_write(); });
  }

private:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_stream_conn_ptr_with;
  template<typename>
  friend class iou_stream_conn_with_state;

public:
  explicit iou_stream_conn(allow, iouring_loop& loop, net_socket&& sock,
      const net_endpoint& remote, iou_stream_conn_handlers&& h, size_t rbs,
      bool connecting, bool listening) noexcept
      : iou_io_conn{std::move(sock)}, loop_{loop}, remote_{remote},
        own_handlers_{std::move(h)}, active_handlers_{&own_handlers_},
        open_{true}, connecting_{connecting}, listening_{listening} {
    recv_buf_.min_capacity =
        std::min(rbs, std::numeric_limits<std::size_t>::max() / 2);
    if (listening) write_open_ = false;
  }

protected:
  iouring_loop& loop_;

  [[nodiscard]] virtual std::shared_ptr<iou_stream_conn>
  accept_clone(net_socket&& sock, const net_endpoint& remote,
      iou_stream_conn_handlers handlers) const {
    return std::make_shared<iou_stream_conn>(allow::ctor, loop_,
        std::move(sock), remote, std::move(handlers), recv_buf_size(),
        /*connecting=*/false, /*listening=*/false);
  }

private:
  net_endpoint remote_;
  iou_stream_conn_handlers own_handlers_;
  std::atomic<iou_stream_conn_handlers*> active_handlers_;

  std::deque<std::string> send_queue_;
  std::string_view head_span_;
  recv_buffer recv_buf_;

  relaxed_atomic_bool open_;
  relaxed_atomic_bool no_hangup_on_destruct_{false};
  relaxed_atomic_bool read_open_{true};
  relaxed_atomic_bool write_open_{true};
  relaxed_atomic_bool registered_{false};

  bool connecting_ = false;
  bool listening_ = false;
  bool close_requested_ = false;
  bool close_notified_ = false;
  bool eof_pending_ = false;
  bool mutual_close_{false};

  [[nodiscard]] std::shared_ptr<iou_stream_conn> self() {
    return std::static_pointer_cast<iou_stream_conn>(shared_from_this());
  }

  // -----------------------------------------------------------------------
  // Registration.
  // -----------------------------------------------------------------------

  [[nodiscard]] bool register_with_loop() {
    if (!open_) return false;
    if (listening_) {
      // Poll mode: multi-shot POLL_ADD (EPOLLIN) to detect new connections.
      if (loop_.register_socket(shared_from_this(), /*readable=*/true,
              /*writable=*/false))
      {
        registered_ = true;
        return true;
      }
      return do_close_now(close_mode::forceful) && false;
    }
    if (connecting_) {
      // Poll mode briefly: EPOLLOUT fires when connect completes.
      if (loop_.register_socket(shared_from_this(), /*readable=*/false,
              /*writable=*/true))
      {
        registered_ = true;
        return true;
      }
      return do_close_now(close_mode::forceful) && false;
    }
    // Completion mode: register without POLL_ADD, then arm multi-shot recv.
    if (!loop_.register_conn(shared_from_this()))
      return do_close_now(close_mode::forceful) && false;
    registered_ = true;
    if (wants_read_events()) {
      if (!loop_.arm_recv(*this))
        return do_close_now(close_mode::forceful) && false;
    }
    return true;
  }

  // -----------------------------------------------------------------------
  // iou_io_conn virtual overrides.
  // -----------------------------------------------------------------------

  // Poll-mode readable: only called for listener sockets (accept loop).
  [[nodiscard]] bool on_readable() override {
    assert(loop_.is_loop_thread());
    if (listening_) return handle_listen();
    // Connecting sockets may see EPOLLIN/EPOLLRDHUP before the error fires;
    // treat as a spurious event.
    return true;
  }

  // Poll-mode writable: only called while `connecting_` is true (EPOLLOUT
  // signals that the async connect has finished, successfully or not).
  [[nodiscard]] bool on_writable() override {
    assert(loop_.is_loop_thread());
    if (connecting_) return handle_connect();
    return true;
  }

  // Poll-mode error: for listeners and connect failures.
  [[nodiscard]] bool on_error() override {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    if (listening_) return do_close_now(close_mode::forceful);
    if (connecting_) return handle_connect(); // SO_ERROR will be non-zero
    return true;
  }

  // Completion-mode receive: called when a multi-shot RECV CQE arrives.
  // `data` is valid only during this call; copy before returning.
  [[nodiscard]] bool
  on_recv_complete(int32_t res, const void* data, size_t len) override {
    assert(loop_.is_loop_thread());
    if (!open_) return true;

    if (res == -ECANCELED) return true; // cancelled by cancel_recv
    if (res == -ENOBUFS) {
      // Pool exhausted; subscription terminated. It will be re-armed by the
      // next refresh_read_interest call (e.g., from resume_receive).
      return true;
    }
    if (res < 0) return do_close_now(close_mode::forceful) && false;
    if (res == 0) return handle_read_eof(); // peer closed write side (EOF)

    // TODO: !!! This is an unnecessary copy. We need to change recv_buffer to
    // be more flexible.

    // Copy received bytes into the per-connection recv buffer.
    if (!ensure_recv_buf()) return do_close_now(close_mode::forceful) && false;
    const size_t old_end = recv_buf_.end.load(std::memory_order::relaxed);
    const size_t needed = old_end + len;
    if (recv_buf_.buffer.size() < needed)
      no_zero::enlarge_to(recv_buf_.buffer, needed);
    std::memcpy(recv_buf_.buffer.data() + old_end, data, len);
    recv_buf_.end.store(old_end + len, std::memory_order::release);
    no_zero::enlarge_to_cap(recv_buf_.buffer);

    if (recv_buf_.view_active) return true; // deliver when view is released
    if (!recv_buf_.active().empty()) return notify_read_ready();
    return true;
  }

  // Completion-mode send: called when a SEND CQE arrives.
  [[nodiscard]] bool on_send_complete(int32_t res) override {
    assert(loop_.is_loop_thread());
    if (!open_) return false;

    if (res == -ECANCELED) return true;
    if (res < 0) return handle_write_failure() && false;

    assert(res > 0);
    head_span_.remove_prefix(static_cast<size_t>(res));

    if (!head_span_.empty())
      return do_submit_head_send(); // short send: re-submit remainder

    send_queue_.pop_front();
    if (!send_queue_.empty()) {
      head_span_ = send_queue_.front();
      return do_submit_head_send();
    }

    // Queue drained.
    if (close_requested_) {
      if (are_handlers_external()) (void)notify_drained();
      return do_finish_close();
    }
    return notify_drained();
  }

  // -----------------------------------------------------------------------
  // Internal helpers.
  // -----------------------------------------------------------------------

  template<typename FN>
  [[nodiscard]] bool execute_or_post(FN&& fn) {
    if (loop_.is_loop_thread() && registered_) return fn();
    return loop_.post(std::forward<FN>(fn));
  }

  template<typename FN>
  [[nodiscard]] bool exec_lambda(execution exec, FN&& fn) {
    const auto is_loop_thread = loop_.is_loop_thread();
    const bool registered = registered_;
    if (is_loop_thread && registered) return fn();
    if (exec == execution::nonblocking || is_loop_thread || !registered)
      return loop_.post(std::forward<FN>(fn));
    return loop_.post_and_wait(std::forward<FN>(fn));
  }

  [[nodiscard]] iou_stream_conn_handlers*
  acquire_active_handlers() const noexcept {
    return active_handlers_.load(std::memory_order::acquire);
  }

  [[nodiscard]] bool wants_read_events() const noexcept {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    if (listening_) return true;
    if (!recv_buf_.reads_enabled) return false;
    if (!read_open_ || !acquire_active_handlers()->on_data) return false;
    return true;
  }

  [[nodiscard]] bool enable_reads(bool on = true) {
    assert(loop_.is_loop_thread());
    recv_buf_.reads_enabled = on;
    return refresh_read_interest();
  }

  [[nodiscard]] bool are_handlers_external() const noexcept {
    return acquire_active_handlers() != &own_handlers_;
  }

  [[nodiscard]] bool refresh_read_interest() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    return loop_.enable_reads(*this, wants_read_events());
  }

  [[nodiscard]] bool notify_close_once() {
    assert(loop_.is_loop_thread());
    if (close_notified_) return false;
    close_notified_ = true;
    auto* h = acquire_active_handlers();
    if (h->on_close) return h->on_close(*this);
    return true;
  }

  [[nodiscard]] bool notify_read_ready() {
    assert(loop_.is_loop_thread());
    auto* h = acquire_active_handlers();
    if (h->on_data) {
      recv_buf_.view_active = true;
      return h->on_data(*this,
          recv_buffer_view{recv_buf_, [sp = self()](size_t n, size_t lse) {
                             sp->resume_receive(n, lse);
                           }});
    }
    return refresh_read_interest();
  }

  [[nodiscard]] bool notify_read_closed() {
    assert(loop_.is_loop_thread());
    assert(!recv_buf_.view_active);
    auto* h = acquire_active_handlers();
    if (h->on_data) {
      recv_buf_.begin.store(0, std::memory_order::relaxed);
      recv_buf_.end.store(0, std::memory_order::relaxed);
      recv_buf_.view_active = true;
      return h->on_data(*this,
          recv_buffer_view{recv_buf_, [sp = self()](size_t n, size_t lse) {
                             sp->resume_receive(n, lse);
                           }});
    }
    return refresh_read_interest();
  }

  [[nodiscard]] bool notify_drained() {
    auto* h = acquire_active_handlers();
    if (h->on_drain) return h->on_drain(*this);
    return true;
  }

  [[nodiscard]] bool do_shutdown_read() {
    assert(loop_.is_loop_thread());
    if (!open_ || !read_open_) return false;
    if (!sock().shutdown(SHUT_RD))
      return do_close_now(close_mode::forceful) && false;
    read_open_ = false;
    // enable_reads(false) -> cancel_recv in completion mode, unset EPOLLIN in
    // poll mode.
    if (!loop_.enable_reads(*this, false)) return false;
    if (are_handlers_external() && !recv_buf_.view_active)
      (void)notify_read_closed();
    return maybe_finish_after_side_close() || true;
  }

  [[nodiscard]] bool do_shutdown_write() {
    assert(loop_.is_loop_thread());
    if (!open_ || !write_open_) return false;
    if (!sock().shutdown(SHUT_WR))
      return do_close_now(close_mode::forceful) && false;
    write_open_ = false;
    send_queue_.clear();
    head_span_ = {};
    // enable_writes(false) is a no-op in completion mode.
    if (!loop_.enable_writes(*this, false)) return false;
    return maybe_finish_after_side_close() || true;
  }

  [[nodiscard]] bool maybe_finish_after_side_close(
      close_mode mode = close_mode::graceful) {
    assert(loop_.is_loop_thread());
    if (!read_open_ && !write_open_) return do_close_now(mode) && false;
    return true;
  }

  [[nodiscard]] bool do_eof_notifications() {
    assert(loop_.is_loop_thread());
    if (are_handlers_external()) (void)notify_read_closed();
    (void)notify_close_once();
    if (close_requested_ && send_queue_.empty()) return do_close_now();
    if (!acquire_active_handlers()->on_close) return do_close();
    return maybe_finish_after_side_close();
  }

  [[nodiscard]] bool handle_read_eof() {
    assert(loop_.is_loop_thread());
    read_open_ = false;
    (void)loop_.enable_reads(*this, false); // cancel recv / unset EPOLLIN
    (void)loop_.enable_rdhup(*this, false); // no-op in completion mode
    if (recv_buf_.view_active) {
      eof_pending_ = true;
      return true;
    }
    if (!recv_buf_.active().empty()) {
      if (!notify_read_ready()) return false;
      if (recv_buf_.view_active) {
        eof_pending_ = true;
        return true;
      }
    }
    return do_eof_notifications() && false;
  }

  [[nodiscard]] bool handle_deferred_eof() {
    if (!recv_buf_.active().empty()) {
      return loop_.post([p = self()]() -> bool {
        if (!p->open_) return false;
        return p->notify_read_ready();
      });
    }
    eof_pending_ = false;
    return do_eof_notifications() && false;
  }

  [[nodiscard]] bool handle_write_failure() {
    assert(loop_.is_loop_thread());
    if (!write_open_->exchange(false, std::memory_order::relaxed))
      return false;
    send_queue_.clear();
    head_span_ = {};
    (void)loop_.enable_writes(*this, false); // no-op in completion mode
    if (close_requested_ || !read_open_)
      return do_close_now(close_mode::forceful);
    return maybe_finish_after_side_close(close_mode::forceful);
  }

  // Called from `on_writable()` when `connecting_` is true (EPOLLOUT fired).
  [[nodiscard]] bool handle_connect() {
    assert(loop_.is_loop_thread());
    assert(connecting_);
    connecting_ = false;

    const auto err = sock().get_option<int>(SOL_SOCKET, SO_ERROR);
    if (!err || *err != 0) return do_close_now(close_mode::forceful) && false;

    // Cancel the POLL_ADD (EPOLLOUT) and switch fd_reg to completion mode.
    (void)loop_.cancel_poll(*this);

    if (wants_read_events()) {
      if (!loop_.arm_recv(*this))
        return do_close_now(close_mode::forceful) && false;
    }

    if (!send_queue_.empty()) return do_submit_head_send();
    return notify_drained();
  }

  [[nodiscard]] bool handle_listen() {
    assert(loop_.is_loop_thread());
    while (true) {
      auto accepted = sock().accept();
      if (!accepted) break;
      auto peer = accept_clone(std::move(accepted->first),
          net_endpoint{accepted->second},
          iou_stream_conn_handlers{own_handlers_});
      if (!peer) continue;
      if (!peer->register_with_loop()) return false;
    }
    return true;
  }

  [[nodiscard]] bool ensure_recv_buf() {
    if (!recv_buf_.buffer.empty()) return true;
    const size_t configured = recv_buf_.min_capacity;
    const size_t actual =
        no_zero::enlarge_to(recv_buf_.buffer, configured).size();
    auto expected = configured;
    recv_buf_.min_capacity->compare_exchange_strong(expected, actual,
        std::memory_order::relaxed, std::memory_order::relaxed);
    return true;
  }

  // Submit `IORING_OP_SEND` for the head of the send queue.
  [[nodiscard]] bool do_submit_head_send() {
    assert(loop_.is_loop_thread());
    assert(!send_queue_.empty());
    if (!loop_.submit_send(*this, head_span_.data(), head_span_.size()))
      return do_close_now(close_mode::forceful) && false;
    return true;
  }

  // Enqueue `their_buf` for sending. If no send is in flight and we are not
  // still connecting, kick off an immediate `IORING_OP_SEND`.
  [[nodiscard]] bool enqueue_send(std::string&& their_buf) {
    assert(loop_.is_loop_thread());
    auto buf = std::move(their_buf);

    if (!open_ || !write_open_) return false;

    const bool was_empty = send_queue_.empty();
    send_queue_.push_back(std::move(buf));
    if (was_empty) head_span_ = send_queue_.front();

    if (connecting_) return true; // send will be submitted after connect
    if (!was_empty) return true;  // send already in flight

    return do_submit_head_send();
  }

  void resume_receive(size_t new_size = 0, size_t last_seen_end = 0) {
    (void)execute_or_post([p = self(), new_size, last_seen_end]() -> bool {
      if (!p->open_) return false;
      p->recv_buf_.view_active = false;

      const bool unseen_bytes =
          p->recv_buf_.end.load(std::memory_order::acquire) > last_seen_end;
      p->recv_buf_.compact(new_size);

      if (p->eof_pending_) return p->handle_deferred_eof();

      if (p->recv_buf_.write_space() == 0) {
        return p->loop_.post([p]() -> bool {
          if (!p->open_) return false;
          return p->notify_read_ready();
        });
      }

      if (unseen_bytes && !p->recv_buf_.active().empty()) {
        return p->loop_.post([p]() -> bool {
          if (!p->open_) return false;
          return p->notify_read_ready();
        });
      }

      return p->refresh_read_interest();
    });
  }

  [[nodiscard]] bool do_close() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    close_requested_ = true;
    if (!send_queue_.empty()) return true;
    return do_finish_close();
  }

  [[nodiscard]] bool do_finish_close() {
    assert(loop_.is_loop_thread());
    if (!mutual_close_) return do_close_now();
    if (!do_shutdown_write()) return true;
    return loop_.enable_reads(*this, true);
  }

  [[nodiscard]] bool handle_drain_reads() {
    assert(loop_.is_loop_thread());
    ensure_recv_buf();
    if (!sock().recv_at(recv_buf_.buffer, 0)) {
      const bool hard_error = recv_buf_.buffer.empty();
      no_zero::enlarge_to_cap(recv_buf_.buffer);
      if (hard_error) return do_close_now(close_mode::forceful) && false;
      return do_close_now();
    }
    recv_buf_.end.store(0, std::memory_order::relaxed);
    no_zero::enlarge_to_cap(recv_buf_.buffer);
    return true;
  }

  [[nodiscard]] bool do_hangup() {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    // TODO: Deal with redundancy between this and do_close_now().
    send_queue_.clear();
    head_span_ = {};
    close_requested_ = false;
    return do_close_now(close_mode::forceful);
  }

  [[nodiscard]] bool do_close_now(close_mode mode = close_mode::graceful) {
    assert(loop_.is_loop_thread());
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;

    read_open_ = false;
    write_open_ = false;

    (void)loop_.unregister_socket(sock());
    (void)sock().close(mode);

    send_queue_.clear();
    head_span_ = {};
    close_requested_ = false;

    return notify_close_once();
  }
};

// Move-only smart pointer owning an `iou_stream_conn` (or derived class).
template<typename T = iou_stream_conn>
class iou_stream_conn_ptr_with {
  static_assert(std::derived_from<T, iou_stream_conn>,
      "iou_stream_conn_ptr_with<T>: T must derive from iou_stream_conn");

public:
  iou_stream_conn_ptr_with() noexcept = default;

  iou_stream_conn_ptr_with(iou_stream_conn_ptr_with&&) noexcept = default;
  iou_stream_conn_ptr_with(const iou_stream_conn_ptr_with&) = delete;

  iou_stream_conn_ptr_with& operator=(iou_stream_conn_ptr_with&&) = default;
  iou_stream_conn_ptr_with& operator=(
      const iou_stream_conn_ptr_with&) = delete;

  template<typename U>
  requires std::derived_from<U, T>
  iou_stream_conn_ptr_with(iou_stream_conn_ptr_with<U>&& other) noexcept
      : conn_(std::move(other.conn_)) {}

  // NOLINTBEGIN(bugprone-exception-escape)
  ~iou_stream_conn_ptr_with() {
    if (!conn_ || conn_->no_hangup_on_destruct_) return;
    (void)conn_->loop_.execute_or_post([p = std::move(conn_)] {
      return p->do_hangup();
    });
  }
  // NOLINTEND(bugprone-exception-escape)

  [[nodiscard]] const std::shared_ptr<T>& pointer() const { return conn_; }
  [[nodiscard]] std::shared_ptr<T> release() { return std::move(conn_); }

  [[nodiscard]] static iou_stream_conn_ptr_with adopt(iouring_loop& loop,
      net_socket&& sock, const net_endpoint& remote,
      iou_stream_conn_handlers&& h = {},
      size_t recv_buf_size = iou_stream_conn::default_recv_buf_size) {
    assert((sock.get_flags().value_or(0) & O_NONBLOCK) != 0);
    return iou_stream_conn_ptr_with{loop, std::move(sock), remote,
        std::move(h), recv_buf_size, /*connecting=*/false,
        /*listening=*/false};
  }

  [[nodiscard]] static iou_stream_conn_ptr_with connect(iouring_loop& loop,
      const net_endpoint& remote, iou_stream_conn_handlers&& h = {},
      const net_endpoint& local = net_endpoint::invalid,
      size_t recv_buf_size = iou_stream_conn::default_recv_buf_size) {
    auto sock = net_socket::create_for(remote);
    if (!sock.is_open()) return {};

    if (local && !sock.bind(local)) return {};

    const auto result = sock.connect(remote);
    if (result && !*result) return {};
    const auto still_connecting = !result;

    auto ptr = iou_stream_conn_ptr_with{loop, std::move(sock), remote,
        std::move(h), recv_buf_size, /*connecting=*/still_connecting,
        /*listening=*/false};

    if (!still_connecting) {
      if (!loop.post([p = ptr.conn_] {
            if (p->open_ && p->send_queue_.empty()) return p->notify_drained();
            return false;
          }))
        ptr.conn_.reset();
    }

    return ptr;
  }

  [[nodiscard]] static iou_stream_conn_ptr_with listen(iouring_loop& loop,
      const net_endpoint& local, iou_stream_conn_handlers&& h = {},
      bool reuse_port = false) {
    auto sock = net_socket::create_for(local);
    if (!sock.is_open()) return {};

    if (!sock.set_reuse_addr()) return {};
    if (reuse_port && !sock.set_reuse_port()) return {};
    if (!sock.bind(local)) return {};
    if (!sock.listen()) return {};

    return iou_stream_conn_ptr_with{loop, std::move(sock),
        net_endpoint::invalid, std::move(h),
        iou_stream_conn::default_recv_buf_size,
        /*connecting=*/false, /*listening=*/true};
  }

  [[nodiscard]] bool close() {
    if (!conn_) return false;
    return conn_->close();
  }

  [[nodiscard]] T* operator->() noexcept { return conn_.get(); }
  [[nodiscard]] const T* operator->() const noexcept { return conn_.get(); }

  [[nodiscard]] explicit operator bool() const noexcept {
    return conn_ != nullptr;
  }

private:
  template<typename>
  friend class iou_stream_conn_ptr_with;

  explicit iou_stream_conn_ptr_with(iouring_loop& loop, net_socket&& sock,
      const net_endpoint& remote, iou_stream_conn_handlers&& h,
      size_t recv_buf_size, bool connecting, bool listening) {
    assert((sock.get_flags().value_or(0) & O_NONBLOCK) != 0);
    if (recv_buf_size == 0)
      recv_buf_size = iou_stream_conn::default_recv_buf_size;
    conn_ = std::make_shared<T>(iou_stream_conn::allow::ctor, loop,
        std::move(sock), remote, std::move(h), recv_buf_size, connecting,
        listening);
    if (!loop.post([p = conn_] { return p->register_with_loop(); }))
      conn_.reset();
  }

  std::shared_ptr<T> conn_;
};

// Untyped alias.
using iou_stream_conn_ptr = iou_stream_conn_ptr_with<>;

// Extends `iou_stream_conn` with a typed per-connection state value.
template<typename STATE>
class iou_stream_conn_with_state: public iou_stream_conn {
public:
  using state_t = STATE;

  explicit iou_stream_conn_with_state(allow a, iouring_loop& loop,
      net_socket&& sock, const net_endpoint& remote,
      iou_stream_conn_handlers&& h, size_t rbs, bool connecting,
      bool listening) noexcept
      : iou_stream_conn(a, loop, std::move(sock), remote, std::move(h), rbs,
            connecting, listening) {}

  [[nodiscard]] state_t& state() noexcept { return state_; }
  [[nodiscard]] const state_t& state() const noexcept { return state_; }

  [[nodiscard]] static iou_stream_conn_with_state& from(
      iou_stream_conn& c) noexcept {
    assert(dynamic_cast<iou_stream_conn_with_state*>(&c) != nullptr);
    return static_cast<iou_stream_conn_with_state&>(c);
  }

protected:
  [[nodiscard]] std::shared_ptr<iou_stream_conn>
  accept_clone(net_socket&& sock, const net_endpoint& remote,
      iou_stream_conn_handlers handlers) const override {
    return std::make_shared<iou_stream_conn_with_state<state_t>>(allow::ctor,
        loop_, std::move(sock), remote, std::move(handlers), recv_buf_size(),
        /*connecting=*/false, /*listening=*/false);
  }

private:
  state_t state_{};
};

}} // namespace corvid::proto
