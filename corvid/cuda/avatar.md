# Player avatar: a rolling droid

The avatar the player inhabits in the voxel digger. Instead of the genre-standard
human (Astroneer, Satisfactory, and the rest hand you a person, usually in a
spacesuit), the player is a small robot along the lines of Star Wars' BB-8: a
ball body that rolls, and a head that can either ride the ball or detach and fly
a short distance as the camera.

This is a forward-looking sketch, recorded to iterate on. Nothing here is built.
Today the viewer drives a free-flying camera (`camera` / `camera_rays` in
`camera.cuh`) with no embodied avatar at all; the dig brush fires from the camera
(see `voxel_world.md`). The droid replaces that free camera with a body the
camera is attached to.

## Why a droid, not a human

The motivation is pragmatism, not aesthetics. A convincing human, with believable
walking, footstep placement, turning, and slope handling, is a large amount of
animation and locomotion work for little payoff in a game whose whole point is
digging and watching terrain collapse. A ball that rolls needs no gait: its
motion is rigid-body physics, which we want to play with anyway, and which the
CUDA side is already oriented around. A cute droid does everything the avatar has
to do (occupy a place in the world, carry the dig beam, anchor the camera) at a
fraction of the cost, and the ball shape turns the locomotion problem into a
physics toy rather than an animation chore.

## Two bodies: ball and head

The avatar is two linked pieces, not one, and they are aesthetic opposites that
split cleanly into a perceiver and an effector.

- The BALL is the body, and it is not conventionally mechanical: picture a
  formless silver sphere, closer to a blob of liquid metal than to a machine with
  panels and seams. It is what exists in the world, what physics acts on, and
  where the player "is." Traversal is rolling: the player drives the ball and it
  accumulates and sheds momentum, so inertia is part of how it feels to move.
  Coasting, overshooting a stop, and the weight of getting a heavy ball going are
  features to lean into, not bugs to damp out. The ball is also the effector: it
  shoots out beams that DIG (remove material), FILL (deposit material), and DRAG
  (relocate material). All world-acting comes from the ball.
- The HEAD is explicitly mechanical, the hard-tech counterpoint to the formless
  ball: it looks like a traditional flying-saucer UFO, and it is largely unseen
  because the player is usually looking out through it. It is the camera, and it
  has two states: it can REST on the ball (the BB-8 pose, the saucer riding atop
  the rolling body), or DETACH and fly within a limited range around the ball, a
  tethered drone. Beyond the camera, the head carries everything to do with
  perceiving the world: the light and the holographic crosshair (both below).

The division is the organizing rule: the head perceives, the ball acts.

## Both bodies are free SDFs

The terrain renderer marches one density grid and reads one material grid, and
the static props embedded in the world are baked into both (see the object band
in `voxel_world.md`). The avatar is not baked into either. It is a pair of FREE
signed-distance functions the ray-march tests directly: at each step the march
takes the nearer of the density-field surface and the avatar's own SDFs, so the
ball and head occlude and are occluded by terrain for free, with no grid writes.

This is the right split because the avatar is everything an embedded object is
not. It moves every frame, so baking it would mean re-stamping the density
field, rewriting material voxels, and running the smoothing re-distance pass
continuously, all to carry an object the grid's acceleration buys nothing for.
It is never buried or dug out. And there are only two of them, so evaluating two
analytic distances per march step is free next to the texture reads that baking
exists to avoid. Embedded is for the static and the many; free is for the
dynamic and the few.

The head is a free SDF too, even though the player almost never looks at it
directly: the camera sits inside it, so primary rays start past it and it does
not draw over the view. It earns its shape in the BALL's reflection. Look down
at your own body and the saucer overhead shows up, distorted, across the sphere,
which is the cheapest and most diegetic mirror in the game. So the head is
excluded from primary rays but is an ordinary scene object to the reflection
rays that bounce off the ball.

That shape costs almost no math. A classic flying saucer is a wide, flat
ellipsoid (a sphere scaled down on one axis) joined by a `smin` (smooth-minimum
union) to a small dome on top: two distance evaluations and a blend, and the
silhouette already reads as a UFO. An emissive rim band (it carries the glow
anyway, see Light) is the one detail worth adding. It only ever has to look good
reflected and at a glance.

