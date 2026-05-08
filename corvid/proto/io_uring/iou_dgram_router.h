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
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "../../enums/bool_enums.h"
#include "../net_endpoint.h"
#include "iou_buf_pool.h"
#include "iou_loop.h"

namespace corvid { inline namespace proto { namespace iouring {
using namespace bool_enums;

// Fwd.
class iou_dgram_router_base;

class iou_dgram_session_base;

template<typename Key>
class iou_dgram_session;

template<typename STATE, typename Key>
class iou_dgram_session_with_state;

template<typename Extractor>
class iou_dgram_router;

template<typename Router>
class iou_dgram_router_ptr_with;

#pragma region Extractor

// Default key extractor type. Returns the source endpoint stamped onto the
// buffer by `recvmsg`.
using default_dgram_extractor =
    std::function<net_endpoint(const iou_loop::buffer&)>;

// Construct the default extractor closure (for use as the default argument
// of `iou_dgram_router_ptr_with::bind`).
[[nodiscard]] inline default_dgram_extractor
make_default_dgram_extractor() noexcept {
  return [](const iou_loop::buffer& buf) -> net_endpoint {
    return buf.peer_addr();
  };
}

#pragma endregion
#pragma region iou_dgram_router_base

// Non-templated base for `iou_dgram_router`. Owns the UDP socket, runs the
// recv loop, and submits sends. The typed `iou_dgram_router<Extractor>`
// derives from this and adds the typed sessions map plus demux logic.
//
// Sessions hold a `weak_ptr<iou_dgram_router_base>`.
class iou_dgram_router_base
    : public std::enable_shared_from_this<iou_dgram_router_base> {
public:
  using buffer = iou_loop::buffer;
  using completion_token = iou_loop::completion_token;

#pragma region Construction
protected:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_dgram_router;

  // Block size for borrowed buffers. UDP datagrams cap at 64KB but only with
  // fragmentation. The MTU is typically around 1500 bytes, so 2KB is enough.
  static constexpr block_size buf_size = block_size::kb002;

public:
  // Use `make_shared` in child class.
  explicit iou_dgram_router_base(allow, const std::shared_ptr<iou_loop>& loop,
      net_socket&& sock, const net_endpoint& local,
      shot_type recv_shot) noexcept
      : sock_{std::move(sock)}, loop_{*loop}, weak_loop_{loop}, local_{local},
        recv_intended_shot_{recv_shot}, recv_active_shot_{recv_shot} {}

  virtual ~iou_dgram_router_base() = default;

  iou_dgram_router_base(const iou_dgram_router_base&) = delete;
  iou_dgram_router_base(iou_dgram_router_base&&) = delete;
  iou_dgram_router_base& operator=(const iou_dgram_router_base&) = delete;
  iou_dgram_router_base& operator=(iou_dgram_router_base&&) = delete;

#pragma endregion
#pragma region Accessors

  // True until `close()` is called. Safe from any thread.
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // The bound local address. Resolved lazily via `getsockname` on first call
  // to support `port == 0`. Safe from any thread.
  [[nodiscard]] const net_endpoint& local_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (local_.empty()) local_ = net_endpoint{sock_};
    return local_;
  }

  [[nodiscard]] iou_loop& loop() noexcept { return loop_; }

  // Strong pointer to the loop, for callbacks that may run after the router
  // is destroyed. Will be empty if the loop is already destroyed, so callers
  // must check before posting.
  [[nodiscard]] std::shared_ptr<iou_loop> strong_loop() const noexcept {
    return weak_loop_.lock();
  }

  // Strong pointer to self.
  [[nodiscard]] std::shared_ptr<iou_dgram_router_base> self_ptr() {
    return shared_from_this();
  }

#pragma endregion
#pragma region close

  // Begin a graceful close. Drains all sessions (firing their `on_close`),
  // cancels the `recv`, closes the socket, and fires the router's own
  // `on_close`. Idempotent. Safe from any thread.
  [[nodiscard]] bool close() {
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    return loop_.execute_or_post([self = self_ptr()] {
      return self->do_close(true);
    });
  }

#pragma endregion
#pragma region Internals
protected:
  // Submit a `sendmsg` through this router's socket. The completion routes the
  // buffer back to `ssn->dispatch_on_sent` if the session is still open. Safe
  // to call from any thread.
  [[nodiscard]] bool submit_session_send(buffer&& buf,
      const std::shared_ptr<iou_dgram_session_base>& ssn);

