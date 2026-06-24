# Avatar mulligan: a clean model

Development on the game demo has progressed rapidly, growing organically with
many dead ends. At this point we need to stop, rethink what we actually want,
and then change the code to match. The primary goal is to rationalize the
movement model so we can move forward. Cleaning up the tangled config and
retiring dead ends are necessary consequences of that, but not the main point.
Some current features never worked (for example, making the underside of the
Head glow) and should be removed along with the config and code that drive them.

This document is a request for significant changes, not just a new description
of the existing behavior. It builds up a clear model with canonical names and a
declaration of how the pieces are supposed to fit together. Once the open
questions at the bottom are resolved, we will add a section with the specific
steps to bring the code in line.

## The parts

### Avatar = Body + Head

The Avatar consists of the Body and the Head.

### The Body (the Ball)

The Body is currently just one component: the Ball. This is a mirror-shiny
sphere that rests on the ground. It moves by rolling and is constrained by
terrain: it is held down by gravity and unable to squeeze through gaps narrower
than its diameter. It is also the source of the Digging Beam and other similar
Beams.

The Body is what the WASD movement keys control, with the Head following behind,
lagging somewhat. When the Body is in motion, a faint hexagonal grid appears on
its surface, making its rotation visible. This grid exists only to make the
motion legible and is unrelated to the Dome's hex tiling, though the two likely
share the same tiling math.

### The Head

The Head is a little UFO that trails behind the Body, floating in the air. It
acts as the diegetic camera and is visible only in reflective surfaces, such as
that of the Body. It consists of the Saucer and the Dome.

#### The Saucer

The Saucer is a flattened cone on top with a rounded surface below. This lower
surface carries the painted spinning belly (the existing rim lights and hub),
whose spin reflects the Head's movement:

- It spins faster when moving faster, and in reverse when moving in reverse.
- It spins in opposite directions when strafing left versus right.
- When steady, it slowly spins in one direction, then reverses periodically.

#### The Dome

The Dome is tiled hexagonally, and this tiling is always visible as the physical
surface of the Dome. The Dome sits in the implied indentation at the top of the
Saucer. While it is joined to the Saucer, it is able to rotate to some extent.

#### The Eye and the Antenna

The Dome carries an Eye and an Antenna. The Antenna sits 90 degrees above the
equatorial plane and 60 degrees from the Eye. In the resting position, the Eye
points about 30 degrees above the equatorial plane (conventionally 0 degrees),
which puts the Antenna straight up at 90 degrees relative to the ground. These
two features are fixed to the surface of the Dome and never move independently;
instead, the Dome moves and they (along with the tiling) move with it. The
Antenna therefore always keeps its 60-degree offset from the Eye: when the Eye
rotates vertically, the Antenna leans with it.

## Movement and control

Angles below are written in degrees for readability. The config may store them
in radians if that is more natural for the code; what matters is that similar
settings stay consistent.

### Dolly (head trailing distance)

Until we implement a Free Fly mode, the only way the Head can move (translate,
not just rotate) independently of following the Body is by the Dolly adjustment.
This uses the mouse dial (which doubles as the middle button) to change how far
the Head trails the Body. The change is not instantaneous: the Head flies to
the new position at a configurable speed.

The Dolly cannot bring the Head in front of the Body. Its near extreme is the
"jockey" position: above and slightly behind the Body, so that Looking down
gives a nice profile view of it, while Looking level forward puts the Body out
of the line of sight, giving the first-person view we want. The far extreme is
the trailing distance behind the Body.

The one exception to never-in-front is low-ceiling tunnels. If and only if the
Head is at the jockey position, and only for as long as there is no room for the
Head in the tunnel, it automatically shifts to in front of the Body.

### Look / Steer mode

Holding down the right mouse button enters Look/Steer mode:

- Look is when no movement keys are depressed.
- Steer is when they are.

In Look mode, the Eye moves in the direction the mouse does, and because it is
the camera, it changes what we see. Since the Saucer is radially symmetrical,
moving the mouse left and right just rotates the Dome; it does not affect the
Saucer. Looking left and right has no limit: the Dome can yaw a full circle
relative to the Saucer.

Tilting vertically is different. When you move up or down in Look mode, the Dome
rotates to point the Eye in the direction of movement, starting from the resting
position described above (Eye ~30 degrees up, Antenna straight up).

### Dome rotation limits

