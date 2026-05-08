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
#include "../corvid/proto/io_uring/iou_dgram_router.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
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

void IouDgramRouter_BasicSendRecv() {
  // First packet hits `on_new_session`; payload arrives via the buffer.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    using router_t = iou_dgram_router_ptr::router_t;

    auto routerA = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        router_t::handlers{
            .on_new_session =
                [&](router_t& r, const net_endpoint& key,
                    iou_loop::buffer&& buf) -> router_t::session_ptr {
              payload = std::string{buf.payload_view()};
              received.store(true, std::memory_order::release);
              return r.make_session(key, {});
            }});
    EXPECT_TRUE(routerA);

    auto routerB =
        iou_dgram_router_ptr::bind(runner.loop(), net_endpoint::loopback_v4());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    EXPECT_FALSE(destA.empty());

    auto sessB = routerB->make_session(destA, {});
    EXPECT_TRUE(routerB->add_session(sessB));

    EXPECT_TRUE(sessB->send_to(destA, "buf-udp"));

    EXPECT_TRUE(
        WaitFor([&] { return received.load(std::memory_order::acquire); }));
    EXPECT_EQ(payload, "buf-udp");
  }
}

void IouDgramRouter_OnSentReturnsBuffer() {
  // `on_sent` fires with the buffer in success state after a send completes.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool sent{false};
    std::atomic_bool ok{false};
    std::atomic_int sent_bytes{0};

    using router_t = iou_dgram_router_ptr::router_t;

    auto routerA = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        router_t::handlers{
            .on_new_session =
                [&](router_t& r, const net_endpoint& key, iou_loop::buffer&&)
                -> router_t::session_ptr { return r.make_session(key, {}); }});
    EXPECT_TRUE(routerA);

    auto routerB =
        iou_dgram_router_ptr::bind(runner.loop(), net_endpoint::loopback_v4());
    EXPECT_TRUE(routerB);

    auto sessB = routerB->make_session(routerA->local_endpoint(),
        router_t::session_handlers{
            .on_sent = [&](router_t::session_t&, iou_loop::buffer&& buf) {
              ok.store(buf.result().ok(), std::memory_order::release);
              sent_bytes.store(buf.result().value(),
                  std::memory_order::release);
              sent.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(routerB->add_session(sessB));

    EXPECT_TRUE(sessB->send_to(routerA->local_endpoint(), "ping"));
    EXPECT_TRUE(
        WaitFor([&] { return sent.load(std::memory_order::acquire); }));
    EXPECT_TRUE(ok.load(std::memory_order::acquire));
    EXPECT_EQ(sent_bytes.load(std::memory_order::acquire), 4);
  }
}

void IouDgramRouter_LazySession() {
  // Factory fires once per unknown key; subsequent packets bypass the
  // factory and dispatch directly to `on_data`.
  if (true) {
    iou_loop_runner runner;
    std::atomic_int factory_calls{0};
    std::atomic_int data_calls{0};

    using router_t = iou_dgram_router_ptr::router_t;

    auto routerA = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        router_t::handlers{
            .on_new_session =
                [&](router_t& r, const net_endpoint& key,
                    iou_loop::buffer&&) -> router_t::session_ptr {
              ++factory_calls;
              return r.make_session(key,
                  router_t::session_handlers{
                      .on_data =
                          [&](router_t::session_t&, iou_loop::buffer&&) {
                            ++data_calls;
                            return true;
                          }});
            }});
    EXPECT_TRUE(routerA);

    auto routerB =
        iou_dgram_router_ptr::bind(runner.loop(), net_endpoint::loopback_v4());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = routerB->make_session(destA, {});
    EXPECT_TRUE(routerB->add_session(sessB));

    EXPECT_TRUE(sessB->send_to(destA, "p1"));
    EXPECT_TRUE(WaitFor([&] {
      return factory_calls.load(std::memory_order::acquire) >= 1;
    }));

    EXPECT_TRUE(sessB->send_to(destA, "p2"));
    EXPECT_TRUE(sessB->send_to(destA, "p3"));
    EXPECT_TRUE(WaitFor([&] {
      return data_calls.load(std::memory_order::acquire) >= 2;
    }));

    EXPECT_EQ(factory_calls.load(std::memory_order::acquire), 1);
  }
}

