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

#include <cassert>
#include <cmath>
#include <concepts>

#include "arithmetic.h"

namespace corvid { inline namespace math { inline namespace filters {

// A One Euro Filter (Casiez, Roussel, Vogel, CHI 2012): an adaptive low-pass
// over a 2D signal that smooths hard when the input changes slowly and
// steadily (where sampling jitter shows) and eases off as it speeds up (so
// fast motion stays responsive, with little lag). It filters in the signal's
// own units, so the smoothed stream still sums to the same total. Built for
// de-jittering noisy interactive input such as mouse-look deltas.
//
// `rest_ms` is the time constant of the at-rest smoothing (larger is
// smoother); `beta` is how quickly that smoothing relaxes as the input speeds
// up (0 leaves it a plain fixed low-pass). The driving speed is the input
// magnitude per second, so `beta` scales against that.
//
// `T` is the floating-point type filtered and stored in, defaulting to
// `float`. Deduction picks it up from the constructor arguments.
template<std::floating_point T = float>
class one_euro_filter {
public:
  explicit one_euro_filter(T rest_ms, T beta) noexcept
      : min_cutoff_{T{1000} / (two_pi_v<T> * rest_ms)}, beta_{beta} {
    // A non-positive `rest_ms` inverts to a negative or infinite cutoff, which
    // poisons every later sample, and a negative `beta` would sharpen the
    // smoothing as the input speeds up, which is backwards.
    assert(rest_ms > T{});
    assert(beta >= T{});
  }

  // Retune the filter in place, keeping any carried smoothing state so a live
  // tuning change does not jolt the in-flight signal. `rest_ms` and `beta` are
  // as the constructor.
  void set_params(T rest_ms, T beta) noexcept {
    assert(rest_ms > T{});
    assert(beta >= T{});
    min_cutoff_ = T{1000} / (two_pi_v<T> * rest_ms);
    beta_ = beta;
  }

  // Smooth one sample's (`dx`, `dy`) in place over the elapsed `dt` seconds.
  void smooth(T dt, T& dx, T& dy) noexcept {
    if (dt <= T{}) return;
    const auto speed = std::hypot(dx, dy) / dt;

    // The first sample has nothing to smooth against: seed the state and pass
    // the input through unchanged.
    if (!primed_) {
      speed_ = speed;
      dx_ = dx;
      dy_ = dy;
      primed_ = true;
      return;
    }

    // Low-pass the speed with a fixed cutoff first, so the adaptive cutoff
    // does not jitter with the noisy raw input.
    speed_ = std::lerp(speed_, speed, alpha(speed_cutoff, dt));
    // Faster motion raises the cutoff, which lightens the smoothing.
    const auto a = alpha(min_cutoff_ + (beta_ * speed_), dt);
    dx_ = std::lerp(dx_, dx, a);
    dy_ = std::lerp(dy_, dy, a);
    dx = dx_;
    dy = dy_;
  }

  // Forget the carried state so the next run starts clean.
  void reset() noexcept { primed_ = false; }

private:
  // First-order low-pass weight for a cutoff frequency (Hz) over `dt` seconds.
  [[nodiscard]] static T alpha(T cutoff, T dt) noexcept {
    const auto tau = T{1} / (two_pi_v<T> * cutoff);
    return T{1} / (T{1} + (tau / dt));
  }

  // Fixed cutoff (Hz) for the speed low-pass, the One Euro default.
  static constexpr auto speed_cutoff = T{1};
  T min_cutoff_{};
  T beta_{};
  T speed_{};
  T dx_{};
  T dy_{};
  bool primed_{};
};

}}} // namespace corvid::math::filters
