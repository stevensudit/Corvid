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
#include "../corvid/proto/io_uring/iou_dgram_conn.h"

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;
using namespace corvid::iouring;
using namespace std::chrono_literals;

namespace {

bool WaitFor(const auto& pred, std::chrono::milliseconds timeout = 500ms) {
#if DEBUG
  timeout = 1h;
#endif
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return pred();
}

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

void IouDgramConn_SendRecvBuffer() {
#if 0
  // Direct buffer send, received correctly.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    constexpr std::string_view msg{"buf-udp"};

    auto conn1 = iou_dgram_conn_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        iou_dgram_conn_handlers{
            .on_data = [&](iou_dgram_conn&, iou_loop::buffer& buf) {
              payload = buf.payload_view();
              received.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(conn1);

    auto conn2 =
        iou_dgram_conn_ptr::bind(runner.loop(), net_endpoint::loopback_v4());
    EXPECT_TRUE(conn2);

    net_endpoint dest = conn1->local_endpoint();
    EXPECT_TRUE(!dest.empty());
    std::string dest_str = dest.to_string();
    EXPECT_NE(dest_str, "");

    auto buf = runner->borrow_write_buffer();
    buf.peer_addr() = dest;
    EXPECT_TRUE(buf);
    if (!buf) return;
    EXPECT_TRUE(buf.append(msg));
    EXPECT_TRUE(conn2->send_to(std::move(buf)));

    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
  }
#endif
}

void IouDgramConn_OnDrain() {
#if 0
  // `on_drain` fires after all sends complete.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool drained{false};

    auto ep1 = net_endpoint::loopback_v4(0);
    auto ep2 = net_endpoint::loopback_v4(0);

    auto conn1 = iou_dgram_conn_ptr::bind(runner.loop(), ep1);
    EXPECT_TRUE(conn1);

    auto conn2 = iou_dgram_conn_ptr::bind(runner.loop(), ep2,
        iou_dgram_conn_handlers{
            .on_drain = [&](iou_dgram_conn&, iou_loop::buffer&) {
              drained.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(conn2);

    net_endpoint dest = conn1->local_endpoint();
    EXPECT_TRUE(!dest.empty());

    auto buf = runner->borrow_write_buffer();
    ASSERT_TRUE(buf);
    buf.peer_addr() = dest;
    EXPECT_TRUE(buf.append("drain-test"));
    EXPECT_TRUE(conn2->send_to(std::move(buf)));
    EXPECT_TRUE(
        WaitFor([&] { return drained.load(std::memory_order::acquire); }));
  }
#endif
}

void IouDgramConn_WithState() {
#if 0
  // `iou_dgram_conn_with_state` - state accessible in callback.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool received{false};

    struct MyState {
      int recv_count{};
    };

    using my_conn = iou_dgram_conn_with_state<MyState>;
    using my_ptr = iou_dgram_conn_ptr_with<my_conn>;

    auto ep1 = net_endpoint::loopback_v4(0);
    auto ep2 = net_endpoint::loopback_v4(0);

    auto conn1 = my_ptr::bind(runner.loop(), ep1,
        iou_dgram_conn_handlers{
            .on_data = [&](iou_dgram_conn& c, iou_loop::buffer&) {
              auto& s = my_conn::from(c);
              s.state().recv_count++;
              received.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(conn1);

    auto conn2 = iou_dgram_conn_ptr::bind(runner.loop(), ep2);
    EXPECT_TRUE(conn2);

    net_endpoint dest = conn1->local_endpoint();
    EXPECT_TRUE(!dest.empty());

    auto buf = runner->borrow_write_buffer();
    ASSERT_TRUE(buf);
    buf.peer_addr() = dest;
    EXPECT_TRUE(buf.append("state-dgram"));
    EXPECT_TRUE(conn2->send_to(std::move(buf)));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(conn1->state().recv_count, 1);
  }
#endif
}

// NOLINTEND(readability-function-cognitive-complexity)
MAKE_TEST_LIST(IouDgramConn_SendRecvBuffer, IouDgramConn_OnDrain,
    IouDgramConn_WithState)
