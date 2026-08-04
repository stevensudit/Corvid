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

#include "corvid/math/pid_controller.h"
#include "catch2_main.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

using namespace corvid;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

constexpr double eps = 1e-6;

// How close the closed loop has to settle to the setpoint by the end of the
// run. Looser than `eps` because this is a physical result, not arithmetic.
constexpr double settle_eps = 1e-3;

#pragma region Fixture

namespace {

// Simulation of a SOPDT (Second-Order Plus Dead Time) plant, used here to
// close the loop around the controller.
//
// A second-order plant with dead time, represented as a cascaded first-order
// lag system, defined by its gain (K), time constants (tau1, tau2), dead time
// (L), and time step (dt). `update` applies the input and returns the output,
// using a delay buffer to simulate the dead time.
class sopdt_plant {
public:
  sopdt_plant(double K, double tau1, double tau2, double L, double dt)
      : K_{K}, tau1_{tau1}, tau2_{tau2}, L_{L}, dt_{dt} {
    // Compute number of samples corresponding to dead time L, with at least a
    // 1-sample delay when there is any dead time at all.
    auto delay_samples = static_cast<size_t>(std::round(L_ / dt));
    if ((L_ > 0.0) && (delay_samples == 0)) delay_samples = 1;
    delay_buffer_ = std::vector<double>(delay_samples, 0.0);
  }

  [[nodiscard]] double update(double u) {
    // Apply dead time via delay buffer.
    delay_buffer_.push_back(u);
    const auto u_delayed = delay_buffer_.front();
    delay_buffer_.erase(delay_buffer_.begin());

    // Cascaded first-order lag system using Euler integration.
    x1_ += dt_ * (-(x1_ / tau1_) + (K_ * u_delayed / tau1_));
    x2_ += dt_ * (-(x2_ / tau2_) + (x1_ / tau2_));
    return x2_;
  }

private:
  const double K_{};
  const double tau1_{};
  const double tau2_{};
  const double L_{};
  const double dt_{};
  double x1_{};
  double x2_{};
  std::vector<double> delay_buffer_;
};

} // namespace

#pragma endregion
#pragma region PidControllerTest

TEST_CASE("PidControllerTest", "[PidControllerTest]") {
  if (true) {
    // Proportional only.
    pid_controller pid(2.0, 0.0, 0.0);
    double out = pid.update(10.0, 4.0, 1.0);
    CHECK(std::abs((out) - (12.0)) <= eps); // P = 2 * (10 - 4) = 12
  }
  if (true) {
    // Proportional only, no time elapsed.
    pid_controller pid(2.0, 0.0, 0.0);
    double first = pid.update(10.0, 4.0, 1.0);  // First call
    double second = pid.update(10.0, 4.0, 0.0); // No elapsed time
    CHECK(std::abs((first) - (second)) <=
          (eps)); // Should return last value, unchanged
  }
  if (true) {
    // Proportional only, with time elapsing.
    pid_controller pid(2.0, 0.0, 0.0);
    double first = pid.update(10.0, 4.0, 1.0);
    double second = pid.update(10.0, 4.0, 1.0);
    CHECK(std::abs((first) - (second)) <= eps); // Still just P, no change
  }
  if (true) {
    // Integral accumulation.
    pid_controller pid(0.0, 1.0, 0.0);
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (0.0)) <= eps); // Init
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (1.0)) <=
          (eps)); // Integral = 1
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (2.0)) <=
          (eps)); // Integral = 2
  }
  if (true) {
    // Negative elapsed time is rejected.
    pid_controller pid(1.0, 0.0, 0.0);
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (1.0)) <= eps);
    CHECK(std::abs((pid.update(1.0, 0.0, -2.0)) - (1.0)) <= eps); // Last value
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (1.0)) <=
          (eps)); // Still same error
  }
  if (true) {
    // Negative elapsed time is rejected, without losing the integral.
    pid_controller pid(0.0, 1.0, 0.0);
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (0.0)) <= eps); // Init
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (1.0)) <=
          (eps)); // One second of panic.
    CHECK(std::abs((pid.update(1.0, 0.0, 2.0)) - (3.0)) <=
          (eps)); // Two seconds of panic.
    CHECK(
        std::abs((pid.update(1.0, 0.0, -5.0)) - (3.0)) <= (eps)); // Rejected.
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (4.0)) <=
          (eps)); // One second of panic.
  }
  if (true) {
    // Derivative filtering.
    pid_controller pid(0.0, 0.0, 1.0, 0.5);    // D-only, filtered
    double first = pid.update(0.0, 10.0, 0.1); // First call, no D yet
    CHECK(std::abs((first) - (0.0)) <= eps);   // No change, no previous error
    // Error jump from -10 to 0 -> D spike
    double out = pid.update(0.0, 0.0, 0.1);
    // raw D = (0 - (-10)) / 0.1 = 100, but filtered: 0.5 * 0 + 0.5 * 100 = 50
    CHECK(std::abs((out) - (50.0)) <= eps);
  }
  if (true) {
    // Saturation and windup
    // Aggressive gains, clamped
    pid_controller pid(100.0, 50.0, 0.0, 0.0, -10.0, 10.0);
    double first = pid.update(1.0, -1.0, 1.0); // Error = 2 -> unclamped = huge
    CHECK(std::abs((first) - (10.0)) <= eps);  // Clamped at max
    // Integral term would grow, but shouldn't
    double second = pid.update(1.0, -1.0, 1.0);
    CHECK(std::abs((second) - (10.0)) <= eps); // Still clamped, no windup
  }
  if (true) {
    // Reset returns it to the unprimed state, so the next call is a first
    // call again.
    pid_controller pid(0.0, 1.0, 0.0);
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (0.0)) <= eps); // Init
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (1.0)) <=
          (eps)); // Integral = 1
    pid.reset();
    CHECK(std::abs((pid.update(1.0, 0.0, 1.0)) - (0.0)) <= eps); // Init again
    CHECK(std::abs((pid.cumulative_error()) - (0.0)) <= eps);
  }
}

