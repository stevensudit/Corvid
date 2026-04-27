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
#if 0
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

void IouDgramConn_SendRecv() {
  // Single datagram: `from` address correct, payload matches.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;
    net_endpoint from_addr;

    constexpr std::string_view msg{"hello-udp"};

    auto ep1 = net_endpoint{net_endpoint::any_v4(0)};
    auto ep2 = net_endpoint{net_endpoint::any_v4(0)};

    auto conn1 = iou_dgram_conn_ptr::bind(runner.loop(), ep1,
        iou_dgram_conn_handlers{
            .on_data = [&](iou_dgram_conn&, std::string_view data,
                           const net_endpoint& from) {
              payload = data;
              from_addr = from;
              received.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(conn1);

    auto conn2 = iou_dgram_conn_ptr::bind(runner.loop(), ep2);
    EXPECT_TRUE(conn2);

    // Query the OS-assigned port for conn1.
    net_endpoint dest = conn1->local_endpoint();
    EXPECT_TRUE(!dest.empty());

    EXPECT_TRUE(conn2->send_to(dest, std::string{msg}));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
    EXPECT_TRUE(!from_addr.empty());
  }
}

void IouDgramConn_MultipleDatagrams() {
  // Several datagrams queued; all received.
  if (true) {
    iou_loop_runner runner;
    std::atomic<int> count{0};
    constexpr int N = 4;

    auto ep1 = net_endpoint::any_v4(0);
    auto ep2 = net_endpoint::any_v4(0);

    auto conn1 = iou_dgram_conn_ptr::bind(runner.loop(), ep1,
        iou_dgram_conn_handlers{
            .on_data =
                [&](iou_dgram_conn&, std::string_view, const net_endpoint&) {
                  count.fetch_add(1, std::memory_order::relaxed);
                  return true;
                }});
    EXPECT_TRUE(conn1);

    auto conn2 = iou_dgram_conn_ptr::bind(runner.loop(), ep2);
    EXPECT_TRUE(conn2);

    net_endpoint dest = conn1->local_endpoint();
    EXPECT_TRUE(!dest.empty());

    for (int i = 0; i < N; ++i)
      EXPECT_TRUE(conn2->send_to(dest, std::string{"pkt"}));

    EXPECT_TRUE(
        WaitFor([&] { return count.load(std::memory_order::relaxed) >= N; }));
    EXPECT_EQ(count.load(), N);
  }
}

void IouDgramConn_SendRecvBuffer() {
  // Direct buffer send, received correctly.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    constexpr std::string_view msg{"buf-udp"};

    auto ep1 = net_endpoint::any_v4(0);
    auto ep2 = net_endpoint::any_v4(0);

    auto conn1 = iou_dgram_conn_ptr::bind(runner.loop(), ep1,
        iou_dgram_conn_handlers{
            .on_data = [&](iou_dgram_conn&, std::string_view data,
                           const net_endpoint&) {
              payload = data;
              received.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(conn1);

    auto conn2 = iou_dgram_conn_ptr::bind(runner.loop(), ep2);
    EXPECT_TRUE(conn2);

    net_endpoint dest = conn1->local_endpoint();
    EXPECT_TRUE(!dest.empty());

    auto tok = runner->borrow_write_buffer();
    EXPECT_TRUE(tok);
    if (!tok) return;
    EXPECT_TRUE(tok.append(msg));
    EXPECT_TRUE(conn2->send_to(dest, std::move(tok)));

    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
  }
}

void IouDgramConn_OnDrain() {
  // `on_drain` fires after all sends complete.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool drained{false};

    auto ep1 = net_endpoint::any_v4(0);
    auto ep2 = net_endpoint::any_v4(0);

    auto conn1 = iou_dgram_conn_ptr::bind(runner.loop(), ep1);
    EXPECT_TRUE(conn1);

    auto conn2 = iou_dgram_conn_ptr::bind(runner.loop(), ep2,
        iou_dgram_conn_handlers{.on_drain = [&](iou_dgram_conn&) {
          drained.store(true, std::memory_order::release);
          return true;
        }});
    EXPECT_TRUE(conn2);

    net_endpoint dest = conn1->local_endpoint();
    EXPECT_TRUE(!dest.empty());

    EXPECT_TRUE(conn2->send_to(dest, std::string{"drain-test"}));
    EXPECT_TRUE(
        WaitFor([&] { return drained.load(std::memory_order::acquire); }));
  }
}

void IouDgramConn_WithState() {
  // `iou_dgram_conn_with_state` - state accessible in callback.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool received{false};

    struct MyState {
      int recv_count{};
    };

    using my_conn = iou_dgram_conn_with_state<MyState>;
    using my_ptr = iou_dgram_conn_ptr_with<my_conn>;

    auto ep1 = net_endpoint::any_v4(0);
    auto ep2 = net_endpoint::any_v4(0);

    auto conn1 = my_ptr::bind(runner.loop(), ep1,
        iou_dgram_conn_handlers{
            .on_data =
                [&](iou_dgram_conn& c, std::string_view, const net_endpoint&) {
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

    EXPECT_TRUE(conn2->send_to(dest, std::string{"state-dgram"}));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(conn1->state().recv_count, 1);
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
MAKE_TEST_LIST(IouDgramConn_SendRecv, IouDgramConn_MultipleDatagrams,
    IouDgramConn_SendRecvBuffer, IouDgramConn_OnDrain, IouDgramConn_WithState)
#else
MAKE_TEST_LIST();
#endif
