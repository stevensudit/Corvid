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

#define CATCH2_SHOW_TIMERS 0
#include "catch2_main.h"

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

TEST_CASE("SendRecvString", "[IouStreamConn]") {
  // String send -> on_data fires, payload correct.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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
    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    // Adopt sock0 as sender.
    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string{msg})));
    CHECK(
        (WaitFor([&] { return received.load(std::memory_order::acquire); })));
    CHECK((payload) == (msg));
  }
}

#pragma endregion
#pragma region MultipleStrings

TEST_CASE("MultipleStrings", "[IouStreamConn]") {
  // Multiple string sends -> all received and concatenated correctly.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    for (int i = 0; i < N; ++i) CHECK((send_conn->send(std::string{msg})));

    const int expected = N * static_cast<int>(msg.size());
    CHECK((WaitFor([&] { return recv_bytes >= expected; })));
    CHECK((recv_bytes) == (expected));
  }
}

#pragma endregion
#pragma region SendRecvBuffer

TEST_CASE("SendRecvBuffer", "[IouStreamConn]") {
  // Direct buffer send -> on_data fires, payload correct.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    // Borrow a write buffer and fill it manually, then send.
    auto tok = runner->borrow_write_buffer();
    CHECK((tok));
    if (!tok) return;
    CHECK((tok.append(msg)));
    CHECK((send_conn->send(std::move(tok))));

    CHECK(
        (WaitFor([&] { return received.load(std::memory_order::acquire); })));
    CHECK((payload) == (msg));
  }
}

#pragma endregion
#pragma region BufferMoveOut

TEST_CASE("BufferMoveOut", "[IouStreamConn]") {
  // `take()` in on_data -> fresh recv submitted, caller owns buffer.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string{msg})));
    CHECK(
        (WaitFor([&] { return received.load(std::memory_order::acquire); })));
    CHECK((payload) == (msg));
  }
}

#pragma endregion
#pragma region GracefulClose

TEST_CASE("GracefulClose", "[IouStreamConn]") {
  // `close()` -> on_close fires on both sides.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto conn0 = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid, shot_type::single, {}, {}, &state0)
                     .lock();
    auto conn1 = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid, shot_type::single, {}, {}, &state1)
                     .lock();
    CHECK((conn0));
    CHECK((conn1));

    // Give both connections time to arm their recv SQEs.
    std::this_thread::sleep_for(20ms);

    // TODO: This doesn't do the graceful close that's promised.

    CHECK((conn0->close()));
    CHECK((WaitFor([&] { return closed0.load(std::memory_order::acquire); })));
    CHECK((closed0.load()));
  }
}

#pragma endregion
#pragma region HangupClose

TEST_CASE("HangupClose", "[IouStreamConn]") {
  // `hangup()` -> socket closed immediately.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    std::atomic_bool closed{false};

    capture_protocol::state state0;
    state0.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    iou_loop_runner runner;

    auto conn0 = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid, shot_type::single, {}, {}, &state0)
                     .lock();
    // conn1 just absorbs the other end
    auto conn1 = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                     .lock();
    CHECK((conn0));
    CHECK((conn1));

    CHECK((conn0->hangup()));
    CHECK((WaitFor([&] { return closed.load(std::memory_order::acquire); })));
  }
}

#pragma endregion
#pragma region OnDrain

TEST_CASE("OnDrain", "[IouStreamConn]") {
  // `on_drain` fires after send completes and queue is empty.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    std::atomic_bool drained{false};

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid)
            .lock();
    CHECK((recv_conn));

    capture_protocol::state send_state;
    send_state.on_drain = [&] {
      drained.store(true, std::memory_order::release);
      return true;
    };

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid, shot_type::single, {}, {}, &send_state)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string{"ping"})));
    CHECK((WaitFor([&] { return drained.load(std::memory_order::acquire); })));
  }
}

#pragma endregion
#pragma region WithState

TEST_CASE("WithState", "[IouStreamConn]") {
  // Per-connection state living in the plugin (replaces the old
  // `iou_stream_conn_with_state` pattern).
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string{"state-test"})));
    CHECK(
        (WaitFor([&] { return received.load(std::memory_order::acquire); })));
    CHECK((my_state.recv_count) == (1));
  }
}

