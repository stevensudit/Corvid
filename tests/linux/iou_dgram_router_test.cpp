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
#include "corvid/proto/io_uring/iou_dgram_echo_server.h"
#include "corvid/proto/io_uring/iou_dgram_router.h"
#include "corvid/proto/io_uring/iou_dgram_session.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

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

// Shared test plugin pair keyed by `peer_addr`. The handlers and behavior
// switches all live in `state`, owned by the test, so each plugin instance
// just borrows a pointer.
class capture_protocol {
public:
  class session_plugin;

  struct state {
    std::function<void(const iou_loop::buffer&)> on_create;
    std::function<bool(iou_loop::buffer&&)> on_recv;
    std::function<bool(iou_loop::buffer&&)> on_sent;
    std::function<bool()> on_unregister;
    bool auto_create{true};
  };

  class router_plugin {
  public:
    using session_t = iou_dgram_session<session_plugin>;
    using router_t = iou_dgram_router<router_plugin>;
    using key_t = net_endpoint;

    explicit router_plugin(state* state = nullptr) noexcept : state_{state} {}

    [[nodiscard]] key_t extract(const iou_loop::buffer& buf) const noexcept {
      (void)this;
      return buf.peer_addr();
    }

    bool create_session(const iou_loop::buffer& buf, router_t& router) {
      if (state_ && state_->on_create) state_->on_create(buf);
      if (!state_ || !state_->auto_create) return false;
      (void)session_t::make(router, buf, state_);
      return true;
    }

  private:
    state* state_;
  };

  class session_plugin {
  public:
    using router_t = iou_dgram_router<router_plugin>;
    using session_t = iou_dgram_session<session_plugin>;

    session_plugin(router_t& router, session_t& session, state* state,
        net_endpoint preset_key = {}) noexcept
        : router_{router}, session_{session}, state_{state}, key_{preset_key} {
    }

    bool register_self(const iou_loop::buffer& buf) {
      // Empty-buffer sentinel => caller registers manually.
      if (!buf) return true;
      key_ = buf.peer_addr();
      return router_.add_session(key_, session_.self());
    }

    bool handle_recv(iou_loop::buffer&& buf) {
      if (state_ && state_->on_recv) return state_->on_recv(std::move(buf));
      return true;
    }

    bool handle_sent(iou_loop::buffer&& buf) {
      if (state_ && state_->on_sent) return state_->on_sent(std::move(buf));
      return true;
    }

    bool unregister_self() {
      if (state_ && state_->on_unregister) (void)state_->on_unregister();
      if (!key_.empty()) (void)router_.remove_session(key_);
      return true;
    }

  private:
    router_t& router_;
    session_t& session_;
    state* state_;
    net_endpoint key_;
  };
};

using capture_router = iou_dgram_router<capture_protocol::router_plugin>;
using capture_session = iou_dgram_session<capture_protocol::session_plugin>;
using capture_handle =
    iou_dgram_router_handle<capture_protocol::router_plugin>;

} // namespace

// NOLINTBEGIN(readability-function-cognitive-complexity)

TEST_CASE("BasicSendRecv", "[IouDgramRouter]") {
  // First packet hits create_session; payload arrives via the buffer.
  if (true) {
    std::atomic_bool received{false};
    std::string payload;

    capture_protocol::state recvA;
    recvA.on_recv = [&](iou_loop::buffer&& buf) {
      payload = std::string{buf.payload_view()};
      received.store(true, std::memory_order::release);
      return true;
    };
    capture_protocol::state stateB; // sender side: no auto-create needed
    stateB.auto_create = false;

    iou_loop_runner runner;

    auto routerA = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &recvA);
    CHECK(routerA);

    auto routerB = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateB);
    CHECK(routerB);

    const auto destA = routerA->local_endpoint();
    CHECK_FALSE(destA.empty());

    auto sessB = capture_session::make(*routerB.pointer(), iou_loop::buffer{},
        &stateB, destA);
    CHECK(routerB->add_session(destA, sessB));

    CHECK(sessB->send_to(destA, "buf-udp"));

    CHECK(WaitFor([&] { return received.load(std::memory_order::acquire); }));
    CHECK(payload == "buf-udp");
  }
}