  // Begin recv loop. Called by the factory after construction. Safe to call
  // from any thread.
  [[nodiscard]] bool start_reading() {
    if (!open_) return false;
    return loop_.execute_or_post_with_retry([this]() mutable {
      if (recv_active_shot_ == shot_type::single)
        return do_submit_single_recv();
      return do_submit_multi_recv();
    });
  }

  // Hooks the typed router implements. All loop-thread only.

  // Demux a received packet to the right session and dispatch its
  // `on_data`. Returning false aborts further recv loop work; convention is
  // to return true unless the router needs to stop.
  [[nodiscard]] virtual bool dispatch_packet(buffer& buf) = 0;

  // Drain every session from the typed map and fire each session's
  // `on_close`. Called by `do_close` before the router itself notifies.
  virtual void drain_sessions_for_close() = 0;

  // Fire the router's own `on_close` (typed) exactly once. Called by
  // `notify_close_once` inside `do_close`.
  virtual bool fire_router_on_close() = 0;

  // Remove a specific session from the typed map, identified by its base
  // reference. The typed router downcasts and erases by key. Called by
  // `iou_dgram_session_base::close` via the loop.
  [[nodiscard]] virtual bool do_remove_session(iou_dgram_session_base& s) = 0;

#pragma endregion
#pragma region Helpers
private:
  friend class iou_dgram_session_base;

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
        [self = self_ptr()](completion_id,
            buffer& buf) mutable -> slot_retention {
          self->recv_token_ = {};
          if (!self->open_) return slot_retention{};
          const auto res = buf.result();
          if (!res && !res.is_soft_error()) {
            (void)self->do_close();
            return slot_retention{};
          }
          if (res) (void)self->dispatch_packet(buf);
          (void)self->start_reading();
          return slot_retention{};
        });
    return recv_token_.is_valid();
  }

  [[nodiscard]] bool do_submit_multi_recv() {
    assert(loop_.is_loop_thread() && !recv_token_);
    recv_active_shot_ = shot_type::multi;

    recv_token_ = loop_.submit_recvmsg_buffer_multi(sock_,
        [self = self_ptr()](completion_id,
            buffer& buf) mutable -> slot_retention {
          if (!self->open_) return slot_retention::release;
          const auto res = buf.result();
          const bool has_more = buf.has_more();

          // Normal multishot delivery.
          if (has_more) {
            if (res) (void)self->dispatch_packet(buf);
            return slot_retention::automatic;
          }

          // Multishot has stopped. Decide what to do.
          self->recv_token_ = {};

          // Cancelation comes from `do_close`, which has already cleared
          // `open_`; the early check above handles that path. Any other
          // path still funnels through the cases below.
          if (res.err() == EC::canceled) return slot_retention::release;

          // Pool exhausted: downgrade to singleshot.
          if (res.err() == EC::nobufs) {
            (void)self->do_submit_single_recv(false);
            return slot_retention::release;
          }

          // Hard error and not just a glitch: close.
          if (!res && !res.is_soft_error()) {
            (void)self->do_close();
            return slot_retention::release;
          }

          // Soft error or kernel-side glitch: process this packet (if
          // any), then arm a fresh multishot.
          if (res) (void)self->dispatch_packet(buf);
          (void)self->do_submit_multi_recv();
          return slot_retention::release;
        });

    if (!recv_token_) return do_submit_single_recv(false);
    return true;
  }

  [[nodiscard]] bool do_close(bool already_closing = false) {
    assert(loop_.is_loop_thread());
    if (!already_closing &&
        !open_->exchange(false, std::memory_order::relaxed))
      return false;

    drain_sessions_for_close();

    if (sock_)
      (void)loop_.submit_close(std::move(sock_),
          [self = self_ptr()](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });

    return notify_close_once();
  }

