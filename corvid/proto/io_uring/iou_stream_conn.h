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
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "../../concurrency/idle_timeout.h"
#include "../../enums/bool_enums.h"
#include "../net_endpoint.h"
#include "iou_buf_pool.h"
#include "iou_loop.h"
#include "iou_wrap.h"

namespace corvid { inline namespace proto { namespace iouring {
using namespace bool_enums;

// Fwd.
template<typename ConnPlugin>
class iou_stream_conn;

#pragma region iou_recv_view
// Move-only recv view delivered to a plugin's `on_data`. Four consumption
// paths:
//
//  Consume: call `active_view` + `consume`. When the buffer is not Provided by
//    the kernel for a multishot, you can leave some of the payload unconsumed
//    and it will be accumulated onto by subsequent `recv`s. However, if the
//    buffer is full or was Provided, it is an error to leave unconsumed
//    payload.
//
//  Take: call `take` to transfer buffer ownership for off-loop parsing.
//    Incoming data will land in another buffer.
//
//  Stop: extract the buffer first via `take`, then call `stop_receiving` to
//    end the `recv` loop. Returns a `posted_fn` resume callback that keeps the
//    conn alive while held and, when invoked, restarts the `recv` loop. Useful
//    for backpressure.
//
//  Move: transfer ownership of the view itself by binding it into a callback
//    to be executed on another thread. This offers more flexibility.
class iou_recv_view {
#pragma region Construction
public:
  using posted_fn = iou_loop::posted_fn;
  using buffer = iou_loop::buffer;

  iou_recv_view(iou_recv_view&&) noexcept = default;

  iou_recv_view(const iou_recv_view&) = delete;
  iou_recv_view& operator=(const iou_recv_view&) = delete;
  iou_recv_view& operator=(iou_recv_view&&) = delete;

  // NOLINTBEGIN(bugprone-exception-escape)
  ~iou_recv_view() noexcept(false) {
    if (!resume_) return; // View was moved from or stop_receiving consumed.
    if (!buf_.payload_view().empty() && buf_.active_span().empty())
      throw std::logic_error{
          "iou_recv_view with unconsumed payload cannot be reused"};

    // Normal re-arm path. The returned `posted_fn` is empty here.
    assert(buf_.result()); // not deactivated.
    (void)resume_(std::move(buf_));
  }
  // NOLINTEND(bugprone-exception-escape)

#pragma endregion
#pragma region Buffer management

  // Current unconsumed payload.
  [[nodiscard]] std::string_view active_view() noexcept {
    return buf_.payload_view();
  }

  // Advance past `n` bytes, returning them as a view.
  auto consume(size_t n) noexcept { return buf_.consume_read(n); }

  // Transfer buffer to caller for async parsing.
  [[nodiscard]] buffer take() noexcept { return std::move(buf_); }

  // Apply backpressure: end the `recv` loop. The buffer must have been
  // previously extracted via `take` (or fully consumed).
  //
  // Returns a `posted_fn` resume callback:
  //   - Holds a `shared_ptr` to the conn, keeping it alive for as long as
  //     the callback is held.
  //   - When invoked, restarts the `recv` loop.
  //   - When destroyed without invocation, allows the conn to destruct (if
  //     nothing else holds it).
  //
  // Discarding the callback is the (intentional) way to say "stop, and let
  // the conn go away."
  [[nodiscard]] posted_fn stop_receiving() noexcept {
    if (!resume_) return {};
    buf_.deactivate(); // signal stop via `notsock`
    auto pending = std::move(resume_);
    return pending(std::move(buf_));
  }

#pragma endregion
#pragma region Internals
private:
  template<typename ConnPlugin>
  friend class iou_stream_conn;
  // `resume_fn`: invoked on each completed `recv`. Only when the `buffer` is
  // deactivated, signaling that receiving should stop, does the `posted_fn`
  // returned become non-empty.
  using resume_fn =
      fixed_function<default_fixed_function::capacity, posted_fn(buffer&&)>;