The Dome is allowed to rotate downward only to the point where the edge of the
Eye is about to touch the Saucer. It is allowed to rotate upward only to the
point where the edge of the Eye is about to touch the "north pole": the position
that is fully vertical from the point of view of the Saucer.

When the Eye attempts to move past either of these limits, the Saucer tilts
instead. The Eye starts the vertical rotation and the Saucer follows, each with
their own limits.

### Saucer tilt limits

The Saucer can only tilt upward to the point where the Eye is flush against the
"north pole" and is looking just short of straight up. It can likewise tilt
downward only until the Eye is flush against the Saucer and is itself looking
downward. The Saucer therefore cannot flip over.

### Saucer tilt from motion (helicopter mechanics)

Independent of Look/Steer, the Saucer tilts based on its translational motion,
following helicopter mechanics. This happens during Dolly motion and also when
the Head moves to follow the Body:

- Moving forward, the front of the Saucer tilts down.
- Moving backward, the back tilts down.
- Strafing left or right, it tilts toward that direction.

Each tilt direction is independently configurable: Forward Helicopter Tilt,
Backward Helicopter Tilt, and Strafe Helicopter Tilt (one setting covering both
left and right), each defaulting to 45 degrees. The exact values only need to
look reasonable.

### Eye counter-tilt (steadycam)

When the Saucer tilts due to translational motion, as opposed to tilting to
follow the Eye as it Looks, the Eye counter-tilts like a steadycam. So when the
Saucer is moving forward (whether from Dolly motion or from the Head following
the Body), the Saucer's front tilts down while the Eye tilts up, keeping the view
level. The same holds for backward and sideways motion. The counter-tilt is a
rotation of the whole Dome, so the Antenna keeps its 60-degree offset and leans
with the Eye.

The counter-tilt has two portions. The stabilizing portion exactly cancels the
Saucer's tilt: it is real, and it is what keeps the view level in follow and on
the player's aim in Steer. The overcompensation portion is a small extra rotation
on top, purely for show. Perfect cancellation makes the reflected Eye look too
steady to be physical, so the Dome is pushed a little past level. This portion is
rendered and shows up in the Body's reflection, but it is not factored into where
the camera points.

This holds in every mode. Even in Steer, where the player aims the Dome directly,
the Saucer still helicopter-tilts and the stabilizing portion cancels it so the
aim stays where the mouse points, with the overcompensation riding on top. When
the Head is Dollied to the jockey position (see Dolly) and is Looking down at the
Body, the stabilizing portion pins the camera on the Body while the Saucer shows
its helicopter-tilt and the Dome shows the small overcompensation wobble.

The overcompensation rotates the whole Dome, Eye included, rather than wobbling
only the Antenna and tiling around a view-aligned Eye. As long as it stays mild
this should read fine, even though the reflected Eye then aims a few degrees off
the true view. The one illusion-breaker to avoid is Steering while Looking down
with the Eye no longer prominent in the reflection. All of this is expected to
need tuning by feel.

### Steering while moving

When the right mouse button is held while the Body is in motion, the direction
the Eye is looking is what defines forward. This means you can hold down the
forward key and Steer with the mouse. Since the Head trails the Body, it
maintains the Dolly distance while shifting to be behind the Body.

Because the Body is held down by gravity it cannot climb, so "forward" is always
the Eye's heading projected onto the ground plane: looking up while driving does
not lift the Body.

You can also Steer while moving backward or strafing. It's entirely possible to
Dolly while Steering.

### Mode transitions and release

Releasing the right mouse button ends Look mode, and the Head holds its current
pitch rather than easing back to the resting position. What happens next depends
on whether the Body moves and whether the right mouse is held:

- Right mouse released, then the Body moves: there is no Look-to-Steer
  conversion. The Head follows the Body under helicopter mechanics (the Saucer
  tilts from motion and the Eye counter-tilts like a steadycam).
- Right mouse held while the Body moves: Look becomes Steer. The Saucer's tilt
  is controlled by helicopter mechanics, and the Eye stays under mouse control
  (within its rotation limits); the stabilizing counter-tilt still cancels the
  Saucer's tilt so the aim holds, with the overcompensation shown on top.

## Deferred

No questions remain that block the model. Two items are intentionally left for
later:

- The overcompensation magnitude, and the Steering-while-Looking-down case (Eye
  counter-tilt), are settled in model but will be tuned by feel once they can be
  played.
- The explicit list of dead ends to retire, and the concrete config layout, are
  part of the stepwise plan rather than the model.
