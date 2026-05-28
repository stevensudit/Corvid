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
#include <concepts>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../containers/opt_find.h"
#include "../../enums/bool_enums.h"
#include "../net_endpoint.h"
#include "iou_buf_pool.h"
#include "iou_loop.h"

namespace corvid { inline namespace proto { namespace iouring {
using namespace bool_enums;

// Fwd.
template<typename RouterPlugin>
class iou_dgram_router;

#pragma region Concepts

// Plugin contract for `iou_dgram_router`. The plugin owns per-router state
// (rate limiters, connection counts, etc.) and decides how to extract a
// routing key from each received datagram and how to construct a session
// when the key has no current owner.
//
// Required type aliases:
//  `session_t` - typically `iou_dgram_session<MatchingSessionPlugin>`.
//  `key_t`     - the routing key type. Must support `!key` and
//      `bool(key)` (typically via `explicit operator bool`) so the router
//      can check validity uniformly. Built-in integral types and
//      `net_endpoint` already satisfy this.
//
// Required methods (regular, non-static):
//  `key_t extract(const buffer&)` - deterministic key extraction. May
//      return an invalid key (i.e., one for which `!key` is true) to
//      signal parse failure or rejection; the router then drops the
//      originating packet without consulting the session registry or
//      invoking `create_session`.
//  `bool create_session(const buffer&, iou_dgram_router&)` - invoked on
//      key not found in the session registry. Constructs a session via
//      `session_t::make` (whose factory calls the session plugin's
//      `register_self`, which in turn calls `router.add_session(key, self)`
//      one or more times). May choose not to register (rejection path); the
//      router will then drop the originating packet. Return value is
//      informational.
//
// This concept is NOT used as a template parameter constraint on
// `iou_dgram_router` itself. Doing so would force eager checks of the
// `session_t` alias, which names `iou_dgram_session<MatchingSessionPlugin>`,
// whose own constraint would in turn re-check the matching session plugin's
// `router_t` alias - a circular dependency that no two-step ordering can
// satisfy.
//
// Conformance is instead verified by a deferred `static_assert` in the
// `iou_dgram_router` constructor body, which is instantiated only when an
// instance is actually constructed (by which time both plugins are complete).
// Concept failures surface as a clean `static_assert` diagnostic pointing at
// the unsatisfied requirement.
template<typename P>
concept iou_dgram_router_plugin = requires(P p, const iou_loop::buffer& cbuf,
    iou_dgram_router<P>& router, const typename P::key_t& key) {
  typename P::session_t;
  typename P::key_t;
  { p.extract(cbuf) } -> std::convertible_to<typename P::key_t>;
  { p.create_session(cbuf, router) } -> std::same_as<bool>;
  { !key } -> std::convertible_to<bool>;
  { static_cast<bool>(key) } -> std::same_as<bool>;
};

// Plugin contract for `iou_dgram_session`. The plugin owns per-session state
// (registered keys, peer addresses, protocol state). The session's ctor
// constructs the plugin in-place with `(router&, session&, user_args...)`,
// so the plugin captures the references it needs as members.
//
// Required type alias:
//  `router_t` - typically `iou_dgram_router<MatchingRouterPlugin>`.
//
// Required methods (regular, non-static):
//
//  `bool register_self(const buffer&)` - called from `iou_dgram_session::make`
//      immediately after construction. Sniff the key(s) and call
//      `router.add_session(key, session.shared_from_this())` one or more
//      times.

//      CONVENTION: if `!buf` (a default-constructed buffer sentinel), return
//      early without registering. Callers use `make(router, buffer{}, ...)` to
//      construct a session whose registration is deferred (e.g., the sender
//      side that wants to pre-register under a known response key, or a client
//      whose loop-thread registration is posted separately).

//  `bool handle_recv(buffer&&)` - dispatched per received datagram routed to
//      this session.

//  `bool handle_sent(buffer&&)` - the buffer comes back here when a send
//      completes; check `buf.result()` for outcome, and potentially resend.
//
//  `bool unregister_self()` - called on session close. Plugin must call
//      `router.remove_session(key)` once per registered key.
//
// Not used as a template parameter constraint; verified by a deferred
// `static_assert` in the `iou_dgram_session` constructor body. See the note on
// `iou_dgram_router_plugin` above for why.
template<typename P>
concept iou_dgram_session_plugin = requires(P p, const iou_loop::buffer& cbuf,
    iou_loop::buffer buf) {
  typename P::router_t;
  { p.register_self(cbuf) } -> std::same_as<bool>;
  { p.handle_recv(std::move(buf)) } -> std::same_as<bool>;
  { p.handle_sent(std::move(buf)) } -> std::same_as<bool>;
  { p.unregister_self() } -> std::same_as<bool>;
};

#pragma endregion
#pragma region iou_dgram_router

// Owns a datagram socket and demuxes incoming datagrams onto per-key
// `iou_dgram_session` instances via the `RouterPlugin`'s `extract` /
// `create_session` methods.
//
// Recv path: defaults to multishot `recvmsg` using the loop's provided-buffer
// pool. On `EC::nobufs`, downgrades to singleshot `recvmsg` into a borrowed
// read buffer. On hard error, closes the router. The `recv` loop runs
// uninterrupted for the router's lifetime and is not controlled by sessions.
//
// Send path: each session's `send(buffer&&)` flows through the router's
// socket via `submit_sendmsg_buffer`. There is no internal queue; multiple
// datagrams may be in flight concurrently. Each completion routes the
// buffer back to the originating session's `on_sent`, allowing the result to
// be checked and for the buffer to be potentially resent.
//
// Lifetime: the router is intended to outlive all of its sessions. `close`
// drains the sessions map (calling each session's `close`, which fires the
// plugin's `unregister_self`) before submitting the socket close. Sessions
// hold a reference to the router.
//
// Ownership: `iou_dgram_router_handle` is the canonical RAII owner and is
// what tests and most users hold. The router is also self-sustaining: as
// long as a recv is in flight, the recv callback holds a captured
// `shared_ptr<self>`, so the router cannot destruct even with no external
// `shared_ptr`. This permits a fire-and-forget pattern - `bind`, `release`
// the handle, hand the resulting `shared_ptr` to a session plugin (or
// discard it entirely) - and let the router live on its own ref.
//
// Termination then comes from one of:
//   - any holder calling `close` (a session that captured `&router_` can
//     do this from `unregister_self` or elsewhere);
//   - a hard recv error driving `do_close` from inside the recv callback;
//   - the `iou_loop` shutting down, which clears every slot and so releases
//     every captured `shared_ptr`.
//
// In all three, `close` cancels the in-flight recv via `submit_close`
// (`prep_cancel_fd`); the recv callback receives the canceled CQE, releases
// its slot, the last `self` ref drops, and the router destructs.
//
// Re-arming inside the recv callback goes through `start_reading` ->
// `execute_or_post_with_retry`, which keeps retrying transient submission
// failures (SQE pressure, buffer-pool starvation) until they clear. As long
// as the loop itself is alive, the chain heals on its own; the only way
// the self-sustaining ref disappears is one of the termination paths above.
//
// Thread safety: the public API is safe to call from any thread. All state
// mutation happens on the loop thread.
template<typename RouterPlugin>
class iou_dgram_router
    : public std::enable_shared_from_this<iou_dgram_router<RouterPlugin>> {
public:
  using buffer = iou_loop::buffer;
  using completion_token = iou_loop::completion_token;
  using router_plugin_t = RouterPlugin;
  using session_t = RouterPlugin::session_t;
  using key_t = RouterPlugin::key_t;
  using session_ptr = std::shared_ptr<session_t>;
  using router_ptr = std::shared_ptr<iou_dgram_router>;

  // Block size for borrowed buffers. UDP datagrams cap at 64KB but only with
  // fragmentation. The MTU is typically around 1500 bytes, so 2KB is enough.
  static constexpr block_size buf_size = block_size::kb002;

#pragma region Construction
protected:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_dgram_router_handle;

public:
  // Public for `make_shared`; use `iou_dgram_router_handle::bind`. The
  // ctor-body `static_assert` is the deferred concept check described on
  // `iou_dgram_router_plugin`.
  template<typename... PluginArgs>
  explicit iou_dgram_router(allow, iou_loop& loop, net_socket sock,
      const net_endpoint& local, shot_type recv_shot,
      PluginArgs&&... plugin_args) noexcept
      : loop_{loop}, sock_{std::move(sock)}, local_{local},
        recv_intended_shot_{recv_shot}, recv_active_shot_{recv_shot},
        plugin_{std::forward<PluginArgs>(plugin_args)...} {
    static_assert(iou_dgram_router_plugin<RouterPlugin>,
        "RouterPlugin must satisfy the iou_dgram_router_plugin concept");
  }

  iou_dgram_router(const iou_dgram_router&) = delete;
  iou_dgram_router(iou_dgram_router&&) = delete;
  iou_dgram_router& operator=(const iou_dgram_router&) = delete;
  iou_dgram_router& operator=(iou_dgram_router&&) = delete;

#pragma endregion
#pragma region Accessors

  // True until `close` is called. Safe from any thread.
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // The bound local address. Safe from any thread.
  [[nodiscard]] const net_endpoint& local_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (local_.empty()) local_ = net_endpoint{sock_};
    return local_;
  }

