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
#include <condition_variable>
#include <cerrno>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif

#include "../containers/opt_find.h"
#include "ip_socket.h"

namespace corvid { inline namespace proto {

// Abstract base for objects registered with `io_loop`. Higher-level types
// (e.g., `tcp_conn`) inherit from this and override the three event methods.
// The default `on_error` falls through to `on_readable` so that read-path
// code can observe EOF and errors naturally; override to change that behavior.
//
// The `io_loop` stores a `shared_ptr<io_conn>` per registration, so the
// object stays alive for the duration of any in-progress dispatch even if the
// caller unregisters it during a callback.
struct io_conn: std::enable_shared_from_this<io_conn> {
  explicit io_conn(ip_socket&& sock) : sock_(std::move(sock)) {}
  ip_socket& sock() noexcept { return sock_; }
  const ip_socket& sock() const noexcept { return sock_; }

  virtual void on_readable() {}
  virtual void on_writable() {}
  virtual void on_error() { on_readable(); }
  virtual ~io_conn() = default;

private:
  ip_socket sock_;
};

// `epoll`-based I/O event loop, safe for use with a background thread.
//
// This is an internal building block for higher-level socket types
// (e.g., `tcp_conn`). User code drives the loop via `run()` / `run_once()` /
// `stop()`, but never calls `epoll_ctl` directly.
//
// Call `register_socket()` to register an `io_conn`. Read and write readiness
// interest can be chosen at registration time and later toggled with
// `set_readable()` / `set_writable()` without disturbing the registered
// `io_conn`. `EPOLLERR | EPOLLHUP` stay armed regardless, so sockets still
// observe closure and error transitions even when regular reads are disarmed.
//
// `post()` schedules a callback to run at the top of the next `run_once()`
// iteration. It is safe to call from any thread: it locks `post_mutex_`,
// pushes the callback, then writes to `wake_fd_` (an `eventfd`) to interrupt
// a sleeping `epoll_wait`. The expected pattern is that I/O callbacks fire on
// the loop thread and handle I/O immediately, but may hand off work to a
// thread pool, which uses `post()` to deliver results back.
//
// `stop()` is also thread-safe: it sets `running_` (an `atomic<bool>`) and
// writes to `wake_fd_` so the loop exits promptly even if blocked in
// `epoll_wait`.
//
// `register_socket`, `unregister_socket`, `set_readable`, and `set_writable`
// are NOT inherently thread-safe, but they automatically promote a call from
// outside the loop thread into a `post()`.
//
// `io_loop` is non-copyable and non-movable. Linux only; on other platforms
// the class is defined but all methods are no-ops or return failure.
class io_loop {
public:
  // Maximum number of events retrieved per `epoll_wait` call.
  static constexpr size_t max_events = 64;

  // Create the `epoll` instance and the internal `eventfd` wakeup handle.
  // Throws `std::system_error` on failure.
  io_loop() : epoll_fd_{create_epollfd()}, wake_fd_{create_eventfd()} {
#ifdef __linux__
    // The `eventfd` is used by `post()` and `stop()` to interrupt a sleeping
    // `epoll_wait` from another thread.
    epoll_event ev{.events = EPOLLIN,
        .data = epoll_data_t{.fd = wake_fd_.handle()}};
    if (!poll_control(EPOLL_CTL_ADD, wake_fd_.handle(), ev))
      throw std::system_error(errno, std::generic_category(),
          "epoll_ctl wake_fd");
#else
    throw std::runtime_error("io_loop requires Linux");
#endif
  }

  io_loop(const io_loop&) = delete;
  io_loop& operator=(const io_loop&) = delete;
  io_loop(io_loop&&) = delete;
  io_loop& operator=(io_loop&&) = delete;

  ~io_loop() = default;

