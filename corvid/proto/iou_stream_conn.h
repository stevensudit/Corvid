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

// `iou_stream_conn` is the `io_uring`-backed equivalent of `stream_conn`.
//
// The two types are structurally identical: the only difference is that
// `iou_stream_conn` is registered with an `iouring_loop` (via `iou_io_conn`)
// rather than an `epoll_loop` (via `io_conn`). All data-path logic, send/
// receive buffering, half-close semantics, and the three-factory creation
// model (`adopt`, `connect`, `listen`) are unchanged.
//
// When `stream_async.h` support is needed, use `iou_stream_async.h` (not yet
// provided), which mirrors `stream_async.h` with the renamed types.

namespace corvid { inline namespace proto {

using namespace corvid::strings::no_zero_funcs;

// Forward declaration.
class iou_stream_conn;

// User-supplied persistent callbacks for an `iou_stream_conn`. Identical
// interface to `stream_conn_handlers`.
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

  [[nodiscard]] bool register_with_loop() {
    if (!open_) return false;
    const bool want_write = connecting_ || !send_queue_.empty();
    const bool want_read = !connecting_ && wants_read_events();
    if (loop_.register_socket(shared_from_this(), want_read, want_write)) {
      registered_ = true;
      return true;
    }
    return do_close_now(close_mode::forceful) && false;
  }

  [[nodiscard]] bool on_readable() override {
    assert(loop_.is_loop_thread());
    if (listening_) return handle_listen();
    if (connecting_) return handle_connect();
    if (close_requested_) return handle_drain_reads();
    return handle_readable();
  }

  [[nodiscard]] bool on_writable() override {
    assert(loop_.is_loop_thread());
    assert(!listening_);
    if (connecting_) return handle_connect();
    return flush_send_queue();
  }

  [[nodiscard]] bool on_error() override {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    if (listening_) return do_close_now(close_mode::forceful);
    if (connecting_) return handle_connect();
    if (read_open_) {
      if (wants_read_events()) return handle_readable();
      const auto eof = sock().peek_eof();
      if (!eof.has_value() || !*eof)
        return do_close_now(close_mode::forceful) && false;
      return handle_read_eof();
    }

    return maybe_finish_after_side_close() || true;
  }

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
    // TODO: We can do this in one step.
    (void)loop_.enable_reads(*this, false);
    (void)loop_.enable_rdhup(*this, false);
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
    (void)loop_.enable_writes(*this, false);
    if (close_requested_ || !read_open_)
      return do_close_now(close_mode::forceful);
    return maybe_finish_after_side_close(close_mode::forceful);
  }

  [[nodiscard]] bool handle_connect() {
    assert(loop_.is_loop_thread());
    assert(connecting_);
    connecting_ = false;

    const auto err = sock().get_option<int>(SOL_SOCKET, SO_ERROR);
    if (!err || *err != 0) return do_close_now(close_mode::forceful) && false;

    (void)refresh_read_interest();

    if (send_queue_.empty()) {
      if (!loop_.enable_writes(*this, false))
        return do_close_now(close_mode::forceful) && false;
      return notify_drained();
    }
    return true;
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

  void ensure_recv_buf() {
    if (!recv_buf_.buffer.empty()) return;
    const size_t configured = recv_buf_.min_capacity;
    const size_t actual =
        no_zero::enlarge_to(recv_buf_.buffer, configured).size();
    auto expected = configured;
    recv_buf_.min_capacity->compare_exchange_strong(expected, actual,
        std::memory_order::relaxed, std::memory_order::relaxed);
  }

  [[nodiscard]] bool handle_readable() {
    if (!read_open_) return false;

    ensure_recv_buf();

    const size_t space = recv_buf_.write_space();
    if (space == 0) {
      if (!loop_.enable_reads(*this, false)) return false;
      return true;
    }

    const size_t old_end = recv_buf_.end.load(std::memory_order::relaxed);
    if (!sock().recv_at(recv_buf_.buffer, old_end)) {
      const bool hard_error = recv_buf_.buffer.size() == old_end;
      no_zero::enlarge_to_cap(recv_buf_.buffer);
      if (hard_error) return do_close_now(close_mode::forceful) && false;
      return handle_read_eof();
    }

    recv_buf_.end.store(recv_buf_.buffer.size(), std::memory_order::release);
    no_zero::enlarge_to_cap(recv_buf_.buffer);

    if (recv_buf_.view_active) return true;
    if (!recv_buf_.active().empty()) return notify_read_ready();
    return true;
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

  [[nodiscard]] bool enqueue_send(std::string&& their_buf) {
    assert(loop_.is_loop_thread());
    auto buf = std::move(their_buf);

    if (!open_ || !write_open_) return false;

    if (const auto send_queue_empty = send_queue_.empty();
        !send_queue_empty || connecting_)
    {
      send_queue_.push_back(std::move(buf));
      if (send_queue_empty) head_span_ = send_queue_.front();
      return true;
    }

    auto buf_view = std::string_view{buf};
    if (!sock().send(buf_view)) return handle_write_failure() && false;

    if (buf_view.empty()) return notify_drained();

    const size_t sent = buf.size() - buf_view.size();
    send_queue_.push_back(std::move(buf));
    head_span_ = send_queue_.front();
    head_span_.remove_prefix(sent);
    return loop_.enable_writes(*this);
  }

  [[nodiscard]] bool flush_send_queue() {
    if (!open_) return false;

    while (!send_queue_.empty()) {
      if (!sock().send(head_span_)) return handle_write_failure() && false;

      if (!head_span_.empty()) return true;

      send_queue_.pop_front();
      if (!send_queue_.empty()) head_span_ = send_queue_.front();
    }

    if (!loop_.enable_writes(*this, false))
      return do_close_now(close_mode::forceful) && false;

    if (close_requested_) {
      if (are_handlers_external()) (void)notify_drained();
      return do_finish_close();
    }

    return notify_drained();
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
// API-compatible with `stream_conn_ptr_with`.
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
// API-compatible with `stream_conn_with_state<STATE>`.
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
