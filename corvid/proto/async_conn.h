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
#include <coroutine>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "loop_task.h"
#include "stream_conn.h"

namespace corvid { inline namespace proto {

// Base class for per-call async wrappers around a `stream_conn`. These are
// facades that provide alternative async interfaces (e.g., callback-based or
// coroutine-based) by temporarily installing their own handlers into the
// connection for the duration of their lifetime. The connection's original
// handlers are restored when the wrapper object is destroyed.
//
//  Holds a `shared_ptr<stream_conn>` and a `stream_conn_handlers`. On
//  construction of a derived class, `install_handlers()` atomically swaps the
//  `stream_conn::active_handlers_` pointer from
//  `&stream_conn::own_handlers_` to `&async_conn_base::handlers_`. On
//  destruction, it is restored. The `stream_conn` therefore invokes whichever
//  handlers are currently active without any knowledge of this class
//  hierarchy.
//
//  Derived classes must fully initialize the `handlers_` in their constructor
//  before calling `install_handlers()`. If `install_handlers()` fails (another
//  facade already owns the connection), `conn_` is cleared and `is_valid()`
//  returns false. All other methods are undefined behavior if `!is_valid()`.
//
//  Non-copyable and non-movable: the address of `handlers_` is baked into the
//  connection's atomic pointer, so the object must not relocate in memory.
//
//  Thread safety: `install_handlers()` and the destructor use an atomic CAS /
//  store on the pointer, which is lock-free and does not require the loop
//  thread. `refresh_read_interest()` is posted to the loop asynchronously
//  after each pointer swap.
class async_conn_base {
public:
  // True if `install_handlers()` succeeded and this facade owns the
  // connection's handler slot. Call after construction and before any other
  // method.
  [[nodiscard]] bool is_valid() const noexcept { return !!conn_; }
  [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

  // True if the connection has not yet been closed.
  [[nodiscard]] bool is_open() const noexcept { return conn_->is_open(); }

  // True if the read side is still open.
  [[nodiscard]] bool can_read() const noexcept { return conn_->can_read(); }

  // True if the write side is still open.
  [[nodiscard]] bool can_write() const noexcept { return conn_->can_write(); }

protected:
  std::shared_ptr<stream_conn> conn_;

  // The handlers we install into the connection while this object is alive.
  // Derived class fills these in its constructor, then calls
  // `install_handlers()`.
  stream_conn_handlers handlers_;

  async_conn_base(const async_conn_base&) = delete;
  async_conn_base(async_conn_base&&) = delete;
  async_conn_base& operator=(const async_conn_base&) = delete;
  async_conn_base& operator=(async_conn_base&&) = delete;

  // Stores `conn`. Does NOT install handlers yet; the derived class must call
  // `install_handlers()` after fully initializing `handlers_`.
  explicit async_conn_base(std::shared_ptr<stream_conn> conn)
      : conn_(std::move(conn)) {}

  // Atomically redirect the connection's active-handler pointer from
  // `&own_handlers_` to `&handlers_`. On failure (another facade already owns
  // the connection), clears `conn_` so that `is_valid()` returns false. On
  // success, posts `refresh_read_interest()` to the loop to re-evaluate read
  // interest. Since `handlers_.on_data` starts null for all derived types,
  // this will disarm (or confirm already-disarmed) `EPOLLIN` until a read is
  // registered.
  bool install_handlers() {
    auto* expected = &conn_->own_handlers_;
    // Access conn_->loop_ here (friend access in member function). The
    // lambda calls the static helper, which also has friend access, rather
    // than calling stream_conn private methods directly (lambdas do not
    // inherit friend access from the enclosing member function).
    if (!conn_->active_handlers_.compare_exchange_strong(expected, &handlers_,
            std::memory_order::acq_rel, std::memory_order::acquire))
    {
      conn_.reset();
      return false;
    }
    return conn_->loop_.post([p = conn_] {
      return refresh_read_interest(*p);
    });
  }

  ~async_conn_base() {
    if (!conn_) return;
    // Restore the pointer to the connection's own handlers. We own it
    // exclusively while active (enforced by the CAS in `install_handlers()`).
    conn_->active_handlers_.store(&conn_->own_handlers_,
        std::memory_order::release);
    (void)conn_->loop_.post([p = conn_] { return refresh_read_interest(*p); });
  }

  // Static trampoline used by posted lambdas. Static member functions of a
  // friend class have friend access, whereas lambdas defined inside those
  // functions do not.
  static bool refresh_read_interest(stream_conn& conn) {
    return conn.refresh_read_interest();
  }

  // Wrap `stream_conn::exec_lambda` for derived classes. C++ friendship is
  // not inherited, so derived classes reach private `stream_conn` members
  // through these helpers.
  template<typename FN>
  bool exec_on_loop(FN&& fn) {
    return conn_->exec_lambda(execution::nonblocking, std::forward<FN>(fn));
  }

  // Wrap `stream_conn::enqueue_send`. Must be called on the loop thread.
  bool do_enqueue_send(std::string&& buf) {
    assert(conn_->loop_.is_loop_thread());
    return conn_->enqueue_send(std::move(buf));
  }

  // Post `fn` to the loop. Used by derived classes and their member lambdas
  // to resume coroutine handles without needing direct friend access to
  // `loop_`.
  template<typename FN>
  bool post(FN&& fn) {
    return conn_->loop_.post(std::forward<FN>(fn));
  }
};

// Facade for callback-driven asynchronous I/O on a `stream_conn`. Provides
// `read` and `write` for one-shot async I/O, and `sync_read` for blocking
// reads from a non-loop thread.
//
// Usage:
//
//   async_conn_cb cb{conn_ptr.pointer()};  // or any shared_ptr<stream_conn>
//   cb.read([](std::string& data) { ... });
//   cb.write(std::move(buf), [](bool ok) { ... });
//   // handlers restored when cb goes out of scope
//
// At most one `read` and one `write` may be outstanding at a time.
//
// When the connection closes while a callback is pending, the callback is
// invoked with an empty string (read) or `false` (write), and the connection
// is closed gracefully.
class async_conn_cb: public async_conn_base {
public:
  using async_read_cb = std::function<bool(std::string&)>;
  using async_write_cb = std::function<bool(bool completed)>;