TEST_CASE("OnSentReturnsBuffer", "[IouDgramRouter]") {
  // `handle_sent` fires with the buffer in success state after a send
  // completes.
  if (true) {
    std::atomic_bool sent{false};
    std::atomic_bool ok{false};
    std::atomic_int sent_bytes{0};

    // State objects must outlive the runner: router/session close runs on the
    // loop thread during runner.join, and unregister_self dereferences state.
    capture_protocol::state stateA; // receiver: just accept
    capture_protocol::state stateB;
    stateB.auto_create = false;
    stateB.on_sent = [&](iou_loop::buffer&& buf) {
      ok.store(buf.result().ok(), std::memory_order::release);
      sent_bytes.store(buf.result().value(), std::memory_order::release);
      sent.store(true, std::memory_order::release);
      return true;
    };

    iou_loop_runner runner;

    auto routerA = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateA);
    CHECK(routerA);

    auto routerB = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateB);
    CHECK(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = capture_session::make(*routerB.pointer(), iou_loop::buffer{},
        &stateB, destA);
    CHECK(routerB->add_session(destA, sessB));

    CHECK(sessB->send_to(destA, "ping"));
    CHECK(WaitFor([&] { return sent.load(std::memory_order::acquire); }));
    CHECK(ok.load(std::memory_order::acquire));
    CHECK(sent_bytes.load(std::memory_order::acquire) == 4);
  }
}

TEST_CASE("LazySession", "[IouDgramRouter]") {
  // create_session fires once per unknown key; subsequent packets bypass it
  // and dispatch directly to handle_recv.
  if (true) {
    std::atomic_int factory_calls{0};
    std::atomic_int data_calls{0};

    capture_protocol::state stateA;
    stateA.on_create = [&](const iou_loop::buffer&) { ++factory_calls; };
    stateA.on_recv = [&](iou_loop::buffer&&) {
      ++data_calls;
      return true;
    };
    capture_protocol::state stateB;
    stateB.auto_create = false;

    iou_loop_runner runner;

    auto routerA = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateA);
    CHECK(routerA);

    auto routerB = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateB);
    CHECK(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = capture_session::make(*routerB.pointer(), iou_loop::buffer{},
        &stateB, destA);
    CHECK(routerB->add_session(destA, sessB));

    CHECK(sessB->send_to(destA, "p1"));
    CHECK(WaitFor([&] {
      return factory_calls.load(std::memory_order::acquire) >= 1;
    }));

    CHECK(sessB->send_to(destA, "p2"));
    CHECK(sessB->send_to(destA, "p3"));
    CHECK(WaitFor([&] {
      return data_calls.load(std::memory_order::acquire) >= 2;
    }));

    CHECK(factory_calls.load(std::memory_order::acquire) == 1);
  }
}

TEST_CASE("DropOnNullFactory", "[IouDgramRouter]") {
  // `create_session` returns false (no session installed); every arriving
  // packet re-invokes it.
  if (true) {
    std::atomic_int factory_calls{0};

    capture_protocol::state stateA;
    stateA.auto_create = false;
    stateA.on_create = [&](const iou_loop::buffer&) { ++factory_calls; };
    capture_protocol::state stateB;
    stateB.auto_create = false;

    iou_loop_runner runner;

    auto routerA = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateA);
    CHECK(routerA);

    auto routerB = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateB);
    CHECK(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = capture_session::make(*routerB.pointer(), iou_loop::buffer{},
        &stateB, destA);
    CHECK(routerB->add_session(destA, sessB));

    CHECK(sessB->send_to(destA, "drop1"));
    CHECK(sessB->send_to(destA, "drop2"));

    CHECK(WaitFor(
        [&] { return factory_calls.load(std::memory_order::acquire) >= 2; },
        1s));
  }
}

namespace {

// Plugin pair keyed by a 32-bit ID extracted from the first 4 payload bytes.
class id_protocol {
public:
  class session_plugin;

  struct state {
    std::function<bool(std::uint32_t, iou_loop::buffer&&)> on_recv;
  };

  class router_plugin {
  public:
    using session_t = iou_dgram_session<session_plugin>;
    using key_t = std::uint32_t;

    explicit router_plugin(state* state = nullptr) noexcept : state_{state} {}

    [[nodiscard]] key_t extract(const iou_loop::buffer& buf) const noexcept {
      (void)this;
      const auto v = buf.payload_view();
      if (v.size() < 4) return 0;
      key_t id{};
      std::memcpy(&id, v.data(), 4);
      return id;
    }

    bool create_session(const iou_loop::buffer& buf,
        iou_dgram_router<router_plugin>& router) {
      (void)session_t::make(router, buf, state_, extract(buf));
      return true;
    }

  private:
    state* state_;
  };

  class session_plugin {
  public:
    using router_t = iou_dgram_router<router_plugin>;
    using session_t = iou_dgram_session<session_plugin>;

