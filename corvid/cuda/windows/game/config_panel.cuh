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
#include "./avatar_body.cuh"
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

// Body: the metal ball you drive and how its mirror surface reads.
inline void draw_body_section(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Body")) return;
  tuned_slider("ball radius", t.ball_radius, d.ball_radius, 0.1F, 2.0F,
      "Radius of the metal ball body you drive.");
  tuned_slider("dim", c.ball.dim, dc.ball.dim, 0.0F, 1.0F,
      "Darkens the scene the ball mirrors.");
  tuned_color("tint", c.ball.tint, dc.ball.tint, "Tints the ball reflection.");
  tuned_color("ambient floor", c.ball.ambient_floor, dc.ball.ambient_floor,
      "Keeps the darkest reflections from crushing to black.");
  ImGui::SeparatorText("Motion grid");
  tuned_slider_int("hex freq", c.ball.hex_freq, dc.ball.hex_freq, 1, 24,
      "Hex cell density of the rolling motion grid (cells per radian).");
  tuned_slider("hex line", c.ball.hex_line, dc.ball.hex_line, 0.0F, 0.1F,
      "Half-width of the motion grid's glowing seams, radians.");
  tuned_slider("hex glow", c.ball.hex_strength, dc.ball.hex_strength, 0.0F,
      3.0F, "How brightly the motion grid wireframe glows.");
  tuned_color("hex color", c.ball.hex_color, dc.ball.hex_color,
      "Color of the motion grid wireframe.");
  tuned_slider("grid move gain", t.ball_grid_move_gain, d.ball_grid_move_gain,
      0.0F, 8.0F, "How hard the grid flares up with the ball's speed.");
  tuned_slider("grid fade", t.ball_grid_fade, d.ball_grid_fade, 0.1F, 100.0F,
      "How fast the grid fades back to dark when you release the keys; pair "
      "it "
      "with grid move gain's flare-in rate (motion approach) to match.");
  tuned_slider("grid scroll", t.ball_grid_roll_gain, d.ball_grid_roll_gain,
      0.0F, 1.0F,
      "Scrolls the grid as a fraction of the true roll rate; 1 paints it on "
      "the rolling ball, lower slows it for taste. The shader motion-blurs "
      "the "
      "scroll, so a fast spin streaks instead of strobing.");
  tuned_slider("grid extent", c.ball.grid_extent, dc.ball.grid_extent, 0.0F,
      2.0F,
      "How far toward the ball's sides the grid reaches before fading; raise "
      "to show more, lower to hide the cells that shrink at the axle.");
  tuned_slider("grid steer", t.ball_grid_steer_gain, d.ball_grid_steer_gain,
      0.0F, 4.0F,
      "Fakes the turn: drifts the grid sideways while steering, to sell it.");
  tuned_slider("grid steer cap", t.ball_grid_steer_cap, d.ball_grid_steer_cap,
      0.0F, 30.0F,
      "Limits the steer-fake drift (steer-phase per second) so a tight donut "
      "saturates to a readable rate instead of strobing; raise until normal "
      "steering feels under-sold, lower if a donut still flickers.");
  ImGui::SeparatorText("Tracks");
  tuned_slider("track depth", t.track_crush_strength, d.track_crush_strength,
      0.0F, 2.0F,
      "Groove the rolling ball wears into the dirt per unit it rolls (world "
      "units of depth); 0 disables the groove. Only lateral rolling crushes, "
      "so a parked ball leaves nothing and speed does not change one pass; "
      "back and forth wears it deeper.");
  tuned_slider("track width", t.track_crush_radius, d.track_crush_radius, 0.1F,
      1.5F, "Footprint radius of the track groove and stain, world units.");
  tuned_slider("track stain", t.track_darken_strength, d.track_darken_strength,
      0.0F, 2.0F,
      "How fast the track darkens the dirt color per unit rolled, so it reads "
      "as compacted dirt; 0 disables the stain.");
  tuned_slider("track stain floor", t.track_darken_floor, d.track_darken_floor,
      0.0F, 1.0F, "Darkest the stain reaches, so a track never goes black.");
  tuned_slider("grid turn", t.ball_grid_turn_rate, d.ball_grid_turn_rate, 1.0F,
      40.0F,
      "Low-passes the grid's flow axis toward the spin direction; lower "
      "suppresses the low-speed wobble (and lags real turns more), higher "
      "follows turns faster (and lets more jitter through).");
  ImGui::TreePop();
}