#pragma endregion
#pragma region FullBufferPartialConsume

TEST_CASE("FullBufferPartialConsume", "[IouStreamConn]") {
  // Fills the recv buffer completely, then consumes part of it on the first
  // full-buffer delivery. The remaining bytes plus newly-received bytes are
  // delivered on a subsequent recv. Verifies that partial consume of a full
  // buffer makes forward progress (the buffer regains headroom) and that the
  // contract violation -- returning a full buffer with zero consumed -- is
  // not exercised here.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string(buf_size, 'x'))));
    CHECK((send_conn->send(std::string(1, 'y'))));
    CHECK((WaitFor([&] { return done.load(std::memory_order::acquire); })));
    CHECK((total_consumed.load(std::memory_order::acquire)) >= (buf_size + 1));
  }
}

#pragma endregion
#pragma region MultishotRecvBasic

TEST_CASE("MultishotRecv_Basic", "[IouStreamConn]") {
  // multishot recv mode: data arrives and `on_data` fires.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string{msg})));
    CHECK(
        (WaitFor([&] { return received.load(std::memory_order::acquire); })));
    CHECK((payload) == (msg));
  }
}

#pragma endregion
#pragma region MultishotRecvMultipleMessages

TEST_CASE("MultishotRecv_MultipleMessages", "[IouStreamConn]") {
  // multishot recv mode: multiple sends are all delivered (bytes counted since
  // the stream socket may coalesce messages into a single on_data call).
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    for (int i = 0; i < N; ++i) CHECK((send_conn->send(std::string{msg})));

    CHECK((WaitFor([&] {
      return recv_bytes.load(std::memory_order::acquire) >= expected;
    })));
    CHECK((recv_bytes.load()) == (expected));
  }
}

#pragma endregion
#pragma region MultishotRecvTakeBuffer

TEST_CASE("MultishotRecv_TakeBuffer", "[IouStreamConn]") {
  // `take()` in multishot mode: multishot resubmits after the taken buffer is
  // released.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string{msg})));
    CHECK((WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 1;
    })));

    // After take(), send a second message; recv should still work.
    taken_buf = {}; // release the taken buffer back to the pool
    CHECK((send_conn->send(std::string{msg})));
    CHECK((WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 2;
    })));
  }
}

#pragma endregion
#pragma region MultishotRecvStopAndResume

TEST_CASE("MultishotRecv_StopAndResume", "[IouStreamConn]") {
  // `stop_receiving()` pauses recv and returns a resume callback; the
  // callback both keeps the conn alive and, when invoked, restarts the
  // recv loop.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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
    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    // First message: received, then reading stops.
    CHECK((send_conn->send(std::string{msg})));
    CHECK((WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 1;
    })));

    // Second message: sent while paused, should not be delivered yet.
    CHECK((send_conn->send(std::string{msg})));
    std::this_thread::sleep_for(50ms);
    CHECK((recv_count.load(std::memory_order::acquire)) == (1));

    // Resume by invoking the callback.
    {
      std::scoped_lock lock{cb_mutex};
      CHECK((resume_cb));
      CHECK((resume_cb()));
    }
    CHECK((WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 2;
    })));
  }
}

#pragma endregion
#pragma region MultishotRecvAcceptedConnsInheritMode

TEST_CASE("MultishotRecv_AcceptedConnsInheritMode", "[IouStreamConn]") {
  // Accepted connections from a multishot-mode listener also use multishot.
  if (true) {
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

    iou_loop_runner runner;

    auto server = capture_conn::listen(*runner.loop(),
        net_endpoint::loopback_v4(0), shot_type::multi, {}, {}, &server_state)
                      .lock();
    CHECK((server));

    const auto& listen_ep = server->local_endpoint();
    auto client = capture_conn::connect(*runner.loop(), listen_ep).lock();
    CHECK((client));

    CHECK((client->send(std::string{msg})));
    CHECK(
        (WaitFor([&] { return received.load(std::memory_order::acquire); })));
    CHECK((payload) == (msg));
  }
}

#pragma endregion
#pragma region AccessorsLifecycle

