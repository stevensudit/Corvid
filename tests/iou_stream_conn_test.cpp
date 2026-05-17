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
#include <functional>
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
#ifdef DEBUG
  timeout = 1h;
#endif
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (!pred() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::sleep_for(1ms);
  return pred();
}

// Shared test plugin. State lives in a `state` struct owned by the test;
// the plugin just holds a pointer to it (plus a ref to its conn, which it
// captures at construction).
class capture_protocol {
public:
  using conn_t = iou_stream_conn<capture_protocol>;

  struct state {
    std::function<bool(iou_recv_view)> on_data;
    std::function<bool()> on_drain;
    std::function<bool()> on_close;
  };

  explicit capture_protocol(conn_t& conn, state* s = nullptr) noexcept
      : conn_{conn}, state_{s} {}

  bool on_data(iou_recv_view view) {
    if (state_ && state_->on_data) return state_->on_data(std::move(view));
    view.consume(view.active_view().size());
    return true;
  }

  bool on_drain() {
    if (state_ && state_->on_drain) return state_->on_drain();
    return true;
  }

  bool on_close() {
    if (state_ && state_->on_close) return state_->on_close();
    return true;
  }

  capture_protocol make_child_plugin(conn_t& child) const {
    return capture_protocol{child, state_};
  }

  [[nodiscard]] conn_t& conn() noexcept { return conn_; }

private:
  conn_t& conn_;
  state* state_;
};

using capture_conn = iou_stream_conn<capture_protocol>;

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

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      payload = view.active_view();
      view.consume(view.active_view().size());
      received.store(true, std::memory_order::release);
      return true;
    };

    // Adopt sock1 as receiver.
    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    // Adopt sock0 as sender.
    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
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
    relaxed_atomic_int recv_bytes{0};
    std::string payload;

    constexpr int N = 4;
    constexpr std::string_view msg{"abc"};

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      auto sv = view.active_view();
      payload += sv;
      recv_bytes += static_cast<int>(sv.size());
      view.consume(sv.size());
      return true;
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_conn);

    for (int i = 0; i < N; ++i) EXPECT_TRUE(send_conn->send(std::string{msg}));

    const int expected = N * static_cast<int>(msg.size());
    EXPECT_TRUE(WaitFor([&] { return recv_bytes >= expected; }));
    EXPECT_EQ(recv_bytes, expected);
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

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      payload = view.active_view();
      view.consume(view.active_view().size());
      received.store(true, std::memory_order::release);
      return true;
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
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

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      auto buf = view.take(); // move buffer out
      payload.assign(buf.payload_view());
      received.store(true, std::memory_order::release);
      return true;
      // buf returns to pool on scope exit
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
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

    capture_protocol::state state0;
    state0.on_close = [&] {
      closed0.store(true, std::memory_order::release);
      return true;
    };
    capture_protocol::state state1;
    state1.on_close = [&] {
      closed1.store(true, std::memory_order::release);
      return true;
    };

    auto conn0 = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid, shot_type::single, {}, {}, &state0)
                     .lock();
    auto conn1 = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid, shot_type::single, {}, {}, &state1)
                     .lock();
    EXPECT_TRUE(conn0);
    EXPECT_TRUE(conn1);

    // Give both connections time to arm their recv SQEs.
    std::this_thread::sleep_for(20ms);

    // TODO: This doesn't do the graceful close that's promised.

    EXPECT_TRUE(conn0->close());
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

    capture_protocol::state state0;
    state0.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    auto conn0 = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid, shot_type::single, {}, {}, &state0)
                     .lock();
    // conn1 just absorbs the other end
    auto conn1 = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                     .lock();
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

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(recv_conn);

    capture_protocol::state send_state;
    send_state.on_drain = [&] {
      drained.store(true, std::memory_order::release);
      return true;
    };

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid, shot_type::single, {}, {}, &send_state)
            .lock();
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{"ping"}));
    EXPECT_TRUE(
        WaitFor([&] { return drained.load(std::memory_order::acquire); }));
  }
}