  iou_recv_view(buffer buf, resume_fn resume) noexcept
      : buf_{std::move(buf)}, resume_{std::move(resume)} {}

private:
  buffer buf_;
  resume_fn resume_;
#pragma endregion
};

#pragma endregion
#pragma region iou_stream_conn_plugin

// Plugin contract for `iou_stream_conn`. The plugin owns per-connection state
// and is constructed in-place as a member of the conn (so that per-call
// methods do not need to be passed a conn ref, the plugin captures one at ctor
// time).
//
// Required ctor (verified at conn-construction time, not by the concept):
//   `ConnPlugin(iou_stream_conn<ConnPlugin>&, user_args...)` - primary path,
//       called from `iou_stream_conn`'s primary ctor with `*this` plus the
//       caller-supplied `PluginArgs...`. The plugin stores a reference to the
//       conn so its other methods can reach it.
//
// Required methods (regular, non-static):
//   `bool on_data(iou_recv_view)` - dispatched per arriving recv buffer.
//       Call `consume` or `take`, as appropriate.
//   `bool on_drain()` - fired when the `send` queue empties or an async
//       `connect` succeeds.
//   `bool on_close()` - fired exactly once on graceful or error `close`.
//   `ConnPlugin make_child_plugin(iou_stream_conn<ConnPlugin>&) const` -
//       factory invoked on the listening conn's plugin when a new connection
//       is `accept`ed. Constructs the new plugin for the `accept`ed child
//       (typically by sharing the listener's stable state with the child).
//       Only reached by listeners; client-only plugins may write a trivial
//       implementation.
//
// This concept is NOT used as a template parameter constraint on
// `iou_stream_conn`. Doing so would force eager checks at every reference
// to `iou_stream_conn<P>` - including `make_child_plugin(iou_stream_conn<P>&)`
// itself - which would cycle. Conformance is instead verified by a deferred
// `static_assert` in the `iou_stream_conn` constructor body, which is
// instantiated only when an instance is actually constructed.
//
// The primary-path ctor takes `iou_stream_conn<ConnPlugin>&` followed by a
// pack of user-supplied args. Concepts can check construction from a fixed
// arg list, but not "first parameter is X, rest is anything," so this
// requirement is verified by the member-init in the primary ctor: a plugin
// whose ctor does not accept `iou_stream_conn<ConnPlugin>&` as its first
// parameter produces a compile error at that init.
template<typename P>
concept iou_stream_conn_plugin = requires(P p, iou_recv_view&& view,
    iou_stream_conn<P>& child_conn) {
  { p.on_data(std::move(view)) } -> std::same_as<bool>;
  { p.on_drain() } -> std::same_as<bool>;
  { p.on_close() } -> std::same_as<bool>;
  { p.make_child_plugin(child_conn) } -> std::convertible_to<P>;
};

#pragma endregion
#pragma region iou_stream_conn

// An `iou_stream_conn<ConnPlugin>` is a non-blocking stream socket driven by
// an `iou_loop`. Instances are created through the static factory methods
// `adopt`, `connect`, and `listen`. The plugin captures the conn ref at
// construction and supplies the user-customizable surface (`on_data`,
// `on_drain`, `on_close`, `make_child_plugin`).
//
// The factories return a raw `iou_stream_conn*` (or `nullptr` on failure).
// The use of a dumb pointer is deliberate: the conn is self-sustaining (see
// Ownership, below), so the default expectation is "use the pointer
// immediately, then forget it."
//
// Callers who need to hold the conn  should promote the raw pointer to a
// `std::shared_ptr` via `self`. Callers who want to check later whether the
// conn is still alive without keeping it alive themselves can demote that
// strong pointer to a `std::weak_ptr` and `lock` before each use.
//
// Supports three creation paths:
//
// 1. Existing socket (whether from a `connect` or an `accept`): use `adopt`.
//    The socket must be non-blocking and connected.
//
// 2. Async connect: use `connect`. Fires `on_drain` on success, `on_close`
//    on failure.
//
// 3. Listening: use `listen`. Each accepted connection's plugin is built by
//    calling the listener plugin's `make_child_plugin`.
//
// Send path: `send(buffer&&)` submits zero-copy directly. `send(string)`
// copies to a JIT-borrowed `buffer` to pack consecutive string items.
// One SQE in flight at a time for ordering; additional sends queue behind it.
// Attempts to use `submit_send_buffer` (`IORING_OP_SEND_ZC`), which may
// deliver two CQEs per `send`: one for the `send` completion and a
// notification CQE when the kernel releases the `buffer`. For sockets that do
// not support ZC, such as UDS pairs, it will fall back to
// `submit_write_buffer`, which generates a single CQE and is less optimized.
//
// Recv path: one SQE in flight at a time. The `iou_recv_view` delivered to
// the plugin's `on_data` controls re-submission via inline `consume` or
// async `take`.
//
// Thread safety: All public methods are thread-safe unless otherwise labeled.
//
// Ownership: the conn is self-sustaining. Every in-flight `recv`, `send`,
// `connect`, `accept`, or `close` callback captures a `shared_ptr<self>`, so
// the conn cannot destruct as long as any I/O is pending - even with no
// external `shared_ptr`.
//
// The factories return a raw pointer rather than a handle or a `shared_ptr`
// precisely so that callers do not implicitly extend the conn's lifetime; the
// factory's own `shared_ptr` is moved into the initial `start_reading` post
// and then released, and the `recv` (or `accept`) callback's captured `self`
// carries the conn from there.
//
// Termination comes from one of:
//   - any holder calling `close` or `hangup`;
//   - a hard recv error or peer EOF driving `do_close_now` from inside the
//     recv callback;
//   - a failed `connect` driving `do_close_now` from the connect callback;
//   - a conn having no in-flight I/O and no external `std::shared_ptr`
//     holders, so the last `std::shared_ptr` ref drops.
//   - the `iou_loop` shutting down, which clears every slot and so releases
//     every captured `std::shared_ptr`.
//
// In all but the last two cases, `close`/`do_close_now` submits the
// socket close; the in-flight recv (and any pending send) callback receives
// the resulting CQE, releases its slot, and the last `std::shared_ptr` ref
// drops, after which the conn destructs.
//
// In the last two, no close was ever issued, so `~iou_stream_conn` finds
// `sock_` still valid and forces RST on it - the peer learns that this was a
// surprise teardown, not a graceful goodbye.
//
// Re-arming: for singleshot `recv`, each completed `recv` either resubmits
// from inside `on_recv_complete` or via the `iou_recv_view` destructor's
// resume hook; for multishot `recv`, the kernel keeps delivering CQEs on the
// same slot until cancelation. Either way, as long as the loop itself is
// alive, the `recv` chain heals on its own; the only way the self-sustaining
// ref disappears is one of the termination paths above.
//
// Thread safety: `send`, `close`, and `hangup` are safe from any thread.
// All I/O and state mutation run on the loop thread.
//
// Loop lifetime: the conn stores a bare `iou_loop&`. The loop knows nothing
// about the conns running on it - only the slot-captured `shared_ptr<self>`
// refs held inside completion callbacks. When the loop tears down, it drops
// those callback-captured refs; any conn held by nothing else destructs
// inline during the loop's `completion_cb_pool_` teardown.
//
// A conn may briefly outlive its `iou_loop`. The pools the conn's buffers
// borrow from are owned via `std::shared_ptr` (held by each `iou_buffer`),
// so they stay alive as long as any borrowed `buffer` is in scope, even
// past `~iou_loop`. No further methods may be called on the conn after the
// loop is gone, but its destructor (and the destructors of any `buffer`s
// in `send_queue_` or held by callbacks) is safe.
template<typename ConnPlugin>
class iou_stream_conn
    : public std::enable_shared_from_this<iou_stream_conn<ConnPlugin>> {
  enum class allow : bool { ctor };

public:
  using plugin_t = ConnPlugin;
  using buffer = iou_loop::buffer;
  using completion_token = iou_loop::completion_token;
  using sweeper_t = iou_loop::sweeper_t;
  using idle_expiration_time_t = iou_loop::expiration_time_point_t;
  using idle_duration_t = iou_loop::expiration_duration_t;
  using idle_timeout_t =
      concurrency::idle_timeout<iou_stream_conn, iou_loop::sweeper_t>;
  using cancel_action_t = idle_timeout_t::cancel_action_t;
  using idle_mode = idle_timeout_t::mode;

#pragma region Accessors

  // Like all public methods, accessors are thread-safe. However, they may be
  // racy until they've reached their final state.

  // True if the connection has not yet been closed. Once `false`, stays
  // `false`.
  [[nodiscard]] bool is_open() const noexcept { return !closed_; }

  // True once the peer has sent EOF (their `SHUT_WR` or `close`), or we have
  // submitted `SHUT_RD`. The local write side may still be open and `send` may
  // still queue data, unless `close` or `shutdown_send` has also been
  // requested. Once `true`, stays `true`.
  [[nodiscard]] bool is_read_shut() const noexcept { return read_shut_; }

  // True once we have submitted `SHUT_WR` (via `shutdown_send` or `close`).
  // After this, further `send` calls are rejected, though the peer may still
  // send data. Once `true`, stays `true`.
  [[nodiscard]] bool is_write_shut() const noexcept { return write_shut_; }

  // True iff the conn will currently accept new `send` calls: it must be
  // open, with neither `close` nor `shutdown_send` requested. Once `false`,
  // stays `false`.
  [[nodiscard]] bool writes_allowed() const noexcept {
    return !closed_ && !close_requested_ && !shutdown_send_requested_;
  }

  // The remote peer address. For accepted connections, computed lazily via
  // `getpeername` on first call.
  [[nodiscard]] const net_endpoint& remote_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (remote_.empty()) remote_ = net_endpoint::peer_of(sock_);
    return remote_;
  }