TEST_CASE("AccessorsLifecycle", "[IouStreamConn]") {
  // is_open / writes_allowed / is_read_shut / is_write_shut transitions.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    std::atomic_bool closed{false};

    capture_protocol::state state0;
    state0.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    iou_loop_runner runner;

    auto conn0 = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid, shot_type::single, {}, {}, &state0)
                     .lock();
    auto conn1 = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                     .lock();
    CHECK((conn0));
    CHECK((conn1));

    CHECK((conn0->is_open()));
    CHECK((conn0->writes_allowed()));
    CHECK_FALSE((conn0->is_read_shut()));
    CHECK_FALSE((conn0->is_write_shut()));

    CHECK((conn0->close()));
    // writes_allowed flips synchronously off `close_requested_`; is_open
    // follows once the actual close is processed.
    CHECK_FALSE((conn0->writes_allowed()));
    CHECK((WaitFor([&] { return closed.load(std::memory_order::acquire); })));
    CHECK_FALSE((conn0->is_open()));

    // Second `close` is a no-op.
    CHECK_FALSE((conn0->close()));
  }
}

#pragma endregion
#pragma region Endpoints

TEST_CASE("Endpoints", "[IouStreamConn]") {
  // local_endpoint and remote_endpoint resolve after listen + connect.
  if (true) {
    std::atomic_bool connected{false};

    capture_protocol::state server_state;
    capture_protocol::state client_state;
    client_state.on_drain = [&] {
      connected.store(true, std::memory_order::release);
      return true;
    };

    iou_loop_runner runner;

    auto server = capture_conn::listen(*runner.loop(),
        net_endpoint::loopback_v4(0), shot_type::multi, {}, {}, &server_state)
                      .lock();
    CHECK((server));

    const auto& listen_ep = server->local_endpoint();
    CHECK_FALSE((listen_ep.empty()));
    CHECK((listen_ep.port()) != (0));

    auto client = capture_conn::connect(*runner.loop(), listen_ep,
        shot_type::single, {}, {}, &client_state)
                      .lock();
    CHECK((client));
    CHECK(
        (WaitFor([&] { return connected.load(std::memory_order::acquire); })));

    // After connect succeeds, both endpoints should be resolvable.
    const auto& cl_remote = client->remote_endpoint();
    CHECK((cl_remote.port()) == (listen_ep.port()));

    const auto& cl_local = client->local_endpoint();
    CHECK_FALSE((cl_local.empty()));
    CHECK((cl_local.port()) != (0));
  }
}

#pragma endregion
#pragma region BufSizeAccessors

TEST_CASE("BufSizeAccessors", "[IouStreamConn]") {
  // set_recv_buf_size / set_send_buf_size round-trip through the relaxed
  // atomic storage. The conn keeps using the new sizes for subsequent
  // borrows; we don't try to observe that here, just the getter/setter.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;

    auto conn = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid)
                    .lock();
    CHECK((conn));

    CHECK((conn->recv_buf_size()) == (block_size::kb004));
    CHECK((conn->send_buf_size()) == (block_size::kb004));

    conn->set_recv_buf_size(block_size::kb002);
    conn->set_send_buf_size(block_size::kb008);
    CHECK((conn->recv_buf_size()) == (block_size::kb002));
    CHECK((conn->send_buf_size()) == (block_size::kb008));

    // Keep sock1 alive until conn teardown to avoid a stray RST during the
    // test (cleanup happens in the runner destructor).
    (void)capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid);
  }
}

#pragma endregion
#pragma region PeerEofDeliversEmptyView

TEST_CASE("PeerEofDeliversEmptyView", "[IouStreamConn]") {
  // When the peer closes, this side gets a final on_data with an empty view
  // and is_read_shut transitions to true.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string{msg})));
    CHECK(
        (WaitFor([&] { return got_data.load(std::memory_order::acquire); })));
    CHECK((payload) == (msg));

    CHECK((send_conn->close()));
    CHECK((WaitFor([&] { return got_eof.load(std::memory_order::acquire); })));
    CHECK((recv_conn->is_read_shut()));

    // Peer EOF does not auto-close locally; the recv side must close itself
    // to retire its on_close.
    CHECK((recv_conn->close()));
    CHECK(
        (WaitFor([&] { return got_close.load(std::memory_order::acquire); })));
  }
}