void IouDgramRouter_DropOnNullFactory() {
  // Factory returning null drops the packet; no session is registered, so
  // every arriving packet re-invokes the factory.
  if (true) {
    iou_loop_runner runner;
    std::atomic_int factory_calls{0};

    using router_t = iou_dgram_router_ptr::router_t;

    auto routerA = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        router_t::handlers{
            .on_new_session =
                [&](router_t&, const net_endpoint&,
                    iou_loop::buffer&&) -> router_t::session_ptr {
              ++factory_calls;
              return nullptr;
            }});
    EXPECT_TRUE(routerA);

    auto routerB =
        iou_dgram_router_ptr::bind(runner.loop(), net_endpoint::loopback_v4());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = routerB->make_session(destA, {});
    EXPECT_TRUE(routerB->add_session(sessB));

    EXPECT_TRUE(sessB->send_to(destA, "drop1"));
    EXPECT_TRUE(sessB->send_to(destA, "drop2"));

    EXPECT_TRUE(WaitFor(
        [&] { return factory_calls.load(std::memory_order::acquire) >= 2; },
        1s));
  }
}

void IouDgramRouter_CustomKey() {
  // Routing by a 32-bit ID extracted from the first 4 payload bytes,
  // independent of peer_addr.
  if (true) {
    struct id_extractor {
      [[nodiscard]] std::uint32_t operator()(
          iou_loop::buffer& buf) const noexcept {
        const auto v = buf.payload_view();
        if (v.size() < 4) return 0;
        std::uint32_t id{};
        std::memcpy(&id, v.data(), 4);
        return id;
      }
    };

    using my_router = iou_dgram_router<id_extractor>;
    using my_router_ptr = iou_dgram_router_ptr_with<my_router>;
    static_assert(std::same_as<my_router::key_t, std::uint32_t>);

    iou_loop_runner runner;
    std::atomic_int sess1_data{0};
    std::atomic_int sess2_data{0};

    auto routerA = my_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        my_router::handlers{
            .on_new_session =
                [&](my_router& r, const std::uint32_t& key,
                    iou_loop::buffer&&) -> my_router::session_ptr {
              return r.make_session(key,
                  my_router::session_handlers{
                      .on_data =
                          [&, key](my_router::session_t&, iou_loop::buffer&&) {
                            if (key == 1U) ++sess1_data;
                            if (key == 2U) ++sess2_data;
                            return true;
                          }});
            }},
        id_extractor{});
    EXPECT_TRUE(routerA);

    auto routerB =
        iou_dgram_router_ptr::bind(runner.loop(), net_endpoint::loopback_v4());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = routerB->make_session(destA, {});
    EXPECT_TRUE(routerB->add_session(sessB));

    auto send_id = [&](std::uint32_t id, std::string_view tail) {
      std::string payload(4 + tail.size(), '\0');
      std::memcpy(payload.data(), &id, 4);
      std::memcpy(payload.data() + 4, tail.data(), tail.size());
      return sessB->send_to(destA, payload);
    };

    EXPECT_TRUE(send_id(1, "a")); // creates session 1 (factory)
    EXPECT_TRUE(send_id(2, "b")); // creates session 2 (factory)
    EXPECT_TRUE(send_id(1, "c")); // routes to session 1 on_data
    EXPECT_TRUE(send_id(2, "d")); // routes to session 2 on_data
    EXPECT_TRUE(send_id(1, "e"));

    EXPECT_TRUE(WaitFor([&] {
      return sess1_data.load(std::memory_order::acquire) >= 2 &&
             sess2_data.load(std::memory_order::acquire) >= 1;
    }));
  }
}

