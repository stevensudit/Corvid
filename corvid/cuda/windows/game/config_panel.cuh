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

#include "../../render_config.cuh"
#include "../../vec.cuh"
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
// value. Returns whether `v` changed this frame (a slider edit or a reset), so
// a field with a cached derivative can refresh it.
inline bool tuned_slider(const char* label, float& v, float def, float lo,
    float hi, const char* tip) {
  const bool modified = v != def;
  if (modified)
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 220, 80, 255));
  bool changed = ImGui::SliderFloat(label, &v, lo, hi);
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
      "Radius of the saucer head (the camera rides inside it).");
  tuned_slider("head height", t.head_height, d.head_height, 0.0F, 3.0F,
      "How high the saucer head hovers above the ball.");
  tuned_slider("boom min", t.boom_min, d.boom_min, -5.0F, 0.0F,
      "Closest boom: head pushed in front of the ball (first person).");
  tuned_slider("boom max", t.boom_max, d.boom_max, 1.0F, 30.0F,
      "Farthest boom: head pulled back behind the ball (wide view).");
  tuned_slider("boom rise", t.boom_rise, d.boom_rise, 0.0F, 1.0F,
      "How much the head rises as the boom pulls back.");
  tuned_slider("zoom approach", t.zoom_approach, d.zoom_approach, 1.0F, 20.0F,
      "How fast the boom eases toward the zoom target, per second.");
  tuned_slider("spin rate", t.spin_rate, d.spin_rate, 0.0F, 3.0F,
      "Saucer belly spin speed, radians per second.");
  tuned_slider("saucer lean", t.saucer_lean, d.saucer_lean, 0.0F, 1.0F,
      "How far the saucer tilts its belly toward the look direction.");
  tuned_slider("heading approach", t.heading_approach, d.heading_approach,
      1.0F, 20.0F, "How fast the head swings to lead travel while moving.");
  tuned_slider("thrust full", t.thrust_full, d.thrust_full, 1.0F, 30.0F,
      "Movement speed that reads as full propulsion glow.");
  tuned_slider("thrust approach", t.thrust_approach, d.thrust_approach, 1.0F,
      20.0F, "How fast the propulsion glow ramps up and fades.");
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
      "Disc half-height / radius (smaller = flatter saucer).");
  tuned_slider("dome offset", t.dome_offset, d.dome_offset, -0.3F, 0.5F,
      "Dome center height / radius (lower buries the dome for more overlap).");
  tuned_slider("dome radius", t.dome_radius, d.dome_radius, 0.3F, 1.0F,
      "Dome sphere radius / disc radius.");
  tuned_slider("dome blend", t.dome_blend, d.dome_blend, 0.05F, 0.6F,
      "Smooth-union width at the dome/disc seam (wider fills the gap).");
  ImGui::TreePop();
}

// Sky gradient and sun glow.
inline void draw_sky_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Sky")) return;
  tuned_color("zenith", c.sky.zenith, dc.sky.zenith, "Sky color straight up.");
  tuned_color("horizon", c.sky.horizon, dc.sky.horizon,
      "Sky color at the horizon.");
  tuned_slider("gradient bias", c.sky.gradient_bias, dc.sky.gradient_bias,
      0.1F, 2.0F, "Lower biases the gradient toward the horizon band.");
  tuned_color("halo color", c.sky.halo_color, dc.sky.halo_color,
      "Color of the wide glow around the sun.");
  tuned_slider("halo strength", c.sky.halo_strength, dc.sky.halo_strength,
      0.0F, 2.0F, "Brightness of the sun halo.");
  tuned_slider("halo exponent", c.sky.halo_exponent, dc.sky.halo_exponent,
      1.0F, 64.0F, "Higher tightens the halo.");
  tuned_color("core color", c.sky.core_color, dc.sky.core_color,
      "Color of the bright sun disc.");
  tuned_slider("core exponent", c.sky.core_exponent, dc.sky.core_exponent,
      1.0F, 1000.0F, "Higher shrinks and sharpens the sun disc.");
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
      "Bare steel color.");
  tuned_color("canopy", c.head.canopy, dc.head.canopy, "Dome canopy tint.");
  tuned_slider("panel frequency", c.head.panel_frequency,
      dc.head.panel_frequency, 0.0F, 40.0F,
      "Concentric panel-ridge frequency on the dome.");
  tuned_slider("panel amplitude", c.head.panel_amplitude,
      dc.head.panel_amplitude, 0.0F, 0.5F,
      "Strength of the dome panel ridges.");
  tuned_slider("ring frequency", c.head.ring_frequency, dc.head.ring_frequency,
      0.0F, 60.0F, "Concentric ring frequency on the belly.");
  tuned_slider("spoke frequency", c.head.spoke_frequency,
      dc.head.spoke_frequency, 0.0F, 40.0F,
      "Radial spoke frequency on the belly.");
  tuned_slider("paint base", c.head.paint_base, dc.head.paint_base, 0.0F, 1.0F,
      "Darkest the belly paint dims to.");
  tuned_slider("paint range", c.head.paint_range, dc.head.paint_range, 0.0F,
      1.0F, "Brightness added where rings and spokes peak.");
  tuned_slider("hub radius", c.head.hub_radius, dc.head.hub_radius, 0.0F, 0.5F,
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
      "Highlight sharpness on the dome.");
  tuned_slider("belly specular", c.head.belly_specular_power,
      dc.head.belly_specular_power, 1.0F, 256.0F,
      "Highlight sharpness on the belly.");
  tuned_slider("specular strength", c.head.specular_strength,
      dc.head.specular_strength, 0.0F, 2.0F,
      "Overall specular highlight brightness.");
  ImGui::TreePop();
}

// Kernel render options.
inline void draw_render_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Render")) return;
  ImGui::SliderInt("aa samples", &c.aa_samples, 1, 3);
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