#pragma endregion
#pragma region WithState

void IouStreamConn_WithState() {
  // Per-connection state living in the plugin (replaces the old
  // `iou_stream_conn_with_state` pattern).
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool received{false};

    struct MyState {
      int recv_count{};
    };

    MyState my_state;

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      my_state.recv_count++;
      view.consume(view.active_view().size());
      received.store(true, std::memory_order::release);
      return true;
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{"state-test"}));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(my_state.recv_count, 1);
  }
}

#pragma endregion
#pragma region FullBufferPartialConsume

void IouStreamConn_FullBufferPartialConsume() {
  // Fills the recv buffer completely, then consumes part of it on the first
  // full-buffer delivery. The remaining bytes plus newly-received bytes are
  // delivered on a subsequent recv. Verifies that partial consume of a full
  // buffer makes forward progress (the buffer regains headroom) and that the
  // contract violation -- returning a full buffer with zero consumed -- is
  // not exercised here.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool done{false};
    std::atomic_size_t total_consumed{0};
    const size_t buf_size = *block_size::kb004;

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      const auto sz = view.active_view().size();
      view.consume(sz);
      if (total_consumed.fetch_add(sz, std::memory_order::acq_rel) + sz >=
          buf_size + 1)
        done.store(true, std::memory_order::release);
      return true;
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string(buf_size, 'x')));
    EXPECT_TRUE(send_conn->send(std::string(1, 'y')));
    EXPECT_TRUE(
        WaitFor([&] { return done.load(std::memory_order::acquire); }));
    EXPECT_GE(total_consumed.load(std::memory_order::acquire), buf_size + 1);
  }
}

#pragma endregion
#pragma region MultishotRecvBasic

void IouStreamConn_MultishotRecv_Basic() {
  // multishot recv mode: data arrives and `on_data` fires.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    constexpr std::string_view msg{"hello-multishot"};

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      payload = view.active_view();
      view.consume(view.active_view().size());
      received.store(true, std::memory_order::release);
      return true;
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
  }
}

#pragma endregion
#pragma region MultishotRecvMultipleMessages

void IouStreamConn_MultishotRecv_MultipleMessages() {
  // multishot recv mode: multiple sends are all delivered (bytes counted since
  // the stream socket may coalesce messages into a single on_data call).
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic<int> recv_bytes{0};

    constexpr int N = 3;
    constexpr std::string_view msg{"ping"};
    const int expected = static_cast<int>(N * msg.size());

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      recv_bytes.fetch_add(static_cast<int>(view.active_view().size()),
          std::memory_order::release);
      view.consume(view.active_view().size());
      return true;
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_conn);

    for (int i = 0; i < N; ++i) EXPECT_TRUE(send_conn->send(std::string{msg}));

    EXPECT_TRUE(WaitFor([&] {
      return recv_bytes.load(std::memory_order::acquire) >= expected;
    }));
    EXPECT_EQ(recv_bytes.load(), expected);
  }
}

#pragma endregion
#pragma region MultishotRecvTakeBuffer

void IouStreamConn_MultishotRecv_TakeBuffer() {
  // `take()` in multishot mode: multishot resubmits after the taken buffer is
  // released.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic<int> recv_count{0};
    iou_loop::buffer taken_buf;

    constexpr std::string_view msg{"take-multi"};

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      if (recv_count.load(std::memory_order::relaxed) == 0)
        taken_buf = view.take(); // take the first buffer
      else
        view.consume(view.active_view().size());
      recv_count.fetch_add(1, std::memory_order::release);
      return true;
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 1;
    }));

    // After take(), send a second message; recv should still work.
    taken_buf = {}; // release the taken buffer back to the pool
    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 2;
    }));
  }
}

#pragma endregion
#pragma region MultishotRecvStopAndResume

