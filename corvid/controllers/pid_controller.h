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
#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace corvid { inline namespace lang { namespace controllers {

// PID controller class.
//
// The Proportional-Integral-Derivative (PID) controller is a common control
// loop feedback mechanism used in industrial control systems.
//
// The object is stateful and initialized with the three gain parameters. This
// implementation also takes an alpha parameter to control the derivative term,
// as well as min and max values to clamp the output.
//
// The initialized instance is then called periodically with the current
// setpoint and measured value, as well as the timestamp, returning the new
// input to the system. With proper tuning, the measured value will converge to
// the setpoint.
//
// The difference between the setpoint and measured value is the error. The
// controller seeks to minimize it over time by providing the next input into
// the control system. It calculates this by summing the three terms:
// Proportional, Integral, and Derivative.
//
// The P (Proportional) term is calculated by multiplying the current error by
// Kp, the proportional gain. It isn't stateful, since only the current error
// is used.
//
// This term is immediate and reactive. Intuitively, the further we are
// from the setpoint, the more we want to correct things.
//
// If the Kp is too low, the controller may not ever reach the setpoint,
// resulting in a steady-state undershoot error. If it is too high, the
// controller may oscillate around the setpoint, never settling down. Even when
// not overtuned, it might overshoot the setpoint or end up with a steady-state
// error in either direction.
//
// The I (Integral) term is calculated by multiplying the cumulative
// error by Ki, the integral gain. This is stateful: it depends on the
// total error over time.
//
// This term is cumulative and slow. Intuitively, the longer we are
// away from the setpoint, the more we want to correct it. This ensures
// that we eventually reach the setpoint, but it can also lead to
// overshoot and oscillation if not tuned properly.
//
// If the Ki is too low, the controller may never reach the setpoint, leading
// to a steady-state error. If it is too high, the controller may overshoot
// the setpoint or oscillate around it. Even when not overtuned, it might
// overshoot the setpoint before settling down.
//
// The cumulative error is itself calculated by keeping a total of the
// error over time. Each time we accumulate the error, we first scale
// it by the time interval since the last measurement.
//
// However, this leads to the risk of integral windup, where the integral term
// grows too large during periods when the system is not responding, such as
// during a disturbance or a large setpoint change. To mitigate this, we can
// implement anti-windup strategies, such as clamping the integral term (which
// is what we did here) or resetting it when the error is small.
//
// The D (Derivative) term is calculated by multiplying the rate of
// change of the error by Kd, the derivative gain. This is stateful: it
// depends on the change in error over time.
//
// This term is anticipatory and dampening. Intuitively, the faster we
// approach the setpoint, the less we want to correct it. This helps
// prevent overshoot and oscillation by reducing the response.
//
// If the Kd is too low, the controller may overshoot the setpoint or
// oscillate around it. If it is too high, the controller may become
// too slow to respond to changes in the setpoint or disturbances, leading
// to sluggishness.
//
// The rate of change of the error is calculated by taking the difference
// between the current error and the previous error, divided by the time
// interval since the last measurement. However, this can lead to noise
// amplification, especially if the time interval is small or the system is
// noisy. To mitigate this, we can implement filtering strategies, such as
// low-pass filtering the error signal (which is what we did here) or using a
// moving average.
//
// The D-term often spikes on the first update or during transients, which can
// be problematic. Possible mitigations include: applying a low-pass filter to
// the derivative term (we do this), introducing a deadband around the setpoint
// to suppress noise near zero error, zeroing out the derivative on the first
// update (we also do this), and computing the derivative based on the rate of
// change in the measured value instead of the error.
class pid_controller {
public:
  static constexpr double pos_infinity =
      std::numeric_limits<double>::infinity();
  static constexpr double neg_infinity =
      -std::numeric_limits<double>::infinity();

  pid_controller(double kp, double ki, double kd, double alpha = 0.0,
      double min_value = neg_infinity,
      double max_value = pos_infinity) noexcept
      : kp_{kp}, ki_{ki}, kd_{kd}, alpha_{alpha}, min_value_{min_value},
        max_value_{max_value} {
    assert(min_value < max_value);
    // A value of 0.0 for alpha means no filtering. Reasonable values
    // for noisy signals are typically between 0.1 and 0.3.
    assert(alpha >= 0.0 && alpha <= 1.0);
  }