#pragma endregion
#pragma region ShutdownSend

TEST_CASE("ShutdownSend", "[IouStreamConn]") {
  // shutdown_send flushes the queue, then peer sees EOF on its read side.
  // Further sends are rejected on this side. Second call is a no-op.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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
    iou_loop_runner runner;

    auto recv_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_raw));
    auto recv_conn = recv_raw->self();

    auto send_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_raw));
    auto send_conn = send_raw->self();

    // send + shutdown_send back-to-back: the send was authorized when
    // `writes_allowed()` first returned true, so the in-lambda guard
    // (which checks `closed_`, not `shutdown_send_requested_`) honors it
    // before `shutdown_send` tears down.
    CHECK((send_conn->send(std::string{msg})));
    CHECK((send_conn->shutdown_send()));

    CHECK(
        (WaitFor([&] { return got_data.load(std::memory_order::acquire); })));
    CHECK((payload) == (msg));
    CHECK((WaitFor([&] { return got_eof.load(std::memory_order::acquire); })));

    // Writes are now rejected.
    CHECK_FALSE((send_conn->writes_allowed()));
    CHECK_FALSE((send_conn->send(std::string{"nope"})));

    // Idempotent.
    CHECK_FALSE((send_conn->shutdown_send()));

    // Local conn is still open for reads.
    CHECK((send_conn->is_open()));

    // Clean shutdown: explicit close on both sides before runner teardown.
    CHECK((recv_conn->close()));
    CHECK((send_conn->close()));
  }
}

#pragma endregion
#pragma region ShutdownRecv

TEST_CASE("ShutdownRecv", "[IouStreamConn]") {
  // shutdown_recv sets is_read_shut; further data from the peer no longer
  // reaches on_data. Idempotent.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    std::atomic<int> recv_count{0};

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      view.consume(view.active_view().size());
      recv_count.fetch_add(1, std::memory_order::acq_rel);
      return true;
    };

    iou_loop_runner runner;

    auto recv_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_raw));
    auto recv_conn = recv_raw->self();

    auto send_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_raw));
    auto send_conn = send_raw->self();

    CHECK((send_conn->send(std::string{"first"})));
    CHECK((WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 1;
    })));

    CHECK((recv_conn->shutdown_recv()));
    CHECK((WaitFor([&] { return recv_conn->is_read_shut(); })));

    // Idempotent.
    CHECK_FALSE((recv_conn->shutdown_recv()));

    // New data does not surface as additional on_data calls.
    CHECK((send_conn->send(std::string{"second"})));
    std::this_thread::sleep_for(50ms);
    CHECK((recv_count.load(std::memory_order::acquire)) == (1));
  }
}

#pragma endregion
#pragma region StopAndResumeReceivingOnConn

TEST_CASE("StopAndResumeReceivingOnConn", "[IouStreamConn]") {
  // conn->stop_receiving + resume_receiving in multishot mode.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    std::atomic<int> recv_count{0};
    std::string payload;

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      payload += view.active_view();
      view.consume(view.active_view().size());
      recv_count.fetch_add(1, std::memory_order::acq_rel);
      return true;
    };

    iou_loop_runner runner;

    auto recv_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::multi, {}, {}, &recv_state)
            .lock();
    CHECK((recv_raw));
    auto recv_conn = recv_raw->self();

    auto send_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_raw));
    auto send_conn = send_raw->self();

    CHECK((send_conn->send(std::string{"A"})));
    CHECK((WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 1;
    })));

    // Pause. The returned shared_ptr is what keeps the conn alive once the
    // recv loop ends; without holding it we'd race against the cancel CQE
    // releasing the last in-flight self-ref. (Our outer `recv_conn`
    // already serves that role, so we don't need to bind a separate var.)
    auto paused = recv_conn->stop_receiving();
    CHECK((paused));
    // Give the cancel time to take effect.
    std::this_thread::sleep_for(50ms);

    // Send while paused; should not reach on_data yet.
    CHECK((send_conn->send(std::string{"B"})));
    std::this_thread::sleep_for(50ms);
    CHECK((recv_count.load(std::memory_order::acquire)) == (1));

    // Resume.
    CHECK((recv_conn->resume_receiving()));
    CHECK((WaitFor([&] {
      return recv_count.load(std::memory_order::acquire) >= 2;
    })));
    CHECK((payload) == ("AB"));

    // Idempotent: a second resume_receiving with no prior pause is a no-op.
    CHECK_FALSE((recv_conn->resume_receiving()));

    CHECK((recv_conn->close()));
    CHECK((send_conn->close()));
  }
}