    session_plugin(router_t& router, session_t& session, state* state,
        std::uint32_t key) noexcept
        : router_{router}, session_{session}, state_{state}, key_{key} {}

    bool register_self(const iou_loop::buffer&) {
      return router_.add_session(key_, session_.self());
    }
    bool handle_recv(iou_loop::buffer&& buf) {
      if (state_ && state_->on_recv)
        return state_->on_recv(key_, std::move(buf));
      return true;
    }
    bool handle_sent(iou_loop::buffer&&) noexcept {
      (void)this;
      return true;
    }
    bool unregister_self() { return router_.remove_session(key_); }

  private:
    router_t& router_;
    session_t& session_;
    state* state_;
    std::uint32_t key_;
  };
};

} // namespace

TEST_CASE("CustomKey", "[IouDgramRouter]") {
  // Routing by a 32-bit ID extracted from the first 4 payload bytes,
  // independent of peer_addr.
  if (true) {
    using my_router = iou_dgram_router<id_protocol::router_plugin>;
    static_assert(std::same_as<my_router::key_t, std::uint32_t>);

    std::atomic_int sess1_data{0};
    std::atomic_int sess2_data{0};

    id_protocol::state stateA;
    stateA.on_recv = [&](std::uint32_t key, iou_loop::buffer&&) {
      if (key == 1U) ++sess1_data;
      if (key == 2U) ++sess2_data;
      return true;
    };
    capture_protocol::state stateB;
    stateB.auto_create = false;

    iou_loop_runner runner;

    auto routerA = iou_dgram_router_handle<id_protocol::router_plugin>::bind(
        *runner.loop(), net_endpoint::loopback_v4(), shot_type::single,
        &stateA);
    CHECK(routerA);

    auto routerB = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateB);
    CHECK(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = capture_session::make(*routerB.pointer(), iou_loop::buffer{},
        &stateB, destA);
    CHECK(routerB->add_session(destA, sessB));

    auto send_id = [&](std::uint32_t id, std::string_view tail) {
      std::string p(4 + tail.size(), '\0');
      std::memcpy(p.data(), &id, 4);
      std::memcpy(p.data() + 4, tail.data(), tail.size());
      return sessB->send_to(destA, p);
    };

    CHECK(send_id(1, "a"));
    CHECK(send_id(2, "b"));
    CHECK(send_id(1, "c"));
    CHECK(send_id(2, "d"));
    CHECK(send_id(1, "e"));

    CHECK(WaitFor([&] {
      return sess1_data.load(std::memory_order::acquire) >= 2 &&
             sess2_data.load(std::memory_order::acquire) >= 1;
    }));
  }
}

namespace {

// Plugin pair demonstrating per-session state living directly on the
// session plugin (no separate `_with_state` template needed).
class counting_protocol {
public:
  class session_plugin;

  struct state {
    std::atomic_int observed{0};
  };

  class router_plugin {
  public:
    using session_t = iou_dgram_session<session_plugin>;
    using key_t = net_endpoint;

    explicit router_plugin(state* st = nullptr) noexcept : state_{st} {}

    [[nodiscard]] key_t extract(const iou_loop::buffer& b) const noexcept {
      (void)this;
      return b.peer_addr();
    }

    bool create_session(const iou_loop::buffer& b,
        iou_dgram_router<router_plugin>& r) {
      (void)session_t::make(r, b, state_);
      return true;
    }

  private:
    state* state_;
  };

  class session_plugin {
  public:
    using router_t = iou_dgram_router<router_plugin>;
    using session_t = iou_dgram_session<session_plugin>;

    session_plugin(router_t& r, session_t& s, state* st) noexcept
        : router_{r}, session_{s}, state_{st} {}

    bool register_self(const iou_loop::buffer& first) {
      key_ = first.peer_addr();
      return router_.add_session(key_, session_.self());
    }
    bool handle_recv(iou_loop::buffer&&) {
      ++recv_count_;
      if (state_)
        state_->observed.store(recv_count_, std::memory_order::release);
      return true;
    }
    bool handle_sent(iou_loop::buffer&&) noexcept {
      (void)this;
      return true;
    }
    bool unregister_self() { return router_.remove_session(key_); }

  private:
    router_t& router_;
    session_t& session_;
    state* state_;
    net_endpoint key_;
    int recv_count_{0};
  };
};

} // namespace