The ball keeps reflection as its defining look, a formless liquid-metal sphere,
but not a neutral chrome mirror and not at full strength; its reflection model
is in Optics below.

## The camera is diegetic

Because the camera is the head, and the head is a physical object near the ball,
the player can aim the head down and watch the ball body roll beneath them. The
viewpoint is a thing that exists in the world, not an abstract eye floating
behind the avatar. That is the whole appeal: the camera is built in
diegetically, so looking at yourself is just pointing the camera at your own
body, and the limited flight range of the head is a real, visible leash rather
than a UI constraint.

This reframes the existing free-flying camera. The head's motion is the camera's
motion, but bounded: free look while resting on the ball, and a constrained
orbit/hover while detached. The free camera we have now becomes the special
unconstrained case; the droid head is the same camera with a tether to the body.

## Driving: steer the head, the ball follows

Control is Warcraft-style: WASD moves, and holding the right mouse button turns
the view. What the input steers is the HEAD, not the ball. The head's home is a
low hover just above and behind the ball, close enough to read as riding it (the
BB-8 silhouette), and the ball follows. Let that follow lag a beat, so the head
leads and the ball catches up: the eye reads it as the ball doing the moving,
rolling to keep up, with the head riding its tether, which is the read we want
rather than a camera bolted to a stick.

Stand still and the leash goes slack: the head drifts on its own within a short
range, enough to lean and peek, not enough to leave the ball behind. This is the
detached state from Two bodies, used at rest.

It also reframes the usual third-person zoom. Most games start over the shoulder
and zoom in until the camera sits inside the avatar's head; here the head can
instead fly IN FRONT of the ball, which clears the ball out of the field of view
and, as a bonus, threads tight gaps: fly the head ahead through a hole and the
ball rolls after it, instead of fighting to fit a sphere through the opening on
camera. Player-as-ball is well-trodden ground (Super Monkey Ball, Metroid's
morph ball), a familiar pleasure to build on rather than an experiment.

Movement is generally lax and forgiving, easy to fling around, but momentum is
allowed to matter at the top end: a full sprint carries enough that stopping
takes a moment of back-rotation to bleed off, the same inertia the ball is built
around (see Two bodies). That small cost at speed is a feature, the weight of a
heavy ball, not friction to file away.

## Light is diegetic too

The head also carries the light, so illumination is as embodied as the view.
Two lights, both on the head:

- A FLASHLIGHT aimed along the look direction, so wherever the player looks is
  lit. Because the head is the camera, the flashlight and the view share a
  direction for free.
- A diffuse GLOW around the head, a soft fill so the scene is never pitch black
  even where the flashlight is not pointed.

For the CUDA ray-march renderer this means the avatar is the light source: the
shading the kernel computes is lit from the head's position and facing, not from
a fixed world sun. A detached head moves its light with it, so the player can fly
the saucer to throw light into a hole the ball cannot yet see into.

## Optics: every light effect is a secondary ray

The light effects in the game are one mechanism wearing several hats. In a
ray-march renderer, reflection, refraction, and even the flashlight's reach are
the same act: spawn a secondary ray from a surface and march it through the same
scene. That is the optics counterpart of the terrain's one-comparison rule, and
it is what keeps the list below cheap to add. Each effect is a ray, and the cost
is bounded by capping how many bounces a ray may take.

THE BALL'S REFLECTION. A primary ray that hits the ball spawns one reflection
ray in the mirrored direction and marches the same scene (terrain, the head, a
cheap sky term where the ray escapes). Its returned radiance is the ball's
reflection, shaped two ways. The reflection is DIMMED, not merely tinted:
multiply the reflected radiance by a factor below one, so the ball reads as dark
liquid metal rather than a blown-out chrome ball, because a full-strength mirror
flattens to a bright featureless highlight no matter its hue. Then it is TINTED. The
simplest and most constant use is to read the ball's own motion as color: shift
the tint toward blue under acceleration and toward red under braking, so the
momentum from Driving shows in the body's color with no gauge. The same channel
can still carry status (tool mode, charge, damage) when there is something to
show. A small ambient floor keeps the darkest reflections from crushing to
black, and one bounce is enough; the ball need not reflect its own reflection.

