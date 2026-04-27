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
#include "../corvid/proto/io_uring/iou_stream_conn.h"

#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>
#include <sys/socket.h>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;
using namespace corvid::iouring;
using namespace std::chrono_literals;

namespace {

bool WaitFor(const auto& pred, std::chrono::milliseconds timeout = 500ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return pred();
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region SendRecvString
void IouStreamConn_SendRecvString() {
  // String send -> on_data fires, payload correct.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    constexpr std::string_view msg{"hello-stream"};

    // Adopt sock1 as receiver.
    auto recv_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock1), net_endpoint::invalid,
        iou_stream_conn_handlers{
            .on_data = [&](iou_stream_conn&, iou_recv_view view) {
              payload = view.active_view();
              view.consume(view.active_view().size());
              received.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(recv_conn);

    // Adopt sock0 as sender.
    auto send_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock0), net_endpoint::invalid);
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
  }
}
#pragma endregion

#pragma region MultipleStrings
void IouStreamConn_MultipleStrings() {
  // Multiple string sends -> all received and concatenated correctly.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic<int> recv_bytes{0};
    std::string payload;

    constexpr int N = 4;
    constexpr std::string_view msg{"abc"};

    auto recv_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock1), net_endpoint::invalid,
        iou_stream_conn_handlers{
            .on_data = [&](iou_stream_conn&, iou_recv_view view) {
              auto sv = view.active_view();
              payload += sv;
              recv_bytes.fetch_add(static_cast<int>(sv.size()),
                  std::memory_order::relaxed);
              view.consume(sv.size());
              return true;
            }});
    EXPECT_TRUE(recv_conn);

    auto send_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock0), net_endpoint::invalid);
    EXPECT_TRUE(send_conn);

    for (int i = 0; i < N; ++i) EXPECT_TRUE(send_conn->send(std::string{msg}));

    const int expected = N * static_cast<int>(msg.size());
    EXPECT_TRUE(WaitFor([&] {
      return recv_bytes.load(std::memory_order::relaxed) >= expected;
    }));
    EXPECT_EQ(recv_bytes.load(), expected);
  }
}
#pragma endregion

#pragma region SendRecvBuffer
void IouStreamConn_SendRecvBuffer() {
  // Direct buffer send -> on_data fires, payload correct.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    constexpr std::string_view msg{"hello-buffer"};

    auto recv_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock1), net_endpoint::invalid,
        iou_stream_conn_handlers{
            .on_data = [&](iou_stream_conn&, iou_recv_view view) {
              payload = view.active_view();
              view.consume(view.active_view().size());
              received.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(recv_conn);

    auto send_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock0), net_endpoint::invalid);
    EXPECT_TRUE(send_conn);

    // Borrow a write buffer and fill it manually, then send.
    auto tok = runner->borrow_write_buffer();
    EXPECT_TRUE(tok);
    if (!tok) return;
    EXPECT_TRUE(tok.append(msg));
    EXPECT_TRUE(send_conn->send(std::move(tok)));

    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
  }
}
#pragma endregion

#pragma region BufferMoveOut
void IouStreamConn_BufferMoveOut() {
  // `take()` in on_data -> fresh recv submitted, caller owns buffer.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    constexpr std::string_view msg{"take-me"};

    auto recv_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock1), net_endpoint::invalid,
        iou_stream_conn_handlers{
            .on_data = [&](iou_stream_conn&, iou_recv_view view) {
              auto buf = view.take(); // move buffer out
              payload.assign(buf.payload_view());
              received.store(true, std::memory_order::release);
              return true;
              // buf returns to pool on scope exit
            }});
    EXPECT_TRUE(recv_conn);

    auto send_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock0), net_endpoint::invalid);
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
  }
}
#pragma endregion