  pid_controller(const pid_controller&) = default;
  pid_controller& operator=(const pid_controller&) = delete;

  // Update the controller, returning the new input value.
  [[nodiscard]] double
  update(double setpoint, double measured_value, double time_now) {
    assert(std::isfinite(setpoint));
    assert(std::isfinite(measured_value));
    assert(std::isfinite(time_now));

    // For sanity, we don't do anything if the time hasn't changed. Note that
    // we're intentionally not checking for a difference being too small, just
    // for identical values.
    if (time_last_ == time_now) return value_last_;

    // Calculate the error here. Note that, in principle, we could instead
    // accept the precomputed error as a parameter. However, this signature
    // avoids repeating the calculation elsewhere (and risking mistakes like
    // reversing the sign). We also want these values for logging and for
    // potential use in the D term.
    const auto error = setpoint - measured_value;

    // The P term is based entirely on the error.
    const auto p_term = kp_ * error;

    // On the first call, initialize the state and return the clamped P term.
    if (time_last_ == neg_infinity) {
      time_last_ = time_now;
      error_last_ = error;
      cumulative_error_ = 0.0;
      d_term_last_ = 0.0;
      value_last_ = std::clamp(p_term, min_value_, max_value_);
      return value_last_;
    }

    // The other two terms need to apply the time delta. However, if the clock
    // jumped backwards, we want to start using this new time zone while still
    // returning the previously-calculated value.
    const auto time_delta = time_now - time_last_;
    time_last_ = time_now;
    if (time_delta < 0.0) return value_last_;

    // The I term is based on cumulative error, scaled by time delta.
    //
    const auto integral = cumulative_error_ + (error * time_delta);
    // Note: Here is where we would normally update the cumulative error with
    // the integral. However, we postpone this until the very end to give us a
    // chance to avoid integral windup by applying conditional integration.
    const auto i_term = ki_ * integral;

    // The D term is based on the rate of change of error, scaled by time
    // delta.
    const auto derivative = (error - error_last_) / time_delta;
    error_last_ = error;
    const auto d_term_unfiltered = kd_ * derivative;
    // Apply exponential moving average filter to the D term.
    const auto d_term =
        (alpha_ * d_term_last_) + ((1.0 - alpha_) * d_term_unfiltered);
    d_term_last_ = d_term;

    // Clamp input value.
    const auto input = p_term + i_term + d_term;
    const auto clamped_input = std::clamp(input, min_value_, max_value_);

    // Update cumulative error only when not clamped. See integral
    // windup comment above for explanation.
    if (input == clamped_input) cumulative_error_ = integral;

    value_last_ = clamped_input;
    return value_last_;
  }

  void reset() noexcept {
    value_last_ = 0.0;
    time_last_ = neg_infinity;
    error_last_ = 0.0;
    cumulative_error_ = 0.0;
    d_term_last_ = 0.0;
  }

  // Accessors.
  [[nodiscard]] double kp() const noexcept { return kp_; }
  [[nodiscard]] double ki() const noexcept { return ki_; }
  [[nodiscard]] double kd() const noexcept { return kd_; }
  [[nodiscard]] double alpha() const noexcept { return alpha_; }
  [[nodiscard]] double min_value() const noexcept { return min_value_; }
  [[nodiscard]] double max_value() const noexcept { return max_value_; }

  [[nodiscard]] double value_last() const noexcept { return value_last_; }
  [[nodiscard]] double time_last() const noexcept { return time_last_; }
  [[nodiscard]] double error_last() const noexcept { return error_last_; }
  [[nodiscard]] double cumulative_error() const noexcept {
    return cumulative_error_;
  }

private:
  const double kp_{0.0};
  const double ki_{0.0};
  const double kd_{0.0};
  const double alpha_{1.0};
  const double min_value_{neg_infinity};
  const double max_value_{pos_infinity};

  double value_last_{0.0};
  double time_last_{neg_infinity};
  double error_last_{0.0};
  double cumulative_error_{0.0};
  double d_term_last_{0.0};
};

}}} // namespace corvid::lang::controllers