  [[nodiscard]] bool notify_close_once() {
    assert(loop_.is_loop_thread());
    if (close_notified_) return false;
    close_notified_ = true;
    return fire_router_on_close();
  }

#pragma endregion
#pragma region Data members

  net_socket sock_;

  iou_loop& loop_;
  std::weak_ptr<iou_loop> weak_loop_;

  std::mutex endpoint_mutex_; // protects lazy initialization of `local_`.
  net_endpoint local_;        // Always access through `local_endpoint()`.

  relaxed_atomic_bool open_{true}; // Cleared once close starts.
  bool close_notified_{};

  // Recv state. `intended` is the user's preference; `active` may be forced
  // down to single by buffer pressure.
  shot_type recv_intended_shot_{shot_type::single};
  shot_type recv_active_shot_{shot_type::single};
  completion_token recv_token_;

#pragma endregion
};

#pragma endregion
#pragma region iou_dgram_session_base

// Non-templated base for `iou_dgram_session`. Contains the open/close
// machinery, and provides `send`, `send_to`, and `close`. The typed
// `iou_dgram_session<Key>` adds the key, the typed handlers struct, and the
// dispatch overrides.
class iou_dgram_session_base
    : public std::enable_shared_from_this<iou_dgram_session_base> {
public:
  using buffer = iou_loop::buffer;

  virtual ~iou_dgram_session_base() = default;

  iou_dgram_session_base(const iou_dgram_session_base&) = delete;
  iou_dgram_session_base(iou_dgram_session_base&&) = delete;
  iou_dgram_session_base& operator=(const iou_dgram_session_base&) = delete;
  iou_dgram_session_base& operator=(iou_dgram_session_base&&) = delete;

#pragma region Accessors

  [[nodiscard]] bool is_open() const noexcept { return open_; }

#pragma endregion
#pragma region send

  // Borrow a buffer for sending. The buffer is owned by the router's pool and
  // will be returned to the pool on completion. Safe from any thread.
  [[nodiscard]] buffer borrow_send_buffer() {
    auto router = router_.lock();
    if (!router) return {};
    return router->loop().borrow_write_buffer(iou_dgram_router_base::buf_size);
  }

  // Send a datagram. The buffer must already carry its `peer_addr`. Returns
  // the buffer to the caller via `on_sent` on completion. Safe from any
  // thread.
  [[nodiscard]] bool send(buffer&& buf) {
    if (!open_) return false;
    auto router = router_.lock();
    if (!router) return false;
    return router->loop().execute_or_post(
        [r = router, self = shared_from_this(),
            b = std::move(buf)]() mutable -> bool {
          if (!self->open_ || !r->is_open()) return false;
          return self->do_session_send(*r, std::move(b));
        });
  }

  // Convenience: copy `data` into a JIT-borrowed write buffer with the
  // given `peer` address, then send. Safe from any thread.
  [[nodiscard]] bool send_to(net_endpoint peer, std::string_view data) {
    if (!open_) return false;
    if (peer.empty()) return false;
    auto router = router_.lock();
    if (!router) return false;
    return router->loop().execute_or_post(
        [r = router, self = shared_from_this(), peer,
            d = std::string{data}]() mutable -> bool {
          if (!self->open_ || !r->is_open()) return false;
          auto buf =
              r->loop().borrow_write_buffer(iou_dgram_router_base::buf_size);
          if (!buf) return false;
          buf.peer_addr() = peer;
          if (!buf.append(d)) return false;
          return self->do_session_send(*r, std::move(buf));
        });
  }

#pragma endregion
#pragma region close

  // Remove this session from the router's map and fire `on_close`.
  // Idempotent. Safe from any thread.
  [[nodiscard]] bool close() {
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    auto router = router_.lock();
    if (!router) return false;
    return router->loop().execute_or_post(
        [r = router, self = shared_from_this()]() mutable -> bool {
          (void)r->do_remove_session(*self);
          return self->notify_close_once();
        });
  }

#pragma endregion
#pragma region Construction
protected:
  explicit iou_dgram_session_base(
      std::weak_ptr<iou_dgram_router_base> router) noexcept
      : router_{std::move(router)} {}

#pragma endregion
#pragma region Internals

  // Subclass dispatches the user's `on_data` for this session. The buffer
  // holds one complete datagram (`payload_view()`, `peer_addr()`,
  // `result()` are all valid); the user may move it out for off-loop
  // parsing or let it drop to release the slot.
  virtual void dispatch_on_data(buffer&&) = 0;
  // Subclass dispatches the user's `on_sent` for this session.
  virtual void dispatch_on_sent(buffer&&) = 0;
  // Subclass dispatches the user's `on_close` for this session.
  virtual void dispatch_on_close() = 0;

  // Mark closed and fire `on_close` exactly once. Loop-thread only.
  [[nodiscard]] bool notify_close_once() {
    if (close_notified_) return false;
    close_notified_ = true;
    (void)open_->exchange(false, std::memory_order::relaxed);
    dispatch_on_close();
    return true;
  }

#pragma endregion
#pragma region Helpers
private:
  // Submit a send through the router's socket. The completion routes the
  // buffer to this session's `on_sent` (via the virtual). Safe to call from
  // any thread.
  [[nodiscard]] bool
  do_session_send(iou_dgram_router_base& router, buffer&& buf) {
    return router.submit_session_send(std::move(buf), shared_from_this());
  }

#pragma endregion
#pragma region Data members

  std::weak_ptr<iou_dgram_router_base> router_;
  relaxed_atomic_bool open_{true};
  bool close_notified_{};

  friend class iou_dgram_router_base;
  template<typename>
  friend class iou_dgram_router;

#pragma endregion
};

