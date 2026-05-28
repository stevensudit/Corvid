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
#include <atomic>
#include <chrono>
#include <cerrno>
#include <format>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>

#include <pthread.h>

#include "../../concurrency/jthread_stoppable_sleep.h"
#include "../../concurrency/notifiable.h"
#include "../../concurrency/owner_thread_dispatcher.h"
#include "../../concurrency/relaxed_atomic.h"
#include "../../concurrency/tombstone.h"
#include "../../containers/scope_exit.h"
#include "../../containers/opt_find.h"
#include "../../filesys/epoll.h"
#include "../../filesys/event_fd.h"
#include "../../filesys/net_socket.h"
#include "../../meta/fixed_function.h"

namespace corvid { inline namespace proto {

using namespace std::chrono_literals;

#pragma region epoll_io_conn

// Abstract base for objects registered with `epoll_loop`. Higher-level types
// (e.g., `epoll_stream_conn`) inherit from this and override the three event
// methods. The default `on_error` falls through to `on_readable` so that
// read-path code can observe EOF and errors naturally; override to change that
// behavior.
//
// The `epoll_loop` stores a `shared_ptr<epoll_io_conn>` per registration, so
// the object stays alive for the duration of any in-progress dispatch even if
// the caller unregisters it during a callback.
struct epoll_io_conn: std::enable_shared_from_this<epoll_io_conn> {
  explicit epoll_io_conn(net_socket&& sock) : sock_(std::move(sock)) {}
  net_socket& sock() noexcept { return sock_; }
  const net_socket& sock() const noexcept { return sock_; }

  virtual bool on_readable() { return false; }
  virtual bool on_writable() { return false; }
  virtual bool on_error() { return on_readable(); }
  virtual ~epoll_io_conn() = default;