READING THE SPIN. A perfect mirror hides rotation: a featureless reflective
sphere spinning in place looks the same frame to frame, and this ball is on
screen almost all the time, so it has to show its motion. Overlay a faint
pattern fixed in the ball's surface frame (a marbling, flecks, a band) that
turns with the body, breaking the mirror just enough to make spin and roll
legible. It is cheap, a lookup in surface space under the dimmed reflection, and
it doubles as the dial the velocity tint rides on.

THE FLASHLIGHT AND GLOW (from Light is diegetic too) are the lighting half of
the same machinery: the flashlight is the head's directional light along the
view, the glow its soft fill, and a shadow is just a secondary ray asking
whether the head can see a point.

Beyond those, three effects are worth keeping the door open for, all the same
secondary-ray shape:

- MIRRORS. Reflective surfaces placed in the world (an embedded object with a
  reflective material) bounce the view or the flashlight, to see around a corner
  or throw light into a pit the ball cannot reach.
- LENSES AND A TRANSPARENT BALL. Refraction bends a ray instead of mirroring it.
  The ball could switch to a transparent mode and become a sphere lens, bending
  the view and focusing the flashlight; a free-standing lens does the same in the
  world.
- A PUZZLE LAYER. Once light can be routed and bent, exploration can carry light
  puzzles on top: steer a beam through mirrors and lenses to a target, or set the
  transparent ball as a movable lens. Kept secondary to digging, an opening to
  grow into, not a committed pillar.

These last three are direction, not decided design. Their depth caps, whether
refraction is real Snell bending or a cheap fake, whether mirrors are just
reflective embedded materials or a distinct object, and how much puzzle content
to build, are all open.

## The crosshair is a hologram, not a UI element

The crosshair is not screen-space chrome painted over the frame. It is a
holographic projection cast by the head into the world, one more diegetic element
alongside the camera and the light, rendered as part of the scene.

But what it marks is the BALL's aim, not the head's. The beams come from the ball
(the effector), while the crosshair is projected by the head (the perceiver), so
the two have to be reconciled: the player aims through the head's viewpoint, but
the crosshair has to land where the ball's beam will actually reach. That is the
one hard constraint the head-perceives, ball-acts split forces. The naive
screen-center crosshair (see the screen-space crosshair note in
`voxel_world.md`) assumes the beam leaves the eye; here it leaves the ball, and
the head projects a hologram that honestly shows where.

## Still open

- Rolling model: how much inertia, whether the ball is torque-driven (it spins
  itself up) or impulse-driven, how much momentum a sprint carries before it can
  stop, and how it climbs and handles terrain it has just dug.
- Head states: the exact rest-versus-detach control, the size and shape of the
  detached flight range, whether the leash is hard (a wall) or soft (a pull back
  toward the ball), how much the ball's follow lags the head, and the
  fly-in-front pose that clears the ball from view and threads tight gaps.
- Crosshair resolution: how the camera's aim maps to the ball's beam origin, and
  what the crosshair shows when the camera angle and the ball's reachable beam
  disagree (occlusion, extreme look-down, the body between camera and target).
- The three beams: how dig, fill, and drag map onto the material model in
  `voxel_world.md` (dig lowers density, fill raises it, drag relocates material
  while carrying its hardness), and whether fill and drag conserve material the
  ball is carrying or create it from nothing. This also ties into the dig tool
  tiers and the strength-versus-hardness rule.
- Lighting tuning: flashlight cone and range, glow falloff, and how the diffuse
  fill avoids flattening the terrain the player is trying to read.
- Whether the ball collides with and is moved by collapsing terrain, which would
  connect the avatar directly to the dirt physics rather than treating it as a
  separate camera rig.
- Ball reflectivity tuning: the dim factor, the ambient floor, the
  acceleration-to-tint mapping (how blue a hard launch, how red a hard stop),
  and the spin pattern's look and density.
- The head's SDF: the exact saucer-plus-dome form and rim, and confirming it is
  excluded from primary rays but present in reflection rays.
- Optics scope: whether to build mirrors, lenses, and a transparent-ball mode at
  all, the secondary-ray bounce cap, and how large a puzzle layer, if any, to
  grow on the exploration.