  [[nodiscard]] iou_loop& loop() noexcept { return loop_; }
  [[nodiscard]] router_plugin_t& plugin() noexcept { return plugin_; }
  [[nodiscard]] router_ptr self() { return this->shared_from_this(); }

#pragma endregion
#pragma region Flow control

  // Begin `recv` loop. Idempotent. Safe from any thread.
  [[nodiscard]] bool start_reading() {
    if (!open_) return false;
    if (is_reading_.exchange(true)) return false;
    return loop_.execute_or_post_with_retry([this]() mutable {
      if (recv_active_shot_ == shot_type::single)
        return do_submit_single_recv();
      return do_submit_multi_recv();
    });
  }

  // Begin a graceful close. Drains all sessions (firing each session's
  // `unregister_self`), cancels the `recv`, and closes the socket.
  // Idempotent. Safe from any thread.
  [[nodiscard]] bool close() {
    if (!open_.exchange(false)) return false;
    return loop_.execute_or_post([self = self()] {
      return self->do_close(true);
    });
  }

  // TODO: Consider adding a way to pause the server.

#pragma endregion
#pragma region Sessions

  // Pre-register `ssn` under `key`. Posts to the loop thread; actual insert
  // is async when called off-loop. Safe from any thread.
  [[nodiscard]] bool add_session(const key_t& key, const session_ptr& ssn) {
    if (!ssn) return false;
    return loop_.execute_or_post([this, key, ssn]() mutable -> bool {
      if (!is_open()) return false;
      auto [it, inserted] = sessions_.try_emplace(key, std::move(ssn));
      return inserted;
    });
  }

