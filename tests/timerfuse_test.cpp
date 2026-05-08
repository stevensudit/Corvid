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

#include <chrono>
#include <memory>

#include "../corvid/concurrency.h"
#include "minitest.h"

using namespace corvid;
using namespace std::chrono_literals;

// NOLINTBEGIN(readability-function-cognitive-complexity)

// Minimal resource type for timer_fuse tests. Holds the sequencer that
// `timer_fuse` uses for liveness checks.
struct FakeResource {
  std::atomic_uint64_t seq{0};
  int value{42};
};

#pragma region TimerFuse_Default

void TimerFuse_Default() {
  // Default-constructed fuse is permanently unarmed.
  timer_fuse<FakeResource> fuse;
  EXPECT_EQ(fuse.get_if_armed(), nullptr);

  // Copyability: a copy is also unarmed.
  auto copy = fuse;
  EXPECT_EQ(copy.get_if_armed(), nullptr);
  copy = fuse; // copy assignment
}

#pragma endregion
#pragma region TimerFuse_ArmedFires

void TimerFuse_ArmedFires() {
  // A fuse whose payload fires while the sequencer is unchanged returns the
  // live resource from `get_if_armed`.
  auto resource = std::make_shared<FakeResource>();
  auto t0 = std::chrono::steady_clock::now();
  timing_wheel wheel{4, 1ms, t0};

  bool fired{false};
  bool saw_resource{false};
  auto ok = timer_fuse<FakeResource>::set_timeout(wheel, resource->seq,
      resource, 1ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        fired = true;
        saw_resource = (f.get_if_armed() != nullptr);
        return true;
      });
  EXPECT_TRUE(ok);
  wheel.tick(t0 + 4ms);
  EXPECT_TRUE(fired);
  EXPECT_TRUE(saw_resource);
}

#pragma endregion
#pragma region TimerFuse_Disarm

void TimerFuse_Disarm() {
  // Calling `disarm` before the wheel ticks causes the payload to see a null
  // resource from `get_if_armed`.
  auto resource = std::make_shared<FakeResource>();
  auto t0 = std::chrono::steady_clock::now();
  timing_wheel wheel{4, 1ms, t0};

  bool fired{false};
  bool saw_null{false};
  (void)timer_fuse<FakeResource>::set_timeout(wheel, resource->seq, resource,
      1ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        fired = true;
        saw_null = (f.get_if_armed() == nullptr);
        return true;
      });
  timer_fuse<FakeResource>::disarm(resource->seq);
  wheel.tick(t0 + 4ms);
  EXPECT_TRUE(fired);
  EXPECT_TRUE(saw_null);
}

#pragma endregion
#pragma region TimerFuse_Rearm

void TimerFuse_Rearm() {
  // Re-arming increments the sequencer so the earlier fuse fizzles; only the
  // most-recently-armed fuse sees a live resource.
  auto resource = std::make_shared<FakeResource>();
  auto t0 = std::chrono::steady_clock::now();
  timing_wheel wheel{6, 1ms, t0};

  bool first_fired{false};
  bool first_saw_resource{false};
  bool second_fired{false};
  bool second_saw_resource{false};

  (void)timer_fuse<FakeResource>::set_timeout(wheel, resource->seq, resource,
      1ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        first_fired = true;
        first_saw_resource = (f.get_if_armed() != nullptr);
        return true;
      });

  // Re-arm: the earlier fuse's target no longer matches the sequencer.
  (void)timer_fuse<FakeResource>::set_timeout(wheel, resource->seq, resource,
      2ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        second_fired = true;
        second_saw_resource = (f.get_if_armed() != nullptr);
        return true;
      });

  wheel.tick(t0 + 6ms);
  EXPECT_TRUE(first_fired);
  EXPECT_FALSE(first_saw_resource); // fizzled
  EXPECT_TRUE(second_fired);
  EXPECT_TRUE(second_saw_resource); // still armed
}

#pragma endregion
#pragma region TimerFuse_ResourceExpired

void TimerFuse_ResourceExpired() {
  // If the `shared_ptr` owning the resource is released before the payload
  // fires, `get_if_armed` returns nullptr (the `weak_ptr` is expired).
  auto resource = std::make_shared<FakeResource>();
  auto t0 = std::chrono::steady_clock::now();
  timing_wheel wheel{4, 1ms, t0};

  bool fired{false};
  bool saw_null{false};
  (void)timer_fuse<FakeResource>::set_timeout(wheel, resource->seq, resource,
      1ms, [&](const timer_fuse<FakeResource>& f) -> bool {
        fired = true;
        saw_null = (f.get_if_armed() == nullptr);
        return true;
      });
  resource.reset();
  wheel.tick(t0 + 4ms);
  EXPECT_TRUE(fired);
  EXPECT_TRUE(saw_null);
}

#pragma endregion
#pragma region TimerFuse_ExceedMaxDelay

void TimerFuse_ExceedMaxDelay() {
  // `set_timeout` returns false when the delay exceeds the wheel's range.
  // With slot_count=2 and tick_interval=1ms, max delay = 1ms.
  auto resource = std::make_shared<FakeResource>();
  timing_wheel wheel{2, 1ms};

  auto ok = timer_fuse<FakeResource>::set_timeout(wheel, resource->seq,
      resource, 2ms,
      [](const timer_fuse<FakeResource>&) -> bool { return true; });
  EXPECT_FALSE(ok);
}

#pragma endregion

MAKE_TEST_LIST(TimerFuse_Default, TimerFuse_ArmedFires, TimerFuse_Disarm,
    TimerFuse_Rearm, TimerFuse_ResourceExpired, TimerFuse_ExceedMaxDelay);

// NOLINTEND(readability-function-cognitive-complexity)
