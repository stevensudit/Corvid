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

class iou_dgram_session;

template<typename STATE>
class iou_dgram_session_with_state;

template<typename Key>
class iou_dgram_router;

template<typename Router>
class iou_dgram_router_ptr_with;

#pragma region Extractor

// Construct the default extractor closure (for use as the default argument
// of `iou_dgram_router_ptr_with::bind`).
[[nodiscard]] inline auto make_default_dgram_extractor() noexcept {
  return std::function{[](const iou_loop::buffer& buf) -> net_endpoint {
    return buf.peer_addr();
  }};
}

#pragma endregion
#pragma region iou_dgram_router_base

// Non-templated base for `iou_dgram_router`. Owns the UDP socket, runs the
// `recv` loop, and submits `send`s. The typed `iou_dgram_router<Key>`
// derives from this and adds the typed sessions map plus demux logic.
//
// Sessions hold a `weak_ptr<iou_dgram_router_base>`.
class iou_dgram_router_base
    : public std::enable_shared_from_this<iou_dgram_router_base> {
public:
  using buffer = iou_loop::buffer;
  using completion_token = iou_loop::completion_token;

  // Block size for borrowed buffers. UDP datagrams cap at 64KB but only with
  // fragmentation. The MTU is typically around 1500 bytes, so 2KB is enough.
  static constexpr block_size buf_size = block_size::kb002;

#pragma region Construction
protected:
  enum class allow : bool { ctor };

public:
  // Use `make_shared` in child class.
  explicit iou_dgram_router_base(allow, const std::shared_ptr<iou_loop>& loop,
      net_socket&& sock, const net_endpoint& local,
      shot_type recv_shot) noexcept
      : loop_{*loop}, sock_{std::move(sock)}, weak_loop_{loop}, local_{local},
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
#pragma region Session interface

  // Submit a `sendmsg` through this router's socket. The completion routes the
  // buffer back to `ssn->dispatch_on_sent` if the session is still open. Safe
  // to call from any thread.
  [[nodiscard]] bool submit_session_send(buffer&& buf,
      const std::shared_ptr<iou_dgram_session>& ssn);

  // Begin recv loop. Called by the factory after construction; not idempotent.
  // Safe to call from any thread.
  [[nodiscard]] bool start_reading() {
    if (!open_) return false;
    return loop_.execute_or_post_with_retry([this]() mutable {
      if (recv_active_shot_ == shot_type::single)
        return do_submit_single_recv();
      return do_submit_multi_recv();
    });
  }

  // TODO: Add register_session(buffer& buf, session_ptr) and
  // unregister_session(buffer& buf). These run extract to get the key, then
  // use it on the collection.

  [[nodiscard]] bool remove_session(iou_dgram_session& s) {
    if (!open_) return false;
    return loop_.execute_or_post([this, &s] { return do_remove_session(s); });
  }

#pragma endregion
#pragma region Child overrides
protected:
  // Demux a received packet to the right session and dispatch its
  // `on_data`. Returning false aborts further recv loop work; convention is
  // to return true unless the router needs to stop.
  [[nodiscard]] virtual bool dispatch_packet(buffer&& buf) = 0;

  // Drain every session from the typed map and fire each session's
  // `on_close`. Called by `do_close` before the router itself notifies.
  virtual void drain_sessions_for_close() = 0;

  // Fire the router's own `on_close` (typed) exactly once. Called by
  // `notify_close_once` inside `do_close`.
  virtual bool fire_router_on_close() = 0;

  // Remove a specific session from the typed map, identified by reference.
  // Called by `iou_dgram_session::close` via the loop. Used only in the
  // hypothetical worst case.
  [[nodiscard]] virtual bool do_remove_session(iou_dgram_session& s) = 0;

#pragma endregion
#pragma region Helpers
private:
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
        [self = self_ptr()](completion_id,
            buffer& buf) mutable -> slot_retention {
          self->recv_token_ = {};
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
        [self = self_ptr()](completion_id,
            buffer& buf) mutable -> slot_retention {
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
          if (res) (void)self->dispatch_packet(std::move(buf));
          (void)self->do_submit_multi_recv();
          return slot_retention::release;
        });

    if (!recv_token_) return do_submit_single_recv(false);
    return true;
  }

  // Cleanly close the router.
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
protected:
  iou_loop& loop_;

private:
  net_socket sock_;
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
#pragma region iou_dgram_session

