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

#pragma region iou_dgram_session_base

// Non-templated base of `iou_dgram_session<SessionPlugin>`. Owns the
// session-side state and operations that do NOT depend on the plugin type:
// the loop reference, the open flag, the send-buffer block size, and the
// `borrow_send_buffer` / `send` / `send_to` I/O surface.
//
// Lifted out so protocol-layer wrappers (e.g., `quic_session_io`) can hold a
// reference to a non-templated session handle and drive I/O without taking
// the templated session type as a parameter. `shared_from_this` lives here
// (single inheritance below); the templated derived's typed `self` is a
// `static_pointer_cast` of `shared_from_this` into its own type.
//
// The single virtual (`do_send`) bridges to the templated derived, which
// knows how to ship a buffer through the typed router with a typed
// `shared_from_this`.
class iou_dgram_session_base
    : public std::enable_shared_from_this<iou_dgram_session_base> {
public:
  using buffer = iou_loop::buffer;
  using completion_token = iou_loop::completion_token;

  iou_dgram_session_base(const iou_dgram_session_base&) = delete;
  iou_dgram_session_base(iou_dgram_session_base&&) = delete;
  iou_dgram_session_base& operator=(const iou_dgram_session_base&) = delete;
  iou_dgram_session_base& operator=(iou_dgram_session_base&&) = delete;
  virtual ~iou_dgram_session_base() = default;

#pragma region Accessors

  [[nodiscard]] bool is_open() const noexcept { return open_; }
  [[nodiscard]] iou_loop& loop() noexcept { return loop_; }

#pragma endregion
#pragma region Send

  // Borrow a buffer for sending. The buffer is owned by the loop's pool and
  // will be returned to the pool on completion. Safe from any thread.
  [[nodiscard]] buffer borrow_send_buffer() const {
    return loop_.borrow_write_buffer(buf_size_);
  }

  // Send a datagram. The buffer must already carry its `peer_addr`. Returns
  // the `completion_token` for the in-flight send (invalid on failure); the
  // buffer is returned to the plugin via `handle_sent` on completion. Safe
  // from any thread.
  [[nodiscard]] completion_token send(buffer&& buf) {
    if (!open_) return {};
    return do_send(std::move(buf));
  }

  // Convenience: copy `data` into a JIT-borrowed write buffer with the given
  // `peer` address, then send. Safe from any thread.
  [[nodiscard]] completion_token
  send_to(const net_endpoint& peer, std::string_view data) {
    if (!open_) return {};
    auto buf = borrow_send_buffer();
    if (!buf) return {};
    buf.peer_addr() = peer;
    if (!buf.append(data)) return {};
    return do_send(std::move(buf));
  }

#pragma endregion
protected:
  iou_dgram_session_base(iou_loop& loop, block_size buf_size) noexcept
      : loop_{loop}, buf_size_{buf_size} {}

  // Templated derived implements: routes the buffer through the typed
  // router's `submit_session_send` with a typed `shared_from_this`.
  virtual completion_token do_send(buffer&& buf) = 0;

#pragma region Data members

  iou_loop& loop_;
  block_size buf_size_;
  relaxed_atomic_bool open_{true};

#pragma endregion
};

#pragma endregion
#pragma region iou_dgram_session

// A logical conversation, owned by an `iou_dgram_router` via `shared_ptr`. The
// session holds a reference to its router.
//
// (The router is intended to outlive all of its sessions; `router.close`
// drains them first. It is possible for a zombie session, kept alive by an
// outstanding `shared_ptr`, to linger after the router is gone, but it is in
// the closed state and will never attempt to call through to the router).
//
// All per-session state lives on the `SessionPlugin` instance, which the
// session constructs in-place with `(router&, *this, user_args...)`.
//
// Send path: `send(buffer&&)` flows through the router and eventually fires
// the plugin's `handle_sent` with the `buffer`'s `result` reflecting the send
// outcome. The `buffer` itself is available for resending, if needed.
//
// Recv path: the router calls `on_receive(buffer&&)`, which forwards into the
// plugin's `handle_recv`.
//
// Construction: the router plugin uses the static `make` factory, which calls
// `make_shared`, then invokes `plugin.register_self(buf)` so the plugin can
// register the session under one or more keys.
template<typename SessionPlugin>
class iou_dgram_session: public iou_dgram_session_base {
public:
  using session_plugin_t = SessionPlugin;
  using router_t = SessionPlugin::router_t;
  using session_ptr = std::shared_ptr<iou_dgram_session>;

#pragma region Construction
protected:
  enum class allow : bool { ctor };

#pragma endregion
public:
  // Public for `make_shared`; use the `make` factory. The plugin is
  // constructed in-place with `(router, *this, plugin_args...)`. The plugin
  // must only **store** the router and session references during its own ctor;
  // it must not call any member functions on the session, since the session's
  // construction is not yet complete.
  template<typename... PluginArgs>
  explicit iou_dgram_session(allow, router_t& router,
      PluginArgs&&... plugin_args) noexcept
      : iou_dgram_session_base{router.loop(), router_t::buf_size},
        router_{router},
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
  //
  // To construct a session WITHOUT auto-registration (e.g., sender-side
  // pre-register under a known key, or QUIC client whose registration must be
  // posted to the loop thread), pass a default-constructed `buffer{}`. The
  // plugin's `register_self(const buffer&)` is required to early-return on
  // `!buf` per the plugin contract documented in `iou_dgram_router.h`.
  template<typename... PluginArgs>
  [[nodiscard]] static session_ptr
  make(router_t& router, const buffer& buf, PluginArgs&&... plugin_args) {
    auto self = std::make_shared<iou_dgram_session>(allow::ctor, router,
        std::forward<PluginArgs>(plugin_args)...);
    (void)self->plugin_.register_self(buf);
    return self;
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] router_t& router() noexcept { return router_; }
  [[nodiscard]] session_plugin_t& plugin() noexcept { return plugin_; }

  // Typed view of `shared_from_this`. The base's `enable_shared_from_this`
  // yields `shared_ptr<iou_dgram_session_base>`; the cast is safe because the
  // dynamic type of every `iou_dgram_session_base` instance constructed
  // through `make` is `iou_dgram_session<SessionPlugin>`.
  [[nodiscard]] session_ptr self() {
    return std::static_pointer_cast<iou_dgram_session>(
        this->shared_from_this());
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
    if (!open_.exchange(false)) return false;
    return loop_.execute_or_post([self = self()]() mutable -> bool {
      if (self->close_notified_) return false;
      self->close_notified_ = true;
      (void)self->plugin_.unregister_self();
      return true;
    });
  }

#pragma endregion
#pragma region Helpers
private:
  // Bridges the base's `send` / `send_to` to the typed router; gated on
  // router open so a session outliving its router fails-closed.
  completion_token do_send(buffer&& buf) override {
    if (!router_.is_open()) return {};
    return router_.submit_session_send(std::move(buf), self());
  }

#pragma endregion
#pragma region Data members

  router_t& router_;
  SessionPlugin plugin_;
  bool close_notified_{};

#pragma endregion
};

#pragma endregion

}}} // namespace corvid::proto::iouring