  // The local address this socket is bound to, computed lazily via
  // `getsockname` on first call. Useful after `listen` on port 0 to discover
  // the OS-assigned port.
  [[nodiscard]] const net_endpoint& local_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (local_.empty()) local_ = net_endpoint{sock_};
    return local_;
  }

  // The `iou_loop` that drives this connection. Valid for the lifetime of
  // the connection (which should end before the loop is destroyed - see
  // class-level comment).
  [[nodiscard]] iou_loop& loop() noexcept { return loop_; }

  // Access to the plugin held by this conn.
  [[nodiscard]] auto& plugin(this auto& self) noexcept { return self.plugin_; }

  //
  // Block size for borrowed buffers.
  //

  [[nodiscard]] block_size recv_buf_size() const noexcept {
    return recv_buf_size_;
  }
  void set_recv_buf_size(block_size size) noexcept { recv_buf_size_ = size; }

  [[nodiscard]] block_size send_buf_size() const noexcept {
    return send_buf_size_;
  }
  void set_send_buf_size(block_size size) noexcept { send_buf_size_ = size; }

  //
  // Idle timeouts. Configured at construction (via factory). A duration of
  // zero means "no timeout". The two `idle_timeout` instances expose the
  // full API for each direction.
  //

  [[nodiscard]] auto& read_idle(this auto& self) noexcept {
    return self.read_idle_;
  }
  [[nodiscard]] auto& write_idle(this auto& self) noexcept {
    return self.write_idle_;
  }

#pragma endregion
#pragma region send

  // Queue a registered `buffer` for zero-copy sending.
  [[nodiscard]] bool send(buffer&& buf) {
    if (!writes_allowed() || !buf || buf.active_span().empty()) return false;
    return loop_.execute_or_post(
        [this, _ = self(), buf = std::move(buf)]() mutable -> bool {
          if (closed_) return false;
          send_queue_.emplace_back(std::move(buf));
          if (!send_token_) return do_submit_send();
          return true;
        });
  }

  // Queue a string for sending. JIT-borrows a write `buffer` to pack
  // consecutive string items. Prefer the `buffer` overload for performance.
  [[nodiscard]] bool send(std::string&& data) {
    if (!writes_allowed() || data.empty()) return false;
    return loop_.execute_or_post(
        [this, _ = self(), data = std::move(data)]() mutable -> bool {
          if (closed_) return false;
          send_queue_.emplace_back(std::move(data));
          if (!send_token_) return do_submit_send();
          return true;
        });
  }

#pragma endregion
#pragma region close

  // Start a close. Flushes pending `send`s, then fully closes the socket.
  // Idempotent. After `close` is called, further `send`s are rejected.
  [[nodiscard]] bool close() {
    if (closed_) return false;
    if (close_requested_.exchange(true)) return false;
    return loop_.execute_or_post([this, _ = self()] {
      if (send_queue_.empty() && !send_token_) return do_close_now(true);
      return true;
    });
  }

  // Shut down the write side. Flushes pending `send`s, then submits `SHUT_WR`
  // so the peer sees EOF on its read. The read side stays open; `on_data`
  // continues to fire (including with an empty view on peer EOF). After
  // `shutdown_send` is called, further `send` calls are rejected. If reads
  // were already shut, just closes. Idempotent.
  [[nodiscard]] bool shutdown_send() {
    if (!writes_allowed()) return false;
    if (shutdown_send_requested_.exchange(true)) return false;
    return loop_.execute_or_post([this, _ = self()] {
      if (send_queue_.empty() && !send_token_) return do_shutdown_send_now();
      return true;
    });
  }

