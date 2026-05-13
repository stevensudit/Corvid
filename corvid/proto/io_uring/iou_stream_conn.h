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

#include "../../enums/bool_enums.h"
#include "../net_endpoint.h"
#include "iou_buf_pool.h"
#include "iou_loop.h"

namespace corvid { inline namespace proto { namespace iouring {
using namespace bool_enums;

// Fwd.
template<typename ConnPlugin>
class iou_stream_conn;

#pragma region iou_recv_view
// Move-only recv view delivered to a plugin's `on_data`. Three consumption
// paths:
//
//  Inline: call `active_view` + `consume`. When the buffer is not Provided by
//      the kernel for a multishot, you can leave some of the payload
//      unconsumed and it will be accumulated onto by subsequent `recv`s.
//      However, if the buffer is full or was Provided, it is an error to leave
//      unconsumed payload.
//
//  Async: call `take` to transfer buffer ownership for off-loop parsing.
//  Incoming data will land in another buffer.
//
//  Stop: extract the buffer first via `take`, then call `stop_receiving` to
//        end the `recv` loop. Returns a `posted_fn` resume callback that keeps
//        the conn alive while held and, when invoked, restarts the `recv`
//        loop. Useful for backpressure.
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

#pragma region Accessors

  // True if the connection has not yet been closed. Safe to call from any
  // thread, but there's no guarantee that a connection will remain open, while
  // a closed one will remain closed.
  [[nodiscard]] bool is_open() const noexcept { return open_; }

  // The remote peer address. For accepted connections, computed lazily via
  // `getpeername` on first call. Safe from any thread.
  [[nodiscard]] const net_endpoint& remote_endpoint() noexcept {
    std::scoped_lock lock{endpoint_mutex_};
    if (remote_.empty()) remote_ = net_endpoint::peer_of(sock_);
    return remote_;
  }

  // The local address this socket is bound to, computed lazily via
  // `getsockname` on first call. Useful after `listen` on port 0 to discover
  // the OS-assigned port. Safe from any thread.
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
  [[nodiscard]] auto& plugin(this auto& self) noexcept {
    return self->plugin_;
  }

  //
  // Block size for borrowed buffers. Thread-safe, but inherently racy.
  //

  [[nodiscard]] block_size recv_buf_size() const noexcept {
    return recv_buf_size_;
  }
  void set_recv_buf_size(block_size size) noexcept { recv_buf_size_ = size; }

  [[nodiscard]] block_size send_buf_size() const noexcept {
    return send_buf_size_;
  }
  void set_send_buf_size(block_size size) noexcept { send_buf_size_ = size; }

#pragma endregion
#pragma region send

  // Queue a string for sending. JIT-borrows a write `buffer` to pack
  // consecutive string items. Safe from any thread.
  [[nodiscard]] bool send(std::string&& data) {
    if (!open_ || data.empty()) return false;
    return loop_.execute_or_post(
        [conn = self(), d = std::move(data)]() mutable -> bool {
          if (!conn->open_) return false;
          conn->send_queue_.emplace_back(std::move(d));
          if (!conn->send_token_) return conn->do_submit_send();
          return true;
        });
  }

  // Queue a registered `buffer` for zero-copy sending. Safe from any thread.
  [[nodiscard]] bool send(buffer&& buf) {
    if (!open_ || !buf || buf.active_span().empty()) return false;
    return loop_.execute_or_post(
        [conn = self(), b = std::move(buf)]() mutable -> bool {
          if (!conn->open_) return false;
          conn->send_queue_.emplace_back(std::move(b));
          if (!conn->send_token_) return conn->do_submit_send();
          return true;
        });
  }

#pragma endregion
#pragma region close

  // Start a close.
  //
  // If `coordination` is `unilateral` (the default), flushes pending `send`s
  // and then closes the socket. If `bilateral`, instead shuts down the write
  // side after flushing pending `send`s and discards incoming data until the
  // peer closes. Set the policy via `set_shutdown` before calling this.
  //
  // Safe from any thread.
  [[nodiscard]] bool close() {
    return loop_.execute_or_post([conn = self()] { return conn->do_close(); });
  }