TEST_CASE("WithPluginState", "[IouDgramRouter]") {
  // SessionPlugin is the per-session state container.
  if (true) {
    counting_protocol::state stateA;
    capture_protocol::state stateB;
    stateB.auto_create = false;

    iou_loop_runner runner;

    auto routerA =
        iou_dgram_router_handle<counting_protocol::router_plugin>::bind(
            *runner.loop(), net_endpoint::loopback_v4(), shot_type::single,
            &stateA);
    CHECK(routerA);

    auto routerB = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateB);
    CHECK(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = capture_session::make(*routerB.pointer(), iou_loop::buffer{},
        &stateB, destA);
    CHECK(routerB->add_session(destA, sessB));

    CHECK(sessB->send_to(destA, "p1")); // create_session only
    CHECK(sessB->send_to(destA, "p2")); // handle_recv, count=1
    CHECK(sessB->send_to(destA, "p3")); // handle_recv, count=2

    CHECK(WaitFor([&] {
      return stateA.observed.load(std::memory_order::acquire) >= 2;
    }));
  }
}

TEST_CASE("Multishot", "[IouDgramRouter]") {
  // Multishot recvmsg path: a burst of datagrams all arrive.
  if (true) {
    std::atomic_int delivered{0};

    capture_protocol::state stateA;
    stateA.on_create = [&](const iou_loop::buffer&) { ++delivered; };
    stateA.on_recv = [&](iou_loop::buffer&&) {
      ++delivered;
      return true;
    };
    capture_protocol::state stateB;
    stateB.auto_create = false;

    iou_loop_runner runner;

    auto routerA = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::multi, &stateA);
    CHECK(routerA);

    auto routerB = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateB);
    CHECK(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = capture_session::make(*routerB.pointer(), iou_loop::buffer{},
        &stateB, destA);
    CHECK(routerB->add_session(destA, sessB));

    constexpr int total = 16;
    for (int i = 0; i < total; ++i) CHECK(sessB->send_to(destA, "burst"));

    CHECK(WaitFor(
        [&] { return delivered.load(std::memory_order::acquire) >= total; },
        2s));
  }
}

TEST_CASE("OnClose", "[IouDgramRouter]") {
  // Closing the router fires `unregister_self` on registered sessions
  // exactly once.
  if (true) {
    std::atomic_int session_closed{0};

    capture_protocol::state stateA;
    stateA.on_unregister = [&] {
      ++session_closed;
      return true;
    };

    iou_loop_runner runner;

    auto routerA = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateA);
    CHECK(routerA);

    const auto preset_key = net_endpoint::loopback_v4(/*port=*/12345);
    auto sess = capture_session::make(*routerA.pointer(), iou_loop::buffer{},
        &stateA, preset_key);
    CHECK(routerA->add_session(preset_key, sess));

    // `add_session` posts to the loop thread; `close` flips `open_`
    // synchronously. Without a barrier, the queued add can run after that
    // flip, see `!is_open()`, and silently drop. Post a sentinel and wait:
    // FIFO guarantees the add ran first.
    std::atomic_bool synced{false};
    CHECK(runner.loop()->execute_or_post([&] {
      synced.store(true, std::memory_order::release);
      return true;
    }));
    CHECK(WaitFor([&] { return synced.load(std::memory_order::acquire); }));

    CHECK(routerA.close());
    CHECK(WaitFor([&] {
      return session_closed.load(std::memory_order::acquire) >= 1;
    }));
    CHECK(session_closed.load(std::memory_order::acquire) == 1);
  }
}

TEST_CASE("RoundTrip", "[IouDgramEchoProtocol]") {
  // `iou_dgram_echo_server` bounces each datagram back to its sender.
  if (true) {
    std::atomic_bool received{false};
    std::string echoed;

    capture_protocol::state stateB;
    stateB.auto_create = false;
    stateB.on_recv = [&](iou_loop::buffer&& b) {
      echoed = std::string{b.payload_view()};
      received.store(true, std::memory_order::release);
      return true;
    };

    iou_loop_runner runner;

    auto echoA = iou_dgram_echo_server::bind(*runner.loop(),
        net_endpoint::loopback_v4());
    CHECK(echoA);

    auto routerB = capture_handle::bind(*runner.loop(),
        net_endpoint::loopback_v4(), shot_type::single, &stateB);
    CHECK(routerB);

    const auto destA = echoA.local_endpoint();
    auto sessB = capture_session::make(*routerB.pointer(), iou_loop::buffer{},
        &stateB, destA);
    CHECK(routerB->add_session(destA, sessB));

    CHECK(sessB->send_to(destA, "hello-echo"));

    CHECK(WaitFor([&] { return received.load(std::memory_order::acquire); },
        1s));
    CHECK(echoed == "hello-echo");
  }
}

// NOLINTEND(readability-function-cognitive-complexity)
