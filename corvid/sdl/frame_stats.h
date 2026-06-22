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
#include <optional>

// Rolling frame-time statistics for an interactive render loop, for a
// title-bar readout. The frame loop folds each frame's elapsed time in, and
// once a second passes gets back the window's frame rate and min/average/max
// frame time. No windowing dependency: the caller formats and displays the
// figures.

namespace corvid::sdl {

#pragma region frame_stats

// Frame-time statistics over a rolling, roughly one-second window: the frame
// rate and the min, average, and max frame time. Fold each frame's `dt` with
// `record`; it returns the window's `summary` once a second passes and resets
// for the next. The max is what reveals judder that an average frame rate
// hides, so steady pacing reads apart from periodic spikes.
class frame_stats {
public:
  // One window's frame-time figures: the frame rate and the min, average, and
  // max frame time in milliseconds.
  struct summary {
    float fps;
    float min_ms;
    float avg_ms;
    float max_ms;
  };

  // Fold one frame of `dt` seconds into the current window. Returns the
  // window's `summary` once it passes a second, resetting for the next window,
  // and otherwise nothing. The first frame of a window seeds the min and max.
  [[nodiscard]] std::optional<summary> record(float dt) {
    const float frame_ms = dt * 1000.0F;
    min_ms_ = (frames_ == 0) ? frame_ms : std::min(min_ms_, frame_ms);
    max_ms_ = (frames_ == 0) ? frame_ms : std::max(max_ms_, frame_ms);
    sum_ms_ += frame_ms;
    ++frames_;
    window_ += dt;
    if (window_ < window_seconds) return std::nullopt;

    const summary result{static_cast<float>(frames_) / window_, min_ms_,
        sum_ms_ / static_cast<float>(frames_), max_ms_};
    frames_ = 0;
    sum_ms_ = 0.0F;
    window_ = 0.0F;
    return result;
  }

private:
  static constexpr float window_seconds = 1.0F;
  int frames_ = 0;
  float sum_ms_ = 0.0F;
  float min_ms_ = 0.0F;
  float max_ms_ = 0.0F;
  float window_ = 0.0F;
};

#pragma endregion

} // namespace corvid::sdl
