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

#include "../corvid/concurrency/timeout_sweeper.h"
#include "../corvid/meta/fixed_function.h"
#include "../corvid/infra.h"

#include "catch2_main.h"

#include <memory>
#include <vector>

using namespace std::chrono_literals;
using namespace corvid::concurrency;
using namespace corvid::infra;

using sweeper = timeout_sweeper<>;
using tp = sweeper::time_point_t;

// Construct a deterministic `time_point` at `ms` milliseconds past the
// steady-clock epoch. Tests use this rather than `sweeper::now` so that
// expirations are independent of wall time.
static tp T(int ms) { return tp{} + std::chrono::milliseconds{ms}; }

// NOLINTBEGIN(readability-function-cognitive-complexity)

#pragma region BasicFire

TEST_CASE("BasicFire", "[TimeoutSweeper]") {
  sweeper s;
  int fired{0};
  CHECK(s.schedule(T(100), [&](tp) -> tp {
    ++fired;
    return {};
  }));
  CHECK(s.size() == 1U);
  s.tick(T(100));
  CHECK(fired == 1);
  CHECK(s.empty());
}

#pragma endregion
#pragma region NotFiredEarly

TEST_CASE("NotFiredEarly", "[TimeoutSweeper]") {
  // `fired` must outlive `s`: the sweeper's destructor drains the still-queued
  // T(100) callback (neither tick reaches it), which captures `&fired`.
  int fired{0};
  sweeper s;
  s.schedule(T(100), [&](tp) -> tp {
    ++fired;
    return {};
  });
  s.tick(T(50));
  CHECK(fired == 0);
  CHECK(s.size() == 1U);
  s.tick(T(99));
  CHECK(fired == 0);
}

#pragma endregion
#pragma region MinHeapOrder

TEST_CASE("MinHeapOrder", "[TimeoutSweeper]") {
  // Insert out of expiration order; the heap should pop in time order
  // regardless of insertion order.
  sweeper s;
  std::vector<int> order;
  s.schedule(T(300), [&](tp) -> tp {
    order.push_back(3);
    return {};
  });
  s.schedule(T(100), [&](tp) -> tp {
    order.push_back(1);
    return {};
  });
  s.schedule(T(200), [&](tp) -> tp {
    order.push_back(2);
    return {};
  });
  s.tick(T(500));
  REQUIRE(order.size() == 3U);
  CHECK(order[0] == 1);
  CHECK(order[1] == 2);
  CHECK(order[2] == 3);
}

#pragma endregion
#pragma region ExpireParameterIsRegisteredTime

TEST_CASE("ExpireParameterIsRegisteredTime", "[TimeoutSweeper]") {
  // The callback receives the registered expiration, not the tick time.
  sweeper s;
  tp captured{};
  s.schedule(T(100), [&](tp expire) -> tp {
    captured = expire;
    return {};
  });
  s.tick(T(500));
  CHECK((captured.time_since_epoch().count()) ==
        (T(100).time_since_epoch().count()));
}

#pragma endregion
#pragma region RearmReturnsNewTime

TEST_CASE("RearmReturnsNewTime", "[TimeoutSweeper]") {
  sweeper s;
  int fired{0};
  s.schedule(T(100), [&](tp) -> tp {
    ++fired;
    return fired < 3 ? T(100 + (fired * 100)) : tp{};
  });
  s.tick(T(50));
  CHECK(fired == 0);
  s.tick(T(150)); // fire 1, rearm to T(200)
  CHECK(fired == 1);
  CHECK(s.size() == 1U);
  s.tick(T(250)); // fire 2, rearm to T(300)
  CHECK(fired == 2);
  CHECK(s.size() == 1U);
  s.tick(T(350)); // fire 3, returns zero -> drop
  CHECK(fired == 3);
  CHECK(s.empty());
}

#pragma endregion
#pragma region MultipleExpiredInOneTick

TEST_CASE("MultipleExpiredInOneTick", "[TimeoutSweeper]") {
  // A single tick should drain everything whose expiration is at or before
  // the tick time, in order.
  sweeper s;
  int count{0};
  for (int i = 1; i <= 5; ++i)
    s.schedule(T(i * 10), [&](tp) -> tp {
      ++count;
      return {};
    });
  s.tick(T(50));
  CHECK(count == 5);
  CHECK(s.empty());
}