void IouStreamConn_MultishotRecv_StopAndResume() {
  // `stop_receiving()` pauses recv and returns a resume callback; the
  // callback both keeps the conn alive and, when invoked, restarts the
  // recv loop.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic<int> recv_count{0};

    constexpr std::string_view msg{"stop-resume"};

    // The resume callback comes out of on_data. Stash it where the test
    // thread can see it. `iou_loop::posted_fn` is move-only.
    std::mutex cb_mutex;
    iou_loop::posted_fn resume_cb;

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      view.consume(view.active_view().size());
      if (recv_count.fetch_add(1, std::memory_order::acq_rel) == 0) {
        // Buffer must be extracted before stop_receiving (it no longer
        // returns the buffer). We have nothing more to do with it here.
        (void)view.take();
        auto cb = view.stop_receiving();
        std::scoped_lock lock{cb_mutex};
        resume_cb = std::move(cb);
      }
      return true;
    };

    // Raw pointer is fine: the resume callback (when produced) holds the
    // shared_ptr that keeps the conn alive across the pause.
    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_conn);

    // First message: received, then reading stops.
    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 1;
    }));

    // Second message: sent while paused, should not be delivered yet.
    EXPECT_TRUE(send_conn->send(std::string{msg}));
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(recv_count.load(std::memory_order::acquire), 1);

    // Resume by invoking the callback.
    {
      std::scoped_lock lock{cb_mutex};
      EXPECT_TRUE(resume_cb);
      EXPECT_TRUE(resume_cb());
    }
    EXPECT_TRUE(WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 2;
    }));
  }
}

#pragma endregion
#pragma region MultishotRecvAcceptedConnsInheritMode

void IouStreamConn_MultishotRecv_AcceptedConnsInheritMode() {
  // Accepted connections from a multishot-mode listener also use multishot.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    constexpr std::string_view msg{"inherit-multishot"};

    capture_protocol::state server_state;
    server_state.on_data = [&](iou_recv_view view) {
      payload = view.active_view();
      view.consume(view.active_view().size());
      received.store(true, std::memory_order::release);
      return true;
    };

    auto server = capture_conn::listen(*runner.loop(),
        net_endpoint::loopback_v4(0), shot_type::multi, {}, {}, &server_state)
                      .lock();
    EXPECT_TRUE(server);

    const auto& listen_ep = server->local_endpoint();
    auto client = capture_conn::connect(*runner.loop(), listen_ep).lock();
    EXPECT_TRUE(client);

    EXPECT_TRUE(client->send(std::string{msg}));
    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
  }
}

#pragma endregion
#pragma region AccessorsLifecycle

void IouStreamConn_AccessorsLifecycle() {
  // is_open / writes_allowed / is_read_shut / is_write_shut transitions.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool closed{false};

    capture_protocol::state state0;
    state0.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    auto conn0 = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid, shot_type::single, {}, {}, &state0)
                     .lock();
    auto conn1 = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                     .lock();
    EXPECT_TRUE(conn0);
    EXPECT_TRUE(conn1);

    EXPECT_TRUE(conn0->is_open());
    EXPECT_TRUE(conn0->writes_allowed());
    EXPECT_FALSE(conn0->is_read_shut());
    EXPECT_FALSE(conn0->is_write_shut());

    EXPECT_TRUE(conn0->close());
    // writes_allowed flips synchronously off `close_requested_`; is_open
    // follows once the actual close is processed.
    EXPECT_FALSE(conn0->writes_allowed());
    EXPECT_TRUE(
        WaitFor([&] { return closed.load(std::memory_order::acquire); }));
    EXPECT_FALSE(conn0->is_open());

    // Second `close` is a no-op.
    EXPECT_FALSE(conn0->close());
  }
}

#pragma endregion
#pragma region Endpoints

