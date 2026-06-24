Development on the game demo has progressed at a rapid rate, growing organically with many dead ends. At this point, we need to stop and rethink what we actually want, and then change the code to match. As it stands, the config options are a tangled mess, with unprincipled division of values. For example, we have both a Head and a Saucer, with no clear basis for inclusion in one or the other.

The path forward is to build up a clear model, with canonical names and a declaration of how the pieces are supposed to fit together.


The Avatar consists of the Head and the Body.

The Body is currently just one component: the Ball. This is a mirror-shiny sphere that rests on the ground. It moves by rolling and is constrained by terrain. It is held down by gravity and unable to squeeze through gaps narrower than its diameter. It is also the source of the Digging Beam and other similar Beams.

The Head is a little UFO that trails behind the Body, floating in the air. It acts as the diegetic camera and is visible only in reflected surfaces, such as that of the Body.

It consists of the Saucer and the Dome. The Saucer is a flattened cone on top with a rounded surface below. This lower surface has spinning lights that reflect its movement.

It spins faster when it's moving faster, and in reverse when it's moving in reverse. It spins in opposite directions when Strafing left and right. When steady, it slowly spins in one direction, then reverses periodically.

The Dome, which is tiled hexagonally, sits in the implied indentation at the top of the Saucer. While it is joined to the Saucer, it is able to rotate to some extent.

The Dome has an Eye and an Antenna, 90 degrees apart. In resting position, the Antenna pokes straight up relative to the ground. These two features are fixed to the surface of the Dome and never move independently; instead, the Dome moves and these two features on it (along with the tiling) move with it.

The Body is what the WASD movement keys control, with the Head following behind, lagging somewhat. When the Body is in motion, a faint hexagonal grid appears on its surface, allowing its rotation to be visible.

Until we implement a Free Fly mode, the only way the Head can move (by which I mean translate, not just rotate) independently of following the Body is by the Dolly adjustment. This uses the middle mouse button to change the distance that the Head trails the Body. The change is not instantaneous: the Head has to fly to the new position at a configurable speed.

Holding down the right mouse button enters Look/Steer mode. Look is when no movement keys are depressed; Steer is when they are.

In Look mode, the Eye moves in the direction that the mouse does, and because it is the camera, it changes what we see. Since the Saucer is radially symmetrical, moving the mouse left and right just means rotating the Dome; it does not affect the Saucer.

Tilting vertically is different: when you move up or down in Look mode, the Dome rotates to point the Eye in the direction of movement. At resting position, the Eye on the Dome is pointed about 30 deg above the equitorial plane, which is conventionally at 0 deg.

When the Eye is in that resting position, the Antenna points "up" at 90 deg. When the Eye rotates vertically, the Antenna remains at the same offset from it.

The Dome is allowed to rotate downward only to the point where the edge of the Eye is about to touch the Saucer. The Dome is allowed to rotate upward only to the point where the edge of the Eye is about to touch the "north pole": the position that is fully vertical from the POV of the saucer.

When the Eye attempts to move past either of these limitations, the Saucer instead tilts. The Eye starts the vertical rotation, the Saucer follows, each with their own limits.

The Saucer can only tilt upwards to the point where the Eye is flush against the "north pole" and is looking just short of straight up. The Saucer can likewise tilt downwards only until the Eye is flush against Saucer and is itself looking downwards. The Saucer therefore can't flip over.

Independent of Look/Steer, the Saucer tilts based on its translational motion, following helicopter mechanics. This happens during Dolly motion and also when the Head moves to follow the Body. When moving forward, the front of the Saucer tilts down; when backwards, the back does. When it Strafes left or right, it likewise tilts towards that direction.

When the Saucer tilts due to translational motion, as opposed to tilting to follow the Eye as it Looks, then the Eye counter-tilts like a steadycam. So when the Saucer is moving forward (whether because of Dolly motion or the Head following the Body), the Saucer's front tilts down while the Eye tilts up in an attempt to compensate. In fact, the Eye slightly overcompensates, so as to make the effect more visible. This is likewise true for backward and sideways motion.

When the right mouse button is held while the Body is in motion, then the direction that the Eye is looking is what defines forward. This means that you can hold down the forward key and Steer with the mouse. Since the Head trails the Body, it will maintain the Dolly distance while shifting to be behind the Body.
