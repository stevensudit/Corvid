# Voxel digger: roadmap and design notes

Forward-looking notes for the voxel-digger game, captured from playtesting. These
are intent and design, not as-built. The as-built avatar lives in avatar.md, the
terrain/dirt design in voxel_world.md, and the tuning panel in tuning_panel.md.

## Current state (playtested)

- Weight-dig + collision: the ball digs itself down to "waist" (equator) high and
  then has to rev out. This works as intended.
- The wireframe tread (motion grid) works as expected, including spinning/glowing
  in place while revving stuck.

## Drive model: flight-assist (active control)

The droid drives under active control, not as a passive sphere. With no input the
tread actively brakes the ball to a stop (the deceleration glow is that brake at
work) and holds station on a slope by spending traction uphill, up to the
friction budget. The friction-angle slope-hold and the brake-to-rest are this
active controller, not block-model fakes.

- Flight-assist off (future): like the "airplane mode" toggle in Elite:
  Dangerous, a button to disable the auto-brake so releasing the throttle keeps
  the current velocity (coast/drift), e.g. to turn the head around while still
  moving the same direction. Not built; record it as an option.

## Camera in tunnels: auto-merge and the glass-lens view (decided 2026-06-29)

The head and body are separate things: motion keys drive the body, the head only
dollies and rotates around it. That works in the open but fails in cramped space,
because no camera position that needs *air* (jockey, a "front jockey", a push-out
to the nearest clear cell) survives the case where there is only room for the
ball. So the premise changes: the ball only looks like metal; it is a reflective
force-field the head can enter and reside within, and the one camera position
that always exists is the ball's own center.

Why this is the right pivot, not a workaround: every other viewpoint is
conditional on air that may not exist; the ball center is the unique viewpoint
with no failure mode, because the ball is what defines "there is room for the
ball." The merged eye also sits in a guaranteed spherical pocket, so the
buried-pinhole jump (a single eye point grazing a terrain surface flips hard
between seeing out and seeing dirt as you pan) is gone by construction. And
looking down from the center lets the reticle aim straight beneath the ball,
which a trailing seat cannot, so descending-by-digging becomes natural.