  // Shut down the read side. Submits `SHUT_RD` so the kernel discards any
  // queued inbound data and future recvs return EOF, and cancels any
  // in-flight recv so its terminating CQE is dropped silently rather than
  // treated as a hard error. Sets `read_shut_`. The write side stays open.
  // If writes were already shut, just closes. Idempotent.
  //
  // Symmetric to `shutdown_send`, though generally less useful because it
  // doesn't send any indication over the wire or offer strong guarantees about
  // kernel behavior.
  [[nodiscard]] bool shutdown_recv() {
    if (closed_) return false;
    if (read_shut_.exchange(true)) return false;
    return loop_.execute_or_post([this, _ = self()] {
      if (closed_) return false;
      if (write_shut_) return do_close_now();
      if (recv_token_) (void)loop_.submit_cancel(std::move(recv_token_));
      const auto cbtoken = loop_.submit_shutdown(sock_, shutdown_how::rd,
          [_ = self()](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });
      if (!cbtoken.is_valid()) return do_close_now();
      return true;
    });
  }

  // Forceful close: cancel pending I/O and close immediately with RST.
  [[nodiscard]] bool hangup() {
    if (closed_.exchange(true)) return false;
    return loop_.execute_or_post([this, _ = self()] {
      send_queue_.clear();
      if (sock_) {
        (void)sock_.set_option(socket_option::linger,
            linger{.l_onoff = 1, .l_linger = 0});
        (void)loop_.submit_close(std::move(sock_),
            [_ = self()](completion_id, iou_res, iou_cqe_flags) {
              return slot_retention{};
            });
        (void)loop_.immediate_submit();
      }
      return notify_close_once();
    });
  }

  // Pause the `recv` loop. Returns a `shared_ptr` to conn, so that you
  // can keep the conn alive until you call `resume_receiving` through the
  // pointer. Idempotent.
  [[nodiscard]] auto stop_receiving() {
    auto conn = self();
    if (!recv_paused_.exchange(true))
      (void)loop_.execute_or_post([this, _ = conn] {
        (void)read_idle_.pause();
        if (!recv_token_) return false;
        return loop_.submit_cancel(std::move(recv_token_));
      });
    return conn;
  }

  // Resume receiving after `stop_receiving`. Idempotent. A no-op once the read
  // side has been shut (peer EOF observed).
  [[nodiscard]] bool resume_receiving() {
    if (!recv_paused_.exchange(false)) return false;
    return loop_.execute_or_post([this, _ = self()]() -> bool {
      if (closed_ || recv_token_ || read_shut_) return false;
      return do_submit_recv();
    });
  }

#pragma endregion

#pragma region Construction
public:
  // Public only for `std::make_shared`; users go through `adopt`, `connect`,
  // or `listen`. Plugin is constructed in-place with `(*this, plugin_args...)`
  // so it can capture the conn ref.
  template<typename... PluginArgs>
  explicit iou_stream_conn(allow, iou_loop& loop, net_socket sock,
      const net_endpoint* remote, std::optional<connection_role> role,
      block_size recv_buf_size, block_size send_buf_size, shot_type recv_shot,
      idle_duration_t read_idle_timeout, idle_duration_t write_idle_timeout,
      PluginArgs&&... plugin_args)
      : loop_{loop}, sock_{std::move(sock)},
        remote_{remote ? *remote : net_endpoint{}},
        connecting_{role == connection_role::client},
        listening_{role == connection_role::server},
        recv_buf_size_{recv_buf_size}, send_buf_size_{send_buf_size},
        read_idle_{loop_.timeouts(), *this,
            cancel_action_t{[this] { (void)hangup(); }}, read_idle_timeout},
        write_idle_{loop_.timeouts(), *this,
            cancel_action_t{[this] { (void)hangup(); }}, write_idle_timeout},
        recv_intended_shot_{recv_shot}, recv_active_shot_{recv_shot},
        plugin_{*this, std::forward<PluginArgs>(plugin_args)...} {
    static_assert(iou_stream_conn_plugin<plugin_t>,
        "plugin_t must satisfy the iou_stream_conn_plugin concept");
  }

  // Accept-clone construction path. Called by `do_accept_clone` to build a
  // child conn from a parent. The new plugin is constructed by invoking
  // `accept_from.make_child_plugin(*this)`. `send_zc_supported` is inherited
  // from the listener, so children of a transport known not to support ZC
  // (e.g., UDS) skip the per-conn discovery `EOPNOTSUPP`.
  iou_stream_conn(allow, const plugin_t& accept_from, iou_loop& loop,
      net_socket sock, const net_endpoint* remote, block_size recv_buf_size,
      block_size send_buf_size, shot_type recv_shot, bool send_zc_supported,
      idle_duration_t read_idle_timeout, idle_duration_t write_idle_timeout)
      : loop_{loop}, sock_{std::move(sock)},
        remote_{remote ? *remote : net_endpoint{}},
        recv_buf_size_{recv_buf_size}, send_buf_size_{send_buf_size},
        read_idle_{loop_.timeouts(), *this,
            cancel_action_t{[this] { (void)hangup(); }}, read_idle_timeout},
        write_idle_{loop_.timeouts(), *this,
            cancel_action_t{[this] { (void)hangup(); }}, write_idle_timeout},
        recv_intended_shot_{recv_shot}, recv_active_shot_{recv_shot},
        send_zc_supported_{send_zc_supported},
        plugin_{accept_from.make_child_plugin(*this)} {
    static_assert(iou_stream_conn_plugin<plugin_t>,
        "plugin_t must satisfy the iou_stream_conn_plugin concept");
  }