  // Forcibly remove a session by key. Does NOT fire the session's plugin
  // `unregister_self`; use the session's own `close` to notify. Actual
  // removal is async when called off-loop. Safe from any thread.
  [[nodiscard]] bool remove_session(const key_t& key) {
    return loop_.execute_or_post([this, key]() -> bool {
      return sessions_.erase(key) > 0;
    });
  }

  // Submit a `sendmsg` through this router's socket. The completion routes
  // the buffer back to `ssn->on_sent` if the session is still open. Returns
  // the `completion_token` for the in-flight send (invalid token on
  // failure). Safe from any thread.
  [[nodiscard]] completion_token
  submit_session_send(buffer&& buf, const session_ptr& ssn) noexcept {
    if (!open_ || !ssn) return {};
    return loop_.submit_sendmsg_buffer(sock_, std::move(buf),
        [ssn](completion_id, buffer& b) -> slot_retention {
          if (!ssn->on_sent(std::move(b))) (void)ssn->close();
          return slot_retention{};
        });
  }

#pragma endregion
#pragma region Internals
private:
  // Demux a successfully received datagram to a session.
  //
  // `on_receive` is the session's noexcept firewall: a `false` return there
  // means the session is no longer viable (plugin saw a connection-fatal
  // status, or a thrown exception was caught and converted to `false`),
  // and we close that session here. The router and other sessions keep
  // running.
  [[nodiscard]] bool dispatch_packet(buffer&& buf) {
    return loop_.execute_or_post(
        [this, buf = std::move(buf)]() mutable -> bool {
          const key_t& key = plugin_.extract(buf);
          if (!key) return false;

          if (auto found = find_opt(sessions_, key)) {
            if (!(*found)->on_receive(std::move(buf))) (void)(*found)->close();
            return true;
          }

          // Not found in registry: ask plugin to create the session. The
          // session's `make` factory calls `register_self`, which calls
          // `add_session(key, ...)` one or more times.
          (void)plugin_.create_session(buf, *this);

          // Re-lookup. If still missing (rejection), drop.
          if (auto found = find_opt(sessions_, key)) {
            if (!(*found)->on_receive(std::move(buf))) (void)(*found)->close();
            return true;
          }
          return false;
        });
  }