  // Current epoll interest mask. Written only by `epoll_loop` while on the
  // loop thread.
  uint32_t events{0};

private:
  net_socket sock_;
};

#pragma endregion
#pragma region epoll_loop

// `epoll`-based I/O event loop, safe for use with a background thread. This
// implements a Reactor pattern where we wait for readiness and then act.
//
// This is an internal building block for higher-level socket types
// (e.g., `epoll_stream_conn`). User code drives the loop via `run` /
// `run_once` / `stop`, but never calls `epoll_ctl` directly.
//
// Call `register_socket` to register an `epoll_io_conn`. Read and write
// readiness interest can be chosen at registration time and later toggled with
// `enable_reads` / `enable_writes` without disturbing the registered
// `epoll_io_conn`. `EPOLLERR | EPOLLHUP` stay armed regardless, so sockets
// still observe closure and error transitions even when regular reads are
// disarmed.
//
// Cross-thread dispatch is provided by `owner_thread_dispatcher`: `post`,
// `execute_or_post`, `post_and_wait`, and `is_loop_thread` come from the base
// class. The base owns the wakeup `event_fd` (exposed as `wake_fd`), which
// `epoll_loop` registers with `epoll` so that posts interrupt a sleeping
// `epoll_wait`. Posted callbacks run at the top of the next `run_once`. The
// expected pattern is that I/O callbacks fire on the loop thread and handle
// I/O immediately, but may hand off work to a thread pool, which uses `post`
// to deliver results back.
//
// `stop` is also thread-safe: it sets `running_` (a
// `notifiable<std::atomic_bool>`) and signals the wake `event_fd` so the loop
// exits promptly even if blocked in `epoll_wait`.
//
// `register_socket`, `unregister_socket`, `enable_reads`, and `enable_writes`
// are NOT inherently thread-safe, but they automatically promote a call from
// outside the active polling thread for this loop into a `post`.
//
// `epoll_loop` is non-copyable, non-movable, and always heap-allocated via
// `epoll_loop::make`. Per the `owner_thread_dispatcher` contract, the
// instance must be constructed and destructed on the loop thread; use
// `epoll_loop_runner` to satisfy this automatically.
class epoll_loop
    : public concurrency::owner_thread_dispatcher<fixed_function<
          concurrency::default_fixed_function::capacity, bool()>> {
  enum class allow : bool { ctor };

#pragma region Types
public:
  // Maximum number of events retrieved per `epoll_wait` call.
  static constexpr size_t max_events = 64;

  using posted_fn_t =
      fixed_function<concurrency::default_fixed_function::capacity, bool()>;
  using parent = concurrency::owner_thread_dispatcher<posted_fn_t>;

#pragma endregion
#pragma region Infrastructure
public:
  // Create the `epoll` instance and register the base's wakeup `event_fd`.
  // Throws `std::system_error` on failure. Use `make` instead of calling this
  // directly. Per the dispatcher contract, the constructing thread becomes the
  // loop thread.
  explicit epoll_loop(allow) : epoll_{create_epollfd()} {
    // The base's `event_fd` is used by `post` and `stop` to interrupt a
    // sleeping `epoll_wait` from another thread.
    epoll_event ev{.events = EPOLLIN,
        .data = epoll_data_t{.fd = wake_fd().handle()}};
    if (!epoll_.add(wake_fd().handle(), ev))
      throw std::system_error(errno, std::generic_category(),
          "epoll_ctl wake_fd");
  }

  epoll_loop(const epoll_loop&) = delete;
  epoll_loop& operator=(const epoll_loop&) = delete;
  epoll_loop(epoll_loop&&) = delete;
  epoll_loop& operator=(epoll_loop&&) = delete;

  // Factory: create a heap-allocated `epoll_loop` managed by
  // `std::shared_ptr`. Throws `std::system_error` in the improbable event that
  // the underlying `epoll` or `eventfd` cannot be created. Per the dispatcher
  // contract, must be called on the thread that will run the loop.
  //
  // Normally, you would want to use an `epoll_loop_runner` to create an
  // instance in a thread.
  [[nodiscard]] static std::shared_ptr<epoll_loop> make() {
    return std::make_shared<epoll_loop>(allow::ctor);
  }

  [[nodiscard]] std::shared_ptr<epoll_loop> self() {
    return std::static_pointer_cast<epoll_loop>(shared_from_this());
  }

  // Block until `run` enters its polling loop. Returns false on timeout.
  // Pass -1 (the default) to wait up to 60 seconds.
  [[nodiscard]] bool wait_until_running(int timeout_ms = -1) {
    if (timeout_ms < 0) timeout_ms = 60000;
    return running_.wait_for_value(std::chrono::milliseconds{timeout_ms},
        true);
  }

  // Dispatch events in a loop until `stop` is called or `run_once` returns
  // -1. `timeout_ms` is forwarded to each `epoll_wait` call.
  //
  // May only be called once; returns false immediately if called again.
  [[nodiscard]] bool run(int timeout_ms = -1) {
    if (!has_run_.kill()) return false;

    running_.notify(true);
    scope_exit on_exit{[&] { running_.notify(false); }};

    bool ok = true;
    for (; running_.get();)
      if (run_once(timeout_ms) < 0) {
        ok = false;
        break;
      }

    return ok;
  }

  // Wait up to `timeout_ms` milliseconds for events and dispatch ready
  // sockets. Pass -1 to block indefinitely, 0 to poll. Drains the active post
  // queue first. Returns the number of I/O events dispatched, or -1 on error.
  [[nodiscard]] int run_once(int timeout_ms = -1) {
    assert(is_loop_thread());

    // Execute backlog of posts.
    (void)execute_post_queue();

    // Poll for available events.
    epoll_event events[max_events];
    auto available = epoll_.wait(events, max_events, timeout_ms);
    if (!available) return os_file::is_hard_error() ? -1 : 0;

    // Dispatch each event to handler.
    int dispatched = 0;
    for (int ndx = 0; ndx < *available; ++ndx) {
      const int fd = events[ndx].data.fd;

      // Drain the internal wakeup handle and skip: it carries no user event.
      if (fd == wake_fd().handle()) {
        if (!wake_fd().read()) return -1;
        continue;
      }

      // Callback executes in polling thread and should immediately handle the
      // event by performing I/O. It should not block or post more work to the
      // loop until ready to handle the callback.
      ++dispatched;
      if (!dispatch_event(fd, events[ndx].events)) return -1;
    }

    return dispatched;
  }

  // Signal the loop to exit after the current dispatch round completes.
  // Safe from any thread.
  [[nodiscard]] bool stop() {
    running_.notify(false);
    return wake_post_queue();
  }

  // Force shutdown of loop resources.
  [[nodiscard]] bool shutdown_epoll_loop() {
    if (!is_loop_thread()) return false;
    (void)shutdown();
    registrations_.clear();
    return true;
  }

#pragma endregion
#pragma region Registration
public:
  // Register `conn`.
  //
  // The initial event mask always includes `EPOLLERR | EPOLLHUP`; read and
  // write readiness are controlled by `readable` and `writable`. Returns
  // false if `epoll_ctl` fails. If executed outside of loop thread, turns into
  // a `post` and returns true.
  [[nodiscard]] bool register_socket(std::shared_ptr<epoll_io_conn> conn,
      bool readable = true, bool writable = false) {
    return execute_or_post(
        [this, conn = std::move(conn), readable, writable]() mutable {
          return do_register_socket(std::move(conn), readable, writable);
        });
  }

  // Add or remove `EPOLLIN` from the event mask for `conn` without changing
  // the registered `epoll_io_conn`. Returns false if `epoll_ctl` fails. If
  // executed outside of loop thread, turns into a `post` and returns true.
  [[nodiscard]] bool enable_reads(epoll_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      return do_enable_reads(*sp, on);
    });
  }

