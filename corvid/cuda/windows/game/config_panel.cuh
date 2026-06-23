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

#include "imgui.h"

#include "../../vec.cuh"
#include "./render_config.cuh"
#include "./avatar_tuning.cuh"

// The voxel viewer's live tuning panel (Dear ImGui): widgets that edit the
// `avatar_tuning` and `render_config` in place against a default-constructed
// baseline, with a modified field tinted and given an inline reset. Specific
// to the voxel viewer but kept out of the .cu so it stays kernels plus host
// glue plus main. See corvid/cuda/tuning_panel.md.

namespace corvid::cuda {

#pragma region Widgets

// Draw one labeled float slider for `v` over the range [`lo`, `hi`], with
// `tip` shown on hover. When `v` differs from its default `def`, the row is
// tinted and gains an inline reset button, so a changed value is easy to spot
// and to record back into the code. CTRL+click a slider to type an exact
// value. Pass `ImGuiSliderFlags_AlwaysClamp` in `flags` for a field where an
// out-of-range typed value would be invalid (a NaN-producing divisor or
// exponent) rather than merely unusual. Returns whether `v` changed this frame
// (a slider edit or a reset), so a field with a cached derivative can refresh
// it.
inline bool tuned_slider(const char* label, float& v, float def, float lo,
    float hi, const char* tip, ImGuiSliderFlags flags = 0) {
  const bool modified = v != def;
  if (modified)
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 220, 80, 255));
  bool changed = ImGui::SliderFloat(label, &v, lo, hi, "%.3f", flags);
  ImGui::SetItemTooltip("%s", tip);
  if (modified) {
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("reset")) {
      v = def;
      changed = true;
    }
    ImGui::PopID();
  }
  return changed;
}

// Like `tuned_slider` but for an integer value over [`lo`, `hi`]. Used for
// small counts (window panes, eyes) where a fractional value is meaningless.
inline void tuned_slider_int(const char* label, int& v, int def, int lo,
    int hi, const char* tip) {
  const bool modified = v != def;
  if (modified)
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 220, 80, 255));
  ImGui::SliderInt(label, &v, lo, hi, "%d", ImGuiSliderFlags_AlwaysClamp);
  ImGui::SetItemTooltip("%s", tip);
  if (modified) {
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("reset")) v = def;
    ImGui::PopID();
  }
}

// Like `tuned_slider` but for an RGB color, edited with a color swatch. The
// inline fields and the picker show 0..1 floats (ImGuiColorEditFlags_Float),
// so the values read straight back as a `vec3{r, g, b}` in the code.
inline void
tuned_color(const char* label, vec3& v, vec3 def, const char* tip) {
  const bool modified = v.x != def.x || v.y != def.y || v.z != def.z;
  if (modified)
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 220, 80, 255));
  ImGui::ColorEdit3(label, &v.x, ImGuiColorEditFlags_Float);
  ImGui::SetItemTooltip("%s", tip);
  if (modified) {
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("reset")) v = def;
    ImGui::PopID();
  }
}

// Like `tuned_slider` but for a 3-component vector (e.g. a light direction),
// each component over [`lo`, `hi`].
inline void tuned_vec3(const char* label, vec3& v, vec3 def, float lo,
    float hi, const char* tip) {
  const bool modified = v.x != def.x || v.y != def.y || v.z != def.z;
  if (modified)
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 220, 80, 255));
  ImGui::SliderFloat3(label, &v.x, lo, hi);
  ImGui::SetItemTooltip("%s", tip);
  if (modified) {
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushID(label);
    if (ImGui::SmallButton("reset")) v = def;
    ImGui::PopID();
  }
}

#pragma endregion
#pragma region Sections