void IouStreamConn_Endpoints() {
  // local_endpoint and remote_endpoint resolve after listen + connect.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool connected{false};

    capture_protocol::state server_state;
    auto server = capture_conn::listen(*runner.loop(),
        net_endpoint::loopback_v4(0), shot_type::multi, {}, {}, &server_state)
                      .lock();
    EXPECT_TRUE(server);

    const auto& listen_ep = server->local_endpoint();
    EXPECT_FALSE(listen_ep.empty());
    EXPECT_NE(listen_ep.port(), 0);

    capture_protocol::state client_state;
    client_state.on_drain = [&] {
      connected.store(true, std::memory_order::release);
      return true;
    };

    auto client = capture_conn::connect(*runner.loop(), listen_ep,
        shot_type::single, {}, {}, &client_state)
                      .lock();
    EXPECT_TRUE(client);
    EXPECT_TRUE(
        WaitFor([&] { return connected.load(std::memory_order::acquire); }));

    // After connect succeeds, both endpoints should be resolvable.
    const auto& cl_remote = client->remote_endpoint();
    EXPECT_EQ(cl_remote.port(), listen_ep.port());

    const auto& cl_local = client->local_endpoint();
    EXPECT_FALSE(cl_local.empty());
    EXPECT_NE(cl_local.port(), 0);
  }
}

#pragma endregion
#pragma region BufSizeAccessors

void IouStreamConn_BufSizeAccessors() {
  // set_recv_buf_size / set_send_buf_size round-trip through the relaxed
  // atomic storage. The conn keeps using the new sizes for subsequent
  // borrows; we don't try to observe that here, just the getter/setter.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;

    auto conn = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid)
                    .lock();
    EXPECT_TRUE(conn);

    EXPECT_EQ(conn->recv_buf_size(), block_size::kb004);
    EXPECT_EQ(conn->send_buf_size(), block_size::kb004);

    conn->set_recv_buf_size(block_size::kb002);
    conn->set_send_buf_size(block_size::kb008);
    EXPECT_EQ(conn->recv_buf_size(), block_size::kb002);
    EXPECT_EQ(conn->send_buf_size(), block_size::kb008);

    // Keep sock1 alive until conn teardown to avoid a stray RST during the
    // test (cleanup happens in the runner destructor).
    (void)capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid);
  }
}

#pragma endregion
#pragma region PeerEofDeliversEmptyView

void IouStreamConn_PeerEofDeliversEmptyView() {
  // When the peer closes, this side gets a final on_data with an empty view
  // and is_read_shut transitions to true.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool got_data{false};
    std::atomic_bool got_eof{false};
    std::atomic_bool got_close{false};
    std::string payload;

    constexpr std::string_view msg{"bye"};

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      auto sv = view.active_view();
      if (sv.empty()) {
        got_eof.store(true, std::memory_order::release);
      } else {
        payload += sv;
        view.consume(sv.size());
        got_data.store(true, std::memory_order::release);
      }
      return true;
    };
    recv_state.on_close = [&] {
      got_close.store(true, std::memory_order::release);
      return true;
    };

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_conn);

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_conn);

    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(
        WaitFor([&] { return got_data.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);

    EXPECT_TRUE(send_conn->close());
    EXPECT_TRUE(
        WaitFor([&] { return got_eof.load(std::memory_order::acquire); }));
    EXPECT_TRUE(recv_conn->is_read_shut());

    // Peer EOF does not auto-close locally; the recv side must close itself
    // to retire its on_close.
    EXPECT_TRUE(recv_conn->close());
    EXPECT_TRUE(
        WaitFor([&] { return got_close.load(std::memory_order::acquire); }));
  }
}

#pragma endregion
#pragma region ShutdownSend

