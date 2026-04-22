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
#include "../corvid/proto/io_uring/iou_loop.h"

#include <atomic>
#include <chrono>
#include <string_view>
#include <thread>
#include <sys/socket.h>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;
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
void IouLoop_NopCompletion() {
  // Verify that a submitted NOP fires its completion callback on the runner.
  if (true) {
    constexpr uint32_t max_32 = std::numeric_limits<uint32_t>::max();
    iou_loop_runner runner;
    std::atomic_bool fired{false};
    std::atomic<int32_t> result{-999};
    std::atomic<uint32_t> flags_int{max_32};

    const auto token =
        runner->submit_nop([&](iou_res res, iou_cqe_flags flags) {
          result.store(res.value(), std::memory_order::relaxed);
          flags_int.store(*flags, std::memory_order::relaxed);
          fired.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(token.valid());
    EXPECT_TRUE(
        WaitFor([&] { return fired.load(std::memory_order::acquire); }));
    EXPECT_EQ(result.load(), 0);
  }
}

void IouLoop_MultipleNops() {
  // Submit several NOPs and confirm all complete on the runner thread.
  if (true) {
    iou_loop_runner runner;
    std::atomic<int> count{0};

    bool submitted = true;
    for (int i = 0; i < 4; ++i) {
      submitted = submitted && runner->submit_nop([&](iou_res, iou_cqe_flags) {
        count.fetch_add(1, std::memory_order::relaxed);
        return slot_retention{};
      });
    }
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(
        WaitFor([&] { return count.load(std::memory_order::acquire) == 4; }));
    EXPECT_EQ(count.load(), 4);
  }
}

void IouLoop_StopFromThread() {
  // Stop the runner from another thread and let destruction join cleanly.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool stopped{false};

    std::thread t{[&] {
      runner.stop();
      stopped.store(true, std::memory_order::release);
    }};
    std::this_thread::sleep_for(20ms);
    t.join();
    EXPECT_TRUE(stopped.load(std::memory_order::acquire));
  }
}

void IouLoop_PostFromThread() {
  // Post a callback from an external thread; verify the loop executes it.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool fired{false};

    const bool ok = runner->post([&] {
      fired.store(true, std::memory_order::release);
      return true;
    });
    EXPECT_TRUE(ok);

    EXPECT_TRUE(
        WaitFor([&] { return fired.load(std::memory_order::acquire); }));
  }
}

void IouLoop_PostAndWait() {
  // post_and_wait blocks until the callback runs, then returns.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool ran{false};

    const bool ok = runner->post_and_wait([&] {
      ran.store(true, std::memory_order::relaxed);
      return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(ran.load());
  }
}

void IouLoop_RecvSend() {
  // Submit a recv and a send over a Unix socket pair; confirm the payload
  // arrives and the byte counts are correct.
  if (true) {
    int raw_sv[2];
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, raw_sv), 0);
    os_file send_sock{raw_sv[0]};
    os_file recv_sock{raw_sv[1]};

    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::atomic<int32_t> recv_result{-1};
    std::atomic<int32_t> send_result{-1};

    constexpr std::string_view msg{"hello"};
    std::array<std::byte, 16> buf{};

    const bool ok = runner->post_and_wait([&] {
      const auto token = runner->submit_recv_bytes(recv_sock, buf,
          [&](iou_res res, iou_cqe_flags) {
            recv_result.store(res.value(), std::memory_order::relaxed);
            received.store(true, std::memory_order::release);
            return slot_retention{};
          });
      if (!token.valid()) return false;
      const auto send_token = runner->submit_send_bytes(send_sock,
          std::as_bytes(std::span{msg}), [&](iou_res res, iou_cqe_flags) {
            send_result.store(res.value(), std::memory_order::relaxed);
            return slot_retention{};
          });
      if (!send_token.valid()) return false;
      return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));

    EXPECT_EQ(recv_result.load(), static_cast<int32_t>(msg.size()));
    EXPECT_EQ(send_result.load(), static_cast<int32_t>(msg.size()));
    EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(buf.data()),
                  msg.size()),
        msg);
  }
}

void IouLoop_RecvSendFixed() {
  // Submit a recv_fixed and a send_fixed over a Unix socket pair using
  // registered buffers; confirm the payload arrives and byte counts match.
  if (true) {
    int raw_sv[2];
    EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, raw_sv), 0);
    os_file send_sock{raw_sv[0]};
    os_file recv_sock{raw_sv[1]};

    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::atomic<int32_t> recv_n{-1};
    std::atomic<int32_t> send_n{-1};
    std::string payload;

    constexpr std::string_view msg{"hello-fixed"};

    const auto recv_token =
        runner->submit_recv_buffer(recv_sock, [&](iou_loop::buffer& buf) {
          recv_n.store(buf.result().value(), std::memory_order::relaxed);
          auto data = buf.payload_view();
          payload.assign(data);
          received.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(recv_token.valid());

    auto tok = runner->borrow_write_buffer();
    EXPECT_TRUE(tok);
    if (!tok) return;
    auto span = tok.tail_span();
    std::memcpy(span.data(), msg.data(), msg.size());
    span = span.first(msg.size());
    (void)tok.update_payload(span);

    const auto send_token = runner->submit_send_buffer(send_sock,
        std::move(tok), [&](iou_loop::buffer& buf) {
          send_n.store(buf.result().value(), std::memory_order::relaxed);
          return slot_retention{};
        });
    EXPECT_TRUE(send_token.valid());
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));

    EXPECT_EQ(recv_n.load(), static_cast<int32_t>(msg.size()));
    EXPECT_EQ(send_n.load(), static_cast<int32_t>(msg.size()));
    EXPECT_EQ(payload, msg);

    close(send_sock.handle());
    close(recv_sock.handle());
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
MAKE_TEST_LIST(IouLoop_NopCompletion, IouLoop_MultipleNops,
    IouLoop_StopFromThread, IouLoop_PostFromThread, IouLoop_PostAndWait,
    IouLoop_RecvSend, IouLoop_RecvSendFixed)
