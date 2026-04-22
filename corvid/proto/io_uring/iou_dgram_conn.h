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
#include <string>
#include <string_view>
#include <variant>

#include <sys/uio.h>

#include "../../enums/bool_enums.h"
#include "../net_endpoint.h"
#include "iou_loop.h"

namespace corvid { inline namespace proto { inline namespace iouring {
using namespace bool_enums;

class iou_dgram_conn;

// User-supplied persistent callbacks for an `iou_dgram_conn`.
struct iou_dgram_conn_handlers {
  // Fired for each received datagram. `from` is the sender address.
  // `payload` is valid only for the duration of the call.
  std::function<bool(iou_dgram_conn&, std::string_view, const net_endpoint&)>
      on_data = nullptr;
  // Fired when all queued sends have completed and the queue is empty.
  std::function<bool(iou_dgram_conn&)> on_drain = nullptr;
};

template<typename STATE>
class iou_dgram_conn_with_state;

template<typename T>
class iou_dgram_conn_ptr_with;

// Unconnected UDP socket driven by an `iou_loop`. Created via
// `iou_dgram_conn_ptr_with::bind`.
//
// Recv path: one `recvmsg` SQE in flight at a time. A persistent `recv_ctx`
// holds the `iovec` and `msghdr` that must outlive the SQE.
//
// Send path: one `sendmsg` SQE in flight at a time for ordering. Additional
// sends queue behind it. JIT-borrows a write buffer for string items.
//
// Thread safety: `send_to` and `close` are safe from any thread.
class iou_dgram_conn: public std::enable_shared_from_this<iou_dgram_conn> {
public:
  using buffer = iou_loop::buffer;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] net_endpoint local_endpoint() const noexcept {
    return net_endpoint{sock_};
  }

  // Queue a datagram to `to`. JIT-borrows a write buffer.
  // Safe from any thread.
  [[nodiscard]] bool send_to(const net_endpoint& to, std::string data) {
    if (!open_) return false;
    if (data.empty()) return false;
    return loop_.execute_or_post(
        [p = self(), to, d = std::move(data)]() mutable -> bool {
          if (!p->open_) return false;
          p->send_queue_.push_back({to, std::move(d)});
          if (!p->send_in_flight_) return p->do_flush_sends();
          return true;
        });
  }

  // Queue a pre-filled registered buffer as a datagram to `to`. Zero-copy.
  // Safe from any thread.
  [[nodiscard]] bool send_to(const net_endpoint& to, buffer&& buf) {
    if (!open_) return false;
    if (!buf) return false;
    return loop_.execute_or_post(
        [p = self(), to, b = std::move(buf)]() mutable -> bool {
          if (!p->open_) return false;
          p->send_queue_.push_back({to, std::move(b)});
          if (!p->send_in_flight_) return p->do_flush_sends();
          return true;
        });
  }

  void close() {
    open_ = false;
    (void)loop_.execute_or_post([p = self()] {
      (void)p->sock_.close();
      return true;
    });
  }

private:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_dgram_conn_ptr_with;
  template<typename>
  friend class iou_dgram_conn_with_state;

public:
  explicit iou_dgram_conn(allow, const std::shared_ptr<iou_loop>& loop,
      net_socket sock, iou_dgram_conn_handlers h)
      : loop_{*loop}, weak_loop_{loop}, sock_{std::move(sock)},
        own_handlers_{std::move(h)}, open_{true} {}

  virtual ~iou_dgram_conn() = default;

  [[nodiscard]] std::shared_ptr<iou_dgram_conn> self() {
    return std::static_pointer_cast<iou_dgram_conn>(shared_from_this());
  }

protected:
  iou_loop& loop_;
  std::weak_ptr<iou_loop> weak_loop_;

private:
  net_socket sock_;
  iou_dgram_conn_handlers own_handlers_;
  relaxed_atomic_bool open_;

  // Recv context: all fields must outlive the in-flight `recvmsg` SQE.
  struct recv_ctx {
    buffer buf;
    net_endpoint_target peer_target;
    iovec iov{};  // msg_iov[0]: points into buf.active_span()
    msghdr hdr{}; // msg_name -> peer_addr, msg_iov -> &iov
  } recv_ctx_;
  bool recv_in_flight_{};