- **Auto-merge, not auto-jockey.** The boom becomes a 1-D slider clamped by the
  terrain along its own axis, inner stop = the ball center (a full merge). Open
  air slides freely to the trailing seat; a tunnel clamps it short; a ball-sized
  pit clamps all the way in. One rule ("the head cannot dolly into dirt; its
  innermost stop is the center") replaces auto-jockey and the camera push-out,
  and needs only a short terrain query along the boom (one frame late, like the
  ground probe). This supersedes the auto-jockey / teleport-to-jockey idea in the
  pathing notes below.

- **The ball is a real glass lens, not a dimmer tint.** From outside it stays an
  opaque one-way mirror (the flat mirror and the ball's reflection in other
  surfaces are unchanged). A primary ray whose eye is inside it refracts at the
  surface and continues into the world. The eye is the head's off-center opening,
  so the straight-ahead ray meets the sphere at normal incidence and stays clean
  (the aim/dig axis), while everything off-axis is coma: radial comet-tails,
  lateral chromatic fringing, a focus tilt. Functional ahead, degraded around, so
  you can work merged but want out, with no artificial penalty. This is also the
  refraction model `physics.md` lists as absent (O3).

- **Build order: geometric first, aperture second.** GEOMETRIC tier (one ray per
  pixel, accurate, tractable): Snell refraction at the surface (index `n`; model
  it as a solid ball lens, where dead-center is clean), per-channel R/G/B for the
  lateral chromatic, Fresnel dimming (~4% head-on, rising at grazing), one
  internal bounce for the faint saucer ghost, a faked corner vignette. APERTURE
  tier: the spherical-aberration blur, the coma comet-flares, and the focus
  blur/tilt, which only an integration over the finite eye opening produces (real
  multi-sample or a faked radial asymmetric blur), decided after seeing geometric.
  The cost is affordable because FPS is highest in a tunnel, the same reason
  expensive dig VFX is affordable there.

- **From inside you still see the treads.** The propulsion-field hex shell is the
  porthole frame, scrolling when you move, the clean in-focus thing at the
  refraction boundary while the world beyond it warps. Flashlight and reticle work
  from inside, refracted.

- **Merged is a real tradeoff, on purpose.** Run is disabled while merged (as the
  reticle already is while running), so merged is slower as well as distorted, a
  reason to eject and a meaningful walk/run distinction. (The WoW failure is
  walk/run with no reason ever to walk, so everyone runs and walking is
  meaningless "RP-walking".)

- **Exit and the deferred ritual.** Exit by dollying out as room allows; ejected
  is the open-ground default, merged the cramped default, the dolly clamp doing
  the switching. Deferred: whether to allow a full merge on open ground at all, an
  MMB-to-enter, and an auto-eject after a few seconds of detecting room.

## Collision: known issues and next work

- Alt-tab while in a hole shifts the view slightly. Not fatal like the old
  fall-through (the dt cap handles that), but it reads as a sign of trouble worth
  chasing. Could not reproduce reliably yet; capture the collision log when it
  happens.
- "Don't let the ball descend into a sub-ball gap." The weight-dig now self-limits
  at the equator, but a pit dug by the beam can still be narrower or deeper than
  the ball. Falling into such a gap is the unfittable case that the equator gate
  does not cover. We expect to need an explicit rule that keeps the ball on top of
  a sub-ball gap instead of letting it descend into a space it cannot fit.

## Mouse-cursor digging (the beam)

The beam (intentional dig, distinct from the weight-dig) needs:

- Let the mouse cursor show where we are digging (an in-world reticle), instead of
  the current center-ray aim.
- Special effects for dirt being blasted off.
- Darken the dirt that remains, the same stain we apply when digging by weight.

Motivation: without these, you can dig a deep pit but cannot shape a sloping tunnel
out of it. The beam + cursor aim + the dig stain are what make a usable ramp.

## World generation (future)

- When we next revisit world-gen, add caves, including openings to the surface.

## Free-flight mode (camera) and its relevance to pathing

### The dolly air-wall

- Dolly back past the limit: a green wireframe hex grid on a sphere briefly flashes
  in the air, and the Body stops moving. Its origin is the Body, its radius the max
  dolly distance. It flashes like a wall to teach the player they cannot escape
  their Body.

### Free-flight (hold Alt or Middle Mouse Button; either works)

- While held, the ball becomes immobile and WASD drives only the head. You free-fly
  around; exceeding the dolly distance flares the air-wall, which stops you.
- Proposed refinement: flare the wall faintly on entering the mode, so you see your
  limits up front, then flare fully and stop only when you touch it.

### Release vs double-tap

- Release Alt/MMB: the mode ends.
- Double-tap Alt/MMB: teleport to jockey position with a special effect of flying
  through a green-hex tunnel at high speed, passing through whatever is between.

### Relevance to pathing (line of sight)

NOTE (2026-06-29): the auto-jockey / teleport-to-jockey solution below is
SUPERSEDED by auto-merge (see "Camera in tunnels" above). The head retracts into
the ball rather than seeking a clear seat, since a clear seat may not exist.

- During digging, the Head can end up with no line of sight to the Body. The
  proposed solution is to trigger the teleport-to-jockey automatically.
- When the ball enters a tunnel and the view is obscured, that likewise triggers
  the teleport-to-jockey. But if we are already in jockey and colliding with the
  ceiling (expected while digging), a brief teleport/tunnel animation should bring
  the camera to in front of the ball.

Playtest 2026-06-28 (deep pit): in jockey position the view almost never got
occluded, even at the bottom of a deep pit, so the extra "camera in front of the
ball" position above may be unnecessary; drop it unless a real occlusion case
turns up. Auto-moving the Head to jockey position is still wanted, but can wait.