#pragma endregion
#pragma region Clear

TEST_CASE("Clear", "[TimeoutSweeper]") {
  sweeper s;
  int fired{0};
  s.schedule(T(100), [&](tp) -> tp {
    ++fired;
    return {};
  });
  s.schedule(T(200), [&](tp) -> tp {
    ++fired;
    return {};
  });
  CHECK(s.size() == 2U);
  s.clear();
  CHECK(s.empty());
  s.tick(T(500));
  CHECK(fired == 0);
}

#pragma endregion
#pragma region SizeAndEmpty

TEST_CASE("SizeAndEmpty", "[TimeoutSweeper]") {
  sweeper s;
  CHECK(s.empty());
  CHECK(s.size() == 0U);
  s.schedule(T(100), [](tp) -> tp { return {}; });
  CHECK_FALSE(s.empty());
  CHECK(s.size() == 1U);
  s.tick(T(150));
  CHECK(s.empty());
  CHECK(s.size() == 0U);
}

#pragma endregion
#pragma region DestructorDrains

TEST_CASE("DestructorDrains", "[TimeoutSweeper]") {
  // Pending callbacks should each fire exactly once when the sweeper is
  // destroyed without an explicit drain tick.
  int fired{0};
  {
    sweeper s;
    s.schedule(T(100), [&](tp) -> tp {
      ++fired;
      return {};
    });
    s.schedule(T(200), [&](tp) -> tp {
      ++fired;
      return {};
    });
  }
  CHECK(fired == 2);
}

#pragma endregion
#pragma region DestructorShortCircuitsRearm

TEST_CASE("DestructorShortCircuitsRearm", "[TimeoutSweeper]") {
  // A callback that always asks to rearm must still fire only once during
  // the destructor's drain; otherwise the drain would not terminate.
  int fired{0};
  {
    sweeper s;
    s.schedule(T(100), [&](tp) -> tp {
      ++fired;
      return T(200);
    });
  }
  CHECK(fired == 1);
}

#pragma endregion
#pragma region DestructorBlocksFurtherSchedule

TEST_CASE("DestructorBlocksFurtherSchedule", "[TimeoutSweeper]") {
  // While the destructor is draining, `closing_` is set; any `schedule`
  // attempt from inside a fired callback must be rejected.
  bool inner_accepted{true};
  {
    sweeper s;
    s.schedule(T(100), [&inner_accepted, &s](tp) -> tp {
      inner_accepted = s.schedule(T(200), [](tp) -> tp { return {}; });
      return {};
    });
  }
  CHECK_FALSE(inner_accepted);
}

#pragma endregion
#pragma region ScheduleAcceptsDuringNormalTick

TEST_CASE("ScheduleAcceptsDuringNormalTick", "[TimeoutSweeper]") {
  // Outside of destructor drain, a callback fired during a normal `tick`
  // must be able to schedule new entries.
  sweeper s;
  bool inner_accepted{false};
  s.schedule(T(100), [&inner_accepted, &s](tp) -> tp {
    inner_accepted = s.schedule(T(300), [](tp) -> tp { return {}; });
    return {};
  });
  s.tick(T(150));
  CHECK(inner_accepted);
  CHECK(s.size() == 1U);
}

#pragma endregion
#pragma region ConnPattern_CloseOnIdle

namespace {
struct test_conn {
  relaxed_atomic<tp> read_expiration_;
  std::chrono::milliseconds read_timeout_{100ms};
  int close_count{0};
};
} // namespace

TEST_CASE("ConnPattern_CloseOnIdle", "[TimeoutSweeper]") {
  // Canonical idle-timeout pattern. When `read_expiration_` matches the
  // registered time, the callback closes the conn.
  sweeper s;
  auto conn = std::make_shared<test_conn>();
  conn->read_expiration_ = T(100);

  s.schedule(T(100), [w = std::weak_ptr<test_conn>{conn}](tp expire) -> tp {
    auto c = w.lock();
    if (!c) return {};
    const tp current = c->read_expiration_;
    if (current == expire) {
      ++c->close_count;
      return {};
    }
    return current;
  });

  s.tick(T(150));
  CHECK(conn->close_count == 1);
  CHECK(s.empty());
}