  iou_stream_conn(const iou_stream_conn&) = delete;
  iou_stream_conn(iou_stream_conn&&) = delete;
  iou_stream_conn& operator=(const iou_stream_conn&) = delete;
  iou_stream_conn& operator=(iou_stream_conn&&) = delete;

  // Surprise destruction: nobody called `close` or `hangup`, but the last
  // `shared_ptr` is going away (either because the conn has no I/O in flight,
  // or the `iou_loop` is tearing down and dropping its callback-captured
  // refs).
  ~iou_stream_conn() {
    if (!sock_) return;
    // Force RST to signal a surprise teardown, rather than a graceful close.
    (void)sock_.set_option(socket_option::linger,
        linger{.l_onoff = 1, .l_linger = 0});
  }

  [[nodiscard]] std::shared_ptr<iou_stream_conn> self() {
    return this->shared_from_this();
  }

#pragma endregion
#pragma region Factories

  // Adopt an already-connected socket. `sock` must be non-blocking. Returns a
  // raw `iou_stream_conn*` (or `nullptr` on failure). The conn is kept alive
  // by its in-flight callbacks; promote to a `shared_ptr` via `self` if you
  // need to.
  template<typename... PluginArgs>
  [[nodiscard]] static iou_stream_conn* adopt(iou_loop& loop, net_socket sock,
      net_endpoint remote, shot_type recv_shot = shot_type::multi,
      idle_duration_t read_idle_timeout = {},
      idle_duration_t write_idle_timeout = {}, PluginArgs&&... plugin_args) {
    return do_make(loop, std::move(sock), &remote, std::nullopt,
        block_size::kb004, block_size::kb004, recv_shot, read_idle_timeout,
        write_idle_timeout, std::forward<PluginArgs>(plugin_args)...);
  }

  // Initiate an async connect to `remote`. `on_drain` fires on success;
  // `on_close` fires on failure. Returns `nullptr` on socket creation failure.
  template<typename... PluginArgs>
  [[nodiscard]] static iou_stream_conn* connect(iou_loop& loop,
      const net_endpoint& remote, shot_type recv_shot = shot_type::multi,
      idle_duration_t read_idle_timeout = {},
      idle_duration_t write_idle_timeout = {}, PluginArgs&&... plugin_args) {
    auto sock = net_socket::create_for(remote);
    if (!sock.is_open()) return nullptr;
    return do_make(loop, std::move(sock), &remote, connection_role::client,
        block_size::kb004, block_size::kb004, recv_shot, read_idle_timeout,
        write_idle_timeout, std::forward<PluginArgs>(plugin_args)...);
  }

  // Create a listening socket bound to `local`. Each accepted connection
  // gets a fresh plugin built via the parent plugin's `make_child_plugin`.
  // Returns `nullptr` on failure.
  template<typename... PluginArgs>
  [[nodiscard]] static iou_stream_conn* listen(iou_loop& loop,
      const net_endpoint& local, shot_type recv_shot = shot_type::multi,
      idle_duration_t read_idle_timeout = {},
      idle_duration_t write_idle_timeout = {}, PluginArgs&&... plugin_args) {
    auto sock = net_socket::create_for(local);
    if (!sock.is_open()) return nullptr;
    if (!sock.set_reuse_addr()) return nullptr;
    if (!sock.bind(local)) return nullptr;
    if (!sock.listen()) return nullptr;
    return do_make(loop, std::move(sock), nullptr, connection_role::server,
        block_size::kb004, block_size::kb004, recv_shot, read_idle_timeout,
        write_idle_timeout, std::forward<PluginArgs>(plugin_args)...);
  }

#pragma endregion
#pragma region Data members
private:
  iou_loop& loop_;
  net_socket sock_;

  std::mutex endpoint_mutex_; // protects lazy initialization of endpoints.
  net_endpoint remote_;       // Always access through `remote_endpoint` JIT.
  net_endpoint local_;        // Always access through `local_endpoint` JIT.

  relaxed_atomic_bool closed_;          // Socket is effectively closed.
  relaxed_atomic_bool close_requested_; // `close` was called.
  relaxed_atomic_bool shutdown_send_requested_; // `shutdown_send` was called.
  relaxed_atomic_bool write_shut_;              // `write_shut` was called.
  relaxed_atomic_bool read_shut_;               // `read_shut` was called.
  bool on_close_was_notified_{};                // `on_close` was called.

  bool connecting_{}; // Socket started off trying to connect.
  bool listening_{};  // Socket is listening.

  // Size of borrowed buffers.
  relaxed_atomic<block_size> recv_buf_size_{block_size::kb004};
  relaxed_atomic<block_size> send_buf_size_{block_size::kb004};

  // Idle timeouts. One per direction; configured at construction and
  // driven via the `read_idle()` / `write_idle()` accessors.
  idle_timeout_t read_idle_;
  idle_timeout_t write_idle_;

  // Recv state: whether it's paused, whether we're trying for single or
  // multishot, and whether the current shot is single or multi. The
  // distinction is needed because we can be forced by buffer pressure to
  // downgrade from multi to single.
  relaxed_atomic_bool recv_paused_;
  shot_type recv_intended_shot_{shot_type::multi};
  relaxed_atomic<shot_type> recv_active_shot_{shot_type::multi};

  // When receiving, token of callback. Can be used for cancelation.
  completion_token recv_token_;

  // Send state: one SQE in flight at a time; queue holds strings and
  // buffers.
  // TODO: This should probably be a circular buffer. Perhaps keep the deque
  // around for overflow.
  std::deque<std::variant<std::string, buffer>> send_queue_;
  bool send_zc_supported_{true}; // Cleared on first EOPNOTSUPP.
  completion_token send_token_;

  // Connect/listen token.
  completion_token connect_token_;