#pragma region GracefulClose
void IouStreamConn_GracefulClose() {
  // `close()` -> on_close fires on both sides.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool closed0{false};
    std::atomic_bool closed1{false};

    auto make_handlers = [](std::atomic_bool& flag) {
      return iou_stream_conn_handlers{
          .on_data =
              [](iou_stream_conn&, iou_recv_view view) {
                view.consume(view.active_view().size());
                return true;
              },
          .on_close =
              [&flag](iou_stream_conn&) {
                flag.store(true, std::memory_order::release);
                return true;
              }};
    };

    auto conn0 = iou_stream_conn_ptr::adopt(runner.loop(), std::move(sock0),
        net_endpoint::invalid, make_handlers(closed0));
    auto conn1 = iou_stream_conn_ptr::adopt(runner.loop(), std::move(sock1),
        net_endpoint::invalid, make_handlers(closed1));
    EXPECT_TRUE(conn0);
    EXPECT_TRUE(conn1);

    // Give both connections time to arm their recv SQEs.
    std::this_thread::sleep_for(20ms);

    // TODO: This doesn't do the graceful close that's promised.

    EXPECT_TRUE(conn0.close());
    EXPECT_TRUE(
        WaitFor([&] { return closed0.load(std::memory_order::acquire); }));
    EXPECT_TRUE(closed0.load());
  }
}
#pragma endregion

#pragma region HangupClose
void IouStreamConn_HangupClose() {
  // `hangup()` -> socket closed immediately.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool closed{false};

    auto conn0 = iou_stream_conn_ptr::adopt(runner.loop(), std::move(sock0),
        net_endpoint::invalid,
        iou_stream_conn_handlers{.on_close = [&](iou_stream_conn&) {
          closed.store(true, std::memory_order::release);
          return true;
        }});
    // conn1 just absorbs the other end
    auto conn1 = iou_stream_conn_ptr::adopt(runner.loop(), std::move(sock1),
        net_endpoint::invalid);
    EXPECT_TRUE(conn0);
    EXPECT_TRUE(conn1);

    EXPECT_TRUE(conn0->hangup());
    EXPECT_TRUE(
        WaitFor([&] { return closed.load(std::memory_order::acquire); }));
  }
}
#pragma endregion

#pragma region OnDrain
void IouStreamConn_OnDrain() {
  // `on_drain` fires after send completes and queue is empty.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool drained{false};

    auto recv_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock1), net_endpoint::invalid,
        iou_stream_conn_handlers{
            .on_data = [](iou_stream_conn&, iou_recv_view view) {
              view.consume(view.active_view().size());
              return true;
            }});
    EXPECT_TRUE(recv_conn);

    auto send_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock0), net_endpoint::invalid,
        iou_stream_conn_handlers{.on_drain = [&](iou_stream_conn&) {
          drained.store(true, std::memory_order::release);
          return true;
        }});
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{"ping"}));
    EXPECT_TRUE(
        WaitFor([&] { return drained.load(std::memory_order::acquire); }));
  }
}
#pragma endregion

#pragma region WithState
void IouStreamConn_WithState() {
  // `iou_stream_conn_with_state` - state accessible in callback.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool received{false};

    struct MyState {
      int recv_count{};
    };

    using my_conn = iou_stream_conn_with_state<MyState>;
    using my_ptr = iou_stream_conn_ptr_with<my_conn>;

    auto recv_conn = my_ptr::adopt(runner.loop(), std::move(sock1),
        net_endpoint::invalid,
        iou_stream_conn_handlers{
            .on_data = [&](iou_stream_conn& c, iou_recv_view view) {
              auto& s = my_conn::from(c);
              s.state().recv_count++;
              view.consume(view.active_view().size());
              received.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(recv_conn);

    auto send_conn = iou_stream_conn_ptr::adopt(runner.loop(),
        std::move(sock0), net_endpoint::invalid);
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{"state-test"}));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(recv_conn->state().recv_count, 1);
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
MAKE_TEST_LIST(IouStreamConn_SendRecvString, IouStreamConn_MultipleStrings,
    IouStreamConn_SendRecvBuffer, IouStreamConn_BufferMoveOut,
    IouStreamConn_GracefulClose, IouStreamConn_HangupClose,
    IouStreamConn_OnDrain, IouStreamConn_WithState)
