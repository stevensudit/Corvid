# A new kind of physics

Our initial goal is to survey the code for places where we took unwise
shortcuts, faking what should just be calculated according to physics. This is
usually a matter of failing to model features as a real thing in the scene, a
light, a material, a reflected surface, that all ray paths see, rather than a
decal the primary view paints on. It also comes from such errors as tuning the
size of the umbra/penumbra by hand instead of doing the math. This is still just
a game demo, not a physics simulator that astronauts depend upon with their
lives; we're allowed to cut corners if we decide it makes sense to. But it has
to be a decision.

Physics here is not just movement. It spans two domains: MECHANICS (the avatar's
motion, collision, and the terrain that falls) and OPTICS (how light transports
through the scene: reflection, refraction, the camera lens, the materials a ray
hits). The avatar/flashlight LIGHTING slice of optics already has its own survey
in `lighting.md` (light sources, shadows from the lamp, the gloss lobe, night);
this doc points to it rather than repeating it, and covers the rest of the
optics, the reflection and refraction model, the raymarch and normals, the sky,
and the camera, alongside the mechanics.

## Guiding rule (see the `feedback_scene_correctness_over_fakes` memory)

Model the thing as real, then let the result fall out of it. In mechanics that
means a body with mass and a contact with friction, so motion comes from forces.
In optics it means a material with a reflectance and a transport with bounces,
so the look comes from light. The tell is the same in both: a hand-picked
constant or knob standing in for a physical quantity. A pair bracketing one
quantity is the loudest version: the rig's `accel_approach`/`brake_approach`
(one drive against one friction) and `max_climb_deg`/`run_climb_mult` (one
traction limit) were exactly this, and keystone A collapsed both into a single
friction contact. A lone eyeballed multiplier is the same smell (the mirror's
flat `0.9`, the ball's `dim`/`tint`, the march's `lipschitz` fudge).

