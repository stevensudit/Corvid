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
#include <utility>

#include "iou_dgram_router.h"

namespace corvid { inline namespace proto { namespace iouring {

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
      const std::shared_ptr<iou_dgram_router_base>& router,
      handlers h) noexcept
      : router_{router}, h_{std::move(h)} {}

  virtual ~iou_dgram_session() = default;

  iou_dgram_session(const iou_dgram_session&) = delete;
  iou_dgram_session(iou_dgram_session&&) = delete;
  iou_dgram_session& operator=(const iou_dgram_session&) = delete;
  iou_dgram_session& operator=(iou_dgram_session&&) = delete;

  // Construct a session bound to `router`. Wrap this in a `std::function`
  // and pass to `iou_dgram_router_ptr_with::bind` to plug in a session
  // factory. Safe from any thread.
  [[nodiscard]] static std::shared_ptr<iou_dgram_session>
  make(const std::shared_ptr<iou_dgram_router_base>& router, handlers h) {
    return std::make_shared<iou_dgram_session>(allow::ctor, router,
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
  // the `completion_token` for the in-flight send (invalid on failure); the
  // buffer is returned to the caller via `on_sent` on completion. Safe from
  // any thread.
  [[nodiscard]] iou_loop::completion_token send(buffer&& buf) {
    if (!open_) return {};
    auto router = router_.lock();
    if (!router || !router->is_open()) return {};
    return do_session_send(*router, std::move(buf));
  }

  // Convenience: copy `data` into a JIT-borrowed write buffer with the
  // given `peer` address, then send. Safe from any thread.
  [[nodiscard]] iou_loop::completion_token
  send_to(const net_endpoint& peer, std::string_view data) {
    if (!open_) return {};
    auto router = router_.lock();
    if (!router || !router->is_open()) return {};
    auto buf =
        router->loop().borrow_write_buffer(iou_dgram_router_base::buf_size);
    if (!buf) return {};
    buf.peer_addr() = peer;
    if (!buf.append(data)) return {};
    return do_session_send(*router, std::move(buf));
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
        [r = router, self = self_ptr()]() mutable -> bool {
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
  // buffer to this session's `on_sent`. Returns the `completion_token` for
  // the in-flight send (invalid on failure). Safe to call from any thread.
  [[nodiscard]] iou_loop::completion_token
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

// Out-of-line definitions.
inline auto iou_dgram_router_base::submit_session_send(buffer&& buf,
    const std::shared_ptr<iou_dgram_session>& ssn) -> completion_token {
  if (!open_) return {};
  return loop_.submit_sendmsg_buffer(sock_, std::move(buf),
      [ssn](completion_id, buffer& b) -> slot_retention {
        if (ssn->is_open()) ssn->dispatch_on_sent(std::move(b));
        return slot_retention{};
      });
}

inline bool iou_dgram_router_base::do_close(bool already_closing) {
  assert(loop_.is_loop_thread());
  if (!already_closing && !open_->exchange(false, std::memory_order::relaxed))
    return false;
  assert(!open_);

  for_each_session([](auto& ssn) {
    (void)ssn.notify_close_once();
    return true;
  });

  if (sock_)
    (void)loop_.submit_close(std::move(sock_),
        [](completion_id, iou_res, iou_cqe_flags) {
          return slot_retention{};
        });

  return true;
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

  explicit iou_dgram_session_with_state(base::allow a,
      std::shared_ptr<iou_dgram_router_base> router, handlers h,
      state_t state = {}) noexcept
      : base{a, std::move(router), std::move(h)}, state_{std::move(state)} {}

  // Construct a session_with_state bound to `router`, optionally
  // initializing the per-session state. Safe from any thread.
  [[nodiscard]] static std::shared_ptr<iou_dgram_session_with_state>
  make(const std::shared_ptr<iou_dgram_router_base>& router, handlers h,
      state_t state = {}) {
    return std::make_shared<iou_dgram_session_with_state>(base::allow::ctor,
        router, std::move(h), std::move(state));
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

}}} // namespace corvid::proto::iouring
