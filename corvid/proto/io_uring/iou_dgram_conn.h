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
#include <variant>

#include "../../enums/bool_enums.h"
#include "../net_endpoint.h"
#include "iou_loop.h"

namespace corvid { inline namespace proto { namespace iouring {
using namespace bool_enums;

class iou_dgram_conn;

#pragma region Handlers
// User-supplied persistent callbacks for an `iou_dgram_conn`.
struct iou_dgram_conn_handlers {
  // Fired for each received datagram. Use `buf.payload_view()` for the data
  // and `buf.peer_addr()` for the sender. `buf` is valid only for the call.
  std::function<bool(iou_dgram_conn&, iou_loop::buffer&)> on_data = nullptr;
  // Fired for each sent datagram. Use `buf.peer_addr()` for the sender. `buf`
  // is valid only for the call.
  std::function<bool(iou_dgram_conn&, iou_loop::buffer&)> on_drain = nullptr;
};
#pragma endregion

// Fwd.
template<typename STATE>
class iou_dgram_conn_with_state;

template<typename T>
class iou_dgram_conn_ptr_with;

#pragma region iou_dgram_conn

// Unconnected UDP socket driven by an `iou_loop`. Created via
// `iou_dgram_conn_ptr_with::bind`.
//
// Recv path: one `recvmsg` SQE in flight at a time. The buffer is moved into
// the completion closure; `msghdr` and `iovec` are owned by `iou_buffer`.
//
// Send path: one `sendmsg` SQE in flight at a time for ordering. Additional
// sends queue behind it. JIT-borrows a write buffer for string items.
//
// Thread safety: `send_to` and `close` are safe from any thread.
class iou_dgram_conn: public std::enable_shared_from_this<iou_dgram_conn> {
public:
  using buffer = iou_loop::buffer;

#pragma region Control

  // Close the socket. Safe to call from any thread.
  void close() {
    if (!open_->exchange(false, std::memory_order::relaxed)) return;
    (void)loop_.submit_close(std::move(sock_),
        [p = self()](completion_id, iou_res, iou_cqe_flags) {
          return slot_retention{};
        });
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] const net_endpoint& local_endpoint() const noexcept {
    return local_endpoint_;
  }

#pragma endregion
#pragma region Send

  // Send a registered buffer as a datagram to its `peer_addr`. Safe to call
  // from any thread.
  [[nodiscard]] bool send_to(buffer&& buf) {
    if (!open_) return false;
    if (!buf) return false;
    const auto token = loop_.submit_sendmsg_buffer(sock_, std::move(buf),
        [p = self()](completion_id, buffer& b) -> slot_retention {
          (void)p->on_send_complete(b);
          return {};
        });
    return token.is_valid();
  }

private:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_dgram_conn_ptr_with;
  template<typename>
  friend class iou_dgram_conn_with_state;

public:
  explicit iou_dgram_conn(allow, const std::shared_ptr<iou_loop>& loop,
      net_socket sock, net_endpoint bound_endpoint, iou_dgram_conn_handlers h)
      : loop_{*loop}, weak_loop_{loop}, sock_{std::move(sock)},
        local_endpoint_{bound_endpoint}, own_handlers_{std::move(h)},
        open_{true} {}

  virtual ~iou_dgram_conn() = default;

  [[nodiscard]] std::shared_ptr<iou_dgram_conn> self() {
    return std::static_pointer_cast<iou_dgram_conn>(shared_from_this());
  }

#pragma region Child access
protected:
  iou_loop& loop_;
  std::weak_ptr<iou_loop> weak_loop_;

#pragma endregion
#pragma region Data members
private:
  net_socket sock_;
  net_endpoint local_endpoint_;
  iou_dgram_conn_handlers own_handlers_;
  relaxed_atomic_bool open_;

  bool recv_in_flight_{};

#pragma endregion
#pragma region Helpers

  // Receive one datagram.
  bool do_submit_recv() {
    assert(loop_.is_loop_thread());
    assert(!recv_in_flight_);
    auto buf = loop_.borrow_read_buffer();
    if (!buf) return false;
    recv_in_flight_ = true;
    const auto token = loop_.submit_recvmsg_buffer(sock_, std::move(buf),
        [p = self()](completion_id, buffer& b) -> slot_retention {
          (void)p->on_recv_complete(b);
          return {};
        });
    return token.is_valid();
  }
#pragma endregion
#pragma region Handlers

  bool on_recv_complete(buffer& buf) {
    assert(loop_.is_loop_thread());
    recv_in_flight_ = false;
    if (!open_) return true;
    if (!do_submit_recv())
      (void)loop_.post([p = self()] { return p->do_submit_recv(); });

    if (own_handlers_.on_data) return own_handlers_.on_data(*this, buf);
    return true;
  }

  bool on_send_complete(buffer& buf) {
    assert(loop_.is_loop_thread());
    if (own_handlers_.on_drain) return own_handlers_.on_drain(*this, buf);
    return true;
  }

  // TODO: Wrong name.
  bool register_with_loop() {
    if (!open_) return false;
    return do_submit_recv();
  }
#pragma endregion
};

#pragma endregion
#pragma region ptr_with

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
    auto bound = local;
    if (!local.port()) bound = net_endpoint{sock};
    assert(loop.get());
    auto conn = std::make_shared<T>(iou_dgram_conn::allow::ctor, loop,
        std::move(sock), bound, std::move(h));
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

#pragma endregion
#pragma region with_state

// Extends `iou_dgram_conn` with a typed per-connection state value.
template<typename STATE>
class iou_dgram_conn_with_state: public iou_dgram_conn {
public:
  using state_t = STATE;

  explicit iou_dgram_conn_with_state(allow a,
      const std::shared_ptr<iou_loop>& loop, net_socket sock,
      net_endpoint bound_endpoint, iou_dgram_conn_handlers h)
      : iou_dgram_conn(a, loop, std::move(sock), bound_endpoint,
            std::move(h)) {}

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

#pragma endregion

}}} // namespace corvid::proto::iouring