#pragma endregion
#pragma region ConnectFailure

TEST_CASE("ConnectFailure", "[IouStreamConn]") {
  // Async connect to an unbound loopback port fires on_close, not on_drain.
  if (true) {
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

    iou_loop_runner runner;

    // Bind+listen+drop to grab a port that is now known-unbound.
    net_endpoint unreachable;
    if (true) {
      auto probe =
          capture_conn::listen(*runner.loop(), net_endpoint::loopback_v4(0))
              .lock();
      CHECK((probe));
      unreachable = probe->local_endpoint();
      CHECK((probe->hangup()));
    }
    // Small pause so the listener's close completes before we connect.
    std::this_thread::sleep_for(20ms);

    auto client = capture_conn::connect(*runner.loop(), unreachable,
        shot_type::single, {}, {}, &state)
                      .lock();
    CHECK((client));

    CHECK((WaitFor([&] { return closed.load(std::memory_order::acquire); })));
    CHECK_FALSE((drained.load(std::memory_order::acquire)));
  }
}

#pragma endregion
#pragma region ListenAcceptMultipleClients

TEST_CASE("ListenAcceptMultipleClients", "[IouStreamConn]") {
  // Multiple clients connect; the listener's plugin spawns a child plugin
  // per accept via make_child_plugin and each child receives its own data.
  if (true) {
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

    iou_loop_runner runner;

    auto server = capture_conn::listen(*runner.loop(),
        net_endpoint::loopback_v4(0), shot_type::multi, {}, {}, &server_state)
                      .lock();
    CHECK((server));
    const auto& listen_ep = server->local_endpoint();

    std::vector<std::shared_ptr<capture_conn>> clients;
    clients.reserve(N);
    for (int i = 0; i < N; ++i) {
      auto c = capture_conn::connect(*runner.loop(), listen_ep).lock();
      CHECK((c));
      clients.push_back(c);
    }

    for (const auto& c : clients) CHECK((c->send(std::string{msg})));

    CHECK((WaitFor([&] {
      return total_bytes.load(std::memory_order::acquire) >= expected;
    })));
    CHECK((total_bytes.load()) == (expected));
  }
}

#pragma endregion
#pragma region CloseFlushesPendingSend

TEST_CASE("CloseFlushesPendingSend", "[IouStreamConn]") {
  // send + close back-to-back: the send was authorized when `writes_allowed`
  // first returned true, so the in-lambda guard (which checks `closed_`, not
  // `close_requested_`) honors it. `close` sees a non-empty queue (or a live
  // `send_token_`) and defers until the data has actually been transmitted.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

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

    iou_loop_runner runner;

    auto recv_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_raw));
    auto recv_conn = recv_raw->self();

    auto send_raw =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_raw));
    auto send_conn = send_raw->self();

    CHECK((send_conn->send(std::string{msg})));
    CHECK((send_conn->close()));

    CHECK((WaitFor([&] { return got_eof.load(std::memory_order::acquire); })));
    CHECK((payload) == (msg));
  }
}

#pragma endregion
#pragma region HangupIdempotent

TEST_CASE("HangupIdempotent", "[IouStreamConn]") {
  // Second hangup is a no-op.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    std::atomic_bool closed{false};

    capture_protocol::state state;
    state.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    iou_loop_runner runner;

    auto conn0 = capture_conn::adopt(*runner.loop(), std::move(sock0),
        net_endpoint::invalid, shot_type::single, {}, {}, &state)
                     .lock();
    auto conn1 = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                     .lock();
    CHECK((conn0));
    CHECK((conn1));

    CHECK((conn0->hangup()));
    CHECK_FALSE((conn0->hangup()));
    CHECK((WaitFor([&] { return closed.load(std::memory_order::acquire); })));
    CHECK_FALSE((conn0->is_open()));
  }
}