// Out-of-line definition: needs `iou_dgram_session_base::dispatch_on_sent`
// (and `is_open`) visible at the call site.
inline bool iou_dgram_router_base::submit_session_send(buffer&& buf,
    const std::shared_ptr<iou_dgram_session_base>& ssn) {
  if (!open_) return false;
  const auto token = loop_.submit_sendmsg_buffer(sock_, std::move(buf),
      [ssn](completion_id, buffer& b) -> slot_retention {
        if (ssn->is_open()) ssn->dispatch_on_sent(std::move(b));
        return slot_retention{};
      });
  return token.is_valid();
}

#pragma endregion
#pragma region iou_dgram_session

// A logical conversation, keyed by `Key`. The router owns the `shared_ptr`;
// the session back-references the router via a `weak_ptr` to the router's
// non-typed base.
//
// Send path: `send(buffer&&)` posts to the loop; the buffer flows through
// the router's socket via `submit_sendmsg_buffer`. On completion, the
// buffer is returned to the user via `on_sent`. The user can inspect
// `buf.result()` and resend if desired -- the session does not retry.
//
// Recv path: the router demuxes incoming datagrams and hands each one to
// the session's `on_data` as a move-in `buffer&&` (read `payload_view()`,
// `peer_addr()`, etc., or move it out to keep ownership for off-loop
// parsing).
template<typename Key = net_endpoint>
class iou_dgram_session: public iou_dgram_session_base {
public:
  using key_t = Key;
  using base = iou_dgram_session_base;
  using buffer = iou_loop::buffer;

#pragma region Handlers

  // User-supplied callbacks. All fired on the router's loop thread.
  struct handlers {
    // Fires for each datagram routed to this session. The buffer holds
    // the full datagram; user may move it out or let it drop.
    std::function<bool(iou_dgram_session&, buffer&&)> on_data = nullptr;
    // Fires when a queued send completes. The buffer comes back to the
    // user; check `buf.result()` for success/error.
    std::function<bool(iou_dgram_session&, buffer&&)> on_sent = nullptr;
    // Fires once when the session is removed (by `close()`, by
    // `router::remove_session`, or because the router itself closed).
    std::function<bool(iou_dgram_session&)> on_close = nullptr;
  };

#pragma endregion
#pragma region Internals
private:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_dgram_router;
  template<typename, typename>
  friend class iou_dgram_session_with_state;

#pragma endregion
#pragma region Construction
public:
  // Public for `make_shared`; users should construct sessions via
  // `router::make_session` or by accepting the lazy `on_new_session`
  // factory path.
  explicit iou_dgram_session(allow,
      std::weak_ptr<iou_dgram_router_base> router, key_t key,
      handlers h) noexcept
      : base{std::move(router)}, key_{std::move(key)}, h_{std::move(h)} {}

#pragma endregion
#pragma region Accessors