  plugin_t plugin_;

#pragma endregion
#pragma region Helpers
private:
  template<typename... PluginArgs>
  static iou_stream_conn* do_make(iou_loop& loop, net_socket sock,
      const net_endpoint* remote, std::optional<connection_role> role,
      block_size recv_buf_size, block_size send_buf_size, shot_type recv_shot,
      idle_duration_t read_idle_timeout, idle_duration_t write_idle_timeout,
      PluginArgs&&... plugin_args) {
    auto conn = std::make_shared<iou_stream_conn>(allow::ctor, loop,
        std::move(sock), remote, role, recv_buf_size, send_buf_size, recv_shot,
        read_idle_timeout, write_idle_timeout,
        std::forward<PluginArgs>(plugin_args)...);
    auto* ptr = conn.get();
    if (!loop.execute_or_post([conn = std::move(conn)] {
          return conn->start_reading();
        }))
      return nullptr;
    return ptr;
  }

  // Produce a new `iou_stream_conn` for each accepted connection. The new
  // conn's plugin is built by calling `plugin_.make_child_plugin(*new_conn)`.
  [[nodiscard]] std::shared_ptr<iou_stream_conn>
  do_accept_clone(net_socket&& sock, const net_endpoint* remote = nullptr) {
    assert(loop().is_loop_thread());
    return std::make_shared<iou_stream_conn>(allow::ctor, plugin_, loop_,
        std::move(sock), remote, block_size{recv_buf_size_},
        block_size{send_buf_size_}, recv_intended_shot_, send_zc_supported_,
        read_idle_.configured_timeout(), write_idle_.configured_timeout());
  }

  // Submit buffer for recv.
  [[nodiscard]] bool do_submit_recv() {
    assert(loop().is_loop_thread());
    (void)read_idle_.start();
    if (recv_active_shot_ == shot_type::single) return do_submit_single_recv();
    return do_submit_multi_recv();
  }

  // Submit buffer for singleshot recv, borrowing a read buffer if needed.
  // `allow_upgrade` controls whether we can re-enter `do_submit_multi_recv`
  // when we switched to singleshot due to a multishot failure.
  [[nodiscard]] bool
  do_submit_single_recv(buffer* bufptr = {}, bool allow_upgrade = true) {
    assert(loop().is_loop_thread());
    if (closed_ || recv_token_ || read_shut_ || recv_paused_) return false;
    recv_active_shot_ = shot_type::single;

    if (allow_upgrade && recv_intended_shot_ == shot_type::multi &&
        (!bufptr || bufptr->payload_view().empty()) &&
        loop_.free_tcp_block_count() > 16)
      return do_submit_multi_recv();

    // Reuse buffer if possible.
    buffer buf;
    if (bufptr && *bufptr)
      buf = std::move(*bufptr);
    else
      buf = loop_.borrow_read_buffer(recv_buf_size_);

    if (!buf) return false;

    recv_token_ = loop_.submit_read_buffer(sock_, std::move(buf),
        [this, _ = self()](completion_id, buffer& b) {
          recv_token_ = {};
          (void)on_recv_complete(b);
          return slot_retention{};
        });
    return recv_token_.is_valid();
  }

  // Submit a multishot recv using the loop's TCP provided-buffer ring.
  // Automatically handles `has_more` failure.
  [[nodiscard]] bool do_submit_multi_recv() {
    assert(loop().is_loop_thread() && !recv_token_);
    if (closed_ || recv_token_ || read_shut_ || recv_paused_) return false;

    recv_active_shot_ = shot_type::multi;
    recv_token_ = loop_.submit_recv_buffer_multi(sock_,
        [this, _ = self()](completion_id cbid,
            buffer& buf) mutable -> slot_retention {
          if (closed_) return slot_retention::release;
          const auto result = buf.result();
          const bool has_more = buf.has_more();

          // Normal case.
          if (has_more) {
            (void)on_recv_complete(buf);
            return slot_retention::automatic;
          }

          // Multishot has stopped. Decide what to do about it.
          recv_token_ = {};

          // If it's an intentional cancelation, do not resuscitate.
          if (!result && result.err() == EC::canceled) {
            recv_paused_ = true;
            return slot_retention::release;
          }

          // If we ran out of buffers, downgrade to singleshot for now.
          if (!result && result.err() == EC::nobufs) {
            (void)do_submit_single_recv();
            return slot_retention::release;
          }

          // Not EOF or an error, so probably just a glitch. Retry.
          if (result.value() > 0 && !recv_paused_) {
            recv_token_ = completion_token{cbid};
            const bool continued =
                loop_.submit_recv_buffer_multi(sock_, completion_token{cbid});
            (void)on_recv_complete(buf);
            if (continued) return slot_retention::retain;
            recv_token_ = {};
            recv_paused_ = true;
            return slot_retention::release;
          }

          // Pass error on.
          (void)on_recv_complete(buf);
          return slot_retention::release;
        });

    // If we can't start a multishot, try singleshot without upgrading again.
    if (!recv_token_) return do_submit_single_recv({}, false);

    return true;
  }

  // Build the resume callback captured by an `iou_recv_view`. Returns an
  // `iou_loop::posted_fn` resume token on the stop path (notsock signal),
  // or an empty `posted_fn` on the normal re-arm path. Safe to call from any
  // thread.
  [[nodiscard]] iou_recv_view::resume_fn make_view_resume() {
    return [this, conn = self()](buffer&& buf) -> iou_loop::posted_fn {
      // Read side is shut: the recv chain has already ended. Nothing to
      // resubmit, no stop callback to mint.
      if (read_shut_) return {};

      // Stop signal: deactivated buffer carries `EC::notsock`.
      if (!buf.result() && buf.result().err() == EC::notsock)
        return [this, conn = stop_receiving()] { return resume_receiving(); };

      // Nothing more do when in multishot mode.
      if (recv_active_shot_ == shot_type::multi) return {};

      // Rearm singleshot.
      (void)loop_.execute_or_post(
          [this, _ = std::move(conn), buf = std::move(buf)]() mutable -> bool {
            return do_submit_single_recv(&buf);
          });
      return {};
    };
  }