// The avatar feel constants (phase 1): camera and saucer-motion tuning.
inline void draw_avatar_section(avatar_tuning& t, const avatar_tuning& d) {
  if (!ImGui::TreeNodeEx("Avatar", ImGuiTreeNodeFlags_DefaultOpen)) return;
  tuned_slider("ball radius", t.ball_radius, d.ball_radius, 0.1F, 2.0F,
      "Radius of the metal ball body you drive.");
  tuned_slider("head radius", t.head_radius, d.head_radius, 0.1F, 2.0F,
      "Radius of the saucer head (the camera rides inside it).",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("head height", t.head_height, d.head_height, 0.0F, 3.0F,
      "How high the saucer head hovers above the ball.");
  tuned_slider("camera height", t.camera_height, d.camera_height, 0.0F, 2.0F,
      "Eye height above the head center (of the head radius); raises the "
      "viewpoint so the dome-heavy saucer reflects lower in the frame.");
  tuned_slider("front offset", t.front_offset_deg, d.front_offset_deg, -180.0F,
      180.0F,
      "Debug: rotate the head's front (and the cockpit eye) off the camera "
      "heading, to bring the back of the dome into the mirror.");
  tuned_slider("boom min", t.boom_min, d.boom_min, -5.0F, 0.0F,
      "Closest boom: head pushed in front of the ball (first person).");
  tuned_slider("boom max", t.boom_max, d.boom_max, 1.0F, 30.0F,
      "Farthest boom: head pulled back behind the ball (wide view).");
  tuned_slider("boom rise", t.boom_rise, d.boom_rise, 0.0F, 1.0F,
      "How much the head rises as the boom pulls back.");
  tuned_slider("zoom approach", t.zoom_approach, d.zoom_approach, 1.0F, 20.0F,
      "How fast the boom eases toward the zoom target, per second.");
  tuned_slider("spin rate", t.spin_rate, d.spin_rate, -3.0F, 3.0F,
      "Idle saucer belly spin, radians per second.");
  tuned_slider("spin move gain", t.spin_move_gain, d.spin_move_gain, -6.0F,
      6.0F,
      "How much forward travel adds to the spin (reverses when backing up).");
  tuned_slider("spin idle period", t.spin_idle_period, d.spin_idle_period,
      0.5F, 10.0F, "Seconds between idle spin reversals while stationary.",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("saucer lean", t.saucer_lean, d.saucer_lean, 0.0F, 1.0F,
      "How far the saucer tilts its belly toward the look direction.");
  tuned_slider("move tilt", t.move_tilt, d.move_tilt, 0.0F, 2.0F,
      "How far the saucer noses forward into travel at full speed.");
  tuned_slider("back tilt", t.back_tilt, d.back_tilt, 0.0F, 1.0F,
      "Backward tilt as a fraction of the forward tilt.");
  tuned_slider("heading approach", t.heading_approach, d.heading_approach,
      1.0F, 20.0F, "How fast the head swings to lead travel while moving.");
  tuned_slider("motion approach", t.motion_approach, d.motion_approach, 1.0F,
      20.0F, "How fast the motion tilt and spin ramp up and fade.");
  tuned_slider("move speed", t.move_speed, d.move_speed, 1.0F, 30.0F,
      "Planar movement speed, world units per second.");
  // Field of view caches tan(fov/2), so route edits through the setter.
  float fov = t.fov_deg();
  if (tuned_slider("fov", fov, d.fov_deg(), 30.0F, 110.0F,
          "Vertical field of view, in degrees."))
    t.set_fov_deg(fov);
  ImGui::TreePop();
}

// The saucer head shape (fractions of the head radius).
inline void draw_saucer_section(avatar_tuning& t, const avatar_tuning& d) {
  if (!ImGui::TreeNodeEx("Saucer", ImGuiTreeNodeFlags_DefaultOpen)) return;
  tuned_slider("body height", t.body_height, d.body_height, 0.1F, 0.6F,
      "Disc half-height / radius (smaller = flatter saucer).",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("dome offset", t.dome_offset, d.dome_offset, -0.3F, 0.5F,
      "Dome center height / radius (lower buries the dome for more overlap).");
  tuned_slider("dome radius", t.dome_radius, d.dome_radius, 0.3F, 1.0F,
      "Dome sphere radius / disc radius.");
  tuned_slider("dome blend", t.dome_blend, d.dome_blend, 0.00F, 0.6F,
      "Smooth-union width at the dome/disc seam (wider fills the gap).",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("top height", t.top_height, d.top_height, 0.05F, 0.6F,
      "Top-cone apex height / radius: below 'body height' the top is a cone, "
      "at or above it stays rounded.",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("rim round", t.rim_round, d.rim_round, 0.005F, 0.2F,
      "Rounds the sharp brim where the cone meets the bottom (reduces edge "
      "staircasing).",
      ImGuiSliderFlags_AlwaysClamp);
  ImGui::TreePop();
}

// Sky gradient and sun glow.
inline void draw_sky_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Sky")) return;
  tuned_color("zenith", c.sky.zenith, dc.sky.zenith, "Sky color straight up.");
  tuned_color("horizon", c.sky.horizon, dc.sky.horizon,
      "Sky color at the horizon.");
  tuned_slider("gradient bias", c.sky.gradient_bias, dc.sky.gradient_bias,
      0.1F, 2.0F, "Lower biases the gradient toward the horizon band.",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_color("halo color", c.sky.halo_color, dc.sky.halo_color,
      "Color of the wide glow around the sun.");
  tuned_slider("halo strength", c.sky.halo_strength, dc.sky.halo_strength,
      0.0F, 2.0F, "Brightness of the sun halo.");
  tuned_slider("halo exponent", c.sky.halo_exponent, dc.sky.halo_exponent,
      1.0F, 64.0F, "Higher tightens the halo.", ImGuiSliderFlags_AlwaysClamp);
  tuned_color("core color", c.sky.core_color, dc.sky.core_color,
      "Color of the bright sun disc.");
  tuned_slider("core exponent", c.sky.core_exponent, dc.sky.core_exponent,
      1.0F, 1000.0F, "Higher shrinks and sharpens the sun disc.",
      ImGuiSliderFlags_AlwaysClamp);
  ImGui::TreePop();
}

// Terrain lighting.
inline void draw_terrain_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Terrain")) return;
  tuned_color("ambient", c.terrain.ambient, dc.terrain.ambient,
      "Ambient (shadowed) terrain light.");
  tuned_color("sun", c.terrain.sun, dc.terrain.sun,
      "Direct sunlight color on the terrain.");
  ImGui::TreePop();
}

// Metal ball reflection look.
inline void draw_ball_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Ball")) return;
  tuned_slider("dim", c.ball.dim, dc.ball.dim, 0.0F, 1.0F,
      "Darkens the scene the ball mirrors.");
  tuned_color("tint", c.ball.tint, dc.ball.tint, "Tints the ball reflection.");
  tuned_color("ambient floor", c.ball.ambient_floor, dc.ball.ambient_floor,
      "Keeps the darkest reflections from crushing to black.");
  ImGui::TreePop();
}

// Saucer head shading.
inline void draw_head_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Head")) return;
  tuned_color("ambient", c.head.ambient, dc.head.ambient,
      "Ambient light on the saucer.");
  tuned_color("sun", c.head.sun, dc.head.sun,
      "Direct sunlight color on the saucer.");
  tuned_color("base albedo", c.head.base_albedo, dc.head.base_albedo,
      "Bare steel color of the cone and belly.");
  tuned_color("canopy", c.head.canopy, dc.head.canopy, "Dome canopy tint.");
  tuned_color("dome albedo", c.head.dome_albedo, dc.head.dome_albedo,
      "Dome-cap steel color, separate from base albedo so the dome can be "
      "darkened without dimming the hull.");
  tuned_slider_int("hex freq", c.head.dome_hex_freq, dc.head.dome_hex_freq, 1,
      24, "Icosahedron subdivision frequency: higher = more, smaller cells.");
  tuned_slider("hex line", c.head.dome_hex_line, dc.head.dome_hex_line, 0.0F,
      0.05F, "Half-width of the dome hex-grid seams, radians.");
  tuned_slider("hex strength", c.head.dome_hex_strength,
      dc.head.dome_hex_strength, 0.0F, 5.0F,
      "How much the hex-grid seams darken the dome.");
  tuned_slider("hex extent", c.head.dome_hex_extent, dc.head.dome_hex_extent,
      0.0F, 1.0F, "Radius (rr) out to which the hex grid covers the dome.");
  tuned_slider("hex phase", c.head.dome_hex_phase, dc.head.dome_hex_phase,
      -3.15F, 3.15F,
      "Rotate the grid about the eye to align its hexagons with the "
      "porthole.");
  tuned_slider("seam inner", c.head.seam_inner, dc.head.seam_inner, 0.0F, 1.0F,
      "Radius of the inner seam's hard edge; set it to the hex extent so the "
      "groove butts the dome cutoff without leaking onto the dome.");
  tuned_slider("seam inner width", c.head.seam_inner_width,
      dc.head.seam_inner_width, 0.0F, 0.3F,
      "How far the inner seam fades out onto the cone (its soft outer edge).");
  tuned_slider("seam outer", c.head.seam_outer, dc.head.seam_outer, 0.0F, 1.0F,
      "Radius of the rim-emphasis ring on the top cone.");
  tuned_slider("seam outer width", c.head.seam_outer_width,
      dc.head.seam_outer_width, 0.0F, 0.15F,
      "Half-width of the rim-emphasis ring (widen to reach the brim).");
  tuned_slider("seam strength", c.head.seam_strength, dc.head.seam_strength,
      0.0F, 1.0F, "How strongly the seam rings darken the cone.");
  tuned_color("seam color", c.head.seam_color, dc.head.seam_color,
      "Seam-ring color (darker than the canopy reads as a groove).");
  tuned_slider("eye forward", c.head.eye_forward, dc.head.eye_forward, 0.0F,
      3.0F, "How far the eye leans toward the front (0 = at the apex).");
  tuned_slider("eye size", c.head.eye_size, dc.head.eye_size, 0.05F, 0.5F,
      "Radius of the hexagonal eye.");
  tuned_slider("eye hub", c.head.eye_hub, dc.head.eye_hub, 0.0F, 0.25F,
      "Radius of the central hub circle inside the eye.");
  tuned_slider_int("eye spokes", c.head.eye_spokes, dc.head.eye_spokes, 0, 12,
      "Radial panes inside the eye.");
  tuned_slider("eye line", c.head.eye_line, dc.head.eye_line, 0.005F, 0.06F,
      "Thickness of the eye's frame, spokes, and hub ring.");
  tuned_color("eye glass", c.head.eye_glass, dc.head.eye_glass,
      "Iris color (the ring between the hub and the frame).");
  tuned_color("eye pupil", c.head.eye_pupil, dc.head.eye_pupil,
      "Center hub (pupil) color; the beam source.");
  tuned_color("eye frame color", c.head.eye_frame_color,
      dc.head.eye_frame_color, "Frame, spoke, and hub-ring color.");
  tuned_slider("ring frequency", c.head.ring_frequency, dc.head.ring_frequency,
      0.0F, 60.0F, "Concentric ring frequency on the belly.");
  tuned_slider("spoke frequency", c.head.spoke_frequency,
      dc.head.spoke_frequency, 0.0F, 40.0F,
      "Radial spoke frequency on the belly.");
  tuned_slider("paint base", c.head.paint_base, dc.head.paint_base, 0.0F, 1.0F,
      "Darkest the belly paint dims to.");
  tuned_slider("paint range", c.head.paint_range, dc.head.paint_range, 0.0F,
      1.0F, "Brightness added where rings and spokes peak.");
  tuned_slider("hub radius", c.head.hub_radius, dc.head.hub_radius, 0.0F, 2.0F,
      "Radius of the central flashlight hub.");
  tuned_slider("hub softness", c.head.hub_softness, dc.head.hub_softness,
      0.01F, 0.3F, "Edge softness of the hub.");
  tuned_color("hub color", c.head.hub_color, dc.head.hub_color,
      "Flashlight hub color.");
  tuned_slider("hub strength", c.head.hub_strength, dc.head.hub_strength, 0.0F,
      5.0F, "Flashlight hub brightness.");
  tuned_slider("rim center", c.head.rim_center, dc.head.rim_center, 0.0F, 1.0F,
      "Radius of the amber rim-light ring.");
  tuned_slider("rim width", c.head.rim_width, dc.head.rim_width, 1.0F, 40.0F,
      "Higher makes the rim-light ring thinner.");
  tuned_color("rim color", c.head.rim_color, dc.head.rim_color,
      "Rim-light color.");
  tuned_slider("rim strength", c.head.rim_strength, dc.head.rim_strength, 0.0F,
      5.0F, "Rim-light brightness.");
  tuned_slider("rim dot frequency", c.head.rim_dot_frequency,
      dc.head.rim_dot_frequency, 0.0F, 40.0F,
      "Number of dots around the rim-light ring.");
  tuned_slider("jet base", c.head.jet_base, dc.head.jet_base, 0.0F, 1.0F,
      "Baseline propulsion wash over the belly.");
  tuned_slider("jet slope", c.head.jet_slope, dc.head.jet_slope, 0.0F, 2.0F,
      "Extra propulsion wash concentrated at the rim.");
  tuned_color("thrust color", c.head.thrust_color, dc.head.thrust_color,
      "Propulsion glow color.");
  tuned_slider("thrust strength", c.head.thrust_strength,
      dc.head.thrust_strength, 0.0F, 5.0F,
      "Propulsion glow brightness at full motion.");
  tuned_slider("dome specular", c.head.dome_specular_power,
      dc.head.dome_specular_power, 1.0F, 256.0F,
      "Highlight sharpness on the dome.", ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("belly specular", c.head.belly_specular_power,
      dc.head.belly_specular_power, 1.0F, 256.0F,
      "Highlight sharpness on the belly.", ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("specular strength", c.head.specular_strength,
      dc.head.specular_strength, 0.0F, 2.0F,
      "Overall specular highlight brightness.");
  ImGui::TreePop();
}

// Kernel render options.
inline void draw_render_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Render")) return;
  ImGui::SliderInt("aa samples", &c.aa_samples, 1, 3, "%d",
      ImGuiSliderFlags_AlwaysClamp);
  ImGui::SetItemTooltip("%s",
      "Anti-alias samples per axis (1 disables; cost is the square).");
  ImGui::SameLine();
  ImGui::PushID("aa samples reset");
  if (ImGui::SmallButton("reset")) c.aa_samples = dc.aa_samples;
  ImGui::PopID();
  ImGui::TreePop();
}

#pragma endregion
#pragma region Panel

// Draw the live tuning panel: the avatar feel and saucer shape (editing `t`
// against defaults `d`), and the shading config (editing `c` against `dc`).
// "Reset all" restores everything; per-field descriptions are hover tooltips.
// A modified field is tinted and gains an inline reset. The window opens
// centered at a readable size (ImGui's .ini persistence is disabled, so this
// default holds every run).
inline void draw_config_panel(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc) {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_FirstUseEver,
      ImVec2(0.5F, 0.5F));
  ImGui::SetNextWindowSize(ImVec2(780.0F, vp->WorkSize.y * 0.8F),
      ImGuiCond_FirstUseEver);
  ImGui::Begin("Tuning");
  if (ImGui::Button("Reset all")) {
    t = d;
    c = dc;
  }
  // The scene sun direction is global (sky glow, terrain, saucer), so it sits
  // above the per-section trees rather than inside one.
  tuned_vec3("sun direction", c.sun_direction, dc.sun_direction, -1.0F, 1.0F,
      "Scene sun direction: drives the sky glow and lights the terrain and "
      "saucer.");
  draw_avatar_section(t, d);
  draw_saucer_section(t, d);
  draw_sky_section(c, dc);
  draw_terrain_section(c, dc);
  draw_ball_section(c, dc);
  draw_head_section(c, dc);
  draw_render_section(c, dc);
  ImGui::End();
}

#pragma endregion

} // namespace corvid::cuda