  [[nodiscard]] const key_t& key() const noexcept { return key_; }

  // Debug-safe downcast from the non-typed session base.
  [[nodiscard]] static iou_dgram_session& from(base& s) noexcept {
    assert(dynamic_cast<iou_dgram_session*>(&s) != nullptr);
    return static_cast<iou_dgram_session&>(s);
  }

#pragma endregion
#pragma region Dispatch
protected:
  void dispatch_on_data(buffer&& buf) override {
    if (h_.on_data) (void)h_.on_data(*this, std::move(buf));
  }
  void dispatch_on_sent(buffer&& buf) override {
    if (h_.on_sent) (void)h_.on_sent(*this, std::move(buf));
  }
  void dispatch_on_close() override {
    if (h_.on_close) (void)h_.on_close(*this);
  }

#pragma endregion
#pragma region Data members
private:
  key_t key_;
  handlers h_;

#pragma endregion
};

#pragma endregion
#pragma region iou_dgram_session_with_state

// Extends `iou_dgram_session` with a typed per-session state value.
// `STATE` must be default-constructible.
template<typename STATE, typename Key = net_endpoint>
class iou_dgram_session_with_state: public iou_dgram_session<Key> {
public:
  using state_t = STATE;
  using base = iou_dgram_session<Key>;
  using key_t = typename base::key_t;
  using handlers = typename base::handlers;

#pragma region Construction

  explicit iou_dgram_session_with_state(typename base::allow a,
      std::weak_ptr<iou_dgram_router_base> router, key_t key,
      handlers h) noexcept
      : base{a, std::move(router), std::move(key), std::move(h)} {}

#pragma endregion
#pragma region Accessors

  [[nodiscard]] state_t& state() noexcept { return state_; }
  [[nodiscard]] const state_t& state() const noexcept { return state_; }

  // Debug-safe downcast from the typed session reference.
  [[nodiscard]] static iou_dgram_session_with_state& from(base& s) noexcept {
    assert(dynamic_cast<iou_dgram_session_with_state*>(&s) != nullptr);
    return static_cast<iou_dgram_session_with_state&>(s);
  }

#pragma endregion
#pragma region Data members
private:
  state_t state_{};

#pragma endregion
};

#pragma endregion
#pragma region iou_dgram_router

// Owns a UDP socket and demuxes incoming datagrams onto per-key
// `iou_dgram_session` instances. The key type is deduced from the
// extractor's return type.
//
// Recv path: defaults to multishot recvmsg using the loop's UDP
// provided-buffer pool. On `EC::nobufs`, downgrades to singleshot recvmsg
// into a borrowed read buffer. On hard error, closes the router. The recv
// loop runs uninterrupted for the router's lifetime; the socket is shared
// across sessions, so there is no per-session pause.
//
// Send path: each session's `send(buffer&&)` flows through the router's
// socket via `submit_sendmsg_buffer`. There is no internal queue; multiple
// datagrams may be in flight concurrently. Each completion routes the
// buffer back to the originating session's `on_sent`.
//
// Demux: incoming packets are keyed by invoking the user-supplied
// `Extractor` on the buffer. The default extractor returns the buffer's
// stamped `peer_addr`. QUIC users supply a callable that parses the
// connection ID from the packet header.
//
// Lifecycle: sessions can be created lazily on first packet (the router
// fires `on_new_session` for unknown keys) or pre-registered explicitly
// via `add_session`.
//
// Thread safety: the public API is safe to call from any thread. All
// state mutation happens on the loop thread.
template<typename Extractor = default_dgram_extractor>
class iou_dgram_router: public iou_dgram_router_base {
public:
  using extractor_t = Extractor;
  using key_t =
      std::decay_t<std::invoke_result_t<extractor_t&, iou_loop::buffer&>>;
  using session_t = iou_dgram_session<key_t>;
  using session_ptr = std::shared_ptr<session_t>;
  using session_handlers = typename session_t::handlers;
  using buffer = iou_loop::buffer;

#pragma region Handlers

