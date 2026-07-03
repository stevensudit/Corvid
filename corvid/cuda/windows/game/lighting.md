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
  - The ball's flashlight glint is the real reflection of the emissive iris
    (see Iris-as-source below); the faked Blinn-Phong gloss lobe that once sat
    on top of it is gone (glare step, see items 2 and 3).
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

2. RESOLVED (glare step). The faked gloss lobe and its `gloss_power`/`gloss_grow`
   "size near + size far" knobs are gone. The glint is the ball's real
   reflection of the emissive iris, so its breadth is the emitter's apparent
   angular size (source size / distance) for free, not a hand-tuned lobe.

3. RESOLVED (glare step). The duplicate Blinn-Phong lobe is removed; the real
   reflection of the emissive iris IS the highlight, its brightness the
   emitter's `source_strength` (HDR, so it blows out through bloom). Any later
   sparkle-stabilizing fudge goes on top of this real base, in a realistic form.

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

7. The beam is a fixed circular pool regardless of range or grazing angle. A real
   cone's footprint on a surface is an ellipse that grows with distance and
   stretches as the beam rakes a face obliquely. Correct base: size and shape the
   pool from the cone half-angle, the hit distance, and the surface normal, so it
   elongates when aimed far or across an angled surface.

8. The beam is one evenly-lit cone, but the emitter is not a disc: it is the iris,
   six glass segments around a dark pupil and spokes. The real beam is six
   overlapping sub-beams and should carry that structure (lobed edges, brighter
   where the sub-beams overlap, gaps from the pupil and spokes), not a uniform
   wash. Correct base: derive the beam from the iris emitter's actual shape rather
   than a single circular cone.

(Aiming the lamp at the ball stresses several of these at once: the genuine
reflection is (3), the genuine shadow with a real umbra and penumbra is (1), and
all of it reads worst at night, where the faked terms have no daylight to hide
in.)

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

Ordered as shape -> glare -> shadow. Step 1 (shape: the visible flashlight air
cone) shipped. Step 2 (glare) is done here: the faked ball gloss lobe is ripped
out and the glint is now the ball's real reflection of the emissive iris (items
2 and 3 resolved). Next is shadow (item 1): give the flashlight a physical
source size so the umbra and penumbra fall out of geometry, retiring the
`shadow_softness` knob. That same source size then feeds the light-entity
promotion (item 4) and, separately, the reticle world-decal (item 5).