  // Submit a singleshot recv.
  [[nodiscard]] bool do_submit_single_recv(bool allow_upgrade = true) {
    assert(loop_.is_loop_thread());
    if (recv_token_) return false;
    recv_active_shot_ = shot_type::single;

    if (allow_upgrade && (recv_intended_shot_ == shot_type::multi) &&
        loop_.free_udp_block_count() > 16)
      return do_submit_multi_recv();

    auto buf = loop_.borrow_read_buffer(buf_size);
    if (!buf) return false;

    recv_token_ = loop_.submit_recvmsg_buffer(sock_, std::move(buf),
        [self = self()](completion_id, buffer& buf) mutable -> slot_retention {
          self->recv_token_ = {};
          self->is_reading_ = false;
          if (!self->open_) return slot_retention{};

          const auto res = buf.result();
          if (!res && !res.is_soft_error()) {
            (void)self->do_close();
            return slot_retention{};
          }
          if (res) (void)self->dispatch_packet(std::move(buf));
          (void)self->start_reading();
          return slot_retention{};
        });
    return recv_token_.is_valid();
  }

  // Submit a multishot recv.
  [[nodiscard]] bool do_submit_multi_recv() {
    assert(loop_.is_loop_thread() && !recv_token_);
    recv_active_shot_ = shot_type::multi;

    recv_token_ = loop_.submit_recvmsg_buffer_multi(sock_,
        [self = self()](completion_id, buffer& buf) mutable -> slot_retention {
          if (!self->open_) return slot_retention::release;
          const auto res = buf.result();
          const bool has_more = buf.has_more();

          // Normal multishot delivery.
          if (has_more) {
            if (res) (void)self->dispatch_packet(std::move(buf));
            return slot_retention::automatic;
          }

          // Multishot has stopped. Decide what to do.
          self->recv_token_ = {};
          self->is_reading_ = false;

          // Cancelation comes from `do_close`, which has already cleared
          // `open_`; the early check above handles that path.
          if (!res && res.err() == EC::canceled)
            return slot_retention::release;

          // Pool exhausted: downgrade to singleshot.
          if (!res && res.err() == EC::nobufs) {
            (void)self->do_submit_single_recv(false);
            return slot_retention::release;
          }

          // Hard error: close.
          if (!res && !res.is_soft_error()) {
            (void)self->do_close();
            return slot_retention::release;
          }

          // Soft error or kernel-side glitch: process this packet (if any),
          // then arm a fresh multishot.
          if (res) (void)self->dispatch_packet(std::move(buf));

          // This looks recursive but actually can't overflow the stack. We
          // could theoretically reuse the completion ID as a token and queue
          // that, but there are hurdles and it's not worth doing for an error
          // recovery case.
          self->is_reading_ = true;
          (void)self->do_submit_multi_recv();
          return slot_retention::release;
        });

    if (!recv_token_) return do_submit_single_recv(false);
    return true;
  }

  // Cleanly close the router. Walks the sessions map by snapshot so that
  // each session's `close` -> `unregister_self` -> `remove_session(key)`
  // can mutate `sessions_` normally without iterator invalidation. After
  // every session has had its turn, the map should be empty; anything left
  // is a misbehaving plugin that failed to unregister.
  //
  // Holding the snapshot of `shared_ptr<session>`s keeps each session alive
  // for the duration of its `close`. In-flight callbacks (sends in
  // particular) bind their own `shared_ptr<session>`, so the session will
  // also outlive them and be destructed when the last reference drops.
  [[nodiscard]] bool do_close(bool already_closing = false) {
    assert(loop_.is_loop_thread());
    if (!already_closing && !open_.exchange(false)) return false;
    assert(!open_);

    std::vector<session_ptr> closing;
    closing.reserve(sessions_.size());
    for (const auto& [k, ssn] : sessions_) closing.push_back(ssn);

    for (const auto& ssn : closing) (void)ssn->close();

    // Each session was expected to have removed all its keys via
    // `unregister_self`. Anything left points to a buggy plugin,
    // which should be fixed. However, even though it misses `unregister_self`,
    // the router still tears down cleanly.
    assert(sessions_.empty());

    if (sock_)
      (void)loop_.submit_close(std::move(sock_),
          [](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });

    return true;
  }

#pragma endregion
#pragma region Data members
  iou_loop& loop_;
  net_socket sock_;