  struct handlers {
    // Fires when a packet arrives keyed to a session that is not currently
    // registered. Return a session_ptr to register and (logically) attach
    // the packet to it; return nullptr to drop. The buffer holds the first
    // packet -- the factory should consume it (the router does not
    // re-dispatch).
    std::function<session_ptr(iou_dgram_router&, const key_t&, buffer&&)>
        on_new_session = nullptr;
    // Fires once when the router itself closes (after all sessions have
    // been notified).
    std::function<bool(iou_dgram_router&)> on_close = nullptr;
  };

#pragma endregion
#pragma region Internals
private:
  enum class allow : bool { ctor };

  template<typename>
  friend class iou_dgram_router_ptr_with;

#pragma endregion
#pragma region Construction
public:
  // Public for `make_shared`; use `iou_dgram_router_ptr_with::bind`.
  explicit iou_dgram_router(allow, const std::shared_ptr<iou_loop>& loop,
      net_socket sock, net_endpoint local, handlers h, extractor_t extract,
      shot_type recv_shot) noexcept
      : iou_dgram_router_base{iou_dgram_router_base::allow::ctor, loop,
            std::move(sock), std::move(local), recv_shot},
        h_{std::move(h)}, extract_{std::move(extract)} {}

  [[nodiscard]] std::shared_ptr<iou_dgram_router> self() {
    return std::static_pointer_cast<iou_dgram_router>(
        this->shared_from_this());
  }

#pragma endregion
#pragma region Sessions

  // Build a session bound to this router. The session is NOT yet inserted
  // into the routing map; pass it to `add_session`. Safe from any thread.
  template<typename S = session_t>
  [[nodiscard]] std::shared_ptr<S>
  make_session(key_t key, session_handlers h) {
    return std::make_shared<S>(session_t::allow::ctor,
        std::weak_ptr<iou_dgram_router_base>{
            std::static_pointer_cast<iou_dgram_router_base>(
                this->shared_from_this())},
        std::move(key), std::move(h));
  }

  // Pre-register a session under its `key()`. Returns true once the
  // session is in the routing map. Safe from any thread.
  [[nodiscard]] bool add_session(session_ptr s) {
    if (!s) return false;
    return loop().execute_or_post(
        [self = self(), s = std::move(s)]() mutable -> bool {
          if (!self->is_open()) return false;
          auto [it, inserted] =
              self->sessions_.try_emplace(s->key(), std::move(s));
          return inserted;
        });
  }

  // Look up an existing session. Loop-thread only.
  [[nodiscard]] session_ptr find_session(const key_t& key) const {
    assert(loop_.is_loop_thread());
    auto it = sessions_.find(key);
    return (it == sessions_.end()) ? nullptr : it->second;
  }

  // Forcibly remove a session by key. Does NOT fire the session's
  // `on_close`. Use the session's own `close()` to notify. Safe from any
  // thread.
  [[nodiscard]] bool remove_session(const key_t& key) {
    return loop().execute_or_post([self = self(), key]() -> bool {
      return self->sessions_.erase(key) > 0;
    });
  }

#pragma endregion
#pragma region Dispatch
protected:
  // Demux a successfully received datagram to a session. Loop-thread only.
  [[nodiscard]] bool dispatch_packet(buffer& buf) override {
    assert(loop_.is_loop_thread());
    key_t key = extract_(buf);

    if (auto it = sessions_.find(key); it != sessions_.end()) {
      it->second->dispatch_on_data(std::move(buf));
      return true;
    }

    // Unknown key: invoke the lazy factory.
    if (h_.on_new_session) {
      auto ssn = h_.on_new_session(*this, key, std::move(buf));
      if (ssn) (void)sessions_.try_emplace(std::move(key), std::move(ssn));
      return true;
    }

    // Drop. Buffer destructor releases the slot.
    return true;
  }