void IouDgramRouter_WithState() {
  // `iou_dgram_session_with_state` exposes per-session typed state via
  // `from()`.
  if (true) {
    struct MyState {
      int recv_count{};
    };

    using router_t = iou_dgram_router_ptr::router_t;
    using sess_with_state = iou_dgram_session_with_state<MyState>;

    iou_loop_runner runner;
    std::atomic_int observed{0};

    auto routerA = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        router_t::handlers{
            .on_new_session =
                [&](router_t& r, const net_endpoint& key,
                    iou_loop::buffer&&) -> router_t::session_ptr {
              return r.make_session<sess_with_state>(key,
                  router_t::session_handlers{
                      .on_data =
                          [&](router_t::session_t& s, iou_loop::buffer&&) {
                            auto& state = sess_with_state::from(s).state();
                            ++state.recv_count;
                            observed.store(state.recv_count,
                                std::memory_order::release);
                            return true;
                          }});
            }});
    EXPECT_TRUE(routerA);

    auto routerB =
        iou_dgram_router_ptr::bind(runner.loop(), net_endpoint::loopback_v4());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = routerB->make_session(destA, {});
    EXPECT_TRUE(routerB->add_session(sessB));

    EXPECT_TRUE(sessB->send_to(destA, "p1")); // factory only
    EXPECT_TRUE(sessB->send_to(destA, "p2")); // on_data, count=1
    EXPECT_TRUE(sessB->send_to(destA, "p3")); // on_data, count=2

    EXPECT_TRUE(WaitFor([&] {
      return observed.load(std::memory_order::acquire) >= 2;
    }));
  }
}

void IouDgramRouter_Multishot() {
  // Multishot recvmsg path: a burst of datagrams all arrive.
  if (true) {
    iou_loop_runner runner;
    std::atomic_int delivered{0};

    using router_t = iou_dgram_router_ptr::router_t;

    auto routerA = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        router_t::handlers{
            .on_new_session =
                [&](router_t& r, const net_endpoint& key,
                    iou_loop::buffer&&) -> router_t::session_ptr {
              ++delivered;
              return r.make_session(key,
                  router_t::session_handlers{
                      .on_data =
                          [&](router_t::session_t&, iou_loop::buffer&&) {
                            ++delivered;
                            return true;
                          }});
            }},
        make_default_dgram_extractor(), shot_type::multi);
    EXPECT_TRUE(routerA);

    auto routerB =
        iou_dgram_router_ptr::bind(runner.loop(), net_endpoint::loopback_v4());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = routerB->make_session(destA, {});
    EXPECT_TRUE(routerB->add_session(sessB));

    constexpr int total = 16;
    for (int i = 0; i < total; ++i)
      EXPECT_TRUE(sessB->send_to(destA, "burst"));

    EXPECT_TRUE(WaitFor(
        [&] { return delivered.load(std::memory_order::acquire) >= total; },
        2s));
  }
}

void IouDgramRouter_OnClose() {
  // Closing the router fires `on_close` on registered sessions and on the
  // router itself, exactly once.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool router_closed{false};
    std::atomic_int session_closed{0};

    using router_t = iou_dgram_router_ptr::router_t;

    auto routerA = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(),
        router_t::handlers{.on_close = [&](router_t&) {
          router_closed.store(true, std::memory_order::release);
          return true;
        }});
    EXPECT_TRUE(routerA);

    auto sess = routerA->make_session(
        net_endpoint::loopback_v4(/*port=*/12345),
        router_t::session_handlers{.on_close = [&](router_t::session_t&) {
          ++session_closed;
          return true;
        }});
    EXPECT_TRUE(routerA->add_session(sess));

    EXPECT_TRUE(routerA->close());
    EXPECT_TRUE(WaitFor([&] {
      return router_closed.load(std::memory_order::acquire) &&
             session_closed.load(std::memory_order::acquire) >= 1;
    }));
    EXPECT_EQ(session_closed.load(std::memory_order::acquire), 1);
  }
}

// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(IouDgramRouter_BasicSendRecv,
    IouDgramRouter_OnSentReturnsBuffer, IouDgramRouter_LazySession,
    IouDgramRouter_DropOnNullFactory, IouDgramRouter_CustomKey,
    IouDgramRouter_WithState, IouDgramRouter_Multishot, IouDgramRouter_OnClose)
