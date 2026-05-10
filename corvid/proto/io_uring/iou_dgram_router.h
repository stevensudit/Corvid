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

template<typename KEY>
class iou_dgram_router;

template<typename ROUTER>
class iou_dgram_router_ptr_with;

#pragma region Extractor

// Construct the default extractor closure, based on extracting `peer_addr`.
[[nodiscard]] inline auto make_default_dgram_extractor() noexcept {
  return std::function{[](const iou_loop::buffer& buf) -> net_endpoint {
    return buf.peer_addr();
  }};
}

#pragma endregion
#pragma region iou_dgram_router_base

// Non-templated base for `iou_dgram_router`. Owns the socket, runs the
// `recv` loop, and submits `send`s. The typed `iou_dgram_router<KEY>`
// derives from this and adds the typed sessions map, plus demux logic.
//
// Sessions hold a `weak_ptr<iou_dgram_router_base>`.
class iou_dgram_router_base
    : public std::enable_shared_from_this<iou_dgram_router_base> {
public:
  using buffer = iou_loop::buffer;
  using completion_token = iou_loop::completion_token;
  using session_t = iou_dgram_session;
  using session_ptr = std::shared_ptr<session_t>;
  using session_cb = std::function<bool(session_t&)>;
  using loop_ptr = std::shared_ptr<iou_loop>;
  using router_ptr = std::shared_ptr<iou_dgram_router_base>;

  // Block size for borrowed buffers. UDP datagrams cap at 64KB but only with
  // fragmentation. The MTU is typically around 1500 bytes, so 2KB is enough.
  static constexpr block_size buf_size = block_size::kb002;

#pragma region Construction
protected:
  explicit iou_dgram_router_base(iou_loop& loop, net_socket&& sock,
      const net_endpoint& local, shot_type recv_shot) noexcept
      : loop_{loop}, sock_{std::move(sock)}, local_{local},
        recv_intended_shot_{recv_shot}, recv_active_shot_{recv_shot} {}

public:
  virtual ~iou_dgram_router_base() = default;

  iou_dgram_router_base(const iou_dgram_router_base&) = delete;
  iou_dgram_router_base(iou_dgram_router_base&&) = delete;
  iou_dgram_router_base& operator=(const iou_dgram_router_base&) = delete;
  iou_dgram_router_base& operator=(iou_dgram_router_base&&) = delete;

#pragma endregion
#pragma region Accessors

  // True until `close()` is called. Safe from any thread.
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // The bound local address. Safe from any thread.
  [[nodiscard]] const net_endpoint& local_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (local_.empty()) local_ = net_endpoint{sock_};
    return local_;
  }

  [[nodiscard]] iou_loop& loop() noexcept { return loop_; }

  // Strong pointer to self.
  [[nodiscard]] router_ptr base_ptr() { return shared_from_this(); }

#pragma endregion
#pragma region Flow control

  // Begin recv loop. Idempotent. Safe to call from any thread.
  [[nodiscard]] bool start_reading() {
    if (!open_) return false;
    if (is_reading_->exchange(true, std::memory_order::relaxed)) return false;
    return loop_.execute_or_post_with_retry([this]() mutable {
      if (recv_active_shot_ == shot_type::single)
        return do_submit_single_recv();
      else
        return do_submit_multi_recv();
    });
  }

  // Begin a graceful close. Drains all sessions (firing their `on_close`),
  // cancels the `recv`, closes the socket, and fires the router's own
  // `on_close`. Idempotent. Safe from any thread.
  [[nodiscard]] bool close() {
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    return loop_.execute_or_post([self = base_ptr()] {
      return self->do_close(true);
    });
  }

#pragma endregion
#pragma region Session interface

  // Submit a `sendmsg` through this router's socket. The completion routes the
  // buffer back to `ssn->dispatch_on_sent` if the session is still open.
  // Returns the `completion_token` for the submitted send (invalid token on
  // failure), allowing the caller to track or cancel the in-flight op via
  // the loop. Safe to call from any thread.
  [[nodiscard]] completion_token
  submit_session_send(buffer&& buf, const session_ptr& ssn);

  // Demux a received packet to the right session and dispatch its
  // `on_data`.
  [[nodiscard]] virtual bool dispatch_packet(buffer&& buf) = 0;

public:
  // Type-erased session management interface.

  //  Add session to router by extracting the key from `buf`. The `buf` is safe
  //  to destruct as soon as the function returns. Actual adding will be async
  //  when called from outside the loop thread.
  [[nodiscard]] virtual bool
  add_session(const buffer& buf, const session_ptr& ssn) = 0;

  // Remove session from router by extracting the key from `buf`. The `buf` is
  // safe to destruct as soon as the function returns. Actual removing will be
  // async when called from outside the loop thread.
  [[nodiscard]] virtual bool remove_session(const buffer& buf) = 0;

  // Last-ditch removal by address.
  [[nodiscard]] virtual bool remove_session(session_t& s) = 0;

  // Iterate over all sessions. Return false to remove session from map.
  virtual void for_each_session(session_cb&& cbssn) = 0;