  // Send state.
  struct send_item {
    net_endpoint to;
    std::variant<std::string, buffer> data;
  };
  std::deque<send_item> send_queue_;

  // Persistent send context: valid while a send SQE is in flight.
  struct send_ctx {
    buffer buf;
    net_endpoint peer_addr;
    iovec iov{};  // msg_iov[0]: points into buf.active_span()
    msghdr hdr{}; // msg_name -> peer_addr, msg_iov -> &iov
  } send_ctx_;
  bool send_in_flight_{};

  // --- Internal methods. All run on the loop thread. ---

  bool do_submit_recv() {
    assert(!recv_in_flight_);
    recv_ctx_.buf = loop_.borrow_read_buffer();
    if (!recv_ctx_.buf) return false;
    auto span = recv_ctx_.buf.active_span();
    recv_ctx_.iov = {span.data(), span.size()};
    recv_ctx_.peer_target.sockaddr_len = net_endpoint::max_sockaddr_size;
    recv_ctx_.hdr = {};
    recv_ctx_.hdr.msg_name = recv_ctx_.peer_target.sockaddr.as_sockaddr();
    recv_ctx_.hdr.msg_namelen = recv_ctx_.peer_target.sockaddr_len;
    recv_ctx_.hdr.msg_iov = &recv_ctx_.iov;
    recv_ctx_.hdr.msg_iovlen = 1;
    recv_in_flight_ = true;
    return loop_
        .submit_recvmsg(sock_, &recv_ctx_.hdr,
            [p = self()](iou_res res,
                iou_cqe_flags flags) mutable -> slot_retention {
              (void)p->on_recv_complete(res, flags);
              return {};
            })
        .valid();
  }

  bool on_recv_complete(iou_res res, iou_cqe_flags) {
    recv_in_flight_ = false;
    if (!open_) return true;
    if (!res.ok()) {
      if (res.is_soft_error()) return do_submit_recv();
      return true; // Silently drop error datagrams.
    }
    if (res.value() > 0) {
      const auto n = static_cast<size_t>(res.value());
      std::string_view payload{
          reinterpret_cast<const char*>(recv_ctx_.iov.iov_base), n};
      net_endpoint& from = recv_ctx_.peer_target.sockaddr;
      if (own_handlers_.on_data)
        (void)own_handlers_.on_data(*this, payload, from);
    }
    // Rearm immediately.
    return do_submit_recv();
  }

  bool do_flush_sends() {
    assert(!send_in_flight_);
    if (send_queue_.empty()) return true;
    send_in_flight_ = true;

    auto& item = send_queue_.front();

    // Resolve buffer: JIT-borrow for strings, direct for registered buffers.
    if (std::holds_alternative<std::string>(item.data)) {
      send_ctx_.buf = loop_.borrow_write_buffer();
      if (!send_ctx_.buf) {
        send_in_flight_ = false;
        return false;
      }
      (void)send_ctx_.buf.append(std::get<std::string>(item.data));
    } else {
      send_ctx_.buf = std::move(std::get<buffer>(item.data));
    }

    // Build msghdr pointing into the write buffer's active_span.
    send_ctx_.peer_addr = item.to;
    auto span = send_ctx_.buf.active_span();
    send_ctx_.iov = {const_cast<std::byte*>(span.data()), span.size()};
    send_ctx_.hdr = {};
    send_ctx_.hdr.msg_name = &send_ctx_.peer_addr;
    send_ctx_.hdr.msg_namelen = net_socket::sockaddr_size(send_ctx_.peer_addr);
    send_ctx_.hdr.msg_iov = &send_ctx_.iov;
    send_ctx_.hdr.msg_iovlen = 1;

    send_queue_.pop_front();

    return loop_
        .submit_sendmsg(sock_, &send_ctx_.hdr,
            [p = self()](iou_res res,
                iou_cqe_flags flags) mutable -> slot_retention {
              (void)p->on_send_complete(res, flags);
              return {};
            })
        .valid();
  }