  // Drain every session and fire each one's `on_close`. Loop-thread only.
  void drain_sessions_for_close() override {
    assert(loop_.is_loop_thread());
    auto sessions = std::move(sessions_);
    sessions_.clear();
    for (auto& [_, ssn] : sessions)
      if (ssn) (void)ssn->notify_close_once();
  }

  // Fire the router's own `on_close`. Loop-thread only.
  bool fire_router_on_close() override {
    if (h_.on_close) return h_.on_close(*this);
    return true;
  }

  // Remove a session by reference. The session is `iou_dgram_session<key_t>`
  // (this router built it via `make_session`); downcast and erase by key.
  [[nodiscard]] bool do_remove_session(
      iou_dgram_session_base& s_base) override {
    assert(loop_.is_loop_thread());
    auto& s = session_t::from(s_base);
    auto it = sessions_.find(s.key());
    if (it == sessions_.end() || it->second.get() != &s) return false;
    sessions_.erase(it);
    return true;
  }

#pragma endregion
#pragma region Data members
private:
  handlers h_;
  extractor_t extract_;
  std::unordered_map<key_t, session_ptr> sessions_;

#pragma endregion
};

#pragma endregion
#pragma region iou_dgram_router_ptr_with

// RAII handle that owns an `iou_dgram_router`. Destruction calls `close()`
// (which notifies sessions and closes the socket).
template<typename Router = iou_dgram_router<>>
class iou_dgram_router_ptr_with {
public:
  using router_t = Router;
  using shared_ptr_t = std::shared_ptr<router_t>;

#pragma region Construction

  iou_dgram_router_ptr_with() noexcept = default;
  iou_dgram_router_ptr_with(iou_dgram_router_ptr_with&&) noexcept = default;
  iou_dgram_router_ptr_with(const iou_dgram_router_ptr_with&) = delete;
  iou_dgram_router_ptr_with& operator=(
      iou_dgram_router_ptr_with&&) noexcept = default;
  iou_dgram_router_ptr_with& operator=(
      const iou_dgram_router_ptr_with&) = delete;

  // NOLINTBEGIN(bugprone-exception-escape)
  ~iou_dgram_router_ptr_with() {
    if (!router_) return;
    auto loop = router_->strong_loop();
    if (!loop) return;
    (void)router_->close();
  }
  // NOLINTEND(bugprone-exception-escape)

#pragma endregion
#pragma region Factories

  // Bind a UDP socket to `local` and start receiving. Returns an empty
  // handle on failure.
  [[nodiscard]] static iou_dgram_router_ptr_with
  bind(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      typename router_t::handlers h = {},
      typename router_t::extractor_t extract = make_default_dgram_extractor(),
      shot_type recv_shot = shot_type::single) {
    auto sock = net_socket::create_for(local, execution::nonblocking,
        message_style::datagram);
    if (!sock.is_open()) return {};
    if (!sock.set_reuse_addr()) return {};
    if (!sock.bind(local)) return {};
    auto bound = local;
    if (!local.port()) bound = net_endpoint{sock};
    assert(loop.get());
    auto router = std::make_shared<router_t>(router_t::allow::ctor, loop,
        std::move(sock), std::move(bound), std::move(h), std::move(extract),
        recv_shot);
    if (!loop->post([r = router] { return r->start_reading(); })) return {};
    return iou_dgram_router_ptr_with{router};
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

  [[nodiscard]] router_t* operator->() noexcept { return router_.get(); }
  [[nodiscard]] const router_t* operator->() const noexcept {
    return router_.get();
  }
  [[nodiscard]] explicit operator bool() const noexcept {
    return router_ != nullptr;
  }

#pragma endregion
#pragma region Internals
private:
  explicit iou_dgram_router_ptr_with(shared_ptr_t router) noexcept
      : router_{std::move(router)} {}

  shared_ptr_t router_;

#pragma endregion
};

// Untyped alias for the common case (default extractor, peer_addr key).
using iou_dgram_router_ptr = iou_dgram_router_ptr_with<>;

#pragma endregion

}}} // namespace corvid::proto::iouring