  // Send the next buffer in the send queue.
  [[nodiscard]] bool do_submit_send() {
    assert(loop().is_loop_thread() && !send_token_);
    if (send_queue_.empty()) return true;

    buffer buf;
    if (std::holds_alternative<buffer>(send_queue_.front())) {
      buf = std::move(std::get<buffer>(send_queue_.front()));
      send_queue_.pop_front();
    } else {
      buf = loop_.borrow_write_buffer(send_buf_size_);
      while (!send_queue_.empty() &&
             std::holds_alternative<std::string>(send_queue_.front()))
      {
        if (!buf.append(std::get<std::string>(send_queue_.front())))
          return false;
        send_queue_.pop_front();
      }
    }

    return do_submit_send_buffer(std::move(buf));
  }

  // Send buffer. Prefers `submit_send_buffer` (ZC). On first `EOPNOTSUPP`
  // (such as with Unix domain sockets), `on_send_complete` clears
  // `send_zc_supported_` and this helper falls back to `submit_write_buffer`
  // for all subsequent sends.
  [[nodiscard]] bool do_submit_send_buffer(buffer&& buf) {
    assert(loop().is_loop_thread() && !send_token_);
    if (!buf) return false;
    (void)write_idle_.start();

    auto fn = [this, _ = self()](completion_id cbhandle, buffer& buf)
        -> slot_retention {
      if (bitmask::has(buf.cqe_flags(), iou_cqe_flags::notif))
        return buf.pending_releases_decision();

      return on_send_complete(cbhandle, buf);
    };

    if (send_zc_supported_)
      send_token_ =
          loop_.submit_send_buffer(sock_, std::move(buf), std::move(fn));
    else
      send_token_ =
          loop_.submit_write_buffer(sock_, std::move(buf), std::move(fn));

    return send_token_.is_valid();
  }

  // Attempt to connect. On success, fires `on_drain` and transitions to recv
  // state. On failure, fires `on_close`.
  [[nodiscard]] bool do_submit_connect() {
    assert(loop().is_loop_thread() && connecting_ && !connect_token_);
    const auto& remote = remote_endpoint();
    bound_endpoint_with_timeout ep;
    ep.when.ts = iou_timespec{10s};
    ep.sockaddr = {remote, remote.sockaddr_size()};

    connect_token_ = loop_.submit_connect(sock_, std::move(ep),
        [this, _ = self()](completion_id, iou_res res,
            iou_cqe_flags) -> slot_retention {
          (void)on_connect_complete(res);
          return {};
        });
    return connect_token_.is_valid();
  }

  // Submit a multishot accept operation.
  [[nodiscard]] bool do_submit_accept() {
    assert(loop().is_loop_thread() && listening_ && !connect_token_);

    // Don't  waste time attempting ZC writes on UDS.
    if (local_endpoint().is_uds()) send_zc_supported_ = false;

    // Set up a callback that resubmits itself, and use it to bootstrap the
    // initial submission.
    const auto cbtoken = loop_.tokenize(
        [this, _ = self()](completion_id cbid, iou_res res,
            iou_cqe_flags flags) -> slot_retention {
          (void)on_accept_complete(res);
          if (bitmask::has(flags, iou_cqe_flags::more))
            return slot_retention::automatic;
          if (!listening_) return slot_retention::release;
          if (!loop_.submit_accept_multishot(sock_, completion_token{cbid}))
            throw std::runtime_error("Failed to re-arm multishot accept");
          return slot_retention::retain;
        });

    // Pretend a CQE arrived, with a soft error that will be ignored. This will
    // get it to submit itself.
    auto borrowed = loop_.borrow(cbtoken);
    (void)borrowed(cbtoken.as_int(), iou_res{EC::again}, iou_cqe_flags{});
    loop_.detach(std::move(borrowed));

    connect_token_ = cbtoken;
    return connect_token_.is_valid();
  }

  // Close immediately without flushing.
  [[nodiscard]] bool do_close_now(bool already_exchanged = false) {
    assert(loop().is_loop_thread());
    if (!already_exchanged && closed_.exchange(true)) return false;
    send_queue_.clear();
    if (sock_)
      (void)loop_.submit_close(std::move(sock_),
          [_ = self()](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });

    return notify_close_once();
  }

  // Submit `SHUT_WR` and mark the write side as shut. The recv loop is left
  // running so the plugin keeps seeing `on_data` until the peer sends EOF.
  // On submission failure, falls back to a full close. If the read side
  // is already shut (peer EOF observed earlier, or `shutdown_recv` already
  // called), both halves are now down and we cascade straight to a full
  // close rather than leave the socket lingering.
  [[nodiscard]] bool do_shutdown_send_now() {
    assert(loop().is_loop_thread() && !write_shut_);
    if (read_shut_) return do_close_now();
    const auto token = loop_.submit_shutdown(sock_, shutdown_how::wr,
        [this, _ = self()](completion_id, iou_res res, iou_cqe_flags) {
          if (!res) (void)do_close_now();
          return slot_retention{};
        });
    if (!token.is_valid()) return do_close_now();
    write_shut_ = true;
    return true;
  }

#pragma endregion
#pragma region Event handlers

  // Resubmit `buf` using the same callback. This may be used for retriable
  // failures or partial sends.
  [[nodiscard]] bool do_resubmit_send(completion_id cbhandle, buffer& buf) {
    assert(loop().is_loop_thread());
    assert(!buf.active_span().empty());
    send_token_ = completion_token{cbhandle};
    if (send_zc_supported_)
      return loop_.submit_send_buffer(sock_, buf, send_token_,
          slot_retention::automatic);
    else
      return loop_.submit_write_buffer(sock_, buf, send_token_,
          slot_retention::automatic);
  }