  // Add or remove `EPOLLRDHUP` from the event mask for `conn` without
  // changing the registered `epoll_io_conn`. Disarming is useful after EOF is
  // observed on the read side to prevent repeated level-triggered wakeups.
  // Returns false if `epoll_ctl` fails. If executed outside of loop thread,
  // turns into a `post` and returns true.
  [[nodiscard]] bool enable_rdhup(epoll_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      return do_enable_rdhup(*sp, on);
    });
  }

  // Add or remove `EPOLLOUT` from the event mask for `conn` without changing
  // the registered `epoll_io_conn`. Returns false if `epoll_ctl` fails. If
  // executed outside of loop thread, turns into a `post` and returns true.
  [[nodiscard]] bool enable_writes(epoll_io_conn& conn, bool on = true) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp), on] {
      return do_enable_writes(*sp, on);
    });
  }

  // Unregister `conn`. Returns false if `conn` is not registered or
  // `epoll_ctl` fails. If executed outside of loop thread, turns into a
  // `post` and returns true.
  [[nodiscard]] bool unregister_socket(epoll_io_conn& conn) {
    auto sp = conn.shared_from_this();
    return execute_or_post([this, sp = std::move(sp)] {
      return do_unregister_socket(*sp);
    });
  }

  // Unregister `sock`. Returns false if `sock` is not registered or
  // `epoll_ctl` fails. If executed outside of loop thread, turns into a
  // `post` and returns true.
  [[nodiscard]] bool unregister_socket(const net_socket& sock) {
    const auto fd = sock.handle();
    return execute_or_post([this, fd] {
      auto found = find_opt(registrations_, fd);
      if (!found) return false;
      return do_unregister_socket(**found);
    });
  }

  // Look up `fd` in the registration table and return the owning
  // `shared_ptr<epoll_io_conn>`, or null if not found. Must be called on the
  // loop thread.
  [[nodiscard]] std::shared_ptr<epoll_io_conn> find_fd(
      os_file::file_handle_t fd) const {
    assert(is_loop_thread());
    auto found = find_opt(registrations_, fd);
    return found ? *found : nullptr;
  }