  // Register `conn`.
  //
  // The initial event mask always includes `EPOLLERR | EPOLLHUP`; read and
  // write readiness are controlled by `readable` and `writable`. Returns
  // false if `sock` is already registered or `epoll_ctl` fails. If executed
  // outside of loop thread, turns into a `post()`.
  [[nodiscard]] bool register_socket(std::shared_ptr<io_conn> conn,
      bool readable = true, bool writable = false) {
    return execute_or_post(
        [this, conn = std::move(conn), readable, writable]() mutable {
          return do_register_socket(std::move(conn), readable, writable);
        });
  }

  // Add or remove `EPOLLIN` from the event mask for `sock` without changing
  // the registered `io_conn`. Returns false if `sock` is not registered or
  // `epoll_ctl` fails. If executed outside of loop thread, turns into a
  // `post()`.
  bool set_readable(const ip_socket& sock, bool on = true) noexcept {
    const auto fd = sock.file().handle();
    return execute_or_post([this, fd, on] { return do_set_readable(fd, on); });
  }

  // Add or remove `EPOLLOUT` from the event mask for `sock` without changing
  // the registered `io_conn`. Returns false if `sock` is not registered or
  // `epoll_ctl` fails. If executed outside of loop thread, turns into a
  // `post()`.
  bool set_writable(const ip_socket& sock, bool on = true) noexcept {
    const auto fd = sock.file().handle();
    return execute_or_post([this, fd, on] { return do_set_writable(fd, on); });
  }

  // Unregister `sock`. Returns false if `sock` is not registered or
  // `epoll_ctl` fails. If executed outside of loop thread, turns into a
  // `post()`.
  bool unregister_socket(const ip_socket& sock) {
    const auto fd = sock.file().handle();
    return execute_or_post([this, fd] { return do_unregister_socket(fd); });
  }

  // Schedule `fn` to run at the top of the next `run_once()` iteration,
  // before `epoll_wait`. Safe to call from any thread. This is the right
  // place to invoke user completion callbacks, hand work to a thread pool,
  // or call `register_socket`/`unregister_socket`/`set_writable` from outside
  // the dispatch loop.
  //
  // When a thread pool finishes work, it calls `post()` to bring the result
  // back to the loop thread.
  bool post(std::function<void()> fn) {
    {
      std::scoped_lock lock{post_mutex_};
      post_queue_.push_back(std::move(fn));
    }
    wake();
    return true;
  }

  // Run `fn` on the loop thread and block the caller until it returns. If
  // already on the loop thread, executes `fn` inline. Unlike a regular post,
  // which is asychronous, this is synchronous and passes through the return
  // value.
  template<typename FN>
  [[nodiscard]] bool post_and_wait(FN&& fn) {
    if (is_loop_thread()) return fn();

    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool result = false;
    post([&] {
      {
        std::lock_guard lock{mutex};
        result = fn();
        done = true;
      }
      cv.notify_one();
    });
    std::unique_lock lock{mutex};
    cv.wait(lock, [&] { return done; });
    return result;
  }

  // Run `fn` immediately if on the loop thread, otherwise `post()` it. Returns
  // the result of `fn` if executed immediately, otherwise true.
  template<typename FN>
  [[nodiscard]] bool execute_or_post(FN&& fn) {
    if (is_loop_thread()) return fn();
    post(std::forward<FN>(fn));
    return true;
  }

  // Signal the loop to exit after the current dispatch round completes.
  // Safe to call from any thread.
  void stop() {
    running_ = false;
    wake();
  }

  // Check whether the current thread is the loop thread.
  bool is_loop_thread() const noexcept {
    // If we're not looping, then we're effectively in the thread loop. This
    // normally happens during setup, before the loop starts.
    if (!running_ || !loop_thread_id_) return true;
    return std::this_thread::get_id() == *loop_thread_id_;
  }