  // Handle a failed send CQE. Returns the slot retention decision.
  [[nodiscard]] slot_retention
  do_handle_send_error(completion_id cbhandle, buffer& buf) {
    assert(loop().is_loop_thread());

    // Downgrade from ZC if socket doesn't support it.
    auto res = buf.result();
    if (!res && res.err() == EC::opnotsupp && send_zc_supported_) {
      send_zc_supported_ = false;
      res = iou_res{EC::again};
    }

    // Retry if possible.
    if (res.is_soft_error()) {
      (void)do_resubmit_send(cbhandle, buf);
      return slot_retention::retain;
    }

    // Give up and close on hard errors.
    (void)do_close_now();
    return buf.pending_releases_decision();
  }

  // Handle completion of a send CQE (not the optional ZC notification CQE).
  //
  // For partial sends and retriable errors, resubmit using the same callback
  // so the buffer stays in the closure. A new slot is only used when
  // advancing to the next buffer. `pending_releases_` (maintained by
  // `iou_buffer::update`) tracks all outstanding ZC pins; we retain the
  // callback slot until it reaches zero.
  [[nodiscard]] slot_retention
  on_send_complete(completion_id cbhandle, buffer& buf) {
    assert(loop().is_loop_thread());
    send_token_ = {};
    if (closed_) return buf.pending_releases_decision();

    // Retry on soft errors.
    if (!buf.result().ok()) return do_handle_send_error(cbhandle, buf);

    // Continue partial sends.
    if (!buf.active_span().empty()) {
      (void)do_resubmit_send(cbhandle, buf);
      return slot_retention::retain;
    }

    // Send complete. Start the next send if queued; otherwise act on a
    // pending close / shutdown_send; otherwise notify drain.
    if (!send_queue_.empty())
      (void)do_submit_send();
    else if (close_requested_)
      (void)do_close_now();
    else if (shutdown_send_requested_ && !write_shut_)
      (void)do_shutdown_send_now();
    else
      (void)notify_drained();

    return buf.pending_releases_decision();
  }

  // Handle completion of connect operation. On success, fires `on_drain` and
  // transitions to recv state. On failure, fires `on_close` and initiates
  // close.
  [[nodiscard]] bool on_connect_complete(iou_res res) {
    assert(loop().is_loop_thread() && connecting_);
    connecting_ = false;
    if (closed_) return true;

    // Error.
    if (!res) return do_close_now();

    // Start listening for data.
    if (!do_submit_recv()) return do_close_now();

    // In principle, we could have writes queued.
    if (!send_queue_.empty()) return do_submit_send();

    return notify_drained();
  }

  // Handle completion of a multishot accept. On success, creates a new
  // `iou_stream_conn` for the accepted socket and registers it with the
  // loop. On error, initiates close.
  [[nodiscard]] bool on_accept_complete(iou_res res) {
    assert(loop().is_loop_thread() && listening_);
    if (closed_) return true;

    // Error. If it's an `ECANCELED`, then we're either shutting down or
    // pausing: either way, there's nothing for us to do here.
    if (!res) {
      if (res.is_soft_error() || res.err() == EC::canceled) return true;
      return do_close_now();
    }

    net_socket accepted_sock{os_file{res.value()}};
    auto peer = do_accept_clone(std::move(accepted_sock));
    if (peer) (void)peer->start_reading();
    return true;
  }

  // Handle completion of a receive operation. Delivers the buffer to
  // `on_data` (including an empty buffer on peer EOF, as the EOF signal).
  // On hard error, fully closes. On peer EOF with our write side already
  // shut, transitions to full close.
  [[nodiscard]] bool on_recv_complete(buffer& buf) {
    assert(loop().is_loop_thread());
    read_idle_.postpone();
    if (closed_ || read_shut_) return true;

    const auto res = buf.result();

    // EOF from peer. Deliver an empty view so the plugin learns the read side
    // is shut.
    if (res.value() == 0) {
      read_shut_ = true;
      if (write_shut_) return do_close_now();
      iou_recv_view view{std::move(buf), make_view_resume()};
      return plugin_.on_data(std::move(view));
    }

    // Fail on hard errors, retry on soft errors.
    if (!res) {
      if (!res.is_soft_error()) return do_close_now();
      // For singleshot, we need to resubmit on soft errors.
      if (recv_active_shot_ == shot_type::multi) return true;
      return do_submit_single_recv(&buf);
    }

    // If it snuck in after we stopped listening, just drop it.
    if (close_requested_ || read_shut_) return true;

    // Process buffer and count on view destructor to continue receiving.
    iou_recv_view view{std::move(buf), make_view_resume()};
    return plugin_.on_data(std::move(view));
  }

  // Send out SQEs for new connection so that we're ready to receive either a
  // connection completion, an accepted socket, or data. Without this,
  // nothing is keeping this instance alive.
  [[nodiscard]] bool start_reading() {
    assert(loop().is_loop_thread());
    if (closed_) return false;
    if (listening_) return do_submit_accept();
    if (connecting_) return do_submit_connect();
    return do_submit_recv();
  }

  // Notify `on_drain`.
  [[nodiscard]] bool notify_drained() {
    assert(loop().is_loop_thread());
    (void)write_idle_.pause();
    return plugin_.on_drain();
  }

  // Notify `on_close` exactly once.
  [[nodiscard]] bool notify_close_once() {
    assert(loop().is_loop_thread());
    if (on_close_was_notified_) return false;
    on_close_was_notified_ = true;
    return plugin_.on_close();
  }
};

#pragma endregion
}}} // namespace corvid::proto::iouring