#pragma endregion
#pragma region Helpers.
private:
  // Register socket and initial interest in reading and writing. Returns
  // false if already registered or `epoll_ctl` fails.
  [[nodiscard]] bool do_register_socket(std::shared_ptr<epoll_io_conn>&& conn,
      bool readable, bool writable) {
    assert(is_loop_thread());
    const int fd = conn->sock().handle();
    if (registrations_.contains(fd)) return false;

    const uint32_t events = make_event_mask(readable, writable);
    epoll_event ev{.events = events, .data = epoll_data_t{.fd = fd}};
    if (!epoll_.add(fd, ev)) return false;

    conn->events = events;
    registrations_.emplace(fd, std::move(conn));
    return true;
  }

  // Unregister `conn`. Returns false if `conn` is not registered or
  // `epoll_ctl` fails.
  [[nodiscard]] bool do_unregister_socket(epoll_io_conn& conn) {
    assert(is_loop_thread());
    const int fd = conn.sock().handle();
    if (!registrations_.erase(fd)) return false;
    return epoll_.remove(fd);
  }

  // Add or remove `EPOLLIN` from the event mask for `conn` without changing
  // the registered `epoll_io_conn`. Returns false if `epoll_ctl` fails.
  [[nodiscard]] bool
  do_enable_reads(epoll_io_conn& conn, bool on = true) noexcept {
    return do_enable_interest(conn, EPOLLIN, on);
  }

  // Add or remove `EPOLLOUT` from the event mask for `conn` without changing
  // the registered `epoll_io_conn`. Returns false if `epoll_ctl` fails.
  [[nodiscard]] bool
  do_enable_writes(epoll_io_conn& conn, bool on = true) noexcept {
    return do_enable_interest(conn, EPOLLOUT, on);
  }

  // Add or remove `EPOLLRDHUP` from the event mask for `conn` without
  // changing the registered `epoll_io_conn`. Returns false if `epoll_ctl`
  // fails.
  [[nodiscard]] bool
  do_enable_rdhup(epoll_io_conn& conn, bool on = true) noexcept {
    return do_enable_interest(conn, EPOLLRDHUP, on);
  }

  // Add or remove `flag` from the event mask for `conn` without changing the
  // registered `epoll_io_conn`. Returns false if `epoll_ctl` fails.
  [[nodiscard]] bool do_enable_interest(epoll_io_conn& conn, uint32_t flag,
      bool on = true) noexcept {
    assert(is_loop_thread());
    const auto fd = conn.sock().handle();
    assert(registrations_.contains(fd));
    auto& events = conn.events;

    // If no change, skip the `epoll_ctl` call.
    const uint32_t mask = on ? (events | flag) : (events & ~flag);
    if (mask == events) return true;

    epoll_event ev{.events = mask, .data = epoll_data_t{.fd = fd}};
    if (!epoll_.modify(fd, ev)) return false;

    events = mask;
    return true;
  }

  static constexpr uint32_t
  make_event_mask(bool readable = false, bool writable = false) noexcept {
    // `EPOLLRDHUP` is armed by default so that peer half-closes (`SHUT_WR`)
    // are detected even when `EPOLLIN` is not subscribed. It can be disarmed
    // after EOF is observed via `enable_rdhup(sock, false)`.
    // This loop intentionally stays level-triggered. `epoll_stream_conn` only
    // arms `EPOLLOUT` while a send queue is backpressured, so LT does not
    // create steady writable wakeups, and switching to `EPOLLET` would require
    // every read/write handler to drain to `EAGAIN` to avoid missed
    // readiness.
    constexpr uint32_t always_on_events = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    return always_on_events | (readable ? uint32_t{EPOLLIN} : uint32_t{0}) |
           (writable ? uint32_t{EPOLLOUT} : uint32_t{0});
  }

  // Dispatch a single epoll event for `fd` with event mask `ev`. The
  // `shared_ptr<epoll_io_conn>` is copied before any virtual call so the
  // object stays alive even if a callback calls `unregister_socket`. Returns
  // whether the event was dispatched.
  [[nodiscard]] bool dispatch_event(int fd, uint32_t ev) {
    assert(is_loop_thread());
    auto found = find_opt(registrations_, fd);
    if (!found) return true;

    // Keep the conn alive across the entire dispatch.
    const auto conn = *found;

    // Process `EPOLLIN` before `EPOLLERR`/`EPOLLHUP` so that data already in
    // the kernel receive buffer is delivered even when the peer sends data
    // and closes the connection in the same wakeup (i.e., `EPOLLHUP |
    // EPOLLIN`).
    //
    // `on_readable` calls `do_close_now` on EOF/error, and `on_error`
    // is idempotent via `open_.exchange(false)`, so calling it afterward is
    // safe even if the connection was already closed.
    // `EPOLLRDHUP` indicates peer half-close; route it through the readable
    // path so `handle_readable` can detect EOF via `::read` returning 0.
    if (ev & (EPOLLIN | EPOLLRDHUP)) (void)conn->on_readable();
    if (ev & (EPOLLERR | EPOLLHUP)) return conn->on_error() || true;
    if (ev & EPOLLOUT) (void)conn->on_writable();
    return true;
  }

  // Create the loop's epoll instance or throw on failure.
  static epoll create_epollfd() {
    auto f = epoll::create();
    if (!f.is_open())
      throw std::system_error(errno, std::generic_category(), "epoll_create1");
    return f;
  }

