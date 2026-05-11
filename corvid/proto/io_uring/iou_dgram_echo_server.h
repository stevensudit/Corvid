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
#include <memory>
#include <utility>

#include "iou_dgram_session.h"

namespace corvid { inline namespace proto { namespace iouring {

#pragma region iou_dgram_echo_protocol

// Reference implementation of the dgram plugin pair: bounce each received
// datagram back to its sender. Both plugins are nested classes so that the
// inter-plugin forward declaration stays scoped to the protocol class
// instead of leaking into the namespace.
class iou_dgram_echo_protocol {
public:
  class session_plugin;

  class router_plugin {
  public:
    using session_t = iou_dgram_session<session_plugin>;
    using router_t = iou_dgram_router<router_plugin>;
    using key_t = net_endpoint;

    [[nodiscard]] key_t extract(const iou_loop::buffer& b) const noexcept {
      return b.peer_addr();
    }

    bool create_session(const iou_loop::buffer& b, router_t& r) {
      (void)session_t::make(r, b);
      return true;
    }
  };

  class session_plugin {
  public:
    using router_t = iou_dgram_router<router_plugin>;
    using session_t = iou_dgram_session<session_plugin>;

    session_plugin(router_t& r, session_t& s) noexcept
        : router_{r}, session_{s} {}

    bool register_self(const iou_loop::buffer& first) {
      key_ = first.peer_addr();
      return router_.add_session(key_, session_.self_ptr());
    }

    bool handle_recv(iou_loop::buffer&& b) {
      auto out = session_.borrow_send_buffer();
      if (!out) return false;
      out.peer_addr() = key_;
      if (!out.append(b.payload_view())) return false;
      return session_.send(std::move(out)).is_valid();
    }

    bool handle_sent(iou_loop::buffer&&) noexcept { return true; }

    bool unregister_self() { return router_.remove_session(key_); }

  private:
    router_t& router_;
    session_t& session_;
    net_endpoint key_;
  };
};

#pragma endregion
#pragma region iou_dgram_echo_server

// Thin façade that owns a router bound to the echo plugin pair.
class iou_dgram_echo_server {
public:
  using router_plugin_t = iou_dgram_echo_protocol::router_plugin;
  using router_t = iou_dgram_router<router_plugin_t>;
  using handle_t = iou_dgram_router_handle<router_plugin_t>;

  iou_dgram_echo_server() noexcept = default;
  iou_dgram_echo_server(iou_dgram_echo_server&&) noexcept = default;
  iou_dgram_echo_server& operator=(iou_dgram_echo_server&&) noexcept = default;
  iou_dgram_echo_server(const iou_dgram_echo_server&) = delete;
  iou_dgram_echo_server& operator=(const iou_dgram_echo_server&) = delete;

  [[nodiscard]] static iou_dgram_echo_server
  bind(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      shot_type recv_shot = shot_type::single) {
    return iou_dgram_echo_server{handle_t::bind(loop, local, recv_shot)};
  }

  [[nodiscard]] const std::shared_ptr<router_t>& pointer() const noexcept {
    return handle_.pointer();
  }

  [[nodiscard]] const net_endpoint& local_endpoint() {
    return handle_->local_endpoint();
  }

  [[nodiscard]] bool close() { return handle_.close(); }

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(handle_);
  }

private:
  explicit iou_dgram_echo_server(handle_t handle) noexcept
      : handle_{std::move(handle)} {}

  handle_t handle_;
};

#pragma endregion

}}} // namespace corvid::proto::iouring
