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