#pragma endregion
#pragma region Data members
private:
  const epoll epoll_;

  std::unordered_map<int, std::shared_ptr<epoll_io_conn>> registrations_;

  tombstone has_run_;
  notifiable<std::atomic_bool> running_;

#pragma endregion
};

#pragma endregion
#pragma region Loop Runner

// Runs an `epoll_loop` in its own background thread.
//
// The handle is a thin owner of the worker thread. Thread-private state
// (startup synchronization, the loop `shared_ptr`, the cached raw pointer)
// lives in a heap-allocated `runner_state` held by both the handle and the
// worker via `shared_ptr`. This keeps the worker's state alive even when
// the handle is destroyed from a callback running on the loop thread (a
// self-destroy path that would otherwise be a use-after-free).
//
// The loop is constructed inside the worker thread (as required by
// `owner_thread_dispatcher`); the worker resets the loop `shared_ptr`
// before releasing its own ref to `runner_state`, so `~epoll_loop` always
// fires on the worker thread.
//
// Shutdown ordering: call `stop` (or destroy the runner) before destroying
// any object that a pending `post` callback might reference. Pending
// callbacks in the post queue are discarded when the thread exits; they do
// not fire after `stop` returns.
class epoll_loop_runner {
public:
  explicit epoll_loop_runner()
      : state_{std::make_shared<runner_state>()},
        thread_{[state = state_](const std::stop_token& st) {
          run(st, state);
        }} {
    using namespace std::chrono_literals;
    if (!state_->started.wait_for_value(1000ms, true)) {
      thread_.request_stop();
      throw std::runtime_error{"epoll_loop_runner failed to start"};
    }
    std::shared_ptr<epoll_loop> loop;
    std::exception_ptr startup_error;
    if (std::scoped_lock lock{state_->startup_mutex}; true) {
      loop = state_->loop;
      startup_error = state_->startup_error;
    }
    if (startup_error) {
      thread_.join();
      std::rethrow_exception(startup_error);
    }
    if (!loop || !loop->wait_until_running(1000)) {
      // Drop the local ref so the worker's local owns the loop and
      // `~epoll_loop` fires on the worker thread (required by
      // `owner_thread_dispatcher`).
      loop.reset();
      thread_.request_stop();
      if (thread_.joinable()) thread_.join();
      throw std::runtime_error{"epoll_loop_runner failed to start"};
    }
  }

  ~epoll_loop_runner() {
    thread_.request_stop();
    if (!thread_.joinable()) return;
    // `epoll_http_server` can be destroyed from the loop thread when a
    // callback's temporary `shared_ptr` is the final owner. libc++ throws on
    // self-join, so detach in that narrow case after requesting stop. The
    // worker keeps its own ref to `runner_state`, so it remains safe to access
    // after we release `state_` here.
    if (thread_.get_id() == std::this_thread::get_id())
      thread_.detach();
    else
      thread_.join();
  }

  epoll_loop_runner(const epoll_loop_runner&) = delete;
  epoll_loop_runner& operator=(const epoll_loop_runner&) = delete;
  epoll_loop_runner(epoll_loop_runner&&) = delete;
  epoll_loop_runner& operator=(epoll_loop_runner&&) = delete;

  // Signal the thread to exit. Idempotent. Also called implicitly by the
  // destructor.
  void stop() { thread_.request_stop(); }

  [[nodiscard]] epoll_loop* loop() noexcept { return state_->raw_loop; }
  [[nodiscard]] operator epoll_loop&() noexcept { return **state_->raw_loop; }
  [[nodiscard]] epoll_loop* operator->() noexcept { return state_->raw_loop; }

