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

#include "./radians.cuh"
#include "./vec.cuh"

// A free-fly pinhole camera and the per-frame rays a kernel marches.

namespace corvid::cuda {

#pragma region camera_rays

// An image size in pixels.
struct resolution {
  float width;
  float height;
};

// An orthonormal view frame: the `forward` look direction and the `right` and
// `up` screen axes.
//
// `frame.forward` is the view direction: it points away from the eye, into the
// scene, since primary rays travel along +`forward`. The world is right-handed
// with +Y up, but because `forward` is the look direction the view basis is
// left-handed: `right` cross `up` = -`forward`.
//
// In simple terms, the frame's `forward` is the direction the camera is
// looking, `right` is which way is rightwards on the screen, `up` is which way
// is upwards
struct basis {
  vec3 forward;
  vec3 right;
  vec3 up;
};

// The view rays for a frame: the `eye` position, the orthonormal view `frame`,
// and the tangent of the half vertical field of view. `ray_direction` turns
// these into a per-pixel ray direction.
//
// In simple terms, `eye` is the location of the eye, `frame` is where it's
// looking, and `tan_half_fov` controls how wide the field of view is (a larger
// value means a wider view).
struct camera_rays {
  pos3 eye;
  basis frame;
  float tan_half_fov;

  // Direction of the primary ray through `pixel` of an image of size `res`.
  //
  // The vertical field of view is fixed at the camera's `fov_y`; only the
  // horizontal one scales with the aspect ratio, so a wider image shows more
  // to the sides rather than a stretched picture.
  //
  // `fisheye_amount` bends the projection from the default rectilinear
  // (pinhole, 0) toward a full equidistant fisheye (1). Rectilinear grows the
  // off-axis angle with the tangent of the screen radius (the edges stretch);
  // equidistant grows it linearly with the radius (the edges bend in, a barrel
  // look). The two agree at the center and at the vertical edge (radius 1), so
  // the amount only bends the interior, and a small value gives a mild barrel.
  [[nodiscard]] __device__ vec3 ray_direction(pos2 pixel, resolution res,
      float fisheye_amount = 0.0F) const {
    const float aspect = res.width / res.height;
    // Normalized screen offsets, the vertical edge at +/-1, before the field
    // of view scales them.
    const float u =
        ((((2.0F * pixel.v.x) + 1.0F) / res.width) - 1.0F) * aspect;
    const float v = 1.0F - (((2.0F * pixel.v.y) + 1.0F) / res.height);
    if (fisheye_amount <= 0.0F) {
      const float sx = u * tan_half_fov;
      const float sy = v * tan_half_fov;
      return normalize(frame.forward + (frame.right * sx) + (frame.up * sy));
    }
    // Blend the off-axis angle from the rectilinear `atan(r * tan_half_fov)`
    // toward the equidistant `r * (fov_y / 2)` (`atan(tan_half_fov)` is the
    // vertical half-FOV), then rebuild the ray from that angle and the screen
    // azimuth.
    const float r = sqrtf((u * u) + (v * v));
    // Screen radius below which the azimuth is undefined.
    constexpr float min_screen_r = 1.0e-6F;
    if (r < min_screen_r) return frame.forward; // dead center, no azimuth
    const float theta_rect = atanf(r * tan_half_fov);
    const float theta_fish = r * atanf(tan_half_fov);
    const float theta =
        theta_rect + ((theta_fish - theta_rect) * fisheye_amount);
    const vec3 screen_dir = ((frame.right * u) + (frame.up * v)) * (1.0F / r);
    return (frame.forward * cosf(theta)) + (screen_dir * sinf(theta));
  }
};

#pragma endregion
#pragma region orientation

// A look direction as yaw and pitch angles.
struct orientation {
  radians yaw;
  radians pitch;
};

#pragma endregion
#pragma region camera

// A free-fly pinhole camera: a position and a yaw/pitch orientation that input
// drives, from which a frame's `camera_rays` are built.
class camera {
public:
#pragma region constants

  // The world up direction is the +Y axis. This is used to build the view
  // basis and to move vertically, but the camera can look in any direction.
  static constexpr vec3 world_up{0.0F, 1.0F, 0.0F};

  // The pitch is clamped to just shy of straight up or down, so the look never
  // tips past vertical. The view basis itself is robust at the poles now (see
  // `view_basis`), so this is only a look limit, not a basis requirement.
  static constexpr radians max_pitch{89.0_deg};

#pragma endregion
#pragma region Construction

  explicit camera(pos3 position, orientation facing, radians fov_y)
      : position_{position}, orientation_{facing},
        tan_half_fov_{tan(fov_y * 0.5F)} {}

#pragma endregion
#pragma region Control

  // Turn by the given yaw and pitch deltas. Pitch is clamped just shy of
  // straight up or down so the look never tips past vertical.
  void look(orientation delta) {
    orientation_.yaw += delta.yaw;
    orientation_.pitch =
        std::clamp(orientation_.pitch + delta.pitch, -max_pitch, max_pitch);
  }

  // Move by the given amounts along the view forward and right axes and the
  // world up axis.
  void move(float forward, float strafe, float lift) {
    const auto view = view_basis();
    position_ =
        position_ + (view.forward * forward) + (view.right * strafe) +
        (world_up * lift);
  }

#pragma endregion
#pragma region Rays

  // The view rays for the current pose.
  [[nodiscard]] camera_rays rays() const {
    return {position_, view_basis(), tan_half_fov_};
  }

#pragma endregion
#pragma region Basis

  // The orthonormal view basis for the current orientation.
  [[nodiscard]] basis view_basis() const {
    const float cos_pitch = cos(orientation_.pitch);
    const vec3 forward{cos(orientation_.yaw) * cos_pitch,
        sin(orientation_.pitch), sin(orientation_.yaw) * cos_pitch};
    // Right is horizontal and yaw-only, so the basis holds up looking straight
    // up or down, where `cross(forward, world_up)` vanishes and its normalize
    // is ill-conditioned. This is that cross product away from the poles.
    const vec3 right{-sin(orientation_.yaw), 0.0F, cos(orientation_.yaw)};
    return {forward, right, cross(right, forward)};
  }

#pragma endregion
#pragma region Data members
private:
  pos3 position_;
  orientation orientation_;
  float tan_half_fov_;

#pragma endregion
};

#pragma endregion

} // namespace corvid::cuda