  explicit async_conn_cb(std::shared_ptr<stream_conn> conn)
      : async_conn_base(std::move(conn)) {
    // `on_data` starts null; `arm_read_cb()` sets it when `read()` registers
    // a pending callback. EPOLLIN is armed only while a read is pending,
    // providing back-pressure otherwise.
    handlers_.on_drain = [this](stream_conn&) {
      if (!write_cb_) return false;
      return std::exchange(write_cb_, nullptr)(true);
    };
    handlers_.on_close = [this](stream_conn& c) {
      bool not_handled = false;
      if (read_cb_) {
        handlers_.on_data = {}; // cancel the one-shot arm
        std::string empty;
        not_handled = !std::exchange(read_cb_, nullptr)(empty);
      }
      if (write_cb_) not_handled |= !std::exchange(write_cb_, nullptr)(false);
      return c.close() && !not_handled;
    };
    install_handlers();
  }

  // Receive data once it becomes available. The callback is invoked on the
  // loop thread with the received data; it may `std::move` from it. If the
  // connection closes before data arrives, the callback is invoked with an
  // empty string.
  //
  // The read callback may call `write()` to queue a response, or call `read()`
  // again. Either way, it must not block or perform heavy work, since it's
  // running on the loop thread. If it has extended work to do, it should post
  // that work to a worker thread.
  //
  // Returns false if the connection is closed, `cb` is null, or another read
  // is already pending.
  bool read(async_read_cb cb) {
    if (!cb || !conn_->is_open() || !conn_->can_read() || read_cb_)
      return false;
    read_cb_ = std::move(cb);
    return exec_on_loop([this]() -> bool {
      if (!conn_->is_open() || !conn_->can_read() || !read_cb_) return false;
      return arm_read_cb();
    });
    return true;
  }

  // Start sending the data in `buf`, invoking `cb` upon completion or failure.
  // `cb(true)` means the queue drained successfully; `cb(false)` means the
  // connection closed or failed before all bytes were sent.
  //
  // Returns false immediately if the connection is closed, `buf` is empty,
  // `cb` is null, or another write is already pending.
  bool write(std::string&& buf, async_write_cb cb) {
    if (buf.empty() || !cb || !conn_->is_open() || !conn_->can_write() ||
        write_cb_)
      return false;
    write_cb_ = std::move(cb);
    return exec_on_loop([this, b = std::move(buf)]() mutable -> bool {
      if (conn_->is_open() && conn_->can_write() && write_cb_) {
        if (do_enqueue_send(std::move(b))) return true;
      }
      if (write_cb_) return std::exchange(write_cb_, nullptr)(false);
      return false;
    });
    return true;
  }

private:
  async_read_cb read_cb_;
  async_write_cb write_cb_;