  // Get the shutdown `coordination_policy` used by `close`. `bilateral`
  // shuts down the write side after the send queue flushes, then discards
  // incoming data until the peer sends EOF. `unilateral` (the default) closes
  // the entire socket once the queue empties. Safe from any thread.
  [[nodiscard]] coordination_policy get_shutdown() const noexcept {
    return shutdown_;
  }

  // Set the shutdown coordination policy. `shutdown` defaults to `unilateral`.
  // Call before `close`. Safe from any thread.
  void set_shutdown(coordination_policy shutdown) noexcept {
    shutdown_ = shutdown;
  }

  // Forceful close: cancel pending I/O and close immediately with RST.
  // Safe from any thread.
  [[nodiscard]] bool hangup() {
    return loop_.execute_or_post([conn = self()] {
      return conn->do_hangup_now();
    });
  }

  // Pause the recv loop. Returns a `shared_ptr` to conn, so that you
  // can keep the conn alive until you call `resume_receiving` through the
  // pointer. Idempotent. Safe from any thread.
  [[nodiscard]] auto stop_receiving() {
    auto conn = self();
    if (!recv_paused_->exchange(true, std::memory_order::relaxed))
      (void)loop_.execute_or_post([conn] {
        if (!conn->recv_token_) return false;
        return conn->loop_.submit_cancel(std::move(conn->recv_token_));
      });
    return conn;
  }

