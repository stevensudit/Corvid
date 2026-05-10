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
#include "../corvid/proto/io_uring/iou_dgram_session.h"

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
  // First packet hits the session factory; payload arrives via the buffer.
  if (true) {
    iou_loop_runner runner;
    std::atomic_bool received{false};
    std::string payload;

    using router_t = iou_dgram_router_ptr::router_t;

    auto routerA = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor(),
        [&](const std::shared_ptr<iou_dgram_router_base>& r,
            const iou_loop::buffer& buf) -> router_t::session_ptr {
          payload = std::string{buf.payload_view()};
          received.store(true, std::memory_order::release);
          return iou_dgram_session::make(r, {});
        });
    EXPECT_TRUE(routerA);

    auto routerB = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    EXPECT_FALSE(destA.empty());

    auto sessB = iou_dgram_session::make(
        std::shared_ptr<iou_dgram_router_base>{routerB.pointer()}, {});
    EXPECT_TRUE(routerB->add_session(destA, sessB));

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

    auto routerA = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor(),
        [&](const std::shared_ptr<iou_dgram_router_base>& r,
            const iou_loop::buffer&) -> router_t::session_ptr {
          return iou_dgram_session::make(r, {});
        });
    EXPECT_TRUE(routerA);

    auto routerB = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor());
    EXPECT_TRUE(routerB);

    auto sessB = iou_dgram_session::make(
        std::shared_ptr<iou_dgram_router_base>{routerB.pointer()},
        iou_dgram_session::handlers{
            .on_sent = [&](router_t::session_t&, iou_loop::buffer&& buf) {
              ok.store(buf.result().ok(), std::memory_order::release);
              sent_bytes.store(buf.result().value(),
                  std::memory_order::release);
              sent.store(true, std::memory_order::release);
              return true;
            }});
    EXPECT_TRUE(routerB->add_session(routerA->local_endpoint(), sessB));

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
        net_endpoint::loopback_v4(), make_default_dgram_extractor(),
        [&](const std::shared_ptr<iou_dgram_router_base>& r,
            iou_loop::buffer&&) -> router_t::session_ptr {
          ++factory_calls;
          return iou_dgram_session::make(r,
              iou_dgram_session::handlers{
                  .on_data = [&](router_t::session_t&, iou_loop::buffer&&) {
                    ++data_calls;
                    return true;
                  }});
        });
    EXPECT_TRUE(routerA);

    auto routerB = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = iou_dgram_session::make(
        std::shared_ptr<iou_dgram_router_base>{routerB.pointer()}, {});
    EXPECT_TRUE(routerB->add_session(destA, sessB));

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

    auto routerA = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor(),
        [&](const std::shared_ptr<iou_dgram_router_base>&,
            const iou_loop::buffer&) -> router_t::session_ptr {
          ++factory_calls;
          return nullptr;
        });
    EXPECT_TRUE(routerA);

    auto routerB = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = iou_dgram_session::make(
        std::shared_ptr<iou_dgram_router_base>{routerB.pointer()}, {});
    EXPECT_TRUE(routerB->add_session(destA, sessB));

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
    using my_router = iou_dgram_router<std::uint32_t>;
    static_assert(std::same_as<my_router::key_t, std::uint32_t>);

    std::function<std::uint32_t(const iou_loop::buffer&)> id_extractor =
        [](const iou_loop::buffer& buf) -> std::uint32_t {
      const auto v = buf.payload_view();
      if (v.size() < 4) return 0;
      std::uint32_t id{};
      std::memcpy(&id, v.data(), 4);
      return id;
    };

    iou_loop_runner runner;
    std::atomic_int sess1_data{0};
    std::atomic_int sess2_data{0};

    // `Key` is deduced from `id_extractor`'s std::function signature.
    auto routerA = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(), id_extractor,
        [&](const std::shared_ptr<iou_dgram_router_base>& r,
            iou_loop::buffer&& buf) -> my_router::session_ptr {
          const std::uint32_t key = id_extractor(buf);
          return iou_dgram_session::make(r,
              iou_dgram_session::handlers{
                  .on_data =
                      [&, key](my_router::session_t&, iou_loop::buffer&&) {
                        if (key == 1U) ++sess1_data;
                        if (key == 2U) ++sess2_data;
                        return true;
                      }});
        });
    EXPECT_TRUE(routerA);
    static_assert(
        std::same_as<decltype(routerA), iou_dgram_router_ptr_with<my_router>>);
    auto routerB = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = iou_dgram_session::make(
        std::shared_ptr<iou_dgram_router_base>{routerB.pointer()}, {});
    EXPECT_TRUE(routerB->add_session(destA, sessB));

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

    auto routerA = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor(),
        [&](const std::shared_ptr<iou_dgram_router_base>& r,
            iou_loop::buffer&&) -> router_t::session_ptr {
          return sess_with_state::make(r,
              iou_dgram_session::handlers{
                  .on_data = [&](router_t::session_t& s, iou_loop::buffer&&) {
                    auto& state = sess_with_state::from(s).state();
                    ++state.recv_count;
                    observed.store(state.recv_count,
                        std::memory_order::release);
                    return true;
                  }});
        });
    EXPECT_TRUE(routerA);

    auto routerB = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = iou_dgram_session::make(
        std::shared_ptr<iou_dgram_router_base>{routerB.pointer()}, {});
    EXPECT_TRUE(routerB->add_session(destA, sessB));

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

    auto routerA = iou_dgram_router_ptr::bind(
        runner.loop(), net_endpoint::loopback_v4(),
        make_default_dgram_extractor(),
        [&](const std::shared_ptr<iou_dgram_router_base>& r,
            iou_loop::buffer&&) -> router_t::session_ptr {
          ++delivered;
          return iou_dgram_session::make(r,
              iou_dgram_session::handlers{
                  .on_data = [&](router_t::session_t&, iou_loop::buffer&&) {
                    ++delivered;
                    return true;
                  }});
        },
        shot_type::multi);
    EXPECT_TRUE(routerA);

    auto routerB = iou_dgram_router_ptr::bind(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor());
    EXPECT_TRUE(routerB);

    const auto destA = routerA->local_endpoint();
    auto sessB = iou_dgram_session::make(
        std::shared_ptr<iou_dgram_router_base>{routerB.pointer()}, {});
    EXPECT_TRUE(routerB->add_session(destA, sessB));

    constexpr int total = 16;
    for (int i = 0; i < total; ++i)
      EXPECT_TRUE(sessB->send_to(destA, "burst"));

    EXPECT_TRUE(WaitFor(
        [&] { return delivered.load(std::memory_order::acquire) >= total; },
        2s));
  }
}