#pragma endregion
#pragma region Helpers
private:
  // Submit a singleshot recv.
  [[nodiscard]] bool do_submit_single_recv(bool allow_upgrade = true) {
    assert(loop().is_loop_thread());
    if (recv_token_) return false;
    recv_active_shot_ = shot_type::single;

    if (allow_upgrade && (recv_intended_shot_ == shot_type::multi) &&
        loop_.free_udp_block_count() > 16)
      return do_submit_multi_recv();

    auto buf = loop_.borrow_read_buffer(buf_size);
    if (!buf) return false;

    recv_token_ = loop_.submit_recvmsg_buffer(sock_, std::move(buf),
        [self = base_ptr()](completion_id,
            buffer& buf) mutable -> slot_retention {
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
    assert(loop().is_loop_thread() && !recv_token_);
    recv_active_shot_ = shot_type::multi;

    recv_token_ = loop_.submit_recvmsg_buffer_multi(sock_,
        [self = base_ptr()](completion_id,
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
          self->is_reading_ = false;

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
  [[nodiscard]] bool do_close(bool already_closing = false);

#pragma endregion
#pragma region Data members
private:
  iou_loop& loop_;
  net_socket sock_;

  std::mutex endpoint_mutex_; // protects lazy initialization of `local_`.
  net_endpoint local_;        // Always access through `local_endpoint()`.

  relaxed_atomic_bool open_{true}; // Cleared once close starts.

  // Recv state. `intended` is the user's preference; `active` may be forced
  // down to single by buffer pressure.
  shot_type recv_intended_shot_{shot_type::single};
  shot_type recv_active_shot_{shot_type::single};
  relaxed_atomic_bool is_reading_;
  completion_token recv_token_;

#pragma endregion
};

#pragma endregion
#pragma region iou_dgram_router

// Owns a socket and demuxes incoming datagrams onto per-key
// `iou_dgram_session` instances. The class is templated on the routing `KEY`
// type; the extractor is always a `std::function<key_t(const buffer&)>`, which
// lets `iou_dgram_router_ptr_with::bind` deduce `KEY` from the extractor's
// signature.
//
// Recv path: defaults to multishot `recvmsg` using the loop's provided-buffer
// pool. On `EC::nobufs`, downgrades to singleshot `recvmsg` into a borrowed
// read buffer. On hard error, closes the router. The `recv` loop runs
// uninterrupted for the router's lifetime.
//
// Send path: each session's `send(buffer&&)` flows through the router's
// socket via `submit_sendmsg_buffer`. There is no internal queue; multiple
// datagrams may be in flight concurrently. Each completion routes the
// buffer back to the originating session's `on_sent`.
//
// Demux: incoming packets are keyed by invoking the extractor on the
// buffer, then routed by key to the matching session.
//
// Lifecycle: sessions can be created lazily on first packet (the router
// invokes the injected `session_factory_t` for unknown keys) or
// pre-registered explicitly via `add_session`.
//
// Thread safety: the public API is safe to call from any thread. All
// state mutation happens on the loop thread.
template<typename KEY = net_endpoint>
class iou_dgram_router: public iou_dgram_router_base {
public:
  using key_t = KEY;
  using extractor_t = std::function<key_t(const iou_loop::buffer&)>;

  // Fires when a packet arrives keyed to a session that is not currently
  // registered. The factory receives a weak pointer to this router (pass
  // it through to `iou_dgram_session::make`) and a reference to the first
  // packet's buffer. Return a session_ptr to register, or nullptr to drop.
  using session_factory_t = std::function<session_ptr(router_ptr, buffer&&)>;

#pragma region Construction
protected:
  enum class allow : bool { ctor };

  template<typename ROUTER>
  friend class iou_dgram_router_ptr_with;

public:
  // Public for `make_shared`; use `iou_dgram_router_ptr_with::bind`.
  explicit iou_dgram_router(allow, iou_loop& loop, net_socket sock,
      const net_endpoint& local, extractor_t&& extract,
      session_factory_t&& factory, shot_type recv_shot) noexcept
      : iou_dgram_router_base{loop, std::move(sock), local, recv_shot},
        extract_{std::move(extract)}, factory_{std::move(factory)} {}

  [[nodiscard]] std::shared_ptr<iou_dgram_router> self() {
    return std::static_pointer_cast<iou_dgram_router>(
        this->shared_from_this());
  }

#pragma endregion
#pragma region Sessions

  // Add by extracted key. Safe from any thread.S
  [[nodiscard]] bool add_session(const buffer& buf,
      const std::shared_ptr<iou_dgram_session>& ssn) override {
    return add_session(extract_(buf), ssn);
  }

  // Pre-register a session under `key`. Safe from any thread.
  [[nodiscard]] bool add_session(const key_t& key, const session_ptr& ssn) {
    if (!ssn) return false;
    return loop().execute_or_post([this, key, ssn]() mutable -> bool {
      if (!is_open()) return false;
      auto [it, inserted] = sessions_.try_emplace(key, std::move(ssn));
      return inserted;
    });
  }

  // Remove by extracted key. Safe from any thread.
  [[nodiscard]] bool remove_session(const buffer& buf) override {
    return remove_session(extract_(buf));
  }

  // Forcibly remove a session by key. Does NOT fire the session's
  // `on_close`. Use the session's own `close()` to notify. Safe from any
  // thread.
  [[nodiscard]] bool remove_session(const key_t& key) {
    return loop().execute_or_post([this, key]() -> bool {
      return sessions_.erase(key) > 0;
    });
  }

  // Forcibly remove a session by reference. Does NOT fire the session's
  // `on_close`. Use the session's own `close()` to notify. Safe from any
  // thread.
  [[nodiscard]] bool remove_session(iou_dgram_session& s) override {
    return loop().execute_or_post([this, &s]() -> bool {
      return do_remove_session(s);
    });
  }

  // Look up an existing session. Loop-thread only.
  [[nodiscard]] session_ptr find_session(const key_t& key) const {
    assert(loop().is_loop_thread());
    auto it = sessions_.find(key);
    return (it == sessions_.end()) ? nullptr : it->second;
  }

  // Demux a successfully received datagram to a session.
  [[nodiscard]] bool dispatch_packet(buffer&& buf) override {
    assert(loop().is_loop_thread());
    return loop().execute_or_post([this, &buf]() -> bool {
      key_t key = extract_(buf);

      if (auto it = sessions_.find(key); it != sessions_.end()) {
        it->second->dispatch_on_data(std::move(buf));
        return true;
      }

      // Unknown key: invoke the lazy factory.
      if (factory_) {
        auto ssn = factory_(base_ptr(), std::move(buf));
        if (ssn) (void)sessions_.try_emplace(std::move(key), std::move(ssn));
        return true;
      }

      // Drop. Buffer destructor releases the slot.
      return true;
    });
  }

#pragma endregion
#pragma region Dispatch
protected:
  void for_each_session(session_cb&& cbssn) override {
    (void)loop().execute_or_post([this, cbssn = std::move(cbssn)]() -> bool {
      for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (!cbssn(*it->second)) {
          it = sessions_.erase(it);
        } else {
          ++it;
        }
      }
      return true;
    });
  }

#pragma endregion
#pragma region Helpers

  [[nodiscard]] bool do_remove_session(iou_dgram_session& s) {
    assert(loop().is_loop_thread());
    for (auto it = sessions_.begin(); it != sessions_.end(); ++it) {
      if (it->second.get() == &s) {
        sessions_.erase(it);
        return true;
      }
    }
    return false;
  }

#pragma endregion
#pragma region Data members
private:
  extractor_t extract_;
  session_factory_t factory_;
  std::unordered_map<key_t, session_ptr> sessions_;

#pragma endregion
};

#pragma endregion
#pragma region iou_dgram_router_ptr_with

// RAII handle that owns an `iou_dgram_router`. Destruction calls `close()`
// (which notifies sessions and closes the socket).
template<typename ROUTER = iou_dgram_router<>>
class iou_dgram_router_ptr_with {
public:
  using router_t = ROUTER;
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
    (void)router_->close();
  }
  // NOLINTEND(bugprone-exception-escape)

#pragma endregion
#pragma region Factories

  // Bind a UDP socket and start receiving with a user-supplied extractor.
  // The routing `Key` is deduced from the `std::function`'s return type, and
  // the result is an `iou_dgram_router_ptr_with<iou_dgram_router<Key>>`.
  // Returns an empty handle on failure.
  template<typename Key>
  [[nodiscard]] static iou_dgram_router_ptr_with<iou_dgram_router<Key>>
  bind(const std::shared_ptr<iou_loop>& loop, const net_endpoint& local,
      std::function<Key(const iou_loop::buffer&)> extract,
      iou_dgram_router<Key>::session_factory_t factory = {},
      shot_type recv_shot = shot_type::single) {
    return do_bind<iou_dgram_router<Key>>(loop, local, std::move(extract),
        std::move(factory), recv_shot);
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
        *loop, std::move(sock), std::move(bound), std::move(extract),
        std::move(factory), recv_shot);
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
