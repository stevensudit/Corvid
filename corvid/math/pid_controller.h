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
#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <concepts>
#include <limits>

namespace corvid { inline namespace math { inline namespace controllers {

#pragma region pid_controller

// PID controller class.
//
// The Proportional-Integral-Derivative (PID) controller is a common control
// loop feedback mechanism used in industrial control systems.
//
// The object is stateful and initialized with the three gain parameters. This
// implementation also takes an alpha parameter to control the derivative term,
// as well as min and max values to clamp the output. All of these can be
// retuned on the fly with `set_params`, which keeps the accumulated state.
//
// The initialized instance is then called periodically with the current
// setpoint and measured value, as well as the time elapsed since the previous
// call, returning the new input to the system. With proper tuning, the
// measured value will converge to the setpoint.
//
// The caller owns the clock and passes that elapsed time (`dt`) rather than a
// timestamp, so time must move forward: read it from a monotonic source such
// as `std::chrono::steady_clock`, never a wall clock that can be stepped
// backwards. A zero `dt` means no time has passed and a negative one violates
// the contract, so both are rejected by returning the previous value. Passing
// a delta also keeps the arithmetic away from large absolute times, which
// matters at the default `float`: a timestamp in the millions of seconds
// cannot resolve a 60 Hz frame at all, and the controller would silently
// freeze.
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
// Note that the cumulative error is a true accumulator, and the default clamp
// bounds are infinite, so conditional integration never blocks it. An error
// that persists forever therefore grows it without bound, until the type stops
// resolving the increments. Any finite clamp bounds it, so this only bites a
// loop that already failed to converge.
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
//
// `T` is the floating-point type everything is computed and stored in,
// defaulting to `float`. Deduction picks it up from the constructor
// arguments, so `pid_controller pid{2.0, 0.0, 0.0}` is a `double` controller
// while `pid_controller<> pid{2.0F, 0.0F, 0.0F}` is a `float` one.
template<std::floating_point T = float>
class pid_controller {
public:
#pragma region Constants

  static constexpr T pos_infinity = std::numeric_limits<T>::infinity();
  static constexpr T neg_infinity = -std::numeric_limits<T>::infinity();

#pragma endregion
#pragma region Construction

  explicit pid_controller(T kp, T ki, T kd, T alpha = {},
      T min_value = neg_infinity, T max_value = pos_infinity) noexcept
      : kp_{kp}, ki_{ki}, kd_{kd}, alpha_{alpha}, min_value_{min_value},
        max_value_{max_value} {
    assert(min_value < max_value);
    // A value of zero for alpha means no filtering. Reasonable values
    // for noisy signals are typically between 0.1 and 0.3.
    assert((alpha >= T{}) && (alpha <= T{1}));
  }

#pragma endregion
#pragma region Operations

  // Retune the controller in place, keeping the accumulated state so a live
  // tuning change does not jolt the loop in flight. The parameters are as the
  // constructor, and all of them are replaced, so leaving one off restores its
  // default rather than preserving the current setting. The last value is
  // re-clamped into the new bounds, since that is what a rejected update
  // returns.
  void set_params(T kp, T ki, T kd, T alpha = {}, T min_value = neg_infinity,
      T max_value = pos_infinity) noexcept {
    assert(min_value < max_value);
    assert((alpha >= T{}) && (alpha <= T{1}));
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
    alpha_ = alpha;
    min_value_ = min_value;
    max_value_ = max_value;
    value_last_ = std::clamp(value_last_, min_value_, max_value_);
  }

  // Update the controller over the `dt` seconds elapsed since the previous
  // call, returning the new input value.
  [[nodiscard]] T update(T setpoint, T measured_value, T dt) {
    assert(std::isfinite(setpoint));
    assert(std::isfinite(measured_value));
    assert(std::isfinite(dt));

    // Calculate the error here. Note that, in principle, we could instead
    // accept the precomputed error as a parameter. However, this signature
    // avoids repeating the calculation elsewhere (and risking mistakes like
    // reversing the sign). We also want these values for logging and for
    // potential use in the D term.
    const auto error = setpoint - measured_value;

    // The P term is based entirely on the error.
    const auto p_term = kp_ * error;

    // On the first call, there is no interval to work over, so initialize the
    // state and return the clamped P term.
    if (!primed_) {
      primed_ = true;
      error_last_ = error;
      cumulative_error_ = T{};
      d_term_last_ = T{};
      value_last_ = std::clamp(p_term, min_value_, max_value_);
      return value_last_;
    }

    // The other two terms scale by the interval, so it has to be a real one.
    // There is nothing to integrate over a `dt` of zero, and a negative one
    // means the caller broke the monotonic contract, which we refuse to
    // integrate backwards over. Either way, we hold the previous value. Note
    // that we're intentionally not rejecting a merely small `dt`.
    if (dt <= T{}) return value_last_;

    // The I term is based on cumulative error, scaled by time delta.
    const auto integral = cumulative_error_ + (error * dt);
    // Note: Here is where we would normally update the cumulative error with
    // the integral. However, we postpone this until the very end to give us a
    // chance to avoid integral windup by applying conditional integration.
    const auto i_term = ki_ * integral;

    // The D term is based on the rate of change of error, scaled by time
    // delta.
    const auto derivative = (error - error_last_) / dt;
    error_last_ = error;
    const auto d_term_unfiltered = kd_ * derivative;
    // Apply exponential moving average filter to the D term.
    const auto d_term =
        (alpha_ * d_term_last_) + ((T{1} - alpha_) * d_term_unfiltered);
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
    value_last_ = T{};
    primed_ = false;
    error_last_ = T{};
    cumulative_error_ = T{};
    d_term_last_ = T{};
  }

#pragma endregion
#pragma region Accessors

  [[nodiscard]] T kp() const noexcept { return kp_; }
  [[nodiscard]] T ki() const noexcept { return ki_; }
  [[nodiscard]] T kd() const noexcept { return kd_; }
  [[nodiscard]] T alpha() const noexcept { return alpha_; }
  [[nodiscard]] T min_value() const noexcept { return min_value_; }
  [[nodiscard]] T max_value() const noexcept { return max_value_; }

  [[nodiscard]] T value_last() const noexcept { return value_last_; }
  [[nodiscard]] T error_last() const noexcept { return error_last_; }
  [[nodiscard]] T cumulative_error() const noexcept {
    return cumulative_error_;
  }

#pragma endregion
#pragma region Data members
private:
  T kp_{};
  T ki_{};
  T kd_{};
  T alpha_{};
  T min_value_ = neg_infinity;
  T max_value_ = pos_infinity;

  T value_last_{};
  T error_last_{};
  T cumulative_error_{};
  T d_term_last_{};
  bool primed_{};

#pragma endregion
};

#pragma endregion
}}} // namespace corvid::math::controllers