#pragma endregion
#pragma region ConnPattern_RearmOnExtended

TEST_CASE("ConnPattern_RearmOnExtended", "[TimeoutSweeper]") {
  // When the conn extends its deadline before the callback fires, the
  // callback rearms to the new deadline rather than closing.
  sweeper s;
  auto conn = std::make_shared<test_conn>();
  conn->read_expiration_ = T(100);

  s.schedule(T(100), [w = std::weak_ptr<test_conn>{conn}](tp expire) -> tp {
    auto c = w.lock();
    if (!c) return {};
    const tp current = c->read_expiration_;
    if (current == expire) {
      ++c->close_count;
      return {};
    }
    return current;
  });

  // Activity: push the deadline forward.
  conn->read_expiration_ = T(300);
  s.tick(T(150));
  CHECK(conn->close_count == 0);
  CHECK(s.size() == 1U);

  // No further activity. The rearmed entry now matches and should close.
  s.tick(T(350));
  CHECK(conn->close_count == 1);
  CHECK(s.empty());
}

#pragma endregion
#pragma region ConnPattern_WeakPtrExpiry

TEST_CASE("ConnPattern_WeakPtrExpiry", "[TimeoutSweeper]") {
  // If the conn dies before the callback fires, the callback returns zero
  // and the entry is dropped silently.
  sweeper s;
  auto conn = std::make_shared<test_conn>();
  conn->read_expiration_ = T(100);

  s.schedule(T(100), [w = std::weak_ptr<test_conn>{conn}](tp expire) -> tp {
    auto c = w.lock();
    if (!c) return {};
    const tp current = c->read_expiration_;
    if (current == expire) {
      ++c->close_count;
      return {};
    }
    return current;
  });

  conn.reset(); // Conn dies.
  s.tick(T(150));
  CHECK(s.empty());
}

#pragma endregion
#pragma region PausedExpirationClip

TEST_CASE("PausedExpirationClip", "[TimeoutSweeper]") {
  // While the conn is paused (`read_expiration_ == paused_expiration`), the
  // callback rearms to a near-future deadline rather than firing.
  sweeper s;
  auto conn = std::make_shared<test_conn>();
  conn->read_expiration_ = T(100);

  s.schedule(T(100), [w = std::weak_ptr<test_conn>{conn}](tp expire) -> tp {
    auto c = w.lock();
    if (!c) return {};
    tp current = c->read_expiration_;
    if (current == expire) {
      ++c->close_count;
      return {};
    }
    if (current == sweeper::paused_expiration)
      current = T(1000); // stand-in for "now + timeout"
    return current;
  });

  // Pause.
  conn->read_expiration_ = sweeper::paused_expiration;
  s.tick(T(150));
  CHECK(conn->close_count == 0);
  CHECK(s.size() == 1U);

  // Resume by writing a real deadline. Next fire picks it up.
  conn->read_expiration_ = T(1200);
  s.tick(
      T(1100)); // pops the entry rearmed to T(1000), sees mismatch -> T(1200)
  CHECK(conn->close_count == 0);
  CHECK(s.size() == 1U);

  s.tick(T(1250));
  CHECK(conn->close_count == 1);
}

#pragma endregion
#pragma region FixedFunctionSpecialization

TEST_CASE("FixedFunctionSpecialization", "[TimeoutSweeper]") {
  // The class template must accept a `fixed_function` specialization with
  // a small capacity sized to a `weak_ptr` capture.
  using small_cb = corvid::meta::fixed_function<32, tp(tp)>;
  using small_sw = timeout_sweeper<small_cb>;
  static_assert(std::is_same_v<small_sw::time_point_t, tp>);
  static_assert(small_sw::paused_expiration == sweeper::paused_expiration);

  small_sw s;
  int fired{0};
  CHECK(s.schedule(T(100), [&fired](tp) -> tp {
    ++fired;
    return {};
  }));
  s.tick(T(150));
  CHECK(fired == 1);
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity)