void IouDgramRouter_OnClose() {
  // Closing the router fires `on_close` on registered sessions exactly once.
  if (true) {
    iou_loop_runner runner;
    std::atomic_int session_closed{0};

    using router_t = iou_dgram_router_ptr::router_t;

    auto routerA = iou_dgram_router_ptr::bind<net_endpoint>(runner.loop(),
        net_endpoint::loopback_v4(), make_default_dgram_extractor());
    EXPECT_TRUE(routerA);

    auto sess = iou_dgram_session::make(
        std::shared_ptr<iou_dgram_router_base>{routerA.pointer()},
        iou_dgram_session::handlers{.on_close = [&](router_t::session_t&) {
          ++session_closed;
          return true;
        }});
    EXPECT_TRUE(
        routerA->add_session(net_endpoint::loopback_v4(/*port=*/12345), sess));

    // `add_session` posts to the loop thread, while `close()` clears
    // `open_` synchronously on the calling thread. Without a barrier, the
    // queued add can run after that flip, see `!is_open()`, and silently
    // drop the session before `do_close` drains. Post a sentinel and wait
    // for it: FIFO guarantees `add_session` has been processed first.
    std::atomic_bool synced{false};
    EXPECT_TRUE(runner.loop()->execute_or_post([&] {
      synced.store(true, std::memory_order::release);
      return true;
    }));
    EXPECT_TRUE(
        WaitFor([&] { return synced.load(std::memory_order::acquire); }));

    EXPECT_TRUE(routerA->close());
    EXPECT_TRUE(WaitFor([&] {
      return session_closed.load(std::memory_order::acquire) >= 1;
    }));
    EXPECT_EQ(session_closed.load(std::memory_order::acquire), 1);
  }
}

// NOLINTEND(readability-function-cognitive-complexity)

MAKE_TEST_LIST(IouDgramRouter_BasicSendRecv,
    IouDgramRouter_OnSentReturnsBuffer, IouDgramRouter_LazySession,
    IouDgramRouter_DropOnNullFactory, IouDgramRouter_CustomKey,
    IouDgramRouter_WithState, IouDgramRouter_Multishot, IouDgramRouter_OnClose)