  // Resume receiving after `stop_receiving`. Idempotent. Safe from any
  // thread.
  [[nodiscard]] bool resume_receiving() {
    if (!recv_paused_->exchange(false, std::memory_order::relaxed))
      return false;
    return loop_.execute_or_post([conn = self()]() -> bool {
      if (!conn->open_ || conn->recv_token_) return false;
      return conn->do_submit_recv();
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
      coordination_policy shutdown, block_size recv_buf_size,
      block_size send_buf_size, shot_type recv_shot,
      PluginArgs&&... plugin_args)
      : loop_{loop}, sock_{std::move(sock)},
        remote_{remote ? *remote : net_endpoint{}},
        connecting_{role == connection_role::client},
        listening_{role == connection_role::server}, shutdown_{shutdown},
        recv_buf_size_{recv_buf_size}, send_buf_size_{send_buf_size},
        recv_intended_shot_{recv_shot}, recv_active_shot_{recv_shot},
        plugin_{*this, std::forward<PluginArgs>(plugin_args)...} {
    static_assert(iou_stream_conn_plugin<plugin_t>,
        "plugin_t must satisfy the iou_stream_conn_plugin concept");
  }

  // Accept-clone construction path. Called by `do_accept_clone` to build a
  // child conn from a parent. The new plugin is constructed by invoking
  // `accept_from.make_child_plugin(*this)`.
  iou_stream_conn(allow, const plugin_t& accept_from, iou_loop& loop,
      net_socket sock, const net_endpoint* remote,
      coordination_policy shutdown, block_size recv_buf_size,
      block_size send_buf_size, shot_type recv_shot)
      : loop_{loop}, sock_{std::move(sock)},
        remote_{remote ? *remote : net_endpoint{}}, shutdown_{shutdown},
        recv_buf_size_{recv_buf_size}, send_buf_size_{send_buf_size},
        recv_intended_shot_{recv_shot}, recv_active_shot_{recv_shot},
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
  [[nodiscard]] static iou_stream_conn*
  adopt(iou_loop& loop, net_socket sock, net_endpoint remote,
      shot_type recv_shot = shot_type::multi, PluginArgs&&... plugin_args) {
    return do_make(loop, std::move(sock), &remote, std::nullopt,
        coordination_policy::unilateral, block_size::kb004, block_size::kb004,
        recv_shot, std::forward<PluginArgs>(plugin_args)...);
  }

  // Initiate an async connect to `remote`. `on_drain` fires on success;
  // `on_close` fires on failure. Returns `nullptr` on socket creation failure.
  template<typename... PluginArgs>
  [[nodiscard]] static iou_stream_conn*
  connect(iou_loop& loop, const net_endpoint& remote,
      shot_type recv_shot = shot_type::multi, PluginArgs&&... plugin_args) {
    auto sock = net_socket::create_for(remote);
    if (!sock.is_open()) return nullptr;
    return do_make(loop, std::move(sock), &remote, connection_role::client,
        coordination_policy::unilateral, block_size::kb004, block_size::kb004,
        recv_shot, std::forward<PluginArgs>(plugin_args)...);
  }

  // Create a listening socket bound to `local`. Each accepted connection
  // gets a fresh plugin built via the parent plugin's `make_child_plugin`.
  // Returns `nullptr` on failure.
  template<typename... PluginArgs>
  [[nodiscard]] static iou_stream_conn*
  listen(iou_loop& loop, const net_endpoint& local,
      shot_type recv_shot = shot_type::multi, PluginArgs&&... plugin_args) {
    auto sock = net_socket::create_for(local);
    if (!sock.is_open()) return nullptr;
    if (!sock.set_reuse_addr()) return nullptr;
    if (!sock.bind(local)) return nullptr;
    if (!sock.listen()) return nullptr;
    return do_make(loop, std::move(sock), nullptr, connection_role::server,
        coordination_policy::unilateral, block_size::kb004, block_size::kb004,
        recv_shot, std::forward<PluginArgs>(plugin_args)...);
  }

#pragma endregion
#pragma region Data members
private:
  iou_loop& loop_;
  net_socket sock_;

  std::mutex endpoint_mutex_; // protects lazy initialization of endpoints.
  net_endpoint remote_;       // Always access through `remote_endpoint` JIT.
  net_endpoint local_;        // Always access through `local_endpoint` JIT.

  relaxed_atomic_bool open_{true}; // Cleared once close starts.
  bool connecting_{};      // True when socket starts off trying to connect.
  bool listening_{};       // True when socket starts off listening.
  bool close_requested_{}; // True once `close` is called.
  bool close_notified_{};  // True once `on_close` fires.
  bool write_shut_{};      // True after SHUT_WR in bilateral close.
  bool peer_eof_{};        // True once peer sends EOF.
  relaxed_atomic<coordination_policy> shutdown_{
      coordination_policy::unilateral};

  // Size of borrowed buffers.
  relaxed_atomic<block_size> recv_buf_size_{block_size::kb004};
  relaxed_atomic<block_size> send_buf_size_{block_size::kb004};

  // Recv state: whether it's paused, whether we're trying for single or
  // multishot, and whether the current shot is single or multi. The
  // distinction is needed because we can be forced by buffer pressure to
  // downgrade from multi to single.
  relaxed_atomic_bool recv_paused_;
  shot_type recv_intended_shot_{shot_type::multi};
  shot_type recv_active_shot_{shot_type::multi};

  // When receiving, token of callback. Can be used for cancelation.
  completion_token recv_token_;

  // Send state: one SQE in flight at a time; queue holds strings and
  // buffers.
  // TODO: This really should be a circular buffer.
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
  static iou_stream_conn*
  do_make(iou_loop& loop, net_socket sock, const net_endpoint* remote,
      std::optional<connection_role> role, coordination_policy shutdown,
      block_size recv_buf_size, block_size send_buf_size, shot_type recv_shot,
      PluginArgs&&... plugin_args) {
    auto conn = std::make_shared<iou_stream_conn>(allow::ctor, loop,
        std::move(sock), remote, role, shutdown, recv_buf_size, send_buf_size,
        recv_shot, std::forward<PluginArgs>(plugin_args)...);
    auto* raw = conn.get();
    if (!loop.execute_or_post([p = std::move(conn)] {
          return p->start_reading();
        }))
      return nullptr;
    return raw;
  }

  // Produce a new `iou_stream_conn` for each accepted connection. The new
  // conn's plugin is built by calling `plugin_.make_child_plugin(*new_conn)`.
  [[nodiscard]] std::shared_ptr<iou_stream_conn>
  do_accept_clone(net_socket&& sock, const net_endpoint* remote = nullptr) {
    assert(loop().is_loop_thread());
    return std::make_shared<iou_stream_conn>(allow::ctor, plugin_, loop_,
        std::move(sock), remote, coordination_policy{shutdown_},
        block_size{recv_buf_size_}, block_size{send_buf_size_},
        recv_intended_shot_);
  }

  // Submit buffer for recv.
  [[nodiscard]] bool do_submit_recv() {
    if (recv_active_shot_ == shot_type::single) return do_submit_single_recv();
    return do_submit_multi_recv();
  }

  // Submit buffer for singleshot recv, borrowing a read buffer if needed.
  // `allow_upgrade` prevents re-entering `do_submit_multi_recv` when multishot
  // submission itself fails.
  [[nodiscard]] bool
  do_submit_single_recv(buffer* bufptr = {}, bool allow_upgrade = true) {
    assert(loop().is_loop_thread());
    if (recv_token_) return false;
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
        [conn = self()](completion_id, buffer& b) {
          conn->recv_token_ = {};
          (void)conn->on_recv_complete(b);
          return slot_retention{};
        });
    return recv_token_.is_valid();
  }

  // Submit a multishot recv using the loop's TCP provided-buffer ring.
  // Automatically handles `has_more` failure.
  [[nodiscard]] bool do_submit_multi_recv() {
    assert(loop().is_loop_thread() && !recv_token_);
    if (recv_paused_) return false;

    recv_active_shot_ = shot_type::multi;
    recv_token_ = loop_.submit_recv_buffer_multi(sock_,
        [conn = self()](completion_id cbid,
            buffer& buf) mutable -> slot_retention {
          if (!conn->open_) return slot_retention::release;
          const auto result = buf.result();
          const bool has_more = buf.has_more();

          // Normal case.
          if (has_more) {
            (void)conn->on_recv_complete(buf);
            return slot_retention::automatic;
          }

          // Multishot has stopped. Decide what to do about it.
          conn->recv_token_ = {};

          // If it's an intentional cancelation, do not resuscitate.
          if (result.err() == EC::canceled) {
            conn->recv_paused_ = true;
            return slot_retention::release;
          }

          // If we ran out of buffers, downgrade to singleshot for now.
          if (result.err() == EC::nobufs) {
            (void)conn->do_submit_single_recv();
            return slot_retention::release;
          }

          // Not EOF or an error, so probably just a glitch. Retry.
          if (result.value() > 0 && !conn->recv_paused_) {
            conn->recv_token_ = completion_token{cbid};
            const bool continued = conn->loop_.submit_recv_buffer_multi(
                conn->sock_, completion_token{cbid});
            (void)conn->on_recv_complete(buf);
            if (continued) return slot_retention::retain;
            conn->recv_token_ = {};
            conn->recv_paused_ = true;
            return slot_retention::release;
          }

          // Pass error on.
          (void)conn->on_recv_complete(buf);
          return slot_retention::release;
        });

    // If we can't start a multishot, try singleshot without upgrading again.
    if (!recv_token_) return do_submit_single_recv({}, false);

    return true;
  }

  // Build the resume callback captured by an `iou_recv_view`. Returns an
  // `iou_loop::posted_fn` resume token on the stop path (notsock signal),
  // or an empty `posted_fn` on the normal re-arm path. The outer lambda
  // captures a raw `this` rather than `self`: the view is passed by value
  // into `on_data` and destructs (or has its `stop_receiving` called) before
  // `on_data` returns, all on the loop thread inside the recv-completion
  // callback that already holds a `shared_ptr<self>`. So there's no need to
  // extend the conn's lifetime through the view; we avoid a refcount touch
  // per recv. On the stop path, we *do* mint a `shared_ptr` and bind it into
  // the returned token - that's the whole point of the token.
  [[nodiscard]] iou_recv_view::resume_fn make_resume() {
    return [conn = self()](buffer&& buf) -> iou_loop::posted_fn {
      // Stop signal: deactivated buffer carries `EC::notsock`.
      if (buf.result().err() == EC::notsock)
        return [conn = conn->stop_receiving()] {
          return conn->resume_receiving();
        };

      // Normal re-arm. Singleshot needs an explicit resubmission; multishot
      // is kernel-driven and just needs us to not get in the way.
      (void)conn->loop_.execute_or_post(
          [conn = std::move(conn), buf = std::move(buf)]() mutable -> bool {
            if (!conn->open_) return false;
            if (!conn->recv_paused_ &&
                conn->recv_active_shot_ == shot_type::single)
              return conn->do_submit_single_recv(&buf);
            return true;
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

  // Helper for sending specific buffer.
  //
  // Prefers `submit_send_buffer` (ZC). On first `EOPNOTSUPP` (such as with
  // Unix domain sockets), `on_send_complete` clears `send_zc_supported_` and
  // this helper falls back to `submit_write_buffer` for all subsequent sends.
  [[nodiscard]] bool do_submit_send_buffer(buffer&& buf) {
    assert(loop().is_loop_thread() && !send_token_);
    if (!buf) return false;

    auto fn =
        [conn = self()](completion_id cbhandle, buffer& b) -> slot_retention {
      if (bitmask::has(b.cqe_flags(), iou_cqe_flags::notif))
        return b.pending_releases_decision();

      return conn->on_send_complete(cbhandle, b);
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
        [conn = self()](completion_id, iou_res res,
            iou_cqe_flags) -> slot_retention {
          (void)conn->on_connect_complete(res);
          return {};
        });
    return connect_token_.is_valid();
  }

  // Submit a multishot accept operation.
  [[nodiscard]] bool do_submit_accept() {
    assert(loop().is_loop_thread() && listening_ && !connect_token_);
    // Set up a callback that resubmits itself, and use it to bootstrap the
    // initial submission.
    const auto cbtoken = loop_.tokenize(
        [conn = self()](completion_id cbid, iou_res res,
            iou_cqe_flags flags) -> slot_retention {
          (void)conn->on_accept_complete(res);
          if (bitmask::has(flags, iou_cqe_flags::more))
            return slot_retention::automatic;
          if (!conn->listening_) return slot_retention::release;
          if (!conn->loop_.submit_accept_multishot(conn->sock_,
                  completion_token{cbid}))
            throw std::runtime_error("Failed to re-arm multishot accept");
          return slot_retention::retain;
        });

    // Pretend a CQE arrived, with a soft error that will be ignored. This will
    // get it to submit itself.
    auto borrowed = loop_.borrow(cbtoken);
    (void)borrowed(cbtoken.as_int(), iou_res{*EC::again}, iou_cqe_flags{});
    loop_.detach(std::move(borrowed));

    connect_token_ = cbtoken;
    return connect_token_.is_valid();
  }

  // Close immediately without flushing.
  [[nodiscard]] bool do_close_now() {
    assert(loop().is_loop_thread());
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    send_queue_.clear();
    if (sock_)
      (void)loop_.submit_close(std::move(sock_),
          [conn = self()](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });

    return notify_close_once();
  }

  // Close immediately with RST, canceling all pending I/O first.
  [[nodiscard]] bool do_hangup_now() {
    assert(loop().is_loop_thread());
    if (!open_->exchange(false, std::memory_order::relaxed)) return false;
    send_queue_.clear();
    if (sock_) {
      (void)sock_.set_option(socket_option::linger,
          linger{.l_onoff = 1, .l_linger = 0});
      (void)loop_.submit_close(std::move(sock_),
          [conn = self()](completion_id, iou_res, iou_cqe_flags) {
            return slot_retention{};
          });
      (void)loop_.immediate_submit();
    }
    return notify_close_once();
  }

  // Finalize a requested close once the send queue is empty. With `unilateral`
  // (or if the peer already sent EOF), closes immediately. With `bilateral`,
  // shuts down the write side via `SHUT_WR` and keeps the recv loop running to
  // discard incoming data until the peer sends EOF.
  [[nodiscard]] bool do_finish_close() {
    assert(loop().is_loop_thread());
    if (shutdown_ == coordination_policy::unilateral || peer_eof_)
      return do_close_now();
    const auto token = loop_.submit_shutdown(sock_, shutdown_how::wr,
        [conn = self()](completion_id, iou_res res, iou_cqe_flags) {
          if (!res) (void)conn->do_close_now();
          return slot_retention{};
        });
    if (!token.is_valid()) return do_close_now();
    write_shut_ = true;

    // Must keep listening until the peer sends EOF.
    recv_paused_ = false;
    if (!recv_token_) return do_submit_recv();
    return true;
  }

  // Close after flushing.
  [[nodiscard]] bool do_close() {
    assert(loop().is_loop_thread());
    if (!open_) return false;
    close_requested_ = true;
    if (send_queue_.empty() && !send_token_) return do_finish_close();
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
    // Retry if possible.
    if (buf.result().is_soft_error() || buf.result().err() == EC::opnotsupp) {
      if (buf.result().err() == EC::opnotsupp) send_zc_supported_ = false;
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
  // so the buffer stays in the closure. A new slot is only used when advancing
  // to the next buffer. `pending_releases_` (maintained by
  // `iou_buffer::update`) tracks all outstanding ZC pins; we retain the
  // callback slot until it reaches zero.
  [[nodiscard]] slot_retention
  on_send_complete(completion_id cbhandle, buffer& buf) {
    assert(loop().is_loop_thread());
    send_token_ = {};
    if (!open_) return buf.pending_releases_decision();

    // Retry on soft errors.
    if (!buf.result().ok()) return do_handle_send_error(cbhandle, buf);

    // Continue partial sends.
    if (!buf.active_span().empty()) {
      (void)do_resubmit_send(cbhandle, buf);
      return slot_retention::retain;
    }

    // Send complete. Start the next send if queued, or close if requested,
    // else notify drain.
    if (!send_queue_.empty())
      (void)do_submit_send();
    else if (close_requested_)
      (void)do_finish_close();
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
    if (!open_) return true;

    // Error.
    if (!res) return do_close_now();

    // Start listening for data.
    if (!do_submit_recv()) return do_close_now();

    // In principle, we could have writes queued.
    if (!send_queue_.empty()) return do_submit_send();

    return notify_drained();
  }

  // Handle completion of a multishot accept. On success, creates a new
  // `iou_stream_conn` for the accepted socket and registers it with the loop.
  // On error, initiates close.
  [[nodiscard]] bool on_accept_complete(iou_res res) {
    assert(loop().is_loop_thread() && listening_);
    if (!open_) return true;

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

  // Handle completion of a receive operation. If successful, delivers the
  // buffer to `on_data`. On error or EOF, initiates close.
  [[nodiscard]] bool on_recv_complete(buffer& buf) {
    assert(loop().is_loop_thread());
    if (!open_) return true;

    // EOF from peer.
    const auto res = buf.result();
    if (res.value() == 0) {
      peer_eof_ = true;
      // Bilateral drain: peer sent EOF as expected, so close now.
      if (close_requested_ && write_shut_) return do_close_now();
      (void)notify_close_once();
      if (close_requested_ && !send_token_ && send_queue_.empty())
        return do_finish_close();
      return true;
    }

    // Fail on hard errors.
    if (!res && !res.is_soft_error()) return do_close_now();

    // Otherwise, keep reading (when there's a soft error or a drain).
    if (!res || (close_requested_ && write_shut_)) {
      if (recv_active_shot_ == shot_type::multi) {
        recv_token_ = {};
        return do_submit_recv();
      }
      return true;
    }

    // Process buffer and count on view destructor to continue receiving.
    iou_recv_view view{std::move(buf), make_resume()};
    return plugin_.on_data(std::move(view));
  }

  // Send out SQEs for new connection so that we're ready to receive either a
  // connection completion, an accepted socket, or data. Without this, nothing
  // is keeping this instance alive.
  [[nodiscard]] bool start_reading() {
    assert(loop().is_loop_thread());
    if (!open_) return false;
    if (listening_) return do_submit_accept();
    if (connecting_) return do_submit_connect();
    return do_submit_recv();
  }

  // Notify `on_drain`.
  [[nodiscard]] bool notify_drained() {
    assert(loop().is_loop_thread());
    return plugin_.on_drain();
  }

  // Notify `on_close` exactly once.
  [[nodiscard]] bool notify_close_once() {
    assert(loop().is_loop_thread());
    if (close_notified_) return false;
    close_notified_ = true;
    return plugin_.on_close();
  }
};

#pragma endregion
}}} // namespace corvid::proto::iouring