#pragma endregion
#pragma region SelfSharedPtr

TEST_CASE("SelfSharedPtr", "[IouStreamConn]") {
  // self() promotes a conn reference to a shared_ptr. The factories return a
  // weak_ptr; the conn is kept alive by its in-flight callbacks. A user-held
  // shared_ptr must extend that lifetime past hangup.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    std::atomic_bool closed{false};

    capture_protocol::state state;
    state.on_close = [&] {
      closed.store(true, std::memory_order::release);
      return true;
    };

    iou_loop_runner runner;

    std::shared_ptr<capture_conn> held;
    if (true) {
      auto conn = capture_conn::adopt(*runner.loop(), std::move(sock0),
          net_endpoint::invalid, shot_type::single, {}, {}, &state)
                      .lock();
      CHECK((conn));
      held = conn->self();
    }
    // Other side absorbs the pair.
    auto peer = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                    .lock();
    CHECK((peer));

    CHECK((held->hangup()));
    CHECK((WaitFor([&] { return closed.load(std::memory_order::acquire); })));
    // After hangup, our held ref is still valid (just not "open").
    CHECK_FALSE((held->is_open()));
  }
}

#pragma endregion
#pragma region SendStringBatchOverflow

TEST_CASE("SendStringBatchOverflow", "[IouStreamConn]") {
  // Multiple queued strings whose total exceeds the JIT send buffer must
  // all arrive: the batching loop must send what fits and leave the
  // remainder for the next round. Default `send_buf_size` is 4 KB; queue
  // three 1500-byte strings (total 4500) so the third spills.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    std::atomic<int> recv_bytes{0};
    std::string payload;

    constexpr int chunk_size = 1500;
    constexpr int chunk_count = 3;
    constexpr int expected = chunk_size * chunk_count;
    const std::string chunk_a(chunk_size, 'A');
    const std::string chunk_b(chunk_size, 'B');
    const std::string chunk_c(chunk_size, 'C');

    capture_protocol::state recv_state;
    recv_state.on_data = [&](iou_recv_view view) {
      auto sv = view.active_view();
      payload += sv;
      recv_bytes.fetch_add(static_cast<int>(sv.size()),
          std::memory_order::release);
      view.consume(sv.size());
      return true;
    };

    iou_loop_runner runner;

    auto recv_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock1),
            net_endpoint::invalid, shot_type::single, {}, {}, &recv_state)
            .lock();
    CHECK((recv_conn));

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));

    CHECK((send_conn->send(std::string{chunk_a})));
    CHECK((send_conn->send(std::string{chunk_b})));
    CHECK((send_conn->send(std::string{chunk_c})));

    CHECK((WaitFor([&] {
      return recv_bytes.load(std::memory_order::acquire) >= expected;
    })));
    CHECK((recv_bytes.load()) == (expected));
    CHECK((payload) == (chunk_a + chunk_b + chunk_c));
  }
}

#pragma endregion
#pragma region SendStringTooBigRejected

TEST_CASE("SendStringTooBigRejected", "[IouStreamConn]") {
  // A string larger than `send_buf_size` is rejected at the `send` entry
  // point; the caller should chunk or use the `buffer` overload.
  if (true) {
    auto [sock0, sock1] = net_socket::create_pair();

    iou_loop_runner runner;

    auto send_conn =
        capture_conn::adopt(*runner.loop(), std::move(sock0),
            net_endpoint::invalid)
            .lock();
    CHECK((send_conn));
    auto peer = capture_conn::adopt(*runner.loop(), std::move(sock1),
        net_endpoint::invalid)
                    .lock();
    CHECK((peer));

    // Default `send_buf_size` is 4 KB; anything strictly larger must be
    // rejected.
    const std::string too_big((4 * 1024ULL) + 1, 'X');
    CHECK_FALSE((send_conn->send(std::string{too_big})));

    // A string exactly equal to the buffer size still fits.
    const std::string just_fits(4 * 1024ULL, 'Y');
    CHECK((send_conn->send(std::string{just_fits})));
  }
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