  bool on_send_complete(iou_res res, iou_cqe_flags) {
    send_in_flight_ = false;
    (void)res; // Datagrams are best-effort; ignore errors.
    send_ctx_.buf.reset();
    if (!open_) return true;
    if (!send_queue_.empty()) return do_flush_sends();
    if (own_handlers_.on_drain) return own_handlers_.on_drain(*this);
    return true;
  }

  bool register_with_loop() {
    if (!open_) return false;
    return do_submit_recv();
  }
};

// RAII handle that owns an `iou_dgram_conn` (or a derived class).
template<typename T = iou_dgram_conn>
class iou_dgram_conn_ptr_with {
  static_assert(std::derived_from<T, iou_dgram_conn>,
      "iou_dgram_conn_ptr_with<T>: T must derive from iou_dgram_conn");

public:
  using conn_t = T;
  using shared_ptr_t = std::shared_ptr<conn_t>;

  iou_dgram_conn_ptr_with() noexcept = default;
  iou_dgram_conn_ptr_with(iou_dgram_conn_ptr_with&&) noexcept = default;
  iou_dgram_conn_ptr_with(const iou_dgram_conn_ptr_with&) = delete;
  iou_dgram_conn_ptr_with& operator=(
      iou_dgram_conn_ptr_with&&) noexcept = default;
  iou_dgram_conn_ptr_with& operator=(const iou_dgram_conn_ptr_with&) = delete;

  template<typename U>
  requires std::derived_from<U, conn_t>
  iou_dgram_conn_ptr_with(iou_dgram_conn_ptr_with<U>&& other) noexcept
      : conn_{std::move(other.conn_)} {}

  // NOLINTBEGIN(bugprone-exception-escape)
  ~iou_dgram_conn_ptr_with() {
    if (conn_) conn_->close();
  }
  // NOLINTEND(bugprone-exception-escape)

  [[nodiscard]] const shared_ptr_t& pointer() const noexcept { return conn_; }
  [[nodiscard]] shared_ptr_t release() noexcept { return std::move(conn_); }

  // Bind a UDP socket to `local` and start receiving. Returns an empty handle
  // on failure.
  [[nodiscard]] static iou_dgram_conn_ptr_with
  bind(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      iou_dgram_conn_handlers h = {}) {
    auto sock = net_socket::create_for(local, execution::nonblocking,
        message_style::datagram);
    if (!sock.is_open()) return {};
    if (!sock.set_reuse_addr()) return {};
    if (!sock.bind(local)) return {};
    assert(loop.get());
    auto conn = std::make_shared<T>(iou_dgram_conn::allow::ctor, loop,
        std::move(sock), std::move(h));
    if (!loop->post([p = conn] { return p->register_with_loop(); })) return {};
    return iou_dgram_conn_ptr_with{conn};
  }

  void close() {
    if (conn_) conn_->close();
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
  friend class iou_dgram_conn_ptr_with;

  explicit iou_dgram_conn_ptr_with(shared_ptr_t conn)
      : conn_{std::move(conn)} {}

  shared_ptr_t conn_;
};

// Untyped alias for the common case.
using iou_dgram_conn_ptr = iou_dgram_conn_ptr_with<>;

// Extends `iou_dgram_conn` with a typed per-connection state value.
template<typename STATE>
class iou_dgram_conn_with_state: public iou_dgram_conn {
public:
  using state_t = STATE;

  explicit iou_dgram_conn_with_state(allow a,
      const std::shared_ptr<iou_loop>& loop, net_socket sock,
      iou_dgram_conn_handlers h)
      : iou_dgram_conn(a, loop, std::move(sock), std::move(h)) {}

  [[nodiscard]] state_t& state() noexcept { return state_; }
  [[nodiscard]] const state_t& state() const noexcept { return state_; }

  [[nodiscard]] static iou_dgram_conn_with_state& from(
      iou_dgram_conn& c) noexcept {
    assert(dynamic_cast<iou_dgram_conn_with_state*>(&c) != nullptr);
    return static_cast<iou_dgram_conn_with_state&>(c);
  }

private:
  state_t state_{};
};

}}} // namespace corvid::proto::iouring
