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
#include <functional>
#include <memory>
#include <string>

#include "http_head_codec.h"
#include "recv_buffer.h"
#include "stream_conn.h"

namespace corvid { inline namespace proto {

// Whether a transaction retains its claim on a pipeline stream.
//
// Returned by `handle_data` (input side) and `handle_drain` (output side).
// `release`: transaction relinquishes its claim; the server may advance
//            to the next transaction in the pipeline.
// `claim`:   transaction retains its claim and will be called again until it
//            relinquishes it.
enum class stream_claim : bool { release = false, claim = true };

struct http_transaction; // forward declaration for transaction_queue
using transaction_ptr = std::shared_ptr<http_transaction>;

// HTTP/1.x request-response transaction.
//
// Constructed (always via `std::make_shared`) by a `transaction_factory`
// when a matching route is found in `http_server`. Holds the parsed
// `request_head` and a mutable `response_head` populate, as well as
// `close_after`. Writes are accomplished through the `send_fn` callback passed
// in `handle_drain`.
//
// Pipelining: transactions form an intrusive linked list via `next`, which is
// managed by `transaction_queue`.
//
// Two virtual methods drive the transaction lifecycle:
//
//   `handle_data(recv_buffer_view)` is called by `http_server` to offer data
//       for the transaction to read. The first call happens immediately after
//       the transaction is initalized with `request_headers`. This is the
//       transactiion's chance to examine the headers and decide whether it
//       needs to consume a body from the receive buffer. Returns `claim` to
//       keep receiving data, `release` when done.
//
//   `handle_drain(send)` is called by `http_server` when this transaction is
//       the active writer and the send queue drains. The callback fills
//       `response_headers`, serializes and sends these headers, and sends the
//       body (fully or partially) via `send`. Returns `release` when done,
//       `claim` if more remains.
//
// Both methods default to invoking the corresponding `on_data` / `on_drain`
// callback when set, otherwise returning `release`. Override in a derived
// class for stateful behavior, or install callbacks for simple handlers.
struct http_transaction
    : public std::enable_shared_from_this<http_transaction> {
  // Callback to allow sends, resetting the write timer. Calling it with an
  // empty string signals failure and hangs up the connection.
  using send_fn = std::function<bool(std::string&&)>;

  // Callback types for the optional `on_data` / `on_drain` hooks.
  //
  // `data_fn` receives a reference to the connection's receive buffer view.
  // The callback may call `view.active_view()`, `view.consume()`, and
  // `view.update_active_view()` to consume bytes.
  using data_fn =
      std::function<stream_claim(http_transaction&, recv_buffer_view&)>;
  using drain_fn = std::function<stream_claim(http_transaction&, send_fn&)>;

  explicit http_transaction(request_head&& req)
      : request_headers{std::move(req)} {}

  virtual ~http_transaction() = default;

  http_transaction(const http_transaction&) = delete;
  http_transaction& operator=(const http_transaction&) = delete;
  http_transaction(http_transaction&&) = delete;
  http_transaction& operator=(http_transaction&&) = delete;

  // Headers.
  request_head request_headers;
  response_head response_headers;

  // Intrusive forward link for the pipeline queue. Managed directly by
  // `http_server`; do not modify from transaction code.
  transaction_ptr next;

  // Controls whether the connection closes after this response is sent.
  // Initialized by `http_server` from the `Connection` request header;
  // override before `handle_drain` returns `release` to force a close or
  // keep-open regardless of what the client requested.
  after_response close_after{after_response::keep_alive};

  // Optional callbacks. Set by route factories or direct users.
  data_fn on_data;
  drain_fn on_drain;

  // Called by `http_server` once the transaction is created, and repeatedly
  // until this function returns `release`. On the initial call, the
  // transaction sees the fully parsed `request_head` in `request_headers` and
  // the post-header receive buffer via `view`. The transaction may consume
  // body bytes from the view, returning `claim` to keep receiving data until
  // it's done, at which point it returns `release`. If no body is needed, or
  // if it consumes the entire body in the first call, it returns `release`
  // immediately.
  //
  // Default: invoke `on_data` if set, else return `release`.
  [[nodiscard]] virtual stream_claim handle_data(recv_buffer_view& view) {
    if (on_data) return on_data(*this, view);
    return stream_claim::release;
  }

  // Called by `http_server` when this becomes the active write transaction,
  // and then again after the send queue drains, until this function returns
  // `release`. On the initial call, the transaction should populate
  // `response_headers` with the response head, serialize and send the
  // headers, and optionally
  // send some body data. If that's all it needs to do, it returns `release`.
  // On subsequent calls, if any, the transaction should send more body data
  // until the entire response is sent, at which point it returns `release`.
  //
  // Default: invoke `on_drain` if set, else return `release`.
  [[nodiscard]] virtual stream_claim handle_drain(send_fn& send) {
    if (on_drain) return on_drain(*this, send);
    close_after = after_response::close;
    return stream_claim::release;
  }
};

// Factory function type for constructing transactions. Called by
// `http_server::create_transaction` when a matching route is found.
using transaction_factory =
    std::function<std::shared_ptr<http_transaction>(request_head&&)>;

// Pipeline queue of `http_transaction` objects for a single HTTP connection.
// All mutations occur on the epoll loop thread; no locking is needed.
//
// `head_`   = front of queue = active output writer.
// `reader_` = active input reader; may lead `head_` during pipelining
//             (never behind it).
// `tail_`   = back of queue, for O(1) append.
//
// Invariant: all three are null iff the queue is empty.
class transaction_queue {
public:
  // Push `tx` onto the tail of the queue. Sets `reader_` to `tx` if
  // it was null (the previous reader, if any, finished reading its body before
  // `tx` arrived).
  void add_transaction(transaction_ptr tx) {
    if (tail_) {
      tail_->next = tx;
      tail_ = tx;
      if (!reader_) reader_ = std::move(tx);
    } else
      head_ = reader_ = tail_ = std::move(tx);
  }

  // Pop the head transaction once it has finished writing its response.
  [[nodiscard]] bool remove_transaction() {
    if (!head_) return false;
    auto nxt = head_->next;
    head_->next = nullptr;
    head_ = std::move(nxt);
    if (!head_) tail_ = nullptr;
    return true;
  }

  // Return the active input reader, or null if none is pending.
  [[nodiscard]] const transaction_ptr& get_reader() const noexcept {
    return reader_;
  }

  // Return the active output writer (head of queue), or null if empty.
  [[nodiscard]] const transaction_ptr& get_writer() const noexcept {
    return head_;
  }

  // Advance the reader to the next transaction in the queue.
  void next_reader() {
    if (reader_) reader_ = reader_->next;
  }

private:
  transaction_ptr head_;
  transaction_ptr reader_;
  transaction_ptr tail_;
};

}} // namespace corvid::proto