Not every constant is a smell, though. One that styles a quantity that is itself
a free choice (the wireframe's cell count, a tilt gain, the strength of a lean)
is taste on top of a correct base, which is fine and often good; the smell is a
constant standing in for a quantity real physics would compute (a penumbra
width, a reflectance, a step size). Fudging what is already correct, for feel, is
allowed; faking a base that is not there is the bill that comes due.

A diagnostic aside, the same one lighting leaned on: the mirror is a second ray
path onto the avatar, and the `locked` treadmill parks the body at a fixed
distance from it ("so the distance to the mirror stays fixed for testing"). A
fake that only holds up in the primary view breaks in the mirror: a
view-dependent highlight that does not match (lighting's crescent), or the ball
driving through the mirror because it has no collision. The mirror is also where
the sanctioned tilt fudges below are watched and tuned, so it tells the two
apart: a styling fudge should still read right there, a faked base will not.

## Sanctioned fudges (decided: appearance only, on top of correct movement)

Two effects are fudged on purpose, because the real motion read as dull. They are
not errors and not on the fix list. The movement itself is unchanged and correct;
only the appearance is styled, which is the allowed case, taste on top of a
correct base. They are recorded here so they are not mistaken for corners to cut,
and so the decision stays a decision.

- S1. Steer tilts the wireframe tread. While steering, `ball_steer_phase` drifts
  the ball's hex grid sideways (in the tread's `v`, across the roll) in
  proportion to the heading's turn rate, capped to a readable rate. The ball's
  arc is already physically correct; the drift only sells the turn, which the
  ball-tracking camera would otherwise hide. Keep it (the gravity memory says
  so).

- S2. The dolly exaggerates the saucer's tilt. The saucer's helicopter tilt is
  driven by the head's own velocity, which the zoom dolly produces along with
  driving and turning; the tilt gain is dialed up past life so a dolly (or a
  move) leans the saucer with flair. The head motion is real; only the size of
  the lean is exaggerated for show.

The line against the corners below: a sanctioned fudge styles a base that is
already correct (the arc, the head motion); a corner cut fakes a base that is not
there (a friction contact, a reflectance, a distance field). The first is taste;
the second is a debt.

## As built (what exists in code now)

### Mechanics

- Rigid body (`avatar_body`): motion comes from forces. A drive force commands
  traction at the contact, bounded by the friction budget (`mu * normal load`,
  gravity's load only, floor-only); a quadratic drag sets the cruise speed
  (`v = sqrt(drive_acceleration / drag)`) so a bigger drive settles higher
  super-linearly, and a constant rolling resistance brakes a coast to rest.
  Gravity integrates the vertical (semi-implicit Euler). On a floor the spin
  couples to travel (rolling without slipping), airborne it is free (ballistic,
  no air control). The body is host math, CUDA-free and unit-tested; the rig is
  posed from it through `drive_from_body`.
- Contact + settle (`avatar_body::resolve_contact`): the body takes a
  `body_contact` (the seam) from the same stale center probe, widened to a
  tolerance band (`ground_tol`). It pushes out of an overlap and cancels motion
  into the surface only at real contact (`penetration >= 0`), so gravity seats
  the ball on the surface instead of pinning it at the band's edge; the engine
  then applies the probe's wall/ceiling push (`collision_damp`) and fences the
  ball one radius inside the world box (`fence_body`). A jump is an impulse in a
  `jump_up` blend of straight up and the contact normal, fired from a held
  request when on a floor and not rising, so it lands on the next ground
  contact. Slope behavior is the friction angle (`tan theta == mu`): the ball
  holds below it and slides above, with no climb-angle cutoff.
- Collision probe (`ground_probe_kernel`): one center sample for the floor
  normal (the density gradient) and a signed distance (density read as an
  approximate SDF, `-density*2e/|g|`); a fixed fan of 8 equator plus 5 upper
  points for walls and ceilings. One thread, read back one frame late.
- Dig + crush (`field_ops.cuh`): `dig_kernel` subtracts a spherical brush of
  density; `crush_kernel` wears a groove and stains the color under the rolling
  ball, scaled by the frame's lateral roll.
- Terrain gen (`world_gen.cuh`): a heightfield slumped to the angle of repose by
  repeated thermal (talus) erosion. The one place physics is actually modeled,
  the static cousin of the unbuilt dirt collapse.
- Saucer rig (appearance): the wireframe roll (scroll = arc / radius, correct),
  the steer-tread drift, and the helicopter tilt are animation on top of the
  movement; the two deliberate ones are the Sanctioned fudges above.
- Designed but unbuilt (`voxel_world.md`): the hardness dig gate, dirt stability
  and collapse, conserved material, and object physics.

### Optics

- Raymarch + normal (`density_field`): a sphere-trace that sizes steps from an
  assumed `march_lipschitz` max slope (capped at `march_max_step_voxels`,
  grazing-accepted at `hit_epsilon`, bisection-refined); the surface normal is
  the density gradient.
- Reflection (`scene_render.cuh`): the mirror reflects the world one bounce and
  multiplies it by `0.9`; the ball reflects the world one bounce and applies
  `dim`/`tint` plus an `ambient_floor`. Neither reflects itself; no recursion.
- Materials: terrain is Lambert diffuse plus flat ambient (`shade_terrain_hit`);
  the ball is the dimmed reflection plus a faked gloss lobe; no Fresnel, no
  roughness, no transmission.
- Sky (`sky_color`): a zenith-to-horizon gradient plus a `powf` sun halo and
  core; night is the gradient times `0.03`.
- Camera (`camera_rays::ray_direction`): a pinhole, with a `fisheye_amount` knob
  blending rectilinear toward equidistant for a cosmetic barrel.
- Post (`render_kernel.cuh`): bloom in linear HDR, then a per-channel Reinhard
  tonemap.
- The flashlight/lighting model and its cut corners live in `lighting.md`; not
  repeated here.

## Corners cut, and the correct base each wants

### Mechanics (movement, collision, terrain)

M1. RESOLVED (keystone A). Momentum is real dynamics now. `avatar_body` carries a
mass and applies a drive force at the contact; acceleration is `F/m`, a quadratic
drag sets the cruise speed, and a rolling resistance brakes the coast. The old
velocity-easing (`accel_approach`/`brake_approach`, no mass, no drive force) is
gone, knobs and all. (The "torque-vs-impulse" feel work is still deferred.)

M2. RESOLVED (keystone A). Traction is a friction contact, not a binary flag. The
drive is bounded by the friction budget (`mu * normal load`) and skids when it
exceeds it; the ball's spin is real angular-velocity state, coupled to travel on
a floor (rolling without slipping) and free in the air, so the air-vs-ground spin
is a physical consequence rather than a switched flag. (The normal load is still
gravity's alone, so traction stays floor-only, recorded as a v1 decision below.)

M3. The density field is read as a signed-distance field it is not. The probe's
`surface_dist = -density*2e/|g|` assumes a unit-gradient field, but the geometry
grid is `height - y` at gen (gradient above 1 on any slope) and then dented by
non-distance-preserving dig brushes. Correct base: maintain the geometry grid as
a true SDF with a local re-distance pass after every edit (the same pass
`voxel_world.md` already needs for collapse), or do real sphere-vs-isosurface
contact. (This is the mechanics face of the optics cut O4.)

M4. Collision is a center sample plus a fixed 13-ray fan, not the ball as a
volume. Thin features, sharp edges, and anything between the rays are missed
(why tunnel walls and ceilings are deferred). `ground_tol` and `collision_damp`
are not modeling anything; they are compensation for the sparse sampling and the
stale probe. The world-box `fence_body` is a backstop for when collision fails to
catch the ball. Correct base: a closest-point query over the overlapped region,
or enough samples that the error is below a voxel.

M5. Collision lags one frame, and guards exist to hide it. The probe is launched
after the dig and read back next frame to avoid a stall; the `ground_tol` band,
the "not-rising" guard, and the damp are lag-compensation by their own comments.
A defensible performance decision, but read it as lag-comp, not physics: a
host-side collision against a local field mirror, or eating the stall for one
tiny probe, removes the guards.

M6. RESOLVED (keystone A). Climb is the friction angle now, not a hardcoded
angle. The ball holds on a slope up to `tan theta == mu` and slides above it,
derived from the same friction budget that bounds the drive; the
`max_climb_deg`/`run_climb_mult` knobs and the Run-key boost are gone. It is
exactly "can the contact friction supply the demand," as M1/M2 promised.

M7. Jump reads the contact now (keystone A). The impulse is a `jump_up` blend of
straight up and the contact normal, so a steep face kicks the ball off, and a
held request fires it on the next ground contact (on a floor and not rising)
rather than on any frame. Still open: a speed-coupled jump. Minor.

M8. Digging and the crush groove delete mass; nothing is conserved or displaced.
`dig_kernel` lowers density and the soil vanishes; the crush groove deepens with
no berm; the ball's "weight dig" (the crush track, capped at the equator by the
`walled` flag) sinks it with no soil bearing-capacity behind the cap. Correct
base (`voxel_world.md`): conserved material, so dug or crushed soil goes
somewhere and the collapse/flow physics transports it.

M9. Dirt never falls. A dig can leave a floating overhang or a fully undercut
island and nothing moves; the stability and collapse physics is designed but
unbuilt. The headline mechanics gap. Correct base is already written: the
per-voxel integrity relaxation (where it gives way) plus the
one-hardness-as-cohesion collapse continuum (what moves), in `voxel_world.md`.
The static erosion in world-gen is the proof the idea works.

M10. The mirror, and the floating camera/head, are render-only with no collision.
The ball is fenced only to the world box, so it drives straight through the
mirror plane; the head/camera has no terrain collision, so it can sink into a
hill (`head_seat_offset` already flags the low-ceiling tunnel as the case that
forces a physics pass). Correct base: if the mirror is a wall, put it in the
collision world; give the camera at least a soft push-out so the view does not
bury itself in soil.

### Optics (light transport, reflection, the camera)

O1. Reflection is a flat multiplier, not a material. The mirror multiplies its
reflected color by a constant `0.9`, the ball by `dim`/`tint` plus an
`ambient_floor`, both eyeballed to "read as" a surface ("dark liquid metal rather
than blown-out chrome"). No Fresnel (reflectance rising at grazing angles), no
spectral metal tint as reflectance, no roughness. Correct base: a real
reflectance, Fresnel-weighted, the metal's tint as its F0, a roughness that blurs
the reflection, so the look is the material's, derived. (`lighting.md` item 3
covers the faked gloss lobe sitting on top of this.)

O2. One bounce is the ceiling; there is no inter-reflection. Every reflective path
is a single bounce that then shades against terrain/head/sky only: the mirror
does not test itself (no mirror-in-mirror), the ball reflects neither itself nor
the mirror, nothing reflects a reflection. Two mirrors face to face show nothing
between them. Correct base: a bounded recursive or iterative bounce (a small
depth cap), the natural home for any later GI bounce too.

O3. Refraction and transparency are absent. Nothing transmits, bends, or
caustics; the "glass" iris segments emit but do not refract (see `lighting.md`'s
iris-as-source). A fine decision now, but record it: the moment anything wants to
be glass, water, or ice there is no transmission path, no index of refraction, no
Snell bend. Correct base when needed: a transmitted ray with an index of
refraction at the hit, sharing the bounce budget of O2.

O4. The raymarch and the gradient-normal approximate a distance field the grid is
not. The march steps by `|density|/lipschitz` (capped, grazing-accepted), and its
own comment warns "lipschitz must exceed the field's steepest slope, or a dig
wall steeper than it can be overshot" (the ray punches through). The normal is
the density gradient, true only where the field is unit-gradient. This is the
optical face of M3. Correct base: the same maintained true SDF (keystone B) makes
the march exact-stepped and the normal correct, and the lipschitz/step/epsilon
constants stop being fudge.

O5. The sky is a painted gradient with a fake sun, not an atmosphere. `sky_color`
lerps zenith to horizon and adds a `powf(dot, exp)` halo and core at the sun
direction; night is the gradient times `0.03`. No atmospheric scattering, no real
sun disk emitter, and the sky is not an area light the scene samples (terrain
ambient is a flat constant). Correct base, if it ever matters: a scattering sky
that is itself the light, so sun color, horizon glow, and ground ambient are one
model. Likely an acceptable decision for a digger; record it.

O6. There are no shadows except the ball on the flashlight. Terrain takes a
single-direction Lambert sun term with no shadow ray, so nothing shadows the sun:
no terrain self-shadowing, no hill shadowing the ball, no avatar shadow on the
ground. The only occlusion is the ball sphere against the lamp (`shadow_sphere`),
and ambient is a flat constant with no ambient occlusion. Correct base: a shadow
ray for the sun (the march already exists) and AO from the field, so contact and
crevices darken. (`lighting.md` owns the lamp-shadow softness; this is the
missing sun and ambient half.)

O7. The camera is a pinhole with a cosmetic barrel, no real lens. `ray_direction`
blends rectilinear toward equidistant by a `fisheye_amount` knob; there is no
focal length, aperture, depth of field, or chromatic aberration, and the barrel
is a hand-picked bend. Acceptable and probably permanent, but a knob standing in
for an optic; record it.

O8. Bloom and Reinhard tonemap are display fakes, not a camera response. Bloom
blurs the brights to fake lens/sensor scatter (not a measured point-spread
function); `reinhard_tonemap` is `c/(1+c)` per channel, an arbitrary roll-off
with no white point that can shift hue as one channel saturates. Reasonable
display-side choices that likely stay, but taste curves, not physics: do not
mistake the tonemap for exposure or the bloom for real glare.

## The shape of the fix (the keystones)

Four keystones, each collapsing a cluster, plus a set of display-side cuts that
are decisions to ratify rather than work to do.

- A. DONE. The ball is a real rigid body with a mass and a friction contact
  (`avatar_body`); momentum (M1), traction, slip, and the air-vs-ground spin
  (M2), climbing (M6), and a contact-aware jump (M7) are now derived from it. The
  rig keeps the camera and animation harness, posed from the body. The mechanics
  analogue of lighting's "give the light a source size."

- B. Maintain the geometry grid as a true SDF, re-distanced after every edit.
  This is a shared mechanics/optics keystone: honest contact distance (M3), a
  volume collision (M4), dropping the lag guards (M5), and an exact march with a
  correct normal (O4) all rest on the field actually being a distance field, and
  the same re-distance pass is the cost the collapse design (M9) already pays.

- C. Conserve material and build the collapse/flow continuum. Excavation and the
  crush berm moving soil instead of deleting it (M8) and dirt falling when
  undercut (M9) are the two halves of the unbuilt `voxel_world.md` design;
  hardness already does triple duty there (dig gate, stability, collapse).

- D. Make reflection a material with a bounded multi-bounce. A Fresnel-weighted
  reflectance carrying the metal's tint and a roughness (O1), evaluated through a
  small bounce budget (O2) that refraction (O3) shares, replaces the `0.9` and
  the `dim`/`tint` multipliers and the one-bounce ceiling with one transport
  model. The optics analogue of A: the look becomes the material's, derived. Runs
  alongside `lighting.md`'s sized-source keystone (which, with the sun shadow ray
  and AO of O6, is the lighting half of the optics pass).

Display-side cuts are decisions, not builds: the painted sky (O5), the cosmetic
lens (O7), and bloom plus tonemap (O8) are reasonable to keep, like the sanctioned
fudges above. Ratify them as deliberate, and do not let a later "make it real"
reflex treat them as bugs.

Order: A is done. B is next (it unlocks honest collision and an exact march at
once); C and D after (the big terrain-physics and the material/transport builds),
with the lighting pass alongside D. Tune taste on top once each base is right.

## Build approach (keystone A, as built)

The rigid body was built as a standalone `avatar_body`, not by bending
`avatar_rig` in place. "Body" is the canonical name for the ball; the rig keeps
the camera and animation harness (boom, head, eye, tilt, spin) and the
sanctioned-fudge roll and steer, all of which read the body's outputs. The split:
the momentum, integration, and spin dynamics went to the body; the wireframe roll
and steer drift stayed in the rig.

The body is CUDA-free and host-testable. Contacts come in through a plain
`contact` struct, the seam: the GPU probe fills it in the game, while synthetic
floors, ramps, and walls fill it in the tests. (GPU code can be unit-tested too,
just with more effort; it is not needed here, since the dynamics are host math.)
Keystone B later swaps a true contact in through this same seam without touching
the body's dynamics.

The core of the fix is to separate spinning the ball from its consequences.
Today translation and the visual roll are conflated and the air-vs-ground spin is
a flag (M2). The body instead carries angular velocity as real state, with
rolling-without-slipping a friction constraint coupling spin to translation, so
"spin the ball" and "what the spin does" are distinct and the slip between them is
a real quantity, not a switch.

One v1 decision to record: traction is floor-only, and "floor" is the crude,
gravity-aligned `normal.y > 0` (a ramp counts up to vertical, the boundary
arbitrary). A sphere is not a tire, so it has friction at any contact and could
grip given something to press it there, and the presser need not be gravity: a
wedge or chimney brace could load a wall, and the ball's own inertia (the
centripetal reaction of a curved track) could stand in for gravity, so a fast
enough ball could run a loop and keep traction at the top, where the normal
points down. The single-contact, drive-on-terrain model pursues none of that:
the normal load is gravity's alone, so a wall or ceiling only stops motion into
it. Whether a drive slips is then set by the surface's friction budget (its
coefficient and slope), not by Walk versus Run: both grip on good ground, either
skids on a steep or slick one.

The body carries whatever constants it needs (mass, the solid-sphere inertia,
friction, a drive force, gravity, jump). Some surface in ImGui and some do not;
we do not force the config UI to mirror the internal model. An A/B checkbox
compared the body against the old rig mechanics, with the treadmill and mirror
for watching it; the body won, so `avatar_rig`'s mechanics (`move`/`settle`) are
gone and it is now the camera/animation harness alone.

Tests are a Catch2 `.cu` in the CUDA bucket, so the host/device math headers
compile under one toolchain. Assert physical invariants against synthetic
contacts: settles one radius off a flat floor, holds on a slope up to the
friction angle and slips above it (replacing the hardcoded `max_climb_deg`), a
jump apex of `v^2 / 2g`, a friction stop distance, momentum carried across a
slope, and rolling-without-slipping (`omega * r == v`).

## Status / next

Keystone A is built and is now the only avatar physics: `avatar_body` drives the
live avatar and `avatar_rig`'s old `move`/`settle` mechanics have been removed
(it is the camera and animation harness, posed from the body). B (a true contact
from a maintained SDF) is next, fed through the same contact seam; M3, M4, M5 and
the optics O4 all rest on it. C and D follow.
