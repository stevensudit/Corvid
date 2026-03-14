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
#include <functional>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifdef __linux__
#include <sys/epoll.h>
#include <sys/eventfd.h>
#endif

#include "ip_socket.h"

namespace corvid { inline namespace proto {

// Callbacks for a registered socket. All fields are optional; a null handler
// is silently skipped when its event fires.
//
// These callbacks fire directly inside the `epoll_wait` dispatch loop on the
// loop thread. They must not block. Their job is to perform the I/O syscall
// (`::read`, `::write`, `::accept`), update internal state, and call
// `io_loop::post()` to schedule the user's completion callback for the next
// iteration. Invoking user-supplied callbacks directly is only appropriate if
// they are guaranteed to be trivially fast and non-blocking.
//
//  `on_readable` -- fired when `EPOLLIN` is reported.
//  `on_writable` -- fired when `EPOLLOUT` is reported. Should be enabled only
//                   when there is data waiting to be drained; see
//                   `io_loop::set_writable`.
//  `on_error`    -- fired when `EPOLLERR` or `EPOLLHUP` is reported. If null,
//                   falls through to `on_readable` so the read path can
//                   observe the error or EOF naturally.
struct io_handlers {
  std::function<void()> on_readable = nullptr;
  std::function<void()> on_writable = nullptr;
  std::function<void()> on_error = nullptr;
};

// `epoll`-based I/O event loop, safe for use with a background thread.
//
// This is an internal building block for higher-level socket types
// (`tcp_socket`, `tcp_listener`, `tcp_client`). User code drives the loop via
// `run()` / `run_once()` / `stop()`, but never registers fds directly.
//
// Higher-level types call `add()` to register an `ip_socket` with an
// `io_handlers` struct. The initial event mask is always
// `EPOLLIN | EPOLLERR | EPOLLHUP`. Write-readiness interest (`EPOLLOUT`) is
// toggled with `set_writable()` without disturbing the stored handlers --
// this lets a `tcp_socket` arm write interest only while its send buffer is
// non-empty.
//
// `post()` schedules a callback to run at the top of the next `run_once()`
// iteration. It is safe to call from any thread: it locks `post_mutex_`,
// pushes the callback, then writes to `wake_fd_` (an `eventfd`) to interrupt
// a sleeping `epoll_wait`. The expected pattern is that I/O callbacks fire on
// the loop thread and may hand work off to a thread pool, which uses `post()`
// to deliver results back.
//
// `stop()` is also thread-safe: it sets `running_` (an `atomic<bool>`) and
// writes to `wake_fd_` so the loop exits promptly even if blocked in
// `epoll_wait`.
//
// `add`, `remove`, and `set_writable` are NOT thread-safe; call them only
// from the loop thread (i.e., from within a callback or a `post()`'d
// function).
//
// `io_loop` is non-copyable and non-movable. Linux only; on other platforms
// the class is defined but all methods are no-ops or return failure.
class io_loop {
public:
  // Maximum number of events retrieved per `epoll_wait` call.
  static constexpr int max_events = 64;

  // Create the `epoll` instance and the internal `eventfd` wakeup handle.
  // Throws `std::system_error` on failure.
  io_loop() {
#ifdef __linux__
    epoll_fd_ = os_file{::epoll_create1(EPOLL_CLOEXEC)};
    if (!epoll_fd_.is_open())
      throw std::system_error(errno, std::generic_category(), "epoll_create1");

    // The `eventfd` is used by `post()` and `stop()` to interrupt a sleeping
    // `epoll_wait` from another thread.
    wake_fd_ = os_file{::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)};
    if (!wake_fd_.is_open())
      throw std::system_error(errno, std::generic_category(), "eventfd");

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wake_fd_.handle();
    if (::epoll_ctl(epoll_fd_.handle(), EPOLL_CTL_ADD, wake_fd_.handle(),
            &ev) != 0)
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

  // Register `sock` with `handlers`. The initial event mask is
  // `EPOLLIN | EPOLLERR | EPOLLHUP`; `EPOLLOUT` is not enabled until
  // `set_writable(sock, true)` is called. Returns false if `sock` is already
  // registered or `epoll_ctl` fails.
  //
  // Must run on the loop thread. To call safely from another thread, use
  // `post()`: `loop.post([&]{ loop.add(sock, handlers); });`
  bool add(const ip_socket& sock, io_handlers handlers) {
#ifdef __linux__
    const int fd = sock.file().handle();
    if (registrations_.contains(fd)) return false;
    const uint32_t events = EPOLLIN | EPOLLERR | EPOLLHUP;
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (::epoll_ctl(epoll_fd_.handle(), EPOLL_CTL_ADD, fd, &ev) != 0)
      return false;
    registrations_.emplace(fd, registration{events, std::move(handlers)});
    return true;
#else
    (void)sock;
    (void)handlers;
    return false;
#endif
  }

  // Unregister `sock`. Returns false if `sock` is not registered or
  // `epoll_ctl` fails. Must run on the loop thread; use `post()` otherwise.
  bool unregister(const ip_socket& sock) {
#ifdef __linux__
    const int fd = sock.file().handle();
    if (!registrations_.contains(fd)) return false;
    epoll_event ev{};
    const bool ok =
        ::epoll_ctl(epoll_fd_.handle(), EPOLL_CTL_DEL, fd, &ev) == 0;
    registrations_.erase(fd);
    return ok;
#else
    (void)sock;
    return false;
#endif
  }