  // Signal set when the worker thread has fully unwound, after `~epoll_loop`
  // has run on the worker and the loop's `shared_ptr` has been released.
  // Returned as an aliased `shared_ptr` that keeps the underlying
  // `runner_state` alive, so callers can safely wait on it even after the
  // handle is destroyed (e.g., the self-destroy-on-loop-thread path). Useful
  // for tests.
  [[nodiscard]] std::shared_ptr<notifiable<std::atomic_bool>>
  finished_signal() const noexcept {
    return {state_, &state_->finished};
  }

private:
  // Thread-private state shared between the handle and the worker.
  struct runner_state {
    std::mutex startup_mutex;
    std::exception_ptr startup_error;
    notifiable<std::atomic_bool> started{false};
    notifiable<std::atomic_bool> finished{false};
    std::shared_ptr<epoll_loop> loop;
    relaxed_atomic<epoll_loop*> raw_loop{nullptr};
  };

  static void
  run(const std::stop_token& st, const std::shared_ptr<runner_state>& state) {
    jthread_stoppable_sleep::set_thread_name("epoll");
    try {
      auto loop = epoll_loop::make();
      if (std::scoped_lock lock{state->startup_mutex}; true)
        state->loop = loop;
      state->raw_loop = loop.get();
      state->started.notify(true);

      // When stop is requested, wake the `epoll_wait` so the loop can
      // exit. Capture `loop` by value (not `state`) so the callback is
      // self-contained and runs the same regardless of which thread
      // triggered the stop. Inner scope so the `stop_callback` (and
      // its `[loop]` capture) is released before the `use_count` check
      // below.
      {
        std::stop_callback on_stop{st, [loop] { (void)loop->stop(); }};
        (void)loop->run(100);
      }

      // Drop the local loop ref here so that the last `shared_ptr`
      // release (and thus `~epoll_loop`) happens on this thread rather
      // than on whichever thread later destroys `runner_state`. The
      // `owner_thread_dispatcher` base requires destruction on the
      // owning thread.
      //
      // Order:
      //   1. Clear `raw_loop` so external lookups via the public
      //      accessor see nullptr; in-flight callers that already
      //      loaded the pointer are responsible for not outliving the
      //      runner.
      //   2. Break ownership cycles by clearing the loop's
      //      registrations on this thread, so any
      //      `shared_ptr<epoll_loop>` held by a connection is released
      //      here.
      //   3. Drop the local `loop` so `state->loop` should be the sole
      //      owner.
      //   4. Belt-and-suspenders: confirm no external `shared_ptr`
      //      still holds the loop alive. `use_count` is approximate
      //      (the standard explicitly calls it "immediately stale" in
      //      MT contexts), but adequate as a misuse detector. Retry
      //      briefly to absorb in-flight releases. If it never
      //      settles, call `std::terminate` rather than let
      //      `~epoll_loop` fire on a non-owner thread (which would
      //      itself throw from inside a destructor). `std::terminate`
      //      is used instead of `throw` so the catch handler below
      //      cannot swallow it.
      //   5. Drop `state->loop`, triggering `~epoll_loop` on this
      //      thread.
      state->raw_loop = nullptr;
      (void)loop->shutdown_epoll_loop();
      loop.reset();

      for (size_t retry = 0; retry < 10 && state->loop.use_count() != 1;
          ++retry)
        std::this_thread::sleep_for(1s);
      if (state->loop.use_count() != 1) std::terminate();

      if (std::scoped_lock lock{state->startup_mutex}; true)
        state->loop.reset();
    }
    catch (...) {
      if (std::scoped_lock lock{state->startup_mutex}; true)
        state->startup_error = std::current_exception();
      state->started.notify(true);
    }
    // Signal `finished` last, after `~epoll_loop` has run. Both worker and
    // any waiter hold `state` via `shared_ptr`, so the notifiable's
    // destructor synchronizes through the ref-count and cannot fire while
    // `notify_all` is still in flight.
    state->finished.notify(true);
  }

  std::shared_ptr<runner_state> state_;
  std::jthread thread_;
};

#pragma endregion

}} // namespace corvid::proto