  std::mutex endpoint_mutex_; // protects lazy initialization of `local_`.
  net_endpoint local_;        // Always access through `local_endpoint`.

  relaxed_atomic_bool open_{true}; // Cleared once close starts.

  // Recv state. `intended` is the user's preference; `active` may be forced
  // down to single by buffer pressure.
  shot_type recv_intended_shot_{shot_type::single};
  shot_type recv_active_shot_{shot_type::single};
  relaxed_atomic_bool is_reading_;
  completion_token recv_token_;

  [[no_unique_address]] RouterPlugin plugin_;
  std::unordered_map<key_t, session_ptr> sessions_;

#pragma endregion
};

#pragma endregion
#pragma region iou_dgram_router_handle

// RAII handle that owns an `iou_dgram_router`. Destruction calls `close`
// (which drains sessions and closes the socket).
template<typename RouterPlugin>
class iou_dgram_router_handle {
public:
  using router_t = iou_dgram_router<RouterPlugin>;
  using shared_ptr_t = std::shared_ptr<router_t>;

#pragma region Construction

  iou_dgram_router_handle() noexcept = default;
  iou_dgram_router_handle(iou_dgram_router_handle&&) noexcept = default;
  iou_dgram_router_handle(const iou_dgram_router_handle&) = delete;
  iou_dgram_router_handle& operator=(
      iou_dgram_router_handle&&) noexcept = default;
  iou_dgram_router_handle& operator=(const iou_dgram_router_handle&) = delete;

  // Wrap an existing router in the RAII handle. Mainly used by `bind` and by
  // CTAD; users normally go through `bind`.
  explicit iou_dgram_router_handle(const shared_ptr_t& router) noexcept
      : router_{router} {}

  // NOLINTBEGIN(bugprone-exception-escape)
  ~iou_dgram_router_handle() {
    if (router_) (void)router_->close();
  }
  // NOLINTEND(bugprone-exception-escape)

#pragma endregion
#pragma region Factories

  // Bind a UDP socket and start receiving with the given plugin. Forwards
  // any extra `plugin_args` into the plugin's constructor; an empty pack
  // requests default construction. Returns an empty handle on failure.
  template<typename... PluginArgs>
  [[nodiscard]] static iou_dgram_router_handle
  bind(iou_loop& loop, net_endpoint local,
      shot_type recv_shot = shot_type::multi, PluginArgs&&... plugin_args) {
    auto sock = net_socket::create_for(local, execution::nonblocking,
        message_style::datagram);
    if (!sock.is_open() || !sock.set_nonblocking() || !sock.bind(local))
      return {};
    if (!local.port()) local = net_endpoint{sock};
    auto router = std::make_shared<router_t>(router_t::allow::ctor, loop,
        std::move(sock), std::move(local), recv_shot,
        std::forward<PluginArgs>(plugin_args)...);
    (void)router->start_reading();
    return iou_dgram_router_handle{std::move(router)};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] const shared_ptr_t& pointer() const noexcept {
    return router_;
  }
  [[nodiscard]] shared_ptr_t release() noexcept { return std::move(router_); }

  [[nodiscard]] bool close() {
    if (!router_) return false;
    return router_->close();
  }

  [[nodiscard]] auto operator->(this auto&& self) noexcept {
    return self.router_.get();
  }
  [[nodiscard]] auto& operator*(this auto&& self) noexcept {
    return *self.router_;
  }
  [[nodiscard]] explicit operator bool() const noexcept {
    return router_.get();
  }

#pragma endregion
#pragma region Data members
private:
  shared_ptr_t router_;

#pragma endregion
};

// CTAD: deduce RouterPlugin from a shared_ptr.
template<typename RouterPlugin>
iou_dgram_router_handle(std::shared_ptr<iou_dgram_router<RouterPlugin>>)
    -> iou_dgram_router_handle<RouterPlugin>;

#pragma endregion
}}} // namespace corvid::proto::iouring