// A logical conversation, owned by an `iou_dgram_router` via `shared_ptr`.
// The session back-references the router through a `weak_ptr` to the router's
// non-typed base. The session itself is not templated. The router owns the
// `key -> session` map. To attach per-session state, derive via
// `iou_dgram_session_with_state<STATE>`.
//
// Send path: `send(buffer&&)` eventually generates an `on_sent` callback that
// returns the `buffer`, now with its `result()` reflecting the send outcome.
// The `buffer` can then be sent again if desired.
//
// Recv path: the router demuxes incoming datagrams and hands each one to
// the session's `on_data` in a `buffer`.
class iou_dgram_session
    : public std::enable_shared_from_this<iou_dgram_session> {
public:
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
protected:
  enum class allow : bool { ctor };

  //  template<typename>
  // friend class iou_dgram_router;

#pragma endregion
#pragma region Construction
public:
  // Public for `make_shared`; prefer the static `make` factory.
  explicit iou_dgram_session(allow,
      std::weak_ptr<iou_dgram_router_base> router, handlers h) noexcept
      : router_{std::move(router)}, h_{std::move(h)} {}

  virtual ~iou_dgram_session() = default;

  iou_dgram_session(const iou_dgram_session&) = delete;
  iou_dgram_session(iou_dgram_session&&) = delete;
  iou_dgram_session& operator=(const iou_dgram_session&) = delete;
  iou_dgram_session& operator=(iou_dgram_session&&) = delete;

  // Construct a session bound to `router`. Wrap this in a `std::function`
  // and pass to `iou_dgram_router_ptr_with::bind` to plug in a session
  // factory. Safe from any thread.
  [[nodiscard]] static std::shared_ptr<iou_dgram_session>
  make(std::weak_ptr<iou_dgram_router_base> router, handlers h) {
    return std::make_shared<iou_dgram_session>(allow::ctor, std::move(router),
        std::move(h));
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  [[nodiscard]] auto self_ptr() { return shared_from_this(); }

#pragma endregion
#pragma region send

  // Borrow a buffer for sending. The buffer is owned by the router's pool and
  // will be returned to the pool on completion. Safe from any thread.
  [[nodiscard]] buffer borrow_send_buffer() const {
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
        [r = std::move(router), self = shared_from_this(),
            b = std::move(buf)]() mutable -> bool {
          if (!self->open_ || !r->is_open()) return false;
          return self->do_session_send(*r, std::move(b));
        });
  }

  // Convenience: copy `data` into a JIT-borrowed write buffer with the
  // given `peer` address, then send. Safe from any thread.
  [[nodiscard]] bool send_to(net_endpoint peer, std::string_view data) {
    if (!open_) return false;
    auto router = router_.lock();
    if (!router) return false;
    return router->loop().execute_or_post(
        [r = std::move(router), self = self_ptr(), peer,
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
        [r = std::move(router), self = self_ptr()]() mutable -> bool {
          (void)r->remove_session(*self);
          return self->notify_close_once();
        });
  }

  void dispatch_on_data(buffer&& buf) {
    (void)h_.on_data(*this, std::move(buf));
  }
  void dispatch_on_sent(buffer&& buf) {
    if (h_.on_sent) (void)h_.on_sent(*this, std::move(buf));
  }
  void dispatch_on_close() {
    if (h_.on_close) (void)h_.on_close(*this);
  }

#pragma endregion
#pragma region Internals
public:
  // Mark closed and fire `on_close` exactly once. Loop-thread only.
  [[nodiscard]] bool notify_close_once() {
    if (close_notified_) return false;
    close_notified_ = true;
    (void)open_->exchange(false, std::memory_order::relaxed);
    dispatch_on_close();
    return true;
  }

  // Submit a send through the router's socket. The completion routes the
  // buffer to this session's `on_sent`. Safe to call from any thread.
  [[nodiscard]] bool
  do_session_send(iou_dgram_router_base& router, buffer&& buf) {
    return router.submit_session_send(std::move(buf), shared_from_this());
  }

#pragma endregion
#pragma region Data members

  std::weak_ptr<iou_dgram_router_base> router_;
  handlers h_;
  relaxed_atomic_bool open_{true};
  bool close_notified_{};

  friend class iou_dgram_router_base;

#pragma endregion
};

// Out-of-line definition: needs `iou_dgram_session::dispatch_on_sent`
// (and `is_open`) visible at the call site.
inline bool iou_dgram_router_base::submit_session_send(buffer&& buf,
    const std::shared_ptr<iou_dgram_session>& ssn) {
  if (!open_) return false;
  const auto token = loop_.submit_sendmsg_buffer(sock_, std::move(buf),
      [ssn](completion_id, buffer& b) -> slot_retention {
        if (ssn->is_open()) ssn->dispatch_on_sent(std::move(b));
        return slot_retention{};
      });
  return token.is_valid();
}

#pragma endregion
#pragma region iou_dgram_session_with_state

// Extends `iou_dgram_session` with a typed per-session state value.
// `STATE` must be default-constructible.
template<typename STATE>
class iou_dgram_session_with_state: public iou_dgram_session {
public:
  using state_t = STATE;
  using base = iou_dgram_session;
  using handlers = typename base::handlers;

#pragma region Construction

  explicit iou_dgram_session_with_state(typename base::allow a,
      std::weak_ptr<iou_dgram_router_base> router, handlers h,
      state_t state = {}) noexcept
      : base{a, std::move(router), std::move(h)}, state_{std::move(state)} {}

  // Construct a session_with_state bound to `router`, optionally
  // initializing the per-session state. Safe from any thread.
  [[nodiscard]] static std::shared_ptr<iou_dgram_session_with_state>
  make(std::weak_ptr<iou_dgram_router_base> router, handlers h,
      state_t state = {}) {
    return std::make_shared<iou_dgram_session_with_state>(base::allow::ctor,
        std::move(router), std::move(h), std::move(state));
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] state_t& state() noexcept { return state_; }
  [[nodiscard]] const state_t& state() const noexcept { return state_; }

  // Debug-safe downcast from the session reference.
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
// `iou_dgram_session` instances. The class is templated on the routing
// `Key` type; the extractor is always a `std::function<Key(const buffer&)>`,
// which lets `iou_dgram_router_ptr_with::bind` deduce `Key` from the
// extractor's signature.
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
// Demux: incoming packets are keyed by invoking the extractor on the
// buffer. The default extractor returns the buffer's stamped `peer_addr`.
// QUIC users supply a callable wrapped in `std::function<conn_id(const
// buffer&)>` that parses the connection ID from the packet header.
//
// Lifecycle: sessions can be created lazily on first packet (the router
// invokes the injected `session_factory_t` for unknown keys) or
// pre-registered explicitly via `add_session`.
//
// Thread safety: the public API is safe to call from any thread. All
// state mutation happens on the loop thread.
template<typename Key = net_endpoint>
class iou_dgram_router: public iou_dgram_router_base {
public:
  using key_t = Key;
  using extractor_t = std::function<key_t(const iou_loop::buffer&)>;
  using session_t = iou_dgram_session;
  using session_ptr = std::shared_ptr<session_t>;
  using session_handlers = session_t::handlers;
  using buffer = iou_loop::buffer;

  // Fires when a packet arrives keyed to a session that is not currently
  // registered. The factory receives a weak pointer to this router (pass
  // it through to `iou_dgram_session::make`) and a reference to the first
  // packet's buffer. Return a session_ptr to register, or nullptr to drop.
  using session_factory_t = std::function<session_ptr(
      std::weak_ptr<iou_dgram_router_base>, buffer&)>;

#pragma region Handlers

  struct handlers {
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
      session_factory_t factory, shot_type recv_shot) noexcept
      : iou_dgram_router_base{iou_dgram_router_base::allow::ctor, loop,
            std::move(sock), std::move(local), recv_shot},
        h_{std::move(h)}, extract_{std::move(extract)},
        factory_{std::move(factory)} {}

  [[nodiscard]] std::shared_ptr<iou_dgram_router> self() {
    return std::static_pointer_cast<iou_dgram_router>(
        this->shared_from_this());
  }

#pragma endregion
#pragma region Sessions

  // Pre-register a session under `key`. Returns true once the session is
  // in the routing map. Safe from any thread.
  [[nodiscard]] bool add_session(key_t key, session_ptr s) {
    if (!s) return false;
    return loop().execute_or_post(
        [self = self(), key = std::move(key),
            s = std::move(s)]() mutable -> bool {
          if (!self->is_open()) return false;
          auto [it, inserted] =
              self->sessions_.try_emplace(std::move(key), std::move(s));
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
  [[nodiscard]] bool dispatch_packet(buffer&& buf) override {
    assert(loop_.is_loop_thread());
    key_t key = extract_(buf);

    if (auto it = sessions_.find(key); it != sessions_.end()) {
      it->second->dispatch_on_data(std::move(buf));
      return true;
    }

    // Unknown key: invoke the lazy factory.
    if (factory_) {
      std::weak_ptr<iou_dgram_router_base> weak_self{
          std::static_pointer_cast<iou_dgram_router_base>(
              this->shared_from_this())};
      auto ssn = factory_(std::move(weak_self), buf);
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

  // Remove a session by reference. The session does not carry its key, so
  // locate the entry by pointer identity.
  [[nodiscard]] bool do_remove_session(iou_dgram_session& s) override {
    assert(loop_.is_loop_thread());
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it)
      if (it->second.get() == &s) {
        sessions_.erase(it);
        return true;
      }
    return false;
  }

#pragma endregion
#pragma region Data members
private:
  handlers h_;
  extractor_t extract_;
  session_factory_t factory_;
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

  // Wrap an existing router in the RAII handle. Mainly used by `bind` and by
  // CTAD; users normally go through `bind`.
  explicit iou_dgram_router_ptr_with(shared_ptr_t router) noexcept
      : router_{std::move(router)} {}

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

  // Bind a UDP socket to `local` and start receiving with the default
  // peer-address extractor. Available only when `router_t::key_t` is
  // `net_endpoint`. Returns an empty handle on failure.
  [[nodiscard]] static iou_dgram_router_ptr_with
  bind(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      typename router_t::handlers h = {},
      typename router_t::session_factory_t factory = {},
      shot_type recv_shot = shot_type::single)
  requires std::same_as<typename router_t::key_t, net_endpoint>
  {
    return do_bind<router_t>(loop, local, std::move(h),
        make_default_dgram_extractor(), std::move(factory), recv_shot);
  }

  // Bind a UDP socket and start receiving with a user-supplied extractor.
  // The routing `Key` is deduced from the `std::function`'s return type, and
  // the result is an `iou_dgram_router_ptr_with<iou_dgram_router<Key>>`.
  // Returns an empty handle on failure.
  template<typename Key>
  [[nodiscard]] static iou_dgram_router_ptr_with<iou_dgram_router<Key>>
  bind(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      typename iou_dgram_router<Key>::handlers h,
      std::function<Key(const iou_loop::buffer&)> extract,
      typename iou_dgram_router<Key>::session_factory_t factory = {},
      shot_type recv_shot = shot_type::single) {
    return do_bind<iou_dgram_router<Key>>(loop, local, std::move(h),
        std::move(extract), std::move(factory), recv_shot);
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
  // Shared bind implementation parameterized by the target router type.
  template<typename TargetRouter>
  [[nodiscard]] static iou_dgram_router_ptr_with<TargetRouter>
  do_bind(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      typename TargetRouter::handlers h,
      typename TargetRouter::extractor_t extract,
      typename TargetRouter::session_factory_t factory, shot_type recv_shot) {
    auto sock = net_socket::create_for(local, execution::nonblocking,
        message_style::datagram);
    if (!sock.is_open()) return {};
    if (!sock.set_reuse_addr()) return {};
    if (!sock.bind(local)) return {};
    auto bound = local;
    if (!local.port()) bound = net_endpoint{sock};
    assert(loop.get());
    auto router = std::make_shared<TargetRouter>(TargetRouter::allow::ctor,
        loop, std::move(sock), std::move(bound), std::move(h),
        std::move(extract), std::move(factory), recv_shot);
    if (!loop->post([r = router] { return r->start_reading(); })) return {};
    return iou_dgram_router_ptr_with<TargetRouter>{std::move(router)};
  }

  shared_ptr_t router_;

#pragma endregion
};

// CTAD: deduce Router from a shared_ptr. Lets `iou_dgram_router_ptr_with{p}`
// pick up the router type without the user spelling it.
template<typename Router>
iou_dgram_router_ptr_with(std::shared_ptr<Router>)
    -> iou_dgram_router_ptr_with<Router>;

// Untyped alias for the common case (default extractor, peer_addr key).
using iou_dgram_router_ptr = iou_dgram_router_ptr_with<>;

#pragma endregion

}}} // namespace corvid::proto::iouring