void IouStreamConn_ShutdownSend() {
  // shutdown_send flushes the queue, then peer sees EOF on its read side.
  // Further sends are rejected on this side. Second call is a no-op.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool got_data{false};
    std::atomic_bool got_eof{false};
    std::string payload;

    constexpr std::string_view msg{"last-message"};

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      auto sv = view.active_view();
      if (sv.empty()) {
        got_eof.store(true, std::memory_order::release);
      } else {
        payload += sv;
        view.consume(sv.size());
        got_data.store(true, std::memory_order::release);
      }
      return true;
    };

    // Hold shared_ptrs to both conns. Otherwise, once the recv loop stops
    // (peer EOF), recv_conn loses its last in-flight self-ref, destructs,
    // and force-RSTs the socket - which would then drive send_conn's
    // post-shutdown_send read to EOF and auto-close it.
    auto recv_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_raw);
    auto recv_conn = recv_raw->self();

    auto send_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_raw);
    auto send_conn = send_raw->self();

    // send + shutdown_send back-to-back: the send was authorized when
    // `writes_allowed()` first returned true, so the in-lambda guard
    // (which checks `closed_`, not `shutdown_send_requested_`) honors it
    // before `shutdown_send` tears down.
    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(send_conn->shutdown_send());

    EXPECT_TRUE(
        WaitFor([&] { return got_data.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
    EXPECT_TRUE(
        WaitFor([&] { return got_eof.load(std::memory_order::acquire); }));

    // Writes are now rejected.
    EXPECT_FALSE(send_conn->writes_allowed());
    EXPECT_FALSE(send_conn->send(std::string{"nope"}));

    // Idempotent.
    EXPECT_FALSE(send_conn->shutdown_send());

    // Local conn is still open for reads.
    EXPECT_TRUE(send_conn->is_open());

    // Clean shutdown: explicit close on both sides before runner teardown.
    EXPECT_TRUE(recv_conn->close());
    EXPECT_TRUE(send_conn->close());
  }
}

#pragma endregion
#pragma region ShutdownRecv

void IouStreamConn_ShutdownRecv() {
  // shutdown_recv sets is_read_shut; further data from the peer no longer
  // reaches on_data. Idempotent.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic<int> recv_count{0};

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      view.consume(view.active_view().size());
      recv_count.fetch_add(1, std::memory_order::acq_rel);
      return true;
    };

    auto recv_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_raw);
    auto recv_conn = recv_raw->self();

    auto send_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_raw);
    auto send_conn = send_raw->self();

    EXPECT_TRUE(send_conn->send(std::string{"first"}));
    EXPECT_TRUE(WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 1;
    }));

    EXPECT_TRUE(recv_conn->shutdown_recv());
    EXPECT_TRUE(WaitFor([&] { return recv_conn->is_read_shut(); }));

    // Idempotent.
    EXPECT_FALSE(recv_conn->shutdown_recv());

    // New data does not surface as additional on_data calls.
    EXPECT_TRUE(send_conn->send(std::string{"second"}));
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(recv_count.load(std::memory_order::acquire), 1);
  }
}

#pragma endregion
#pragma region StopAndResumeReceivingOnConn

void IouStreamConn_StopAndResumeReceivingOnConn() {
  // conn->stop_receiving + resume_receiving in multishot mode.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic<int> recv_count{0};
    std::string payload;

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      payload += view.active_view();
      view.consume(view.active_view().size());
      recv_count.fetch_add(1, std::memory_order::acq_rel);
      return true;
    };

    auto recv_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_raw);
    auto recv_conn = recv_raw->self();

    auto send_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_raw);
    auto send_conn = send_raw->self();

    EXPECT_TRUE(send_conn->send(std::string{"A"}));
    EXPECT_TRUE(WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 1;
    }));

    // Pause. The returned shared_ptr is what keeps the conn alive once the
    // recv loop ends; without holding it we'd race against the cancel CQE
    // releasing the last in-flight self-ref. (Our outer `recv_conn`
    // already serves that role, so we don't need to bind a separate var.)
    auto paused = recv_conn->stop_receiving();
    EXPECT_TRUE(paused);
    // Give the cancel time to take effect.
    std::this_thread::sleep_for(50ms);

    // Send while paused; should not reach on_data yet.
    EXPECT_TRUE(send_conn->send(std::string{"B"}));
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(recv_count.load(std::memory_order::acquire), 1);

    // Resume.
    EXPECT_TRUE(recv_conn->resume_receiving());
    EXPECT_TRUE(WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 2;
    }));
    EXPECT_EQ(payload, "AB");

    // Idempotent: a second resume_receiving with no prior pause is a no-op.
    EXPECT_FALSE(recv_conn->resume_receiving());

    EXPECT_TRUE(recv_conn->close());
    EXPECT_TRUE(send_conn->close());
  }
}

