# Avatar / flashlight lighting: correctness pass (the "lighting mulligan")

A correctness pass on the avatar and flashlight lighting model, the same move
mulligan.md made for movement: back off the faking and keep the physics. The
features built this session (HDR + bloom, barrel, flashlight, night, ball gloss,
shadow) work but arrived through a fake-then-backpedal cycle: a corner cut looks
good in the primary daytime view, then the flat mirror, night, or a zoom exposes
it, and we patch. This doc surveys the corners we cut and the geometric base each
should rest on, so we rebuild once instead of patching N times.

Guiding rule (see the `feedback_scene_correctness_over_fakes` memory): model each
thing as a real scene entity every ray path sees, derived from scene geometry.
Tuning knobs are for taste ON TOP of a correct base, never a substitute for it. A
pair of knobs bracketing a physical quantity (a "size near" and a "size far", a
free "softness") is the tell that we are hand-faking what geometry should
produce.

## As built (what exists in code now, all uncommitted on windowes-cuda-102)

- Post: HDR `float4` buffer -> bloom (prefilter + separable blur) -> Reinhard
  tonemap, in render_kernel.cuh. Barrel/fisheye blend in camera.cuh
  `ray_direction` (`fisheye_amount`).
- Flashlight: a headlamp, `origin` = eye, `direction` = view forward, written
  per frame by the engine. In voxel_render.cuh:
  - `flashlight_spot`: soft cone (`cone_degrees`/`softness`) + range `fade` +
    ball shadow.
  - `flashlight_terrain`: cone * fade^2 * Lambert, ball-shadowed.
  - `flashlight_gloss`: a Blinn-Phong half-vector lobe on the ball (energy-
    conserving spread so a broader spot dims), with an N.L gate so a mirror does
    not glare the far side.
- Iris-as-source: the head eye's glass segments emit `source_strength` while the
  lamp is on (scene_render.cuh `shade_head`), so the ball's reflection of the
  head carries the real glint.
- Night: `cfg.night` gates the sun (diffuse + specular off) and dims ambient on
  terrain and head, and darkens the sky.
- Knobs threaded through `render_config::flashlight_params` + the panel.

## Corners cut, and the correct base each wants

1. Shadow softness is a free knob (`shadow_softness`). CONCEPTUALLY WRONG: a
   point light has a hard shadow; a penumbra exists only because the light has
   area. Correct base: give the flashlight a physical source size; the umbra and
   penumbra then fall out of (source size, occluder = ball, the two distances).
   Softness stops being a knob.

2. Gloss spot size uses two knobs, `gloss_power` (near breadth) + `gloss_grow`
   (spread with distance). This is the "size near + size far" anti-pattern.
   Correct base: a specular highlight's breadth is the light's apparent angular
   size (source size / distance), one physical quantity, from the same source
   size as (1). (The brightness-vs-distance was already made correct this session
   via energy conservation; the size is still faked.)

3. The gloss lobe duplicates the iris reflection. We have the real thing (the
   ball reflecting the emissive iris) but judged it too small/jittery, so we
   added a fake Blinn-Phong lobe on top. Correct base: make the source big and
   bright enough (a sized emitter) that its real reflection IS the highlight; the
   lobe, if kept at all, is an artistic layer on that base, not the base.

4. The flashlight is a set of per-surface fakes (`flashlight_terrain` +
   `flashlight_gloss`), not one light the whole scene evaluates. So the flat
   mirror shows lit ground (the real terrain it reflects) but cannot re-reflect
   the beam, and the ball's catch in the mirror is a view-dependent fake (the
   crescent that "doesn't match"). Correct base: one spot-light entity (position,
   direction, cone, source size) that terrain, ball, head, AND mirror reflections
   all sample, so a mirror can bounce it.

5. The reticle is painted only on the primary terrain (`apply_reticle` in
   `shade_primary_ray`). It is not in the world, so the mirror shows none of it
   and the ball occludes the outer ring without reflecting it (the hand-wave that
   fails at night). Correct base: a projected world-space mark at the pick point
   that any ray path (primary, ball reflection, mirror) samples.

6. Night is hardcoded scales (ambient * 0.1, sky * 0.03, sun off). Minor; this is
   an acceptable finger-on-the-scale, but note it is not a real day/night model.

## The shape of the fix

The keystone for 1-4 is the same: make the flashlight a real light with a
**source size** (the iris is the obvious emitter, radius and brightness known).
From that one quantity the shadow penumbra (1), the highlight breadth (2), and a
single reflected-source highlight (3) all derive, and promoting it to a light
entity the mirror can bounce (4) is the natural next step. The reticle (5) is a
separate but parallel move: a world decal instead of a primary-view paint. Do the
light first; 1-3 collapse into it, then 4, then 5. Tune taste on top once the
base is right.

## Status / next

Survey only. No code yet on the correctness pass. Resume by reviewing this list
with the user, turning it into an ordered to-do, then building the sized-light
base first.