  // Wait up to `timeout_ms` milliseconds for events and dispatch ready
  // sockets. Pass -1 to block indefinitely, 0 to poll. Drains `post_queue_`
  // first. Returns the number of I/O events dispatched, or -1 on error.
  int run_once(int timeout_ms = -1) {
#ifdef __linux__
    // Execute backlog of posts.
    drain_post_queue();

    // Poll for available events.
    epoll_event events[max_events];
    int available =
        ::epoll_wait(epoll_fd_.handle(), events, max_events, timeout_ms);
    if (available < 0) return os_file::is_hard_error() ? -1 : 0;

    // Dispatch each event to handler.
    int dispatched = 0;
    int woken = 0;
    for (int ndx = 0; ndx < available; ++ndx) {
      const int fd = events[ndx].data.fd;

      // Drain the internal wakeup handle and skip: it carries no user event.
      if (fd == wake_fd_.handle()) {
        // `eventfd` expects an 8-byte read; the value is ignored.
        std::string buf{"12345678"};
        (void)wake_fd_.read(buf);
        ++woken;
        continue;
      }

      // Callback executes in polling thread and should immediately handle the
      // event by performing I/O. It should not block or post more work to the
      // loop until ready to handle the

      ++dispatched;
      dispatch_event(fd, events[ndx].events);
    }

    assert(dispatched + woken == available);
    return dispatched;
#else
    (void)timeout_ms;
    return -1;
#endif
  }

  // Dispatch events in a loop until `stop()` is called or `run_once` returns
  // -1. `timeout_ms` is forwarded to each `epoll_wait` call.
  void run(int timeout_ms = -1) {
    {
      std::scoped_lock lock{post_mutex_};
      loop_thread_id_ = std::this_thread::get_id();
      running_ = true;
    }

    while (running_)
      if (run_once(timeout_ms) < 0) break;

    {
      std::scoped_lock lock{post_mutex_};
      loop_thread_id_.reset();
    }
  }

private:
  struct registration {
    uint32_t events;
    std::shared_ptr<io_conn> conn;
  };

  // Register socket and initial interest in reading and writing. Returns false
  // if already registered or `epoll_ctl` fails.
  bool do_register_socket(std::shared_ptr<io_conn>&& conn, bool readable,
      bool writable) {
    assert(is_loop_thread());
#ifdef __linux__
    const int fd = conn->sock().file().handle();
    if (registrations_.contains(fd)) return false;

    const uint32_t events = make_event_mask(readable, writable);
    epoll_event ev{.events = events, .data = epoll_data_t{.fd = fd}};
    if (!poll_control(EPOLL_CTL_ADD, fd, ev)) return false;

    registrations_.emplace(fd, registration{events, std::move(conn)});
    return true;
#else
    (void)conn;
    return false;
#endif
  }

  // Unregister `sock`. Returns false if `sock` is not registered or
  // `epoll_ctl` fails.
  bool do_unregister_socket(os_file::file_handle_t fd) {
    assert(is_loop_thread());
#ifdef __linux__
    if (!registrations_.contains(fd)) return false;
    epoll_event ev{};
    const bool ok = poll_control(EPOLL_CTL_DEL, fd, ev);
    registrations_.erase(fd);
    return ok;
#else
    (void)fd;
    return false;
#endif
  }

  // Add or remove `EPOLLOUT` from the event mask for `sock` without changing
  // the registered `io_conn`. Returns false if `sock` is not registered or
  // `epoll_ctl` fails.
  bool do_set_readable(os_file::file_handle_t fd, bool on = true) noexcept {
    return do_set_interest(fd, EPOLLIN, on);
  }

  // Add or remove `EPOLLOUT` from the event mask for `sock` without changing
  // the registered `io_conn`. Returns false if `sock` is not registered or
  // `epoll_ctl` fails.
  bool do_set_writable(os_file::file_handle_t fd, bool on = true) noexcept {
    return do_set_interest(fd, EPOLLOUT, on);
  }

  bool do_set_interest(os_file::file_handle_t fd, uint32_t flag,
      bool on = true) noexcept {
    assert(is_loop_thread());
#ifdef __linux__
    auto found = find_opt(registrations_, fd);
    if (!found) return false;
    auto& events = found->events;

    const uint32_t mask = on ? (events | flag) : (events & ~flag);
    if (mask == events) return true;

    epoll_event ev{.events = mask, .data = epoll_data_t{.fd = fd}};
    if (!poll_control(EPOLL_CTL_MOD, fd, ev)) return false;

    events = mask;
    return true;
#else
    (void)fd;
    (void)flag;
    (void)on;
    return false;
#endif
  }