// Head: properties of the whole saucer head, above the saucer/dome split.
inline void draw_head_section(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Head")) return;
  tuned_slider("head radius", t.head_radius, d.head_radius, 0.1F, 2.0F,
      "Radius of the saucer head (the camera rides inside it).",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_color("ambient", c.head.ambient, dc.head.ambient,
      "Ambient light on the saucer head.");
  tuned_color("sun", c.head.sun, dc.head.sun,
      "Direct sunlight color on the saucer head.");
  tuned_slider("specular strength", c.head.specular_strength,
      dc.head.specular_strength, 0.0F, 2.0F,
      "Overall specular highlight brightness.");
  ImGui::TreePop();
}

// Saucer: the flying-saucer hull below the dome. Split into shape, surface
// shading, the painted belly, the spin, and the tilt feel.
inline void draw_saucer_section(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc) {
  if (ImGui::TreeNode("Saucer - Shape")) {
    tuned_slider("disc height", t.disc_height, d.disc_height, 0.1F, 0.6F,
        "Disc half-height / radius (smaller = flatter saucer).",
        ImGuiSliderFlags_AlwaysClamp);
    tuned_slider("top height", t.top_height, d.top_height, 0.05F, 0.6F,
        "Top-cone apex height / radius: below 'disc height' the top is a "
        "cone, "
        "at or above it stays rounded.",
        ImGuiSliderFlags_AlwaysClamp);
    tuned_slider("rim round", t.rim_round, d.rim_round, 0.005F, 0.2F,
        "Rounds the sharp brim where the cone meets the bottom (reduces edge "
        "staircasing).",
        ImGuiSliderFlags_AlwaysClamp);
    tuned_slider("dome blend", t.dome_blend, d.dome_blend, 0.00F, 0.6F,
        "Smooth-union width at the dome/disc seam (wider fills the gap).",
        ImGuiSliderFlags_AlwaysClamp);
    tuned_slider("edge tol", t.head_hit_cap, d.head_hit_cap, 0.00F, 0.1F,
        "Caps the head's silhouette hit tolerance (fraction of radius). Lower "
        "sharpens the far-mirror edge; too low reopens the dome/disc seam.");
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Saucer - Surface")) {
    tuned_color("base albedo", c.head.base_albedo, dc.head.base_albedo,
        "Bare steel color of the cone and belly.");
    tuned_slider("belly specular", c.head.belly_specular_power,
        dc.head.belly_specular_power, 1.0F, 256.0F,
        "Highlight sharpness on the belly.", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Saucer - Top")) {
    tuned_slider_int("port count", c.head.port_count, dc.head.port_count, 0,
        32, "Number of portholes ringing the upper cone (0 removes them).");
    tuned_slider("port center", c.head.port_center, dc.head.port_center, 0.0F,
        1.0F, "Porthole ring radius (fraction of disc radius).");
    tuned_slider("port radius", c.head.port_radius, dc.head.port_radius, 0.0F,
        0.3F, "Porthole radius (fraction of disc radius).");
    tuned_color("port color", c.head.port_color, dc.head.port_color,
        "Dark porthole glass color.");
    tuned_slider("port phase", c.head.port_phase, dc.head.port_phase, -3.15F,
        3.15F, "Rotate the porthole ring about the hull axis, radians.");
    tuned_slider_int("panel count", c.head.panel_count, dc.head.panel_count, 0,
        32,
        "Number of radial panel grooves on the upper cone (0 removes "
        "them).");
    tuned_slider("panel line", c.head.panel_line, dc.head.panel_line, 0.0F,
        0.05F,
        "Half-width of the radial panel grooves (arc, fraction of radius).");
    tuned_slider("panel strength", c.head.panel_strength,
        dc.head.panel_strength, 0.0F, 1.0F,
        "How much the panel grooves darken the hull.");
    tuned_slider("panel phase", c.head.panel_phase, dc.head.panel_phase,
        -3.15F, 3.15F,
        "Rotate the panel grooves about the hull axis, radians.");
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Saucer - Belly")) {
    tuned_slider("ring paint freq", c.head.ring_paint_frequency,
        dc.head.ring_paint_frequency, 0.0F, 60.0F,
        "Concentric ring frequency on the belly paint.");
    tuned_slider("spoke paint freq", c.head.spoke_paint_frequency,
        dc.head.spoke_paint_frequency, 0.0F, 40.0F,
        "Radial spoke frequency on the belly paint.");
    tuned_slider("paint base", c.head.paint_base, dc.head.paint_base, 0.0F,
        1.0F, "Darkest the belly paint dims to.");
    tuned_slider("paint range", c.head.paint_range, dc.head.paint_range, 0.0F,
        1.0F, "Brightness added where rings and spokes peak.");
    tuned_slider("hub radius", c.head.hub_radius, dc.head.hub_radius, 0.0F,
        2.0F, "Radius of the central flashlight hub.");
    tuned_slider("hub softness", c.head.hub_softness, dc.head.hub_softness,
        0.01F, 0.3F, "Edge softness of the hub.");
    tuned_color("hub color", c.head.hub_color, dc.head.hub_color,
        "Flashlight hub color.");
    tuned_slider("hub strength", c.head.hub_strength, dc.head.hub_strength,
        0.0F, 5.0F, "Flashlight hub brightness.");
    tuned_slider("spoke center", c.head.spoke_center, dc.head.spoke_center,
        0.0F, 1.0F, "Radius of the amber belly spoke-light ring.");
    tuned_slider("spoke width", c.head.spoke_width, dc.head.spoke_width, 1.0F,
        40.0F, "Higher makes the belly spoke-light ring thinner.");
    tuned_color("spoke color", c.head.spoke_color, dc.head.spoke_color,
        "Belly spoke-light color.");
    tuned_slider("spoke strength", c.head.spoke_strength,
        dc.head.spoke_strength, 0.0F, 5.0F, "Belly spoke-light brightness.");
    tuned_slider("spoke dot freq", c.head.spoke_dot_frequency,
        dc.head.spoke_dot_frequency, 0.0F, 40.0F,
        "Number of dots around the belly spoke-light ring.");
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Saucer - Rim")) {
    tuned_slider("rim top", c.head.rim_top, dc.head.rim_top, 0.0F, 1.0F,
        "Crisp top edge of the rim lights, on the shoulder where the flat top "
        "ends (normal's vertical value: 1 = flat top, 0 = brim). The panel "
        "grooves stop here too.");
    tuned_slider("rim width", c.head.rim_width, dc.head.rim_width, 0.1F, 2.0F,
        "How far the rim lights fade downward from 'rim top' across the "
        "shoulder and onto the belly (normal units).");
    tuned_color("rim color", c.head.rim_color, dc.head.rim_color,
        "Rim running-light color.");
    tuned_slider("rim strength", c.head.rim_strength, dc.head.rim_strength,
        0.0F, 5.0F, "Rim running-light brightness.");
    tuned_slider_int("rim count", c.head.rim_count, dc.head.rim_count, 0, 48,
        "Number of running lights around the ring.");
    tuned_slider("rim floor", c.head.rim_floor, dc.head.rim_floor, 0.0F, 1.0F,
        "Dark end of the fade between segments (0 = fades to black, 1 = "
        "solid); "
        "scales the pattern up rather than clipping it.");
    tuned_slider("rim spin scale", c.head.rim_spin_scale,
        dc.head.rim_spin_scale, 0.0F, 1.0F,
        "Rim spin rate as a fraction of the belly spin (lower is calmer and "
        "avoids the fast-spin strobe).");
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Saucer - Spin")) {
    tuned_slider("spin rate", t.spin_rate, d.spin_rate, -3.0F, 3.0F,
        "Idle saucer belly spin, radians per second.");
    tuned_slider("spin move gain", t.spin_move_gain, d.spin_move_gain, -6.0F,
        6.0F,
        "How much forward travel adds to the spin (reverses when backing "
        "up).");
    tuned_slider("spin strafe gain", t.spin_strafe_gain, d.spin_strafe_gain,
        -6.0F, 6.0F,
        "How much strafing adds to the spin (opposite ways left versus "
        "right).");
    tuned_slider("spin idle period", t.spin_idle_period, d.spin_idle_period,
        0.5F, 10.0F, "Seconds between idle spin reversals while stationary.",
        ImGuiSliderFlags_AlwaysClamp);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Saucer - Tilt")) {
    tuned_slider("max dip", t.dip_max_deg, d.dip_max_deg, 0.0F, 80.0F,
        "Cap on the saucer's nose-down dip when looking down (degrees); the "
        "eye tracks down until the dip reaches this, then holds.");
    tuned_slider("forward tilt", t.forward_tilt_deg, d.forward_tilt_deg, 0.0F,
        80.0F, "Helicopter nose-down tilt at full forward travel (degrees).");
    tuned_slider("backward tilt", t.backward_tilt_deg, d.backward_tilt_deg,
        0.0F, 80.0F, "Helicopter tail-down tilt at full reverse (degrees).");
    tuned_slider("strafe tilt", t.strafe_tilt_deg, d.strafe_tilt_deg, 0.0F,
        80.0F, "Helicopter bank toward the strafe at full strafe (degrees).");
    ImGui::TreePop();
  }
}

// Dome: the hex-tiled cap on the saucer. Split into shape and albedo, the hex
// grid, and the seam band.
inline void draw_dome_section(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc) {
  if (ImGui::TreeNode("Dome - Shape")) {
    tuned_slider("dome offset", t.dome_offset, d.dome_offset, -0.3F, 0.5F,
        "Dome center height / radius (lower buries the dome for more "
        "overlap).");
    tuned_slider("dome radius", t.dome_radius, d.dome_radius, 0.3F, 1.0F,
        "Dome sphere radius / disc radius.");
    tuned_color("canopy", c.head.canopy, dc.head.canopy, "Dome canopy tint.");
    tuned_color("dome albedo", c.head.dome_albedo, dc.head.dome_albedo,
        "Dome-cap steel color, separate from base albedo so the dome can be "
        "darkened without dimming the hull.");
    tuned_slider("dome specular", c.head.dome_specular_power,
        dc.head.dome_specular_power, 1.0F, 256.0F,
        "Highlight sharpness on the dome.", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Dome - Hex")) {
    tuned_slider_int("hex freq", c.head.dome_hex_freq, dc.head.dome_hex_freq,
        1, 24,
        "Icosahedron subdivision frequency: higher = more, smaller cells.");
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
    ImGui::TreePop();
  }
  if (ImGui::TreeNode("Dome - Seam")) {
    tuned_slider("seam offset", c.head.seam_offset, dc.head.seam_offset, -0.3F,
        0.3F,
        "How far the black band's inner edge sits inside the hex cutoff.");
    tuned_slider("seam width", c.head.seam_width, dc.head.seam_width, 0.0F,
        0.3F, "Width of the solid black dome-base band.");
    tuned_color("seam color", c.head.seam_color, dc.head.seam_color,
        "Dome-base band color (black reads as a separation groove).");
    ImGui::TreePop();
  }
}

// Eye: the cockpit porthole on the dome front, its placement gimbal and iris.
inline void draw_eye_section(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Eye")) return;
  tuned_slider("eye lift", t.eye_lift_deg, d.eye_lift_deg, 0.0F, 60.0F,
      "Degrees the eye aims above the look so it makes eye contact: the "
      "camera "
      "rides above the eye, so a level aim reads as looking low.");
  tuned_slider("stabilize", t.stabilize, d.stabilize, 0.0F, 1.0F,
      "How much the dome cancels the disc's motion bank: 1 holds the eye and "
      "grid level (a steadycam) while the disc banks; 0 lets them ride it.");
  tuned_slider("overcomp", t.overcomp, d.overcomp, 0.0F, 0.5F,
      "Extra dome tilt past level, opposite the bank, rendered for show only "
      "(never in the camera aim).");
  tuned_slider("eye hub", c.head.eye_hub, dc.head.eye_hub, 0.0F, 0.25F,
      "Radius of the central hub circle inside the eye.");
  tuned_slider_int("eye spokes", c.head.eye_spokes, dc.head.eye_spokes, 0, 12,
      "Radial spokes inside the iris (6 reaches the vertices, 3 alternates).");
  tuned_slider("eye line", c.head.eye_line, dc.head.eye_line, 0.005F, 0.06F,
      "Thickness of the eye's frame, spokes, and hub ring.");
  tuned_color("eye glass", c.head.eye_glass, dc.head.eye_glass,
      "Opaque iris color.");
  tuned_color("eye pupil", c.head.eye_pupil, dc.head.eye_pupil,
      "Center hub (pupil) color; the beam source.");
  tuned_color("eye frame color", c.head.eye_frame_color,
      dc.head.eye_frame_color, "Frame and hub-ring color.");
  tuned_color("eye glow color", c.head.eye_glow_color, dc.head.eye_glow_color,
      "Color the pupil glows while the dig tool is projecting (seen in the "
      "ball reflection).");
  tuned_slider("eye glow", c.head.eye_glow_strength, dc.head.eye_glow_strength,
      0.0F, 6.0F,
      "How brightly the pupil charges up while the dig tool is projecting; 0 "
      "disables the glow.");
  ImGui::TreePop();
}

// Antenna: the rod and beacon tip standing off the dome top.
inline void draw_antenna_section(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Antenna")) return;
  tuned_slider("antenna length", t.antenna_length, d.antenna_length, 0.0F,
      2.0F, "Antenna rod length / head radius (0 removes the antenna).");
  tuned_slider("antenna thick", t.antenna_thickness, d.antenna_thickness, 0.0F,
      0.1F, "Antenna rod thickness / head radius.");
  tuned_slider("antenna ball", t.antenna_ball, d.antenna_ball, 0.0F, 0.2F,
      "Antenna tip ball radius / head radius (the beacon bead).");
  tuned_slider("antenna collar", t.antenna_collar, d.antenna_collar, 0.005F,
      0.05F,
      "Metal collar where the antenna meets the dome. Shrink to declutter the "
      "hex tiling; the rod itself is unaffected.");
  tuned_slider("antenna lead", t.antenna_lead_deg, d.antenna_lead_deg, 15.0F,
      75.0F,
      "Antenna's angular lead over the eye toward the pole (deg): dials where "
      "its base meets the hex grid (onto a node) and how vertical it stands.");
  tuned_color("antenna tip", c.head.antenna_tip_color,
      dc.head.antenna_tip_color,
      "Glowing bead atop the antenna (emissive beacon, the running light).");
  tuned_color("beacon alt color", c.head.antenna_alt_color,
      dc.head.antenna_alt_color,
      "Second beacon color: shown when backing up, and alternated with the "
      "tip "
      "color (in tune with the idle belly spin) at rest.");
  tuned_slider("blink depth", c.head.blink_depth, dc.head.blink_depth, 0.0F,
      1.0F,
      "On/off blink while moving: 0 holds the beacon lit, 1 blinks it fully "
      "dark. Steady at rest.");
  tuned_slider("blink move gain", t.blink_move_gain, d.blink_move_gain, 0.0F,
      6.0F,
      "Beacon blink rate per unit of Head speed (sprint included); no pulse "
      "at "
      "rest.");
  tuned_slider("color phase", t.color_phase, d.color_phase, 0.0F, 1.0F,
      "Resting color phase against the belly spin: 0 in phase (pure color at "
      "each reversal), 1 opposite; ~0.5 is pure mid-spin, neutral at "
      "reversal.");
  tuned_slider("color rate", t.color_spin_ratio, d.color_spin_ratio, 1.0F,
      6.0F,
      "Resting beacon color cycles per belly reversal. A small whole number "
      "above 1; 1 locks it to the spin, which reads wrong.");
  ImGui::TreePop();
}

// Movement: how the rig follows the body, dollies, zooms, and frames it.
inline void draw_movement_section(avatar_tuning& t, const avatar_tuning& d) {
  if (!ImGui::TreeNode("Movement")) return;
  tuned_slider("move speed", t.move_speed, d.move_speed, 1.0F, 30.0F,
      "Planar movement speed, world units per second.");
  tuned_slider("ground tol", t.ground_tol, d.ground_tol, 0.0F, 1.0F,
      "Contact band counted as grounded (world units): how far above the "
      "surface the ball can skim and still jump and have traction. Higher "
      "makes jumping while running over bumps more forgiving.");
  tuned_slider("collision damp", t.collision_damp, d.collision_damp, 0.001F,
      1.0F,
      "Fraction of each frame's collision penetration that is corrected. "
      "Lower eases to rest (calmer, but sinks deeper into contact); near 1 "
      "brings  back the stale-probe jitter and the wall-to-wall slam in a "
      "tight slot.");
  tuned_slider("head height", t.head_height, d.head_height, 0.0F, 3.0F,
      "How high the saucer head hovers above the ball.");
  tuned_slider("camera height", t.camera_height, d.camera_height, 0.0F, 2.0F,
      "Eye height above the head center (of the head radius); raises the "
      "viewpoint so the dome-heavy saucer reflects lower in the frame.");
  tuned_slider("boom min", t.boom_min, d.boom_min, 0.0F, 5.0F,
      "Closest boom: the jockey position, head above and slightly behind "
      "the ball. Never in front.");
  tuned_slider("boom max", t.boom_max, d.boom_max, 1.0F, 30.0F,
      "Farthest boom: head pulled back behind the ball (wide view).");
  tuned_slider("boom rise", t.boom_rise, d.boom_rise, 0.0F, 1.0F,
      "How much the head rises as the boom pulls back.");
  tuned_slider("zoom approach", t.zoom_approach, d.zoom_approach, 1.0F, 20.0F,
      "How fast the boom eases toward the zoom target, per second.");
  tuned_slider("zoom step", t.zoom_step, d.zoom_step, 0.1F, 5.0F,
      "How far the boom target moves per mouse-wheel notch.");
  tuned_slider("heading approach", t.heading_approach, d.heading_approach,
      1.0F, 20.0F,
      "Steer: how fast the heading chases the look, so the ball arcs toward "
      "where you aim.");
  tuned_slider("follow approach", t.follow_approach, d.follow_approach, 0.5F,
      20.0F,
      "Follow: how fast the view rotates to frame travel when the right "
      "button is released.");
  tuned_slider("motion approach", t.motion_approach, d.motion_approach, 1.0F,
      20.0F, "How fast the motion tilt and spin ramp up and fade.");
  tuned_slider("look smooth", t.look_rest_ms, d.look_rest_ms, 1.0F, 200.0F,
      "Mouse-look de-jitter: the One Euro Filter's at-rest smoothing time "
      "constant in milliseconds. Higher is steadier at slow speeds but adds "
      "lag.",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("look beta", t.look_beta, d.look_beta, 0.0F, 0.02F,
      "How fast the look smoothing relaxes as the mouse speeds up, so a fast "
      "flick stays responsive. 0 leaves it a plain fixed low-pass.");
  ImGui::TreePop();
}

// Body physics: the rigid body's constants. The radius is shared with the
// drawn ball (in the Body section), so it is not repeated here.
inline void draw_body_physics_section(body_params& p, const body_params& d,
    float& run_multiplier) {
  if (!ImGui::TreeNode("Body physics")) return;
  tuned_slider("mass", p.mass, d.mass, 0.1F, 20.0F,
      "Ball mass: sets the rotational inertia and the contact normal load. It "
      "cancels out of the friction-angle threshold but scales how fast a "
      "force "
      "spins the ball up.");
  tuned_slider("gravity", p.gravity, d.gravity, 0.0F, 60.0F,
      "Downward acceleration on the body, units per second squared.");
  tuned_slider("drive force", p.drive_force, d.drive_force, 0.0F, 200.0F,
      "Peak traction force the player commands at full walk. Friction caps "
      "what "
      "reaches the ground, so a command past the budget skids instead of "
      "accelerating.");
  tuned_slider("run mult", run_multiplier, 5.0F, 1.0F, 6.0F,
      "How many times the walk drive Run (Shift) commands; the run cruise is "
      "this times the walk cruise, as long as friction leaves the bigger "
      "drive "
      "headroom below the traction ceiling. (An input mapping in drive_input, "
      "not a body constant.)");
  tuned_slider("friction", p.friction, d.friction, 0.0F, 10.0F,
      "Contact friction coefficient (mu): the ball holds a slope up to "
      "atan(mu) and slips above it, and mu bounds the drive before it skids. "
      "Raise it above run mult * drive force / gravity so even Run sits below "
      "the traction ceiling and reaches its full cruise instead of skidding.");
  tuned_slider("drag", p.drag, d.drag, 0.0F, 4.0F,
      "Quadratic resistance (force ~ speed^2) that sets the cruise speed: "
      "terminal is sqrt(drive acceleration / this), so Run's bigger drive "
      "settles higher super-linearly (the rolling-resistance offset lets a 3x "
      "power ratio reach roughly a 3x speed ratio). 0 removes the cap.");
  tuned_slider("jump speed", p.jump_speed, d.jump_speed, 0.0F, 30.0F,
      "Launch speed on a grounded jump; height is jump_speed^2 / (2 * "
      "gravity).");
  tuned_slider("jump up", p.jump_up, d.jump_up, 0.0F, 1.0F,
      "Jump direction: 1 launches straight up (propulsion, leaps regardless "
      "of "
      "the ground), 0 along the contact normal (pushes off the surface, up "
      "and "
      "out of a pit but backward off an uphill ramp). Blend to taste.");
  tuned_slider("rolling resistance", p.rolling_resistance,
      d.rolling_resistance, 0.0F, 30.0F,
      "Constant coasting brake that brings a rolling ball fully to rest (drag "
      "alone only asymptotes); 0 lets a perfect ball roll on forever.");
  ImGui::TreePop();
}

// Sky gradient, sun direction, and sun glow.
inline void draw_sky_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Sky")) return;
  tuned_vec3("sun direction", c.sun_direction, dc.sun_direction, -1.0F, 1.0F,
      "Scene sun direction: drives the sky glow and lights the terrain and "
      "saucer.");
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

// Terrain march: the sphere-trace tunables, trading march speed against
// robustness over steep dig walls.
inline void draw_march_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Terrain - March")) return;
  tuned_slider("lipschitz", c.march.lipschitz, dc.march.lipschitz, 1.0F, 8.0F,
      "Assumed max field slope used to size the sphere-trace steps. Lower "
      "takes bigger steps (faster) but can overshoot a dig wall steeper than "
      "it; the eroded terrain slope is only about 1.4.",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_slider("max step", c.march.max_step_voxels, dc.march.max_step_voxels,
      1.0F, 32.0F,
      "Single-step cap in voxels: bounds how far one step can jump so a thin "
      "wall cannot be tunneled.",
      ImGuiSliderFlags_AlwaysClamp);
  tuned_slider_int("max steps", c.march.max_steps, dc.march.max_steps, 64,
      2048, "Maximum march steps per ray before it gives up as a miss.");
  ImGui::TreePop();
}

// Kernel render options and the camera's field of view.
inline void draw_render_section(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Render")) return;
  ImGui::SliderInt("aa samples", &c.aa_samples, 1, 3, "%d",
      ImGuiSliderFlags_AlwaysClamp);
  ImGui::SetItemTooltip("%s",
      "Anti-alias samples per axis (1 disables; cost is the square).");
  ImGui::SameLine();
  ImGui::PushID("aa samples reset");
  if (ImGui::SmallButton("reset")) c.aa_samples = dc.aa_samples;
  ImGui::PopID();
  tuned_slider("aa edge", c.aa_edge_depth, dc.aa_edge_depth, 0.0F, 0.5F,
      "Adaptive-AA silhouette threshold: a pixel supersamples only where its "
      "hit kind changes or its depth bends by more than this fraction of its "
      "depth across the neighbors. Lower catches finer creases (slower); "
      "higher restricts the cost to the strong edges.");
  // Field of view caches tan(fov/2), so route edits through the setter.
  float fov = t.fov_deg();
  if (tuned_slider("fov", fov, d.fov_deg(), 30.0F, 110.0F,
          "Vertical field of view, in degrees."))
    t.set_fov_deg(fov);
  tuned_slider("barrel", c.fisheye_amount, dc.fisheye_amount, 0.0F, 1.0F,
      "Barrel distortion: 0 is the rectilinear pinhole, 1 a full equidistant "
      "fisheye. A small amount bends the edges in for a wide-angle feel "
      "without the full-fisheye pitch nausea.");

  ImGui::SeparatorText("Bloom");
  ImGui::Checkbox("bloom", &c.bloom.enabled);
  ImGui::SetItemTooltip("%s",
      "Bloom the brights: a soft-thresholded, blurred copy of the HDR render "
      "added back before the tone map. Off is a plain tone map (identical "
      "output to before bloom existed).");
  tuned_slider("bloom threshold", c.bloom.threshold, dc.bloom.threshold, 0.0F,
      4.0F,
      "Luma at which a pixel starts to bloom. Lower blooms more of the scene; "
      "raise it to restrict bloom to the hottest highlights.");
  tuned_slider("bloom knee", c.bloom.knee, dc.bloom.knee, 0.0F, 1.0F,
      "Soft-threshold width below the threshold: how gradually a pixel fades "
      "into blooming as it brightens (0 is a hard cutoff).");
  tuned_slider("bloom intensity", c.bloom.intensity, dc.bloom.intensity, 0.0F,
      2.0F, "How much of the blurred bloom is added back over the image.");
  tuned_slider("bloom radius", c.bloom.sigma, dc.bloom.sigma, 0.5F, 8.0F,
      "Gaussian blur radius for the bloom, in half-resolution texels. Wider "
      "spreads the glow farther.");
  ImGui::Spacing();

  ImGui::SeparatorText("Lighting");
  ImGui::Checkbox("night", &c.night);
  ImGui::SetItemTooltip("%s",
      "Turn off the sun and darken the sky and ambient, as though it were "
      "night, so there is darkness to test the flashlight against.");
  ImGui::Text("flashlight (F): %s", c.flashlight.enabled ? "on" : "off");
  tuned_color("flashlight color", c.flashlight.color, dc.flashlight.color,
      "Flashlight beam color.");
  tuned_slider("flashlight power", c.flashlight.intensity,
      dc.flashlight.intensity, 0.0F, 12.0F, "Flashlight brightness.");
  tuned_slider("flashlight range", c.flashlight.range, dc.flashlight.range,
      5.0F, 120.0F, "How far the beam reaches, world units.");
  tuned_slider("flashlight cone", c.flashlight.cone_degrees,
      dc.flashlight.cone_degrees, 5.0F, 70.0F,
      "Beam half-angle, degrees: how wide the cone opens.");
  tuned_slider("flashlight softness", c.flashlight.softness,
      dc.flashlight.softness, 0.0F, 5.0F,
      "How soft the cone edge is: 0 is a hard circle, 1 fades from the "
      "center.");
  tuned_slider("flashlight shadow soft", c.flashlight.shadow_softness,
      dc.flashlight.shadow_softness, 0.0F, 3.0F,
      "Penumbra of the ball's shadow, as a fraction of the ball radius: wider "
      "fades the shadow edge so it shows around the ball that hides the "
      "umbra. "
      "0 is a hard shadow.");
  tuned_slider("flashlight source", c.flashlight.source_strength,
      dc.flashlight.source_strength, 0.0F, 64.0F,
      "Brightness of the lamp emitter on the head (the eye's iris segments): "
      "the ball's reflection of it is the glint, so raise it until that blows "
      "out through bloom.");
  tuned_slider("flashlight gloss", c.flashlight.gloss_strength,
      dc.flashlight.gloss_strength, 0.0F, 40.0F,
      "Brightness of the ball's glossy catch of the beam: the broad highlight "
      "that makes the chrome light up. Raise until it blows out.");
  tuned_slider("flashlight gloss tight", c.flashlight.gloss_power,
      dc.flashlight.gloss_power, 1.0F, 32.0F,
      "Breadth of the ball's glossy catch near the ball: lower spreads it "
      "over "
      "the ball, higher tightens it to a smaller spot.");
  tuned_slider("flashlight gloss grow", c.flashlight.gloss_grow,
      dc.flashlight.gloss_grow, 0.0F, 0.5F,
      "How much the glossy spot spreads (and dims) with distance, like a cone "
      "footprint: 0 holds a fixed-breadth spot, higher grows it as the ball "
      "gets farther.");
  ImGui::Spacing();

  ImGui::Checkbox("debug raw ball", &c.debug_ball_raw);
  ImGui::SetItemTooltip("%s",
      "Show the ball reflection undimmed, to tell a real black artifact from "
      "the dark belly crushed by the dim factor.");
  ImGui::SameLine();
  ImGui::Checkbox("show mirror", &c.show_mirror);
  ImGui::SetItemTooltip("%s",
      "Show the flat mirror wall along the world's -z edge. Off shows the "
      "bare "
      "terrain box.");
  ImGui::Checkbox("debug aa edges", &c.debug_aa_edges);
  ImGui::SetItemTooltip("%s",
      "Flat-tint the pixels the adaptive-AA resolve supersamples: red = "
      "geometry silhouette, blue = target reticle. Pixels left shaded were "
      "judged flat interiors. Verifies edge detection.");
  ImGui::TreePop();
}

// Target reticle: the in-world laser projection marking where the active tool
// aims (toggle the dig tool with the 1 key).
inline void draw_reticle_section(render_config& c, const render_config& dc) {
  if (!ImGui::TreeNode("Target reticle")) return;
  tuned_slider("spin rate", c.reticle.spin_rate, dc.reticle.spin_rate, -3.0F,
      3.0F, "How fast the hexagon reticle turns, radians per second.");
  tuned_slider("outer radius", c.reticle.outer_radius, dc.reticle.outer_radius,
      0.2F, 6.0F,
      "Outer hexagon size (apothem), world units; roughly the dig footprint.");
  tuned_slider("outer clip", c.reticle.outer_clip, dc.reticle.outer_clip, 0.9F,
      1.16F,
      "Circular clip on the outer hexagon (times the apothem): below 1.155 it "
      "cuts the corners for a round-aperture look, at 1.155 it shows the full "
      "hexagon.");
  tuned_slider("inner radius", c.reticle.inner_radius, dc.reticle.inner_radius,
      0.001F, 3.0F, "Inner crosshair hexagon size (apothem), world units.");
  tuned_slider("outer line", c.reticle.outer_line, dc.reticle.outer_line,
      0.01F, 0.5F, "Half-thickness of the bold outer ring, world units.");
  tuned_slider("inner line", c.reticle.inner_line, dc.reticle.inner_line,
      0.005F, 0.3F,
      "Half-thickness of the fine inner crosshair and its spokes, world "
      "units.");
  tuned_color("color", c.reticle.color, dc.reticle.color,
      "Reticle glow color.");
  tuned_slider("glow", c.reticle.strength, dc.reticle.strength, 0.0F, 10.0F,
      "Reticle glow brightness.");
  tuned_slider_int("inner spokes", c.reticle.inner_spokes,
      dc.reticle.inner_spokes, 0, 6,
      "Crosshair spokes from the center to the inner hexagon vertices (0 "
      "none, "
      "3 alternating, 6 all).");
  tuned_slider("max dig distance", c.reticle.max_dig_distance,
      dc.reticle.max_dig_distance, 2.0F, 64.0F,
      "Max dig reach from the ball, world units: a target farther than this "
      "drops the inner crosshair and blocks the dig, the same as when the "
      "ball "
      "occludes the aim.");
  ImGui::TreePop();
}

// Animation rigging: knobs for posing and animating the head, also handy when
// debugging.
inline void
draw_animation_rigging_section(avatar_tuning& t, const avatar_tuning& d) {
  if (!ImGui::TreeNode("Animation rigging")) return;
  tuned_slider("front offset", t.front_offset_deg, d.front_offset_deg, -180.0F,
      180.0F,
      "Rotate the head's front (and the cockpit eye) off the camera heading. "
      "Mainly to animate the UFO shaking its head; also brings the back of "
      "the "
      "dome into the mirror when debugging.");
  ImGui::TreePop();
}

#pragma endregion
#pragma region Panel

// Draw the live tuning panel: the avatar feel and saucer shape (editing `t`
// against defaults `d`), and the shading config (editing `c` against `dc`).
// "Reset all" restores everything; the "freeze camera" checkbox toggles the
// observer freeze (`freeze_camera`), "lock position" the treadmill
// (`lock_position`), and "uncap fps" the benchmark vsync-off (`uncap_fps`).
// Per-field descriptions are hover tooltips. A modified field is tinted and
// gains an inline reset.
// The window opens centered at a readable size (ImGui's .ini persistence is
// disabled, so this default holds every run).
inline void draw_config_panel(avatar_tuning& t, const avatar_tuning& d,
    render_config& c, const render_config& dc, bool& freeze_camera,
    bool& lock_position, bool& uncap_fps, bool& log_collision, body_params& bp,
    const body_params& bpd, bool& flatten_requested, bool& tunnels_requested,
    float& run_multiplier) {
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
  ImGui::SameLine();
  if (ImGui::Button("Flatten terrain")) flatten_requested = true;
  ImGui::SetItemTooltip("%s",
      "Flatten the whole world to a level plane at the ball's feet: a test "
      "track for measuring speeds. Overwrites the terrain, so the dug shape "
      "is "
      "lost.");
  ImGui::SameLine();
  if (ImGui::Button("Dig test tunnels")) tunnels_requested = true;
  ImGui::SetItemTooltip("%s",
      "Carve nine straight tunnels in front of the ball, 10 to 90 degrees in "
      "10-degree steps, side by side along the same heading: a reproducible "
      "grade fixture for the climb and slip tests. Flatten first.");
  ImGui::Checkbox("freeze camera", &freeze_camera);
  ImGui::SetItemTooltip("%s",
      "Observer mode: pin the camera in place and draw the saucer head, "
      "normally hidden because the camera rides inside it. The mouse and "
      "movement keys then turn and move the ship in front of the fixed "
      "camera, "
      "so you can inspect it from any side. Off rides the head again.");
  ImGui::SameLine();
  ImGui::Checkbox("lock position", &lock_position);
  ImGui::SetItemTooltip("%s",
      "Treadmill: hold the body in place (so the distance to the mirror stays "
      "fixed) while the saucer still tilts and spins as if moving, so a "
      "movement key animates it without changing where it is.");
  ImGui::SameLine();
  ImGui::Checkbox("uncap fps", &uncap_fps);
  ImGui::SetItemTooltip("%s",
      "Benchmark: drop the vsync cap so the frame rate floats above the "
      "refresh rate (the image tears). Lets the title-bar FPS read true GPU "
      "cost for a specific view, e.g. dollied right up to the ball. Needs a "
      "tearing-capable swapchain; otherwise it has no effect.");
  ImGui::SameLine();
  ImGui::Checkbox("log collision", &log_collision);
  ImGui::SetItemTooltip("%s",
      "Debug: write a per-frame CSV of the ball's collision state (position, "
      "velocity, probe normal/distance, push, wall normal, the grounded and "
      "overhead flags) to collision_log.csv in the prefs folder, to diagnose "
      "settle instability. Off closes the file.");
  draw_body_section(t, d, c, dc);
  draw_head_section(t, d, c, dc);
  draw_saucer_section(t, d, c, dc);
  draw_dome_section(t, d, c, dc);
  draw_eye_section(t, d, c, dc);
  draw_antenna_section(t, d, c, dc);
  draw_movement_section(t, d);
  draw_body_physics_section(bp, bpd, run_multiplier);
  draw_sky_section(c, dc);
  draw_terrain_section(c, dc);
  draw_march_section(c, dc);
  draw_render_section(t, d, c, dc);
  draw_reticle_section(c, dc);
  draw_animation_rigging_section(t, d);
  ImGui::End();
}

#pragma endregion

} // namespace corvid::cuda
