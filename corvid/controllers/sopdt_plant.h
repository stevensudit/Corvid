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
#include <cmath>
#include <vector>

namespace corvid { inline namespace lang { namespace controllers {

// Simulation of SOPTD (Second-Order Plus Dead Time) plant for testing
// purposes.
//
// A second-order plant with dead time, represented as a cascaded first-order
// lag system. The plant is defined by its gain (K), time constants (tau1,
// tau2), dead time (L), and the time step (dt).
//
// The update method applies the input to the plant and returns the output.
// It uses a delay buffer to simulate the dead time.
//
// Note: This was created by ChatGPT with minimal modification, as it is
// intended for testing, not production use.
class soptd_plant {
public:
  soptd_plant(double K, double tau1, double tau2, double L, double dt)
      : K_{K}, tau1_{tau1}, tau2_{tau2}, L_{L}, dt_{dt} {
    // Compute number of samples corresponding to dead time L.
    auto delay_samples = static_cast<size_t>(std::round(L_ / dt));
    // Ensure at least 1-sample delay if L > 0
    if (L_ > 0.0 && delay_samples == 0) delay_samples = 1;

    delay_buffer_ = std::vector<double>(delay_samples, 0.0);
  }

  soptd_plant(const soptd_plant&) = default;
  soptd_plant& operator=(const soptd_plant&) = delete;

  [[nodiscard]] double update(double u) {
    // Apply dead time via delay buffer.
    // Note: This could be optimized with a circular buffer.
    delay_buffer_.push_back(u);
    const auto u_delayed = delay_buffer_.front();
    delay_buffer_.erase(delay_buffer_.begin());

    // Cascaded first-order lag system using Euler integration.
    x1_ += dt_ * (-(x1_ / tau1_) + (K_ * u_delayed / tau1_));
    x2_ += dt_ * (-(x2_ / tau2_) + (x1_ / tau2_));
    return x2_;
  }

  void reset() {
    x1_ = 0.0;
    x2_ = 0.0;
    std::ranges::fill(delay_buffer_, 0.0);
  }

  [[nodiscard]] double get_gain() const { return K_; }
  [[nodiscard]] double get_tau1() const { return tau1_; }
  [[nodiscard]] double get_tau2() const { return tau2_; }
  [[nodiscard]] double get_dead_time() const { return L_; }
  [[nodiscard]] double get_dt() const { return dt_; }

private:
  const double K_;
  const double tau1_;
  const double tau2_;
  const double L_;
  const double dt_;
  double x1_ = 0.0;
  double x2_ = 0.0;
  std::vector<double> delay_buffer_;
};

}}} // namespace corvid::lang::controllers