  static constexpr uint32_t
  make_event_mask(bool readable = false, bool writable = false) noexcept {
    // `EPOLLRDHUP` is always armed so that peer half-closes (`SHUT_WR`) are
    // detected even when `EPOLLIN` is not subscribed.
    constexpr uint32_t always_on_events = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    return always_on_events | (readable ? uint32_t{EPOLLIN} : uint32_t{0}) |
           (writable ? uint32_t{EPOLLOUT} : uint32_t{0});
  }

#ifdef __linux__
  bool poll_control(int op, int fd, epoll_event& ev) const noexcept {
    return ::epoll_ctl(epoll_fd_.handle(), op, fd, &ev) == 0;
  }
#endif

  // Swap-and-drain `post_queue_` under the mutex, then invoke each callback.
  // Callbacks posted from within a drained callback land in the new
  // `post_queue_` and run on the next iteration.
  void drain_post_queue() {
    assert(is_loop_thread());
    std::vector<std::function<void()>> pending;
    {
      std::scoped_lock lock{post_mutex_};
      pending.swap(post_queue_);
    }
    for (auto& fn : pending) fn();
  }

  // Dispatch a single epoll event for `fd` with event mask `ev`. The
  // `shared_ptr<io_conn>` is copied before any virtual call so the object
  // stays alive even if a callback calls `unregister_socket()`.
  void dispatch_event(int fd, uint32_t ev) {
    assert(is_loop_thread());
#ifdef __linux__
    auto found = find_opt(registrations_, fd);
    if (!found) return;

    // Keep the conn alive across the entire dispatch.
    const auto conn = found->conn;

    // Process `EPOLLIN` before `EPOLLERR`/`EPOLLHUP` so that data already in
    // the kernel receive buffer is delivered even when the peer sends data and
    // closes the connection in the same wakeup (i.e., `EPOLLHUP | EPOLLIN`).
    //
    // `on_readable()` calls `do_close_now()` on EOF/error, and `on_error()`
    // is idempotent via `open_.exchange(false)`, so calling it afterward is
    // safe even if the connection was already closed.
    // `EPOLLRDHUP` indicates peer half-close; route it through the readable
    // path so `handle_readable()` can detect EOF via `::read` returning 0.
    if (ev & (EPOLLIN | EPOLLRDHUP)) conn->on_readable();
    if (ev & (EPOLLERR | EPOLLHUP)) {
      conn->on_error();
      return;
    }
    if (ev & EPOLLOUT) conn->on_writable();
#else
    (void)fd;
    (void)ev;
#endif
  }

  // Write to `wake_fd_` to interrupt a sleeping `epoll_wait`. Idempotent and
  // safe to call from any thread.
  void wake() {
    // `eventfd` expects an 8-byte write; the value is ignored.
    std::string buf{"12345678"};
    std::string_view val{buf};
    (void)wake_fd_.write(val);
  }

  static os_file create_epollfd() {
#ifdef __linux__
    auto f = os_file{::epoll_create1(EPOLL_CLOEXEC)};
    if (!f.is_open())
      throw std::system_error(errno, std::generic_category(), "epoll_create1");
    return f;
#else
    return os_file{};
#endif
  }

  static os_file create_eventfd() {
#ifdef __linux__
    auto f = os_file{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    if (!f.is_open())
      throw std::system_error(errno, std::generic_category(), "eventfd");
    return f;
#else
    return os_file{};
#endif
  }

  const os_file epoll_fd_;
  const os_file wake_fd_;

  std::unordered_map<int, registration> registrations_;

  std::mutex post_mutex_;
  std::vector<std::function<void()>> post_queue_;

  std::atomic<bool> running_{false};
  std::optional<std::thread::id> loop_thread_id_;
};

}} // namespace corvid::proto