  // Add or remove `EPOLLOUT` from the event mask for `sock` without changing
  // handlers. Returns false if `sock` is not registered or `epoll_ctl` fails.
  // Must run on the loop thread; use `post()` otherwise.
  bool set_writable(const ip_socket& sock, bool on = true) noexcept {
#ifdef __linux__
    const int fd = sock.file().handle();
    const auto it = registrations_.find(fd);
    if (it == registrations_.end()) return false;
    const uint32_t mask =
        on ? (it->second.events | EPOLLOUT)
           : (it->second.events & ~uint32_t{EPOLLOUT});
    if (mask == it->second.events) return true;
    epoll_event ev{};
    ev.events = mask;
    ev.data.fd = fd;
    if (::epoll_ctl(epoll_fd_.handle(), EPOLL_CTL_MOD, fd, &ev) != 0)
      return false;
    it->second.events = mask;
    return true;
#else
    (void)sock;
    (void)on;
    return false;
#endif
  }

  // Schedule `fn` to run at the top of the next `run_once()` iteration,
  // before `epoll_wait`. Safe to call from any thread. This is the right
  // place to invoke user completion callbacks, hand work to a thread pool,
  // or call `add`/`remove`/`set_writable` from outside the dispatch loop.
  // When a thread pool finishes work, it calls `post()` to bring the result
  // back to the loop thread.
  void post(std::function<void()> fn) {
    {
      std::scoped_lock lock{post_mutex_};
      post_queue_.push_back(std::move(fn));
    }
    wake();
  }

  // Signal the loop to exit after the current dispatch round completes.
  // Safe to call from any thread.
  void stop() noexcept {
    running_ = false;
    wake();
  }

  // Wait up to `timeout_ms` milliseconds for events and dispatch ready
  // sockets. Pass -1 to block indefinitely, 0 to poll. Drains `post_queue_`
  // first. Returns the number of I/O events dispatched, or -1 on error.
  int run_once(int timeout_ms = -1) {
#ifdef __linux__
    drain_post_queue();

    epoll_event events[max_events];
    const int n =
        ::epoll_wait(epoll_fd_.handle(), events, max_events, timeout_ms);
    if (n < 0) return -1;

    int dispatched = 0;
    for (int i = 0; i < n; ++i) {
      const int fd = events[i].data.fd;

      // Drain the internal wakeup handle and skip -- it carries no user event.
      if (fd == wake_fd_.handle()) {
        uint64_t val;
        ::read(wake_fd_.handle(), &val, sizeof(val));
        continue;
      }

      ++dispatched;
      dispatch_event(fd, events[i].events);
    }

    return dispatched;
#else
    (void)timeout_ms;
    return -1;
#endif
  }

  // Dispatch events in a loop until `stop()` is called or `run_once` returns
  // -1. `timeout_ms` is forwarded to each `epoll_wait` call.
  void run(int timeout_ms = -1) {
    running_ = true;
    while (running_) {
      if (run_once(timeout_ms) < 0) break;
    }
  }

private:
  struct registration {
    uint32_t events;
    io_handlers handlers;
  };

  // Swap-and-drain `post_queue_` under the mutex, then invoke each callback.
  // Callbacks posted from within a drained callback land in the new
  // `post_queue_` and run on the next iteration.
  void drain_post_queue() {
    std::vector<std::function<void()>> pending;
    {
      std::scoped_lock lock{post_mutex_};
      pending.swap(post_queue_);
    }
    for (auto& fn : pending) fn();
  }

  // Dispatch a single epoll event for `fd` with event mask `ev`. Re-looks up
  // the registration before each callback because a callback may call
  // `remove()` or `add()`.
  void dispatch_event(int fd, uint32_t ev) {
#ifdef __linux__
    const auto it = registrations_.find(fd);
    if (it == registrations_.end()) return;

    if (ev & (EPOLLERR | EPOLLHUP)) {
      if (it->second.handlers.on_error)
        it->second.handlers.on_error();
      else if (const auto jt = registrations_.find(fd);
          jt != registrations_.end() && jt->second.handlers.on_readable)
        jt->second.handlers.on_readable();
      return;
    }
    if (ev & EPOLLIN) {
      if (const auto jt = registrations_.find(fd);
          jt != registrations_.end() && jt->second.handlers.on_readable)
        jt->second.handlers.on_readable();
    }
    if (ev & EPOLLOUT) {
      if (const auto jt = registrations_.find(fd);
          jt != registrations_.end() && jt->second.handlers.on_writable)
        jt->second.handlers.on_writable();
    }
#else
    (void)fd;
    (void)ev;
#endif
  }

  // Write to `wake_fd_` to interrupt a sleeping `epoll_wait`. Idempotent and
  // safe to call from any thread.
  void wake() noexcept {
    const uint64_t val = 1;
    ::write(wake_fd_.handle(), &val, sizeof(val));
  }

  os_file epoll_fd_;
  os_file wake_fd_;
  std::unordered_map<int, registration> registrations_;
  std::mutex post_mutex_;
  std::vector<std::function<void()>> post_queue_;
  std::atomic<bool> running_{false};
};

}} // namespace corvid::proto