#pragma endregion
#pragma region ConnectFailure

void IouStreamConn_ConnectFailure() {
  // Async connect to an unbound loopback port fires on_close, not on_drain.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool drained{false};
    std::atomic_bool closed{false};

    capture_protocol::state state;
    state.on_drain = [&] {
      drained.store(true, std::memory_order::release);
      return true;
    };
    state.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    // Bind+listen+drop to grab a port that is now known-unbound.
    net_endpoint unreachable;
    if (true) {
      auto probe =
          capture_conn::listen(*runner.loop(), net_endpoint::loopback_v4(0))
              .lock();
      EXPECT_TRUE(probe);
      unreachable = probe->local_endpoint();
      EXPECT_TRUE(probe->hangup());
    }
    // Small pause so the listener's close completes before we connect.
    std::this_thread::sleep_for(20ms);

    auto client = capture_conn::connect(*runner.loop(), unreachable,
        shot_type::single, {}, {}, &state)
                      .lock();
    EXPECT_TRUE(client);

    EXPECT_TRUE(
        WaitFor([&] { return closed.load(std::memory_order::acquire); }));
    EXPECT_FALSE(drained.load(std::memory_order::acquire));
  }
}

#pragma endregion
#pragma region ListenAcceptMultipleClients

void IouStreamConn_ListenAcceptMultipleClients() {
  // Multiple clients connect; the listener's plugin spawns a child plugin
  // per accept via make_child_plugin and each child receives its own data.
  if (true) {
    iou_loop_runner runner;
    constexpr int N = 3;
    std::atomic<int> total_bytes{0};
    std::mutex m;
    std::string aggregated;

    constexpr std::string_view msg{"hi"};
    const int expected = N * static_cast<int>(msg.size());

    capture_protocol::state server_state;
    server_state.on_data = [&](iou_recv_view view) {
      auto sv = view.active_view();
      if (std::scoped_lock lock{m}; true) aggregated += sv;
      total_bytes.fetch_add(static_cast<int>(sv.size()),
          std::memory_order::acq_rel);
      view.consume(sv.size());
      return true;
    };

    auto server = capture_conn::listen(*runner.loop(),
        net_endpoint::loopback_v4(0), shot_type::multi, {}, {}, &server_state)
                      .lock();
    EXPECT_TRUE(server);
    const auto& listen_ep = server->local_endpoint();

    std::vector<std::shared_ptr<capture_conn>> clients;
    clients.reserve(N);
    for (int i = 0; i < N; ++i) {
      auto c = capture_conn::connect(*runner.loop(), listen_ep).lock();
      EXPECT_TRUE(c);
      clients.push_back(c);
    }

    for (const auto& c : clients) EXPECT_TRUE(c->send(std::string{msg}));

    EXPECT_TRUE(WaitFor([&] {
      return total_bytes.load(std::memory_order::acquire) >= expected;
    }));
    EXPECT_EQ(total_bytes.load(), expected);
  }
}

#pragma endregion
#pragma region CloseFlushesPendingSend