  // Set `handlers_.on_data` to deliver the pending callback once, then
  // disarm. Called on the loop thread from within the `exec_on_loop` lambda
  // in `read()`. After one batch is delivered the handler clears itself so
  // that `EPOLLIN` is disarmed until the next `read()` call.
  bool arm_read_cb() {
    handlers_.on_data = [this](stream_conn&, std::string& d) {
      // Disarm the handler and update EPOLLIN before invoking the callback.
      handlers_.on_data = {};        // one-shot: disarm after delivery
      refresh_read_interest(*conn_); // update EPOLLIN (disarms it)
      if (!read_cb_) return false;
      return std::exchange(read_cb_, nullptr)(d);
    };
    // arm EPOLLIN immediately (loop thread)
    return refresh_read_interest(*conn_);
  }
};

// Facade for coroutine-based asynchronous I/O on a `stream_conn`. Provides
// `read` and `write` await-based data transfer.
//
// Usage (inside any coroutine):
//
//   async_conn_coro coro{conn_ptr.pointer()};
//   std::string data = co_await coro.read();
//   if (data.empty()) return;          // connection closed
//   co_await coro.write(std::move(data));
//
// `EPOLLIN` is armed only while a read waiter is registered:
// `handlers_.on_data` starts null and is set for exactly one read at a time.
// This prevents data from being consumed by `handle_readable` without a
// pending reader.
//
// Coroutine resumption is always via `post()`, never inline, to avoid
// use-after-free when `do_close_now` fires from within `await_suspend`.
//
// At most one `read` and one `write` may be outstanding at a time.
//
// Non-copyable and non-movable.
class async_conn_coro: public async_conn_base {
public:
  explicit async_conn_coro(std::shared_ptr<stream_conn> conn)
      : async_conn_base(std::move(conn)) {
    // `on_data` starts null; `arm_read()` sets it when a read waiter
    // registers.
    handlers_.on_drain = [this](stream_conn&) {
      if (write_coro_)
        return post([h = std::exchange(write_coro_, {})] {
          h.resume();
          return true;
        });
      return true;
    };
    handlers_.on_close = [this](stream_conn& c) {
      if (read_coro_) {
        pending_read_data_.clear();
        post([h = std::exchange(read_coro_, {})] {
          h.resume();
          return true;
        });
      }
      if (write_coro_)
        post([h = std::exchange(write_coro_, {})] {
          h.resume();
          return true;
        });
      // replicate the auto-close that fires when no handler exists
      return c.close();
    };
    install_handlers();
  }

  // Return an awaitable that suspends the calling coroutine until one batch
  // of data arrives or the connection closes. Returns the received bytes as a
  // `std::string`; an empty string signals that the connection closed before
  // data arrived.
  [[nodiscard]] auto read() noexcept { return read_awaitable{this}; }

  // Return an awaitable that takes ownership of `buf`, enqueues it for
  // sending, and suspends the calling coroutine until the send queue fully
  // drains or the connection closes.
  [[nodiscard]] auto write(std::string&& buf) noexcept {
    return write_awaitable{this, std::move(buf)};
  }

private:
  std::coroutine_handle<> read_coro_;
  std::coroutine_handle<> write_coro_;
  std::string pending_read_data_;

  // Set `handlers_.on_data` to receive the next batch of data and arm
  // `EPOLLIN`. Called from `read_awaitable::await_suspend` on the loop
  // thread. After one batch is delivered the handler clears itself so that
  // `EPOLLIN` is disarmed until the next `read()` call.
  bool arm_read() {
    handlers_.on_data = [this](stream_conn&, std::string& d) {
      handlers_.on_data = {};        // disarm until next read()
      refresh_read_interest(*conn_); // update EPOLLIN (disarms it)
      if (!read_coro_) return false;
      pending_read_data_ = std::move(d);
      return post([h = std::exchange(read_coro_, {})] {
        h.resume();
        return true;
      });
    };
    // arm EPOLLIN immediately (loop thread)
    return refresh_read_interest(*conn_);
  }

  // NOLINTBEGIN(readability-convert-member-functions-to-static)
  struct read_awaitable {
    async_conn_coro* c_;

    // Skip suspension if the connection is already closed or read side is
    // gone.
    [[nodiscard]] bool await_ready() const noexcept {
      return !c_->conn_->is_open() || !c_->conn_->can_read();
    }

    void await_suspend(std::coroutine_handle<> h) {
      assert(!c_->read_coro_);
      c_->read_coro_ = h;
      c_->arm_read();
    }

    [[nodiscard]] std::string await_resume() noexcept {
      return std::move(c_->pending_read_data_);
    }
  };

  // `write_coro_` is set BEFORE `do_enqueue_send()` is called so that if
  // `do_enqueue_send` triggers `do_close_now`, `on_close` fires and resumes
  // the coro before `await_suspend` returns. `await_suspend` always returns
  // `true` (stay suspended); the coro is resumed via a posted task in all
  // cases -- drain, close, or synchronous-write-failure half-close.
  struct write_awaitable {
    async_conn_coro* c_;
    std::string buf_;

    [[nodiscard]] bool await_ready() const noexcept {
      return !c_->conn_->is_open() || !c_->conn_->can_write();
    }

    bool await_suspend(std::coroutine_handle<> h) {
      assert(!c_->write_coro_);
      c_->write_coro_ = h;
      if (!c_->do_enqueue_send(std::move(buf_))) {
        // Synchronous write failure. `on_close` may not fire in a half-close
        // scenario (write fails but read is still open). Resume explicitly if
        // `write_coro_` was not already cleared by `on_close`.
        if (c_->write_coro_)
          return c_->post([hh = std::exchange(c_->write_coro_, {})] {
            hh.resume();
            return true;
          });
      }
      return true;
    }

    void await_resume() noexcept {}
  };
  // NOLINTEND(readability-convert-member-functions-to-static)
};

}} // namespace corvid::proto
