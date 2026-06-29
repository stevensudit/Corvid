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
quantity is the loudest version (`accel_approach` plus `brake_approach` for one
drive against one friction; `max_climb_deg` plus `run_climb_mult` for one
traction limit), but a lone eyeballed multiplier is the same smell (the mirror's
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

## As built (what exists in code now, uncommitted on windowes-cuda-102)

### Mechanics

- Drive + momentum (`avatar_rig::move`): `ground_vel` eased toward an input
  target velocity by the `1 - exp(-rate*dt)` idiom, faster `accel_approach`
  toward a held target, gentler `brake_approach` toward rest, snapped to zero
  below `coast_min`. Eased only when `grounded` (traction); airborne the
  velocity is preserved (ballistic, no air control).
- Gravity + settle (`avatar_rig::settle`): `vel_y -= gravity*dt`, integrate,
  then push the center out along the surface normal by the penetration, damped by
  `collision_damp`. A jump sets `vel_y = jump_speed`. `grounded` is a contact
  band; climbing is gated by slope steepness (`max_climb_deg`, raised by
  `run_climb_mult` while running). `fence` clamps the ball one radius inside the
  world box.
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

M1. Momentum is velocity easing, not dynamics. `move` eases `ground_vel` toward a
target speed with hand-tuned `accel_approach`/`brake_approach`; there is no mass,
no drive force, no rolling resistance. The "weight of getting the heavy ball
going" is an exponential rate constant, and the two knobs are the "size near +
size far" anti-pattern. Correct base: a rigid ball with a mass, an applied drive
force or torque, and rolling friction, so acceleration is `F/m` and braking is
the same friction with the throttle off. (The deferred "torque-vs-impulse" feel
work.)

M2. Traction is a binary `grounded` flag, not a friction contact. Perfect grip on
the ground and zero in the air, nothing between: no coefficient, no partial slip,
no surface dependence. The same flag also picks the wireframe's spin source,
commanded spin in the air and actual travel on the ground, switched hard at the
contact (the roll itself, scrolling at arc over radius, is already correct; it is
only this air-vs-ground switch that is faked). Correct base: a friction force at
the contact bounded by `mu*N`; the ball slips when the demanded force exceeds it,
and the wheel slip (today a display value, `wheel_spin` vs `moving`) becomes the
real, continuous consequence of that switch.

M3. The density field is read as a signed-distance field it is not. The probe's
`surface_dist = -density*2e/|g|` assumes a unit-gradient field, but the geometry
grid is `height - y` at gen (gradient above 1 on any slope) and then dented by
non-distance-preserving dig brushes. Correct base: maintain the geometry grid as
a true SDF with a local re-distance pass after every edit (the same pass
`voxel_world.md` already needs for collapse), or do real sphere-vs-isosurface
contact. (This is the mechanics face of the optics cut O4.)

M4. Collision is a center sample plus a fixed 13-ray fan, not the ball as a
volume. Thin features, sharp edges, and anything between the rays are missed
(why tunnel walls and ceilings are deferred). `contact_tol` and `collision_damp`
are not modeling anything; they are compensation for the sparse sampling and the
stale probe. The vertical `fence` is itself documented as "a last resort if
collision ever fails to catch it." Correct base: a closest-point query over the
overlapped region, or enough samples that the error is below a voxel.

M5. Collision lags one frame, and guards exist to hide it. The probe is launched
after the dig and read back next frame to avoid a stall; the `ground_tol` band,
the "not-rising" guard, and the damp are lag-compensation by their own comments.
A defensible performance decision, but read it as lag-comp, not physics: a
host-side collision against a local field mirror, or eating the stall for one
tiny probe, removes the guards.

M6. Climb is a hardcoded angle doubled by a key, not a friction result. Whether
the ball climbs is `normal.y >= cos(max_climb_deg)`, widened by `run_climb_mult`
while Run is held. Physically that is set by friction, drive force, and gravity,
not a fixed angle and certainly not a Shift key. A deliberate gameplay knob that
probably stays, but record it as a knob standing in for traction; with M1/M2 real
it would just be "can the contact friction supply the demand."

M7. Jump is a world-up velocity set that ignores the contact. `vel_y = jump_speed`
always launches straight up regardless of the surface normal or state. Standard
and fine, but the deferred wants (speed-coupled jump, a jump along the contact
normal so a steep face kicks you off) both need the jump to read the contact.
Minor.

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

- A. Make the ball a real rigid body with a mass and a friction contact. From it,
  momentum (M1), traction, slip, and the air-vs-ground spin (M2), climbing (M6),
  and a contact-aware jump (M7) all become derived quantities. The mechanics
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

Order: A first (most visible, stands alone); B next (it unlocks honest collision
and an exact march at once); C and D after (the big terrain-physics and the
material/transport builds), with the lighting pass alongside D. Tune taste on top
once each base is right.

## Build approach (keystone A)

Build the rigid body as a standalone `avatar_body`, not by bending `avatar_rig`
in place. "Body" is the canonical name for the ball; the rig keeps the camera and
animation harness (boom, head, eye, tilt, spin) and the sanctioned-fudge roll and
steer, all of which read the body's outputs. Split `move`: the momentum,
integration, and spin dynamics go to the body; the wireframe roll and steer drift
stay in the rig.

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
friction, a drive force, gravity, jump). Some may surface in ImGui and some may
not; we do not force the config UI to mirror the internal model. To compare feel,
keep both paths runnable and add an A/B checkbox that switches the live avatar
between the old rig mechanics and the new body (the treadmill and mirror are
already there for watching it). Gut `avatar_rig`'s mechanics only once the body
wins.

Tests are a Catch2 `.cu` in the CUDA bucket, so the host/device math headers
compile under one toolchain. Assert physical invariants against synthetic
contacts: settles one radius off a flat floor, holds on a slope up to the
friction angle and slips above it (replacing the hardcoded `max_climb_deg`), a
jump apex of `v^2 / 2g`, a friction stop distance, momentum carried across a
slope, and rolling-without-slipping (`omega * r == v`).

## Status / next

Building keystone A. The survey above is the agreed scope; B (a true contact from
a maintained SDF) follows once the body is done, feeding it through the same
contact seam. Scaffolding `avatar_body` and its first test now; the rest of the
list is still survey only.