void IouStreamConn_CloseFlushesPendingSend() {
  // send + close back-to-back: the send was authorized when `writes_allowed`
  // first returned true, so the in-lambda guard (which checks `closed_`, not
  // `close_requested_`) honors it. `close` sees a non-empty queue (or a live
  // `send_token_`) and defers until the data has actually been transmitted.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::string payload;
    std::atomic_bool got_eof{false};

    constexpr std::string_view msg{"last-words"};

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      auto sv = view.active_view();
      if (sv.empty()) {
        got_eof.store(true, std::memory_order::release);
      } else {
        payload += sv;
        view.consume(sv.size());
      }
      return true;
    };

    auto recv_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    EXPECT_TRUE(recv_raw);
    auto recv_conn = recv_raw->self();

    auto send_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    EXPECT_TRUE(send_raw);
    auto send_conn = send_raw->self();

    EXPECT_TRUE(send_conn->send(std::string{msg}));
    EXPECT_TRUE(send_conn->close());

    EXPECT_TRUE(
        WaitFor([&] { return got_eof.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, msg);
  }
}

#pragma endregion
#pragma region HangupIdempotent

void IouStreamConn_HangupIdempotent() {
  // Second hangup is a no-op.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool closed{false};

    capture_protocol::state state;
    state.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    auto conn0 = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid, shot_type::single, {}, {}, &state)
                     .lock();
    auto conn1 = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                     .lock();
    EXPECT_TRUE(conn0);
    EXPECT_TRUE(conn1);

    EXPECT_TRUE(conn0->hangup());
    EXPECT_FALSE(conn0->hangup());
    EXPECT_TRUE(
        WaitFor([&] { return closed.load(std::memory_order::acquire); }));
    EXPECT_FALSE(conn0->is_open());
  }
}

#pragma endregion
#pragma region SelfSharedPtr

void IouStreamConn_SelfSharedPtr() {
  // self() promotes a raw conn pointer to a shared_ptr. The factories return
  // a raw pointer; the conn is kept alive by its in-flight callbacks. A
  // user-held shared_ptr must extend that lifetime past hangup.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;
    std::atomic_bool closed{false};

    capture_protocol::state state;
    state.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    std::shared_ptr<capture_conn> held;
    if (true) {
      auto conn = capture_conn::adopt(*runner.loop(), std::move(sock0),
          net_endpoint::invalid, shot_type::single, {}, {}, &state)
                      .lock();
      EXPECT_TRUE(conn);
      held = conn->self();
    }
    // Other side absorbs the pair.
    auto peer = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                    .lock();
    EXPECT_TRUE(peer);

    EXPECT_TRUE(held->hangup());
    EXPECT_TRUE(
        WaitFor([&] { return closed.load(std::memory_order::acquire); }));
    // After hangup, our held ref is still valid (just not "open").
    EXPECT_FALSE(held->is_open());
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
MAKE_TEST_LIST(IouStreamConn_SendRecvString, IouStreamConn_MultipleStrings,
    IouStreamConn_SendRecvBuffer, IouStreamConn_BufferMoveOut,
    IouStreamConn_GracefulClose, IouStreamConn_HangupClose,
    IouStreamConn_OnDrain, IouStreamConn_WithState,
    IouStreamConn_FullBufferPartialConsume, IouStreamConn_MultishotRecv_Basic,
    IouStreamConn_MultishotRecv_MultipleMessages,
    IouStreamConn_MultishotRecv_TakeBuffer,
    IouStreamConn_MultishotRecv_StopAndResume,
    IouStreamConn_MultishotRecv_AcceptedConnsInheritMode,
    IouStreamConn_AccessorsLifecycle, IouStreamConn_Endpoints,
    IouStreamConn_BufSizeAccessors, IouStreamConn_PeerEofDeliversEmptyView,
    IouStreamConn_ShutdownSend, IouStreamConn_ShutdownRecv,
    IouStreamConn_StopAndResumeReceivingOnConn, IouStreamConn_ConnectFailure,
    IouStreamConn_ListenAcceptMultipleClients,
    IouStreamConn_CloseFlushesPendingSend, IouStreamConn_HangupIdempotent,
    IouStreamConn_SelfSharedPtr)
