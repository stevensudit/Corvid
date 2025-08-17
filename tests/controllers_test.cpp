// Corvid: A general-purpose modern C++ library extending std.
// https://github.com/stevensudit/Corvid
//
// Copyright 2022-2025 Steven Sudit
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

#include "../corvid/controllers.h"
#include "minitest.h"

#include <iomanip>
#include <iostream>

using namespace std::literals;
using namespace corvid;
using namespace corvid::controllers;

// NOLINTBEGIN(readability-function-cognitive-complexity,
// readability-function-size)

constexpr double eps = 1e-6;

void PidControllerTest() {
  if (true) {
    // Proportional only.
    pid_controller pid(2.0, 0.0, 0.0);
    double out = pid.update(10.0, 4.0, 0.0);
    EXPECT_NEAR(out, 12.0, eps); // P = 2 * (10 - 4) = 12
  }
  if (true) {
    // Proportional only, repeated time.
    pid_controller pid(2.0, 0.0, 0.0);
    double first = pid.update(10.0, 4.0, 0.0);  // First call
    double second = pid.update(10.0, 4.0, 0.0); // Same time
    EXPECT_NEAR(first, second, eps); // Should return last value, unchanged
  }
  if (true) {
    // Proportional only, repeated time with same values.
    pid_controller pid(2.0, 0.0, 0.0);
    double first = pid.update(10.0, 4.0, 0.0);
    double second = pid.update(10.0, 4.0, 1.0);
    EXPECT_NEAR(first, second, eps); // Still just P, no change
  }
  if (true) {
    // Integral accumulation.
    pid_controller pid(0.0, 1.0, 0.0);
    EXPECT_NEAR(pid.update(1.0, 0.0, 0.0), 0.0, eps); // Init
    EXPECT_NEAR(pid.update(1.0, 0.0, 1.0), 1.0, eps); // Integral = 1
    EXPECT_NEAR(pid.update(1.0, 0.0, 2.0), 2.0, eps); // Integral = 2
  }
  if (true) {
    // Clock moves backwards.
    pid_controller pid(1.0, 0.0, 0.0);
    EXPECT_NEAR(pid.update(1.0, 0.0, 5.0), 1.0, eps);
    EXPECT_NEAR(pid.update(1.0, 0.0, 3.0), 1.0, eps); // Last value
    EXPECT_NEAR(pid.update(1.0, 0.0, 6.0), 1.0, eps); // Still same error
  }
  if (true) {
    // Clock moves backwards, without losing integral.
    pid_controller pid(0.0, 1.0, 0.0);
    EXPECT_NEAR(pid.update(1.0, 0.0, 5.0), 0.0, eps); // Init
    EXPECT_NEAR(pid.update(1.0, 0.0, 6.0), 1.0, eps); // One second of panic.
    EXPECT_NEAR(pid.update(1.0, 0.0, 8.0), 3.0, eps); // Two seconds of panic.
    EXPECT_NEAR(pid.update(1.0, 0.0, 3.0), 3.0, eps); // Time jump.
    EXPECT_NEAR(pid.update(1.0, 0.0, 4.0), 4.0, eps); // One second of panic.
  }
  if (true) {
    // Derivative filtering.
    pid_controller pid(0.0, 0.0, 1.0, 0.5);    // D-only, filtered
    double first = pid.update(0.0, 10.0, 0.0); // First call, no D yet
    EXPECT_NEAR(first, 0.0, eps);              // No change, no previous error
    // Error jump from -10 to 0 → D spike
    double out = pid.update(0.0, 0.0, 0.1);
    // raw D = (0 - (-10)) / 0.1 = 100, but filtered: 0.5 * 0 + 0.5 * 100 = 50
    EXPECT_NEAR(out, 50.0, eps);
  }
  if (true) {
    // Saturation and windup
    // Aggressive gains, clamped
    pid_controller pid(100.0, 50.0, 0.0, 0.0, -10.0, 10.0);
    double first = pid.update(1.0, -1.0, 0.0); // Error = 2 → unclamped = huge
    EXPECT_NEAR(first, 10.0, eps);             // Clamped at max
    // Integral term would grow, but shouldn't
    double second = pid.update(1.0, -1.0, 1.0);
    EXPECT_NEAR(second, 10.0, eps); // Still clamped, no windup
  }
}

void sopdt_plant_test() {
  if (true) {
    // K=1, tau1=tau2=1, L=1.0, dt=0.1
    soptd_plant plant(1.0, 1.0, 1.0, 1.0, 0.1);

    // Apply 1.0 for multiple steps
    for (int i = 0; i < 10; ++i) {
      // dead time, output should still be 0.0
      EXPECT_NEAR(plant.update(1.0), 0.0, eps);
    }

    double output = plant.update(1.0); // step 11: input has reached system
    // This is now valid after 1 time constant (tau = 1)
    EXPECT_NEAR(output, 0.01, eps);

    // Loop more to approximate steady state.
    for (int i = 0; i < 1000; ++i) {
      output = plant.update(1.0);
      EXPECT_TRUE(std::isfinite(output)); // Ensure output is finite
    }

    output = plant.update(1.0);
    EXPECT_NEAR(output, 1, eps);
  }
  if (true) {
    const double dt = 0.01;
    const double total_time = 30.0;
    const int steps = static_cast<int>(total_time / dt);

    // Plant parameters: K = 1, tau1 = 3s, tau2 = 1s, L = 0.5s
    soptd_plant plant(1.0, 3.0, 1.0, 0.5, dt);

    // PID gains (tune as needed for rise time and damping)
    pid_controller pid(
        /* Kp = */ 2.0,
        /* Ki = */ 0.5,
        /* Kd = */ 1.0,
        /* alpha = */ 0.1,
        /* min = */ -10.0,
        /* max = */ 10.0);

    double setpoint = 1.0;
    double measured = 0.0;
    double time = 0.0;

    std::cout << std::fixed << std::setprecision(4);
    std::cout << "time\tsetpt\toutput\tcontrol\n";

    for (int i = 0; i < steps; ++i) {
      double control = pid.update(setpoint, measured, time);
      measured = plant.update(control);
#if 0
      std::cout << time << "\t" << setpoint << "\t" << measured << "\t"
                << control << "\n";
#endif
      time += dt;
    }
  }
}

// TODO:
// Consider writing a class that tunes a PID controller to work with a given
// SOPDT plant. The plant is handed to it as a lambda which takes a setting and
// returns the output. In the first phase, the PID controller iterates over the
// P value, increasing it until the output just overshoots the setpoint. This
// will likely not be stable in the steady state, though. So, for the second
// phase, it increases the I value until the steady state is reached matches
// the setpoint (within tolerances). This will likely cause overshoot, so the
// third phase will tune the D value to reduce the overshoot and improve
// settling time. When the value stays within tolerances after hitting the
// setpoint, we're done. Consider implementing integral of time-weighted
// absolute error (ITEA) as a metric.
//
// We could use a binary search or gradient descent
// instead of a linear sweep.
//
// It might be convenient if passing an input of NaN resets the plant.
// Alternately, just implement a simple Ziegler-Nichols tuning method.
//
// Note: The lambda must maintain state across calls, as with a closure.
// Also, for real systems, we want to test against measurement noise,
// quantization, and nonlinearities(e.g., saturation, backlash), as by
// introducing white noise into the lambda.

MAKE_TEST_LIST(PidControllerTest, sopdt_plant_test);

// NOLINTEND(readability-function-cognitive-complexity,
// readability-function-size)
