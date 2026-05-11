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
#include <memory>
#include <string_view>
#include <utility>

#include "iou_dgram_router.h"

namespace corvid { inline namespace proto { namespace iouring {

#pragma region iou_dgram_session

// A logical conversation, owned by an `iou_dgram_router` via `shared_ptr`. The
// session holds a reference to its router.
//
// (The router is intended to outlive all of its sessions; `router.close()`
// drains them first. It is possible for a zombie session, kept alive by an
// outstanding `shared_ptr`, to linger after the router is gone, but it is in
// the closed state and will never attempt to call through to the router).
//
// All per-session state lives on the `SessionPlugin` instance, which the
// session constructs in-place with `(router&, *this, user_args...)`.
//
// Send path: `send(buffer&&)` flows through the router and eventually
// fires the plugin's `handle_sent` with the `buffer`'s `result()` reflecting
// the send outcome. The `buffer` itself is available for resending, if needed.
//
// Recv path: the router calls `on_receive(buffer&&)`, which forwards into
// the plugin's `handle_recv`.
//
// Construction: the router plugin uses the static `make` factory, which calls
// `make_shared`, then invokes `plugin.register_self(buf)` so the plugin
// can register the session under one or more keys.
template<typename SessionPlugin>
class iou_dgram_session
    : public std::enable_shared_from_this<iou_dgram_session<SessionPlugin>> {
public:
  using buffer = iou_loop::buffer;
  using completion_token = iou_loop::completion_token;
  using session_plugin_t = SessionPlugin;
  using router_t = SessionPlugin::router_t;
  using session_ptr = std::shared_ptr<iou_dgram_session>;

#pragma region Construction
protected:
  enum class allow : bool { ctor };

#pragma endregion
public:
  // Public for `make_shared`; use `make` / `make_unregistered` factories. The
  // plugin is constructed in-place with `(router, *this, plugin_args...)`. The
  // plugin must only **store** the router and session references during its
  // own ctor; it must not call any member functions on the session, since the
  // session's construction is not yet complete.
  template<typename... PluginArgs>
  explicit iou_dgram_session(allow, router_t& router,
      PluginArgs&&... plugin_args) noexcept
      : loop_{router.loop()}, router_{router},
        plugin_{router, *this, std::forward<PluginArgs>(plugin_args)...} {
    // Deferred concept check: see the matching note in iou_dgram_router.h.
    static_assert(iou_dgram_session_plugin<SessionPlugin>,
        "SessionPlugin must satisfy the iou_dgram_session_plugin concept");
  }

  iou_dgram_session(const iou_dgram_session&) = delete;
  iou_dgram_session(iou_dgram_session&&) = delete;
  iou_dgram_session& operator=(const iou_dgram_session&) = delete;
  iou_dgram_session& operator=(iou_dgram_session&&) = delete;

  // Construct a session bound to `router` and call the plugin's
  // `register_self(buf)` immediately afterwards. Caller should invoke from the
  // loop thread so that any `router.add_session(...)` performed by
  // `register_self` takes effect inline.
  template<typename... PluginArgs>
  [[nodiscard]] static session_ptr
  make(router_t& router, const buffer& buf, PluginArgs&&... plugin_args) {
    auto self = std::make_shared<iou_dgram_session>(allow::ctor, router,
        std::forward<PluginArgs>(plugin_args)...);
    (void)self->plugin_.register_self(buf);
    return self;
  }

  // Construct a session bound to `router` without calling `register_self`.
  // The caller is responsible for registering it under whatever keys it
  // wants via `router.add_session(key, sess)`. Useful when pre-registering
  // a session under a known key (e.g., on the sender side, to receive
  // responses).
  template<typename... PluginArgs>
  [[nodiscard]] static session_ptr
  make_unregistered(router_t& router, PluginArgs&&... plugin_args) {
    return std::make_shared<iou_dgram_session>(allow::ctor, router,
        std::forward<PluginArgs>(plugin_args)...);
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool is_open() const noexcept { return open_; }
  [[nodiscard]] router_t& router() noexcept { return router_; }
  [[nodiscard]] iou_loop& loop() noexcept { return loop_; }
  [[nodiscard]] session_plugin_t& plugin() noexcept { return plugin_; }

  [[nodiscard]] session_ptr self() { return this->shared_from_this(); }

#pragma endregion
#pragma region Send

  // Borrow a buffer for sending. The buffer is owned by the loop's pool and
  // will be returned to the pool on completion. Safe from any thread.
  [[nodiscard]] buffer borrow_send_buffer() const {
    return loop_.borrow_write_buffer(router_t::buf_size);
  }

  // Send a datagram. The buffer must already carry its `peer_addr`. Returns
  // the `completion_token` for the in-flight send (invalid on failure); the
  // buffer is returned to the plugin via `handle_sent` on completion. Safe
  // from any thread.
  [[nodiscard]] completion_token send(buffer&& buf) {
    if (!open_ || !router_.is_open()) return {};
    return router_.submit_session_send(std::move(buf), self());
  }

  // Convenience: copy `data` into a JIT-borrowed write buffer with the given
  // `peer` address, then send. Safe from any thread.
  [[nodiscard]] completion_token
  send_to(const net_endpoint& peer, std::string_view data) {
    if (!open_ || !router_.is_open()) return {};
    auto buf = loop_.borrow_write_buffer(router_t::buf_size);
    if (!buf) return {};
    buf.peer_addr() = peer;
    if (!buf.append(data)) return {};
    return router_.submit_session_send(std::move(buf), self());
  }

#pragma endregion
#pragma region Dispatch

  // Loop-thread only. Forwards to the plugin's `handle_recv`.
  bool on_receive(buffer&& buf) {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    return plugin_.handle_recv(std::move(buf));
  }

  // Loop-thread only. Forwards to the plugin's `handle_sent`.
  bool on_sent(buffer&& buf) {
    assert(loop_.is_loop_thread());
    if (!open_) return false;
    return plugin_.handle_sent(std::move(buf));
  }

#pragma endregion
#pragma region Close

  // Close the session: post `unregister_self` to the loop thread. The plugin
  // is responsible for calling `router.remove_session(key)` for each
  // registered key. Idempotent. Safe from any thread.
  [[nodiscard]] bool close() {
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    return loop_.execute_or_post([self = self()]() mutable -> bool {
      if (self->close_notified_) return false;
      self->close_notified_ = true;
      (void)self->plugin_.unregister_self();
      return true;
    });
  }

#pragma endregion
#pragma region Data members
private:
  iou_loop& loop_;
  router_t& router_;
  SessionPlugin plugin_;
  relaxed_atomic_bool open_{true};
  bool close_notified_{};

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::iouring