#pragma endregion
#pragma region SopdtPlantTest

TEST_CASE("plant_test", "[sopdt]") {
  if (true) {
    // K=1, tau1=tau2=1, L=1.0, dt=0.1
    sopdt_plant plant(1.0, 1.0, 1.0, 1.0, 0.1);

    // Apply 1.0 for multiple steps
    for (auto ndx = 0; ndx < 10; ++ndx) {
      // dead time, output should still be 0.0
      CHECK(std::abs((plant.update(1.0)) - (0.0)) <= eps);
    }

    double output = plant.update(1.0); // step 11: input has reached system
    // This is now valid after 1 time constant (tau = 1)
    CHECK(std::abs((output) - (0.01)) <= eps);

    // Loop more to approximate steady state.
    for (auto ndx = 0; ndx < 1000; ++ndx) {
      output = plant.update(1.0);
      CHECK(std::isfinite(output)); // Ensure output is finite
    }

    output = plant.update(1.0);
    CHECK(std::abs((output) - (1)) <= eps);
  }
  if (true) {
    const double dt = 0.01;
    const double total_time = 30.0;
    const auto steps = static_cast<int>(total_time / dt);

    // Plant parameters: K = 1, tau1 = 3s, tau2 = 1s, L = 0.5s
    sopdt_plant plant(1.0, 3.0, 1.0, 0.5, dt);

    // PID gains (tune as needed for rise time and damping)
    pid_controller pid(
        /* Kp = */ 2.0,
        /* Ki = */ 0.5,
        /* Kd = */ 1.0,
        /* alpha = */ 0.1,
        /* min = */ -10.0,
        /* max = */ 10.0);

    const double setpoint = 1.0;
    double measured = 0.0;

    for (auto ndx = 0; ndx < steps; ++ndx) {
      const double control = pid.update(setpoint, measured, dt);
      CHECK(control >= pid.min_value());
      CHECK(control <= pid.max_value());
      measured = plant.update(control);
    }

    // Closing the loop is the point: with integral action on a stable plant,
    // the measured value has to arrive at the setpoint.
    CHECK(std::abs(measured - setpoint) <= settle_eps);
  }
}

#pragma endregion
#pragma region FloatInstantiation

TEST_CASE("FloatInstantiation", "[PidControllerTest]") {
  // The default is `float`, and deduction follows the arguments, so the same
  // arithmetic holds in either precision.
  pid_controller<> pid{2.0F, 0.0F, 0.0F};
  static_assert(std::is_same_v<decltype(pid.update(0.0F, 0.0F, 1.0F)), float>);
  CHECK(pid.update(10.0F, 4.0F, 1.0F) == 12.0F);

  // Deduced from the arguments instead.
  pid_controller deduced{2.0, 0.0, 0.0};
  static_assert(std::is_same_v<decltype(deduced), pid_controller<double>>);

  // The infinite clamp bounds come out in the right type.
  CHECK(pid.min_value() == -std::numeric_limits<float>::infinity());
  CHECK(pid.max_value() == std::numeric_limits<float>::infinity());
}

#pragma endregion

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
