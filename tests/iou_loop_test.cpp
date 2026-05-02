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
#include "../corvid/filesys/net_socket.h"
#include "../corvid/strings/enum_conversion.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>
#include <sys/socket.h>
#include <sys/uio.h>

#define MINITEST_SHOW_TIMERS 0
#include "minitest.h"

using namespace corvid;
using namespace corvid::filesys;
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
#pragma region NopCompletion
void IouLoop_NopCompletion() {
  // Verify that a submitted NOP fires its completion callback on the runner.
  if (true) {
    constexpr uint32_t max_32 = std::numeric_limits<uint32_t>::max();
    iou_loop_runner loop;
    std::atomic_bool fired{false};
    std::atomic_int32_t result{-999};
    std::atomic<uint32_t> flags_int{max_32};

    const auto token =
        loop->submit_nop([&](completion_id, iou_res res, iou_cqe_flags flags) {
          result.store(res.value(), std::memory_order::relaxed);
          flags_int.store(*flags, std::memory_order::relaxed);
          fired.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(token.is_valid());
    EXPECT_TRUE(
        WaitFor([&] { return fired.load(std::memory_order::acquire); }));
    EXPECT_EQ(result.load(), 0);
  }
}
#pragma endregion

#pragma region MultipleNops
void IouLoop_MultipleNops() {
  // Submit several NOPs and confirm all complete on the runner thread.
  if (true) {
    iou_loop_runner loop;
    std::atomic<int> count{0};

    bool submitted = true;
    for (int i = 0; i < 4; ++i) {
      submitted =
          submitted &&
          loop->submit_nop([&](completion_id, iou_res, iou_cqe_flags) {
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
#pragma endregion

#pragma region StopFromThread
void IouLoop_StopFromThread() {
  // Stop the runner from another thread and let destruction join cleanly.
  if (true) {
    iou_loop_runner loop;
    std::atomic_bool stopped{false};

    std::thread t{[&] {
      loop.stop();
      stopped.store(true, std::memory_order::release);
    }};
    std::this_thread::sleep_for(20ms);
    t.join();
    EXPECT_TRUE(stopped.load(std::memory_order::acquire));
  }
}
#pragma endregion

#pragma region PostFromThread
void IouLoop_PostFromThread() {
  // Post a callback from an external thread; verify the loop executes it.
  if (true) {
    iou_loop_runner loop;
    std::atomic_bool fired{false};

    const bool ok = loop->post([&] {
      fired.store(true, std::memory_order::release);
      return true;
    });
    EXPECT_TRUE(ok);

    EXPECT_TRUE(
        WaitFor([&] { return fired.load(std::memory_order::acquire); }));
  }
}
#pragma endregion

#pragma region PostAndWait
void IouLoop_PostAndWait() {
  // `post_and_wait` blocks until the callback runs, then returns.
  if (true) {
    iou_loop_runner loop;
    std::atomic_bool ran{false};

    const bool ok = loop->post_and_wait([&] {
      ran.store(true, std::memory_order::relaxed);
      return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(ran.load());
  }
}
#pragma endregion

#pragma region RecvSend
void IouLoop_RecvSend() {
  // Submit a recv and a send over a Unix socket pair; confirm the payload
  // arrives and the byte counts are correct.
  if (true) {
    auto [send_sock, recv_sock] = net_socket::create_pair();

    iou_loop_runner loop;
    std::atomic_bool received{false};
    std::atomic_int32_t recv_result{-1};
    std::atomic_int32_t send_result{-1};

    constexpr std::string_view msg{"hello"};
    std::array<std::byte, 16> buf{};

    const bool ok = loop->post_and_wait([&] {
      const auto token = loop->submit_recv_bytes(recv_sock, buf,
          [&](completion_id, iou_res res, iou_cqe_flags) {
            recv_result.store(res.value(), std::memory_order::relaxed);
            received.store(true, std::memory_order::release);
            return slot_retention{};
          });
      if (!token.is_valid()) return false;
      const auto send_token = loop->submit_send_bytes(send_sock,
          std::as_bytes(std::span{msg}),
          [&](completion_id, iou_res res, iou_cqe_flags) {
            send_result.store(res.value(), std::memory_order::relaxed);
            return slot_retention{};
          });
      if (!send_token.is_valid()) return false;
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
#pragma endregion

#pragma region RecvWriteFixed
void IouLoop_RecvWriteFixed() {
  // Submit a recv_fixed and a send_fixed over a Unix socket pair using
  // registered buffers; confirm the payload arrives and byte counts match.
  if (true) {
    auto [send_sock, recv_sock] = net_socket::create_pair();

    iou_loop_runner loop;
    std::atomic_bool received{false};
    std::atomic_int32_t recv_n{-1};
    std::atomic_int32_t send_n{-1};
    std::string payload;

    constexpr std::string_view msg{"hello-fixed"};

    const auto recv_token = loop->submit_read_buffer(recv_sock,
        [&](completion_id, iou_loop::buffer& buf) {
          recv_n.store(buf.result().value(), std::memory_order::relaxed);
          auto data = buf.payload_view();
          payload.assign(data);
          received.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(recv_token.is_valid());

    auto tok = loop->borrow_write_buffer();
    EXPECT_TRUE(tok);
    if (!tok) return;
    auto span = tok.tail_span();
    std::memcpy(span.data(), msg.data(), msg.size());
    span = span.first(msg.size());
    (void)tok.update_payload(span);

    const auto send_token = loop->submit_write_buffer(send_sock,
        std::move(tok), [&](completion_id, iou_loop::buffer& buf) {
          send_n.store(buf.result().value(), std::memory_order::relaxed);
          return slot_retention{};
        });
    EXPECT_TRUE(send_token.is_valid());
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));

    EXPECT_EQ(recv_n.load(), static_cast<int32_t>(msg.size()));
    EXPECT_EQ(send_n.load(), static_cast<int32_t>(msg.size()));
    EXPECT_EQ(payload, msg);
  }
}
#pragma endregion

#pragma region IsLoopThread
void IouLoop_IsLoopThread() {
  // `is_loop_thread()` returns false on the test thread and true inside a
  // callback executing on the loop thread.
  if (true) {
    iou_loop_runner loop;
    EXPECT_FALSE(loop->is_loop_thread());

    std::atomic_bool confirmed{false};
    const bool ok = loop->post_and_wait([&] {
      confirmed.store(loop->is_loop_thread(), std::memory_order::release);
      return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(confirmed.load(std::memory_order::acquire));
  }
}
#pragma endregion

#pragma region ExecuteOrPost
void IouLoop_ExecuteOrPost() {
  // `execute_or_post` from an off-thread posts; the callback still runs.
  if (true) {
    iou_loop_runner loop;
    std::atomic_bool executed{false};

    const bool ok = loop->execute_or_post([&] {
      executed.store(true, std::memory_order::release);
      return true;
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(
        WaitFor([&] { return executed.load(std::memory_order::acquire); }));
  }
}
#pragma endregion

#pragma region NopTokenVariant
void IouLoop_NopTokenVariant() {
  // `tokenize` + `submit_nop(token)` exercises the token-based submission
  // path.
  if (true) {
    iou_loop_runner loop;
    std::atomic_bool fired{false};
    std::atomic_int32_t result{-999};

    const auto token = loop->tokenize(
        [&](completion_id, iou_res res, iou_cqe_flags) -> slot_retention {
          result.store(res.value(), std::memory_order::relaxed);
          fired.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(token.is_valid());

    const bool submitted = loop->submit_nop(token, slot_retention::automatic);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(
        WaitFor([&] { return fired.load(std::memory_order::acquire); }));
    EXPECT_EQ(result.load(), 0);
  }
}
#pragma endregion

#pragma region TokenIsReleased
void IouLoop_TokenIsReleased() {
  // `tokenize` produces a valid token; `is_released` returns false while the
  // slot is live, true after explicit release.
  if (true) {
    iou_loop_runner loop;
    auto token = loop->tokenize(
        [](completion_id, iou_res, iou_cqe_flags) -> slot_retention {
          return slot_retention{};
        });
    EXPECT_TRUE(token.is_valid());
    EXPECT_FALSE(loop->is_released(token));

    const bool released = loop->release(std::move(token));
    EXPECT_TRUE(released);
    // The slot's generation was bumped; `is_released` detects the mismatch and
    // nullifies the token.
    EXPECT_TRUE(loop->is_released(token));
    EXPECT_FALSE(token.is_valid());
  }
}
#pragma endregion

#pragma region SubmitClose
void IouLoop_SubmitClose() {
  // `submit_close` fires its callback with `res == 0` after the fd is closed.
  if (true) {
    auto [keep, to_close] = net_socket::create_pair();

    iou_loop_runner loop;
    std::atomic_bool fired{false};
    std::atomic_int32_t result{-1};

    const auto token = loop->submit_close(std::move(to_close),
        [&](completion_id, iou_res res, iou_cqe_flags) -> slot_retention {
          result.store(res.value(), std::memory_order::relaxed);
          fired.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(token.is_valid());
    EXPECT_TRUE(
        WaitFor([&] { return fired.load(std::memory_order::acquire); }));
    EXPECT_EQ(result.load(), 0);
  }
}
#pragma endregion

#pragma region SubmitTimeout
void IouLoop_SubmitTimeout() {
  // A single-shot timeout fires with `-ETIME` after the specified duration.
  if (true) {
    iou_loop_runner loop;
    bound_timeout timeout{
        .when = {.ts = iou_timespec{50ms}, .flags = iou_timeout_flags::rel}};
    std::atomic_bool fired{false};
    std::atomic_int32_t result{0};

    const auto token = loop->submit_timeout(std::move(timeout),
        [&](completion_id, iou_res res, iou_cqe_flags) -> slot_retention {
          result.store(res.value(), std::memory_order::relaxed);
          fired.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(token.is_valid());
    EXPECT_TRUE(WaitFor([&] { return fired.load(std::memory_order::acquire); },
        500ms));
    EXPECT_EQ(result.load(), -ETIME);
  }
}
#pragma endregion

#pragma region SubmitTimeoutMultishot
void IouLoop_SubmitTimeoutMultishot() {
  // A multishot timeout with `cqe_count`=3 fires exactly 3 times then stops.
  if (true) {
    iou_loop_runner loop;
    bound_timeout timeout{
        .when = {.ts = iou_timespec{20ms},
            .flags = iou_timeout_flags::rel | iou_timeout_flags::multishot}};
    std::atomic<int> count{0};

    const auto token = loop->submit_timeout(
        std::move(timeout),
        [&](completion_id, iou_res, iou_cqe_flags) -> slot_retention {
          count.fetch_add(1, std::memory_order::relaxed);
          return slot_retention::automatic;
        },
        3);
    EXPECT_TRUE(token.is_valid());
    EXPECT_TRUE(WaitFor(
        [&] { return count.load(std::memory_order::acquire) == 3; }, 500ms));
    EXPECT_EQ(count.load(), 3);
  }
}
#pragma endregion

#pragma region SubmitCancelFile
void IouLoop_SubmitCancelFile() {
  // Canceling a file's pending ops delivers a negative result to the recv
  // callback (typically `ECANCELED`).
  if (true) {
    auto [send_sock, recv_sock] = net_socket::create_pair();

    iou_loop_runner loop;
    std::atomic_bool recv_done{false};
    std::atomic_int32_t recv_res{0};
    std::array<std::byte, 16> buf{};

    EXPECT_TRUE(loop->post_and_wait([&] {
      (void)loop->submit_recv_bytes(recv_sock, buf,
          [&](completion_id, iou_res res, iou_cqe_flags) -> slot_retention {
            recv_res.store(res.value(), std::memory_order::relaxed);
            recv_done.store(true, std::memory_order::release);
            return slot_retention{};
          });
      return true;
    }));

    EXPECT_TRUE(loop->post_and_wait([&] {
      (void)loop->submit_cancel(recv_sock,
          [](completion_id, iou_res, iou_cqe_flags) -> slot_retention {
            return slot_retention{};
          });
      return true;
    }));

    EXPECT_TRUE(
        WaitFor([&] { return recv_done.load(std::memory_order::acquire); }));
    EXPECT_LT(recv_res.load(), 0);
  }
}
#pragma endregion

#pragma region SubmitCancelToken
void IouLoop_SubmitCancelToken() {
  // Canceling via `completion_token` delivers a negative result to the
  // matching recv callback (typically `ECANCELED`).
  if (true) {
    auto [send_sock, recv_sock] = net_socket::create_pair();

    iou_loop_runner loop;
    std::atomic_bool recv_done{false};
    std::atomic_int32_t recv_res{0};
    std::array<std::byte, 16> buf{};
    iou_loop::completion_token recv_token;

    EXPECT_TRUE(loop->post_and_wait([&] {
      recv_token = loop->submit_recv_bytes(recv_sock, buf,
          [&](completion_id, iou_res res, iou_cqe_flags) -> slot_retention {
            recv_res.store(res.value(), std::memory_order::relaxed);
            recv_done.store(true, std::memory_order::release);
            return slot_retention{};
          });
      return true;
    }));

    EXPECT_TRUE(loop->post_and_wait([&] {
      (void)loop->submit_cancel(std::move(recv_token),
          [](completion_id, iou_res, iou_cqe_flags) -> slot_retention {
            return slot_retention{};
          });
      return true;
    }));

    EXPECT_TRUE(
        WaitFor([&] { return recv_done.load(std::memory_order::acquire); }));
    EXPECT_LT(recv_res.load(), 0);
  }
}
#pragma endregion

#pragma region AcceptConnect
void IouLoop_AcceptConnect() {
  // `submit_accept` and `submit_connect` complete successfully over a Unix ANS
  // socket. The accepted socket fd (>= 0) is immediately closed to avoid
  // leaks.
  if (true) {
    auto listen_sock = net_socket::create_uds();
    EXPECT_TRUE(listen_sock);
    net_endpoint ep{"@corvid_iouloop_acceptconnect"};
    EXPECT_TRUE(listen_sock.bind(ep));
    EXPECT_TRUE(listen_sock.listen());

    iou_loop_runner loop;
    std::atomic_bool accepted{false};
    std::atomic_int32_t accept_res{-1};
    std::atomic_bool connected{false};
    std::atomic_int32_t connect_res{-2};

    const auto accept_tok = loop->submit_accept(listen_sock,
        [&](completion_id, iou_res res, iou_cqe_flags,
            const combined_endpoint&) -> slot_retention {
          accept_res.store(res.value(), std::memory_order::relaxed);
          accepted.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(accept_tok.is_valid());

    auto client_sock = net_socket::create_uds();
    EXPECT_TRUE(client_sock);
    bound_endpoint_with_timeout connect_ep{};
    connect_ep.sockaddr.sockaddr = ep;
    connect_ep.sockaddr.len = ep.sockaddr_size();
    const auto connect_tok = loop->submit_connect(client_sock,
        std::move(connect_ep),
        [&](completion_id, iou_res res, iou_cqe_flags) -> slot_retention {
          connect_res.store(res.value(), std::memory_order::relaxed);
          connected.store(true, std::memory_order::release);
          return slot_retention{};
        });
    EXPECT_TRUE(connect_tok.is_valid());

    EXPECT_TRUE(
        WaitFor([&] { return accepted.load(std::memory_order::acquire); }));
    EXPECT_TRUE(
        WaitFor([&] { return connected.load(std::memory_order::acquire); }));
    // `accept_res` is the new socket fd (>= 0).
    EXPECT_GE(accept_res.load(), 0);
    EXPECT_EQ(connect_res.load(), 0);
    if (accept_res.load() >= 0) ::close(accept_res.load());
  }
}
#pragma endregion

#pragma region RecvSendMsg
void IouLoop_RecvSendMsg() {
  // `submit_recvmsg_buffer` and `submit_sendmsg_buffer` exchange a datagram
  // between two UDP sockets. On completion, the received payload and sender
  // address are available via the buffer.
  if (true) {
    auto recv_ep = net_endpoint::any_v4(0);
    auto send_ep = net_endpoint::any_v4(0);
    auto recv_sock = net_socket::create_for(recv_ep, execution::nonblocking,
        message_style::datagram);
    auto send_sock = net_socket::create_for(send_ep, execution::nonblocking,
        message_style::datagram);
    EXPECT_TRUE(recv_sock.bind(recv_ep));
    EXPECT_TRUE(send_sock.bind(send_ep));
    net_endpoint recv_addr{recv_sock};

    iou_loop_runner loop;
    std::atomic_bool received{false};
    std::atomic_int32_t recv_n{-1};
    std::atomic_int32_t send_n{-1};
    std::string recv_result;

    constexpr std::string_view payload{"recvmsg-test"};

    const bool ok = loop->post_and_wait([&] {
      auto recv_buf = loop->borrow_read_buffer();
      if (!recv_buf) return false;
      const auto rtok = loop->submit_recvmsg_buffer(recv_sock,
          std::move(recv_buf),
          [&](completion_id, iou_loop::buffer& buf) -> slot_retention {
            recv_n.store(buf.result().value(), std::memory_order::relaxed);
            recv_result = std::string{buf.payload_view()};
            received.store(true, std::memory_order::release);
            return {};
          });
      if (!rtok.is_valid()) return false;

      auto send_buf = loop->borrow_write_buffer();
      if (!send_buf) return false;
      (void)send_buf.append(payload);
      send_buf.peer_addr() = recv_addr;
      const auto stok = loop->submit_sendmsg_buffer(send_sock,
          std::move(send_buf),
          [&](completion_id, iou_loop::buffer& buf) -> slot_retention {
            send_n.store(buf.result().value(), std::memory_order::relaxed);
            return {};
          });
      if (!stok.is_valid()) return false;
      return loop->immediate_submit();
    });
    EXPECT_TRUE(ok);
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(recv_n.load(), static_cast<int32_t>(payload.size()));
    EXPECT_EQ(send_n.load(), static_cast<int32_t>(payload.size()));
    EXPECT_EQ(recv_result, std::string{payload});
  }
}
#pragma endregion

#pragma region BorrowBufferSizes
void IouLoop_BorrowBufferSizes() {
  // `borrow_read_buffer` and `borrow_write_buffer` succeed for all three block
  // sizes, and the buffers are returned to the pool on destruction.
  if (true) {
    iou_loop_runner loop;
    using bs = iou_loop::block_size;

    {
      auto s = loop->borrow_read_buffer(bs::small);
      auto m = loop->borrow_read_buffer(bs::medium);
      auto l = loop->borrow_read_buffer(bs::large);
      EXPECT_TRUE(s);
      EXPECT_TRUE(m);
      EXPECT_TRUE(l);
    }
    {
      auto s = loop->borrow_write_buffer(bs::small);
      auto m = loop->borrow_write_buffer(bs::medium);
      auto l = loop->borrow_write_buffer(bs::large);
      EXPECT_TRUE(s);
      EXPECT_TRUE(m);
      EXPECT_TRUE(l);
    }
  }
}
#pragma endregion

#pragma region SlotRetentionRetain
void IouLoop_SlotRetentionRetain() {
  // A callback returning `slot_retention::retain` keeps the pool slot live.
  // Re-submitting a NOP with the same token fires the callback a second time,
  // after which it releases normally.
  if (true) {
    iou_loop_runner loop;
    std::atomic<int> count{0};

    const auto token = loop->tokenize(
        [&](completion_id id, iou_res, iou_cqe_flags) -> slot_retention {
          if (count.fetch_add(1, std::memory_order::relaxed) == 0) {
            // Re-submit using the same slot; we're on the loop thread so this
            // executes inline before `do_dispatch`'s detach path runs.
            (void)loop->submit_nop(iou_loop::completion_token{id},
                slot_retention::retain);
            return slot_retention::retain;
          }
          return slot_retention::automatic;
        });
    EXPECT_TRUE(token.is_valid());
    const bool submitted = loop->submit_nop(token, slot_retention::automatic);
    EXPECT_TRUE(submitted);
    EXPECT_TRUE(
        WaitFor([&] { return count.load(std::memory_order::acquire) == 2; }));
    EXPECT_EQ(count.load(), 2);
  }
}
#pragma endregion

#pragma region IouWrap_TimespecDurationRoundTrip
void IouWrap_TimespecDurationRoundTrip() {
  // Construct from durations and verify `as_duration()` recovers the original.
  if (true) {
    iou_timespec invalid;
    EXPECT_TRUE(invalid.as_duration<std::chrono::nanoseconds>().count() < 0);
  }
  if (true) {
    iou_timespec ts{50ms};
    EXPECT_TRUE(ts.as_duration<std::chrono::milliseconds>() == 50ms);
  }
  if (true) {
    // 1.5 s: verifies the seconds/nanoseconds split is lossless.
    iou_timespec ts{1s + 500ms};
    EXPECT_TRUE(ts.as_duration<std::chrono::milliseconds>() == 1500ms);
  }
  if (true) {
    // Negative durations are clamped to zero by `from_duration`.
    iou_timespec ts{-10ms};
    EXPECT_TRUE(ts.as_duration<std::chrono::nanoseconds>().count() == 0);
  }
}
#pragma endregion

#pragma region IouWrap_TimespecTimePointRoundTrip
void IouWrap_TimespecTimePointRoundTrip() {
  // Construct from a `steady_clock` time_point and verify `as_time_point()`
  // recovers the exact same value.
  using clk = std::chrono::steady_clock;
  using dur = clk::duration;
  if (true) {
    auto now = clk::now();
    iou_timespec ts{now};
    auto recovered = ts.as_time_point<clk, dur>();
    EXPECT_TRUE(recovered == now);
  }
}
#pragma endregion

#pragma region IouWrap_TimespecStaticHelpers
void IouWrap_TimespecStaticHelpers() {
  // `from_duration` splits seconds and nanosecond remainder correctly.
  if (true) {
    auto raw = iou_timespec::from_duration(750ms);
    EXPECT_TRUE(raw.tv_sec == 0LL);
    EXPECT_TRUE(raw.tv_nsec == 750'000'000LL);
  }
  if (true) {
    auto raw = iou_timespec::from_duration(2s + 300ms);
    EXPECT_TRUE(raw.tv_sec == 2LL);
    EXPECT_TRUE(raw.tv_nsec == 300'000'000LL);
  }
  // `to_duration` is the inverse of `from_duration`.
  if (true) {
    auto raw = iou_timespec::from_duration(750ms);
    EXPECT_TRUE(
        iou_timespec::to_duration<std::chrono::milliseconds>(raw) == 750ms);
  }
  // `from_time_point` / `to_time_point` round-trip.
  if (true) {
    using clk = std::chrono::steady_clock;
    using dur = clk::duration;
    auto now = clk::now();
    auto raw = iou_timespec::from_time_point(now);
    auto recovered = iou_timespec::to_time_point<clk, dur>(raw);
    EXPECT_TRUE(recovered == now);
  }
}
#pragma endregion

#pragma region IouWrap_TimespecAsPointer
void IouWrap_TimespecAsPointer() {
  // `as_pointer(nullptr)` is null; `as_pointer(&ts)` is non-null.
  if (true) {
    EXPECT_TRUE(
        iou_timespec::to_pointer(static_cast<iou_timespec*>(nullptr)) ==
        nullptr);
    EXPECT_TRUE(
        iou_timespec::to_pointer(static_cast<const iou_timespec*>(nullptr)) ==
        nullptr);
  }
  if (true) {
    iou_timespec ts{50ms};
    EXPECT_TRUE(iou_timespec::to_pointer(&ts) != nullptr);
    const iou_timespec cts{50ms};
    EXPECT_TRUE(iou_timespec::to_pointer(&cts) != nullptr);
  }
}
#pragma endregion

#pragma region IouWrap_ItimerspecConstruct
void IouWrap_ItimerspecConstruct() {
  // Default construction zeros both fields; explicit construction stores the
  // correct interval and value.
  if (true) {
    iou_itimerspec its;
    EXPECT_TRUE(its.it_interval().tv_sec == 0LL);
    EXPECT_TRUE(its.it_interval().tv_nsec == 0LL);
    EXPECT_TRUE(its.it_value().tv_sec == 0LL);
    EXPECT_TRUE(its.it_value().tv_nsec == 0LL);
  }
  if (true) {
    iou_timespec interval{100ms};
    iou_timespec value{500ms};
    iou_itimerspec its{interval, value};
    EXPECT_TRUE(its.it_interval().tv_nsec == 100'000'000LL);
    EXPECT_TRUE(its.it_value().tv_nsec == 500'000'000LL);
  }
}
#pragma endregion

#pragma region IouWrap_ResStatus
void IouWrap_ResStatus() {
  // `ok()`, `bool`, and `!` reflect the sign of the raw result.
  if (true) {
    iou_res r{0};
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r);
    EXPECT_TRUE(r.value() == 0);
  }
  if (true) {
    iou_res r{42};
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.value() == 42);
    EXPECT_TRUE(r.bytes() == size_t{42});
  }
  if (true) {
    iou_res r{-EINVAL};
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(r);
    EXPECT_TRUE(r.value() == -EINVAL);
  }
  // `ok(n)` requires `res` >= n.
  if (true) {
    iou_res r{3};
    EXPECT_TRUE(r.ok(3));
    EXPECT_FALSE(r.ok(4));
  }
  // `ETIME` is a soft error; `EINVAL` is not.
  if (true) {
    EXPECT_TRUE(iou_res{-ETIME}.is_soft_error());
    EXPECT_FALSE(iou_res{-EINVAL}.is_soft_error());
  }
}
#pragma endregion

#pragma region IouWrap_CqeFlagsString
void IouWrap_CqeFlagsString() {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  using namespace corvid::strings;
  using F = iou_cqe_flags;
  if (true) {
    EXPECT_EQ(enum_as_string(F::buffer), "buffer");
    EXPECT_EQ(enum_as_string(F::more), "more");
    EXPECT_EQ(enum_as_string(F::sock_nonempty), "sock_nonempty");
    EXPECT_EQ(enum_as_string(F::notif), "notif");
  }
  if (true) {
    // Higher bits print first.
    EXPECT_EQ(enum_as_string(F::more | F::buffer), "more + buffer");
  }
  if (true) {
    constexpr F bad{0xff};
    EXPECT_EQ(parse_enum("buffer", bad), F::buffer);
    EXPECT_EQ(parse_enum("more", bad), F::more);
    EXPECT_EQ(parse_enum("sock_nonempty", bad), F::sock_nonempty);
    EXPECT_EQ(parse_enum("notif", bad), F::notif);
    EXPECT_EQ(parse_enum("more + buffer", bad), F::more | F::buffer);
  }
}
#pragma endregion

#pragma region IouWrap_SqeFlagsString
void IouWrap_SqeFlagsString() {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  using namespace corvid::strings;
  using F = iou_sqe_flags;
  if (true) {
    EXPECT_EQ(enum_as_string(F::fixed_file), "fixed_file");
    EXPECT_EQ(enum_as_string(F::io_drain), "io_drain");
    EXPECT_EQ(enum_as_string(F::io_link), "io_link");
    EXPECT_EQ(enum_as_string(F::io_hardlink), "io_hardlink");
    EXPECT_EQ(enum_as_string(F::async), "async");
    EXPECT_EQ(enum_as_string(F::buffer_select), "buffer_select");
    EXPECT_EQ(enum_as_string(F::cqe_skip_success), "cqe_skip_success");
  }
  if (true) {
    // Higher bits print first.
    EXPECT_EQ(enum_as_string(F::async | F::io_link), "async + io_link");
  }
  if (true) {
    constexpr F bad{0xff};
    EXPECT_EQ(parse_enum("fixed_file", bad), F::fixed_file);
    EXPECT_EQ(parse_enum("cqe_skip_success", bad), F::cqe_skip_success);
    EXPECT_EQ(parse_enum("async + io_link", bad), F::async | F::io_link);
  }
}
#pragma endregion

#pragma region IouWrap_SetupFlagsString
void IouWrap_SetupFlagsString() {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  using namespace corvid::strings;
  using F = iou_setup_flags;
  if (true) {
    EXPECT_EQ(enum_as_string(F::setup_iopoll), "setup_iopoll");
    EXPECT_EQ(enum_as_string(F::setup_sqpoll), "setup_sqpoll");
    EXPECT_EQ(enum_as_string(F::setup_sq_aff), "setup_sq_aff");
    EXPECT_EQ(enum_as_string(F::setup_cqsize), "setup_cqsize");
    EXPECT_EQ(enum_as_string(F::setup_clamp), "setup_clamp");
    EXPECT_EQ(enum_as_string(F::setup_attach_wq), "setup_attach_wq");
    EXPECT_EQ(enum_as_string(F::setup_r_disabled), "setup_r_disabled");
    EXPECT_EQ(enum_as_string(F::setup_submit_all), "setup_submit_all");
    EXPECT_EQ(enum_as_string(F::setup_coop_taskrun), "setup_coop_taskrun");
    EXPECT_EQ(enum_as_string(F::setup_taskrun_flag), "setup_taskrun_flag");
    EXPECT_EQ(enum_as_string(F::setup_sqe128), "setup_sqe128");
    EXPECT_EQ(enum_as_string(F::setup_cqe32), "setup_cqe32");
    EXPECT_EQ(enum_as_string(F::setup_single_issuer), "setup_single_issuer");
    EXPECT_EQ(enum_as_string(F::setup_defer_taskrun), "setup_defer_taskrun");
    EXPECT_EQ(enum_as_string(F::setup_no_mmap), "setup_no_mmap");
    EXPECT_EQ(enum_as_string(F::setup_registered_fd_only),
        "setup_registered_fd_only");
  }
  if (true) {
    // Higher bits print first.
    EXPECT_EQ(enum_as_string(F::setup_sqpoll | F::setup_iopoll),
        "setup_sqpoll + setup_iopoll");
  }
  if (true) {
    constexpr F bad{0x80000000};
    EXPECT_EQ(parse_enum("setup_iopoll", bad), F::setup_iopoll);
    EXPECT_EQ(parse_enum("setup_registered_fd_only", bad),
        F::setup_registered_fd_only);
    EXPECT_EQ(parse_enum("setup_sqpoll + setup_iopoll", bad),
        F::setup_sqpoll | F::setup_iopoll);
  }
}
#pragma endregion

#pragma region IouWrap_TimeoutFlagsString
void IouWrap_TimeoutFlagsString() {
  // Each named bit round-trips through `enum_as_string` / `parse_enum`.
  // `rel` (value 0) has no bit name and prints as "0x00".
  using namespace corvid::strings;
  using F = iou_timeout_flags;
  if (true) {
    EXPECT_EQ(enum_as_string(F::rel), "0x00000000");
    EXPECT_EQ(enum_as_string(F::abs), "abs");
    EXPECT_EQ(enum_as_string(F::update), "update");
    EXPECT_EQ(enum_as_string(F::boot_time), "boot_time");
    EXPECT_EQ(enum_as_string(F::real_time), "real_time");
    EXPECT_EQ(enum_as_string(F::link_timeout_update), "link_timeout_update");
    EXPECT_EQ(enum_as_string(F::etime_success), "etime_success");
    EXPECT_EQ(enum_as_string(F::multishot), "multishot");
  }
  if (true) {
    // Higher bits print first.
    EXPECT_EQ(enum_as_string(F::multishot | F::abs), "multishot + abs");
  }
  if (true) {
    constexpr F bad{0x80000000};
    EXPECT_EQ(parse_enum("abs", bad), F::abs);
    EXPECT_EQ(parse_enum("multishot", bad), F::multishot);
    EXPECT_EQ(parse_enum("multishot + abs", bad), F::multishot | F::abs);
  }
}
#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
MAKE_TEST_LIST(IouLoop_NopCompletion, IouLoop_MultipleNops,
    IouLoop_StopFromThread, IouLoop_PostFromThread, IouLoop_PostAndWait,
    IouLoop_RecvSend, IouLoop_RecvWriteFixed, IouLoop_IsLoopThread,
    IouLoop_ExecuteOrPost, IouLoop_NopTokenVariant, IouLoop_TokenIsReleased,
    IouLoop_SubmitClose, IouLoop_SubmitTimeout, IouLoop_SubmitTimeoutMultishot,
    IouLoop_SubmitCancelFile, IouLoop_SubmitCancelToken, IouLoop_AcceptConnect,
    IouLoop_RecvSendMsg, IouLoop_BorrowBufferSizes,
    IouLoop_SlotRetentionRetain, IouWrap_TimespecDurationRoundTrip,
    IouWrap_TimespecTimePointRoundTrip, IouWrap_TimespecStaticHelpers,
    IouWrap_TimespecAsPointer, IouWrap_ItimerspecConstruct, IouWrap_ResStatus,
    IouWrap_CqeFlagsString, IouWrap_SqeFlagsString, IouWrap_SetupFlagsString,
    IouWrap_TimeoutFlagsString)
