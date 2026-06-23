# Voxel terrain with embedded SDF objects

How the voxel digger renders analytic SDF objects (a placed prop, a reflective
sphere) in the same world as editable voxel terrain, and how digging, material
hardness, and object collision reduce to one comparison.

This is a forward-looking design. Today the viewer has an editable density field
(`density_field` in `density_field.cuh`), a fixed-step ray march
(`density_field::raymarch`), and a spherical dig brush (`dig_kernel` in
`tests/cuda/windows/notest_voxel_viewer.cu`). The second (material) grid, the
strata, the objects, and the physics below are not built yet.

## Scope: coexistence, not CSG

An object and the terrain never blend. Any world location is terrain or object,
never a mix. Astroneer works this way: a buried artifact occludes the soil and
is occluded by it (a hill in front, dirt on top), but no point is both soil and
artifact. So the renderer needs correct occlusion only, not a combined field
that supports `min`/`max`/smooth-union across the two representations. That rules
out fusing the two into one signed-distance field (which would force the
editable terrain to become a distance volume) and true CSG between them.

## Two grids: geometry and material

The world is two parallel 3D grids over the same voxels.

- The GEOMETRY grid is the density field of today: a `float` texture,
  trilinearly filtered, with a surface for writes. Its sign is solid (positive)
  versus air (negative), its zero crossing is the surface, and its gradient is
  the surface normal. The march reads it, and the normal is built from it.
- The MATERIAL grid is new: a `uint16` per voxel, point-sampled (exact, never
  filtered). It holds one value per voxel: a hardness tier for conventional
  material, or an object slot for an object voxel (see Object index sizing). It
  is read at a render hit and at dig time, and never differentiated.

Why they cannot be one grid:

- The geometry value is load-bearing for shading, not just for finding the
  surface. The surface normal is the GRADIENT of the density field
  (`density_field::normal` is `normalize` of the central differences). So the
  geometry value has to stay a smooth, distance-like field: a sharp jump in it
  skews the normal of every neighbor that samples across the jump. Hardness is
  the opposite (movable and discontinuous: a hard chunk dropped on soft soil, a
  freshly cut crater), and an object marker would be a deliberate spike. Putting
  either into the field whose gradient you take corrupts the normals around it,
  so every object would wear a wrongly lit one-voxel rim of dirt. Geometry wants
  smooth; material is allowed to be sharp.
- The hardware will not merge them anyway. A CUDA texel is homogeneous (one
  channel format kind for the whole element, so `{float, int}` is not a valid
  texel), and a texture object has a single filter mode. Density needs `float`
  plus linear filtering (the trilinear smoothing the normal depends on);
  material needs integer plus point reads (an interpolated id is garbage), and
  linear filtering is only available on float-returning textures, so an int
  channel cannot even be linear-filtered. The two needs cannot share a texture.

The split fits their opposite access patterns. Density is read hundreds of times
per ray in the hot march loop and wants the texture cache and hardware
filtering. Material is read once per ray (at the hit) and per voxel at dig time,
always exact, so it does not even need a texture: a `uint16` buffer or surface
suffices.

## Objects live in both grids

Placing an object writes both grids: a solid region shaped to the object into
the geometry grid (its signed distance in the field's convention, so the march
stops on it for occlusion and its zero crossing lines up with its true surface),
and an object slot into the material grid (which object the voxel belongs to;
because the slot sits in the band above all conventional hardness, the dig gate
refuses it for free). The crisp surface itself comes from the object's analytic
SDF at the hit.

So an object is represented three ways, each for a distinct job: baked into the
geometry grid so the march stops on it without evaluating analytic SDFs per step;
tagged in the material grid so the hit knows which object and that it is
undiggable; and defined by its analytic SDF so the surface and normal are exact
(the mirror ball). Because the object's stamped geometry is its own smooth
signed distance, the soil beside it stays smooth too, so the adjacent soil
normals stay correct. There is no spike to skew them; that is the defect the
rejected one-grid scheme could not avoid (see below).

These grid-baked objects are the EMBEDDED kind, and the object band above is for
them: static enough to bake, and worth baking because there can be many and dirt
can cover them. A dynamic object that moves every frame and is never buried, like
the player's avatar, is left out of both grids and tested by the march directly
as a FREE SDF; see `avatar.md`.

## The unifying rule: strength versus hardness

Digging, mining tiers, and object collision are one comparison: can this actor's
strength overcome this voxel's hardness? Hardness lives in the material grid; the
geometry grid only says where the solid is.

- Strata. Terrain generation lays harder material deeper down, each tier a
  distinct color. The correlation with depth comes from the generator, not from
  geometry, and it is imperfect by design; the player can also relocate soil
  (hoover up hard soil, drop it on softer soil), so hardness travels with the
  material. That is why hardness is stored per voxel in the material grid, not
  read back from depth.
- Tool tiers. A dig proceeds only where tool strength is at least the voxel
  hardness, the way a pickaxe tier gates ore in Minecraft.
- Object physics. An object is not a ghost. Held in the crosshair, it moves into
  air voxels freely (geometry sign negative), but a move into a solid voxel is
  gated on that voxel's hardness: a per-object strength can push through soft
  soil, and an object voxel's value sitting above all conventional hardness
  makes objects impassable to digging and to each other. A moved object leaves air behind: no displaced soil
  is stashed, and dirt does not collapse into the gap, because dirt has no
  physics of its own yet. So a buried object stays put until digging clears air
  around it.

## Rendering a ray

1. Fixed-step march the geometry texture as today, stopping at the first
   `density >= 0` (sign), with the bisection refine to the crossing.
2. Point-read the material grid at the hit voxel:
   - Object id: decode the object, walk at most one voxel forward, and
     sphere-trace its analytic SDF to the true surface. Shade with the analytic
     normal. The refine is required, not optional: a mirror ball cannot have a
     voxel-resolution silhouette.
   - Soil: shade with `density_field::normal` (the geometry gradient) and the
     color of the hardness tier.
3. The crosshair stays a screen-space overlay in the kernel, applied after
   shading.

The material read is one point fetch per ray, at the hit. The hot march loop
touches only the geometry texture.

## Digging

Digging never touches the render texture. It reads and writes the geometry
surface (`surf3Dread`/`surf3Dwrite`, exact) and point-reads the material grid to
gate. For each voxel in the brush the gate is one comparison,
`tool_strength >= value`: it digs dirt the tool can overcome, refuses dirt too
hard for it, and refuses objects for free, since an object slot sits above the
whole tool-strength range. Where the gate passes, lower the geometry density
toward air. Both grids are exact point access over a small
brush, so the layout there does not matter.

## Dirt physics: detached material falls

The Object physics note above defers this ("dirt does not collapse into the
gap, because dirt has no physics of its own yet"). Here is that physics. The
goal: when a dig undercuts terrain, dirt that nothing holds up should fall, and
hardness should decide how far material can span unsupported, so a narrow
bridge of hard rock stands while the same bridge of soft soil collapses.

Two bookend cases set the bar. Digging a tunnel must not drop its roof, because
the roof is still held from the sides; but digging a chunk free on every side
must drop it, because nothing holds it. Everything between those is a matter of
degree, and hardness sets the degree.

Keep two questions apart: WHERE does it give way (the stability test), and WHAT
moves once it does (the collapse).

### Stability: integrity, decaying away from anchors

Connectivity is the wrong test. A thin bridge is fully connected to the ground
at both ends and should still fall if it is soft, so "is this solid reachable
from an anchor" cannot be the whole answer. The test has to know how FAR
support has had to travel, and through how HARD a material.

Carry one scalar per solid voxel, its integrity, the way a distance transform
carries distance. A voxel's integrity is the best any solid neighbor can pass
to it: `max` over solid neighbors `n` of `integrity(n) - cost(n -> v)`, with
anchors clamped to a base maximum and any voxel that lands at or below zero
counted unsupported. This is the same iterative neighbor relaxation as a
wavefront or a chamfer distance transform, so it is GPU-native: ping-pong
passes over the solid set until it stops changing, the pass count bounded by
the longest stable span.

The step cost is where gravity and hardness enter. A step straight down
(material hanging in tension below its support) or straight up (a column
standing in compression on the ground) is cheap; a horizontal step (a
cantilever or a bridge bending sideways) is the expensive one, so horizontal
cost far exceeds vertical. That alone makes towers and hanging columns nearly
free and long flat overhangs the hard case. Then divide the step cost by the
voxel's hardness tier from the material grid: hard material loses integrity
slowly per step so support reaches farther through it, soft material decays
fast. That is the whole "hard bridge stands, soft bridge falls" behavior, and
it reuses the per-voxel hardness already stored for Strata above, with no new
data.

Two consequences come for free. A bridge supported at both ends takes the max
of the reach from each side, so its weakest point is the middle and it spans
about twice a one-sided cantilever, the way a real beam does. And connectivity
falls out at no cost: integrity never reaches a fully detached island, so it
sits at zero and the island reads as unsupported by the same test that condemns
the soft bridge. One comparison, integrity versus zero, the way digging is one
comparison of strength versus hardness.

### Collapse: move the loose material, then re-distance the field

The stability pass labels every solid voxel supported or loose. Loose material
falls, but falling means writing the GEOMETRY grid, and that grid is the
smooth, distance-like field whose gradient is the shading normal. A careless
write is exactly the defect this design fights elsewhere: a sharp step skews
the normals of every neighbor that samples across it. So the collapse carries a
second half the material grid never needs. After material moves, the geometry
field has to be made smooth again, locally.

Rigid block and falling sand are not a fork to choose between; they are the two
ends of one continuum, and hardness is the knob. Read hardness here as
COHESION: how strongly a voxel clings to its loose neighbors as material moves.

- Loose, low cohesion (dirt) flows. A loose voxel with air below drops one step
  per tick and slumps sideways toward a shallow angle of repose, so the pile
  spreads and trickles, the falling-sand look. No clump identity is needed,
  each voxel settles on its own. The cost is churn: every per-voxel swap is a
  sharp occupancy edit, so the re-distance below has to cover all of it.
- Cohesive, high cohesion (rock) holds together and drops as a body. Flood-fill
  the loose region into a connected component and shift it down as one block
  until it rests. Shifting by whole voxels is the kindest edit, because the
  field inside the block is untouched and only the two seams (vacated air on
  top, new contact below) need re-distancing; it reads as "the chunk you
  undercut drops." On a hard landing it can FRACTURE: the impact overruns
  cohesion, the block splits along its weakest cross-section, and the pieces,
  now smaller and looser, settle like the dirt case.

Fracture is the bridge between the ends, so a middle hardness drops a little
way, cracks, and slumps the rest. Both ends carry the material grid along with
the geometry as a sharp integer copy (no smoothing, the grid is allowed to be
sharp), so hardness travels with the dirt as stated above and a relocated hard
chunk stays hard. That makes hardness one scalar doing three jobs: how material
is dug (strength versus hardness), whether it falls (the stability test), and
how it falls (this continuum).

None of this needs a true rigid-body integrator with real fracture mechanics.
The bar is plausible and fun, not rigorous, and it all happens right where the
player is looking, so reading well comes first: a cheap rule that feels right
beats an exact one that does not, which is the arbitrariness a player feels in
Minecraft's physics.

Whichever end runs, the shared and unavoidable cost is the local re-distance:
after material moves, run a short signed-distance relaxation (or a few smoothing
passes) over the touched voxels so the field stays distance-like and the normals
stay correct. The material grid is spared it; only geometry is differentiated.

### Cost and triggering

Nothing moves until geometry changes, so stability and collapse cost nothing in
the steady state. Drive both off edits: after a dig, mark a dirty box around the
hole and run the stability pass only there. A collapse is itself a geometry
change, so it re-dirties the region below it and the loop repeats until it
settles. Cap the work per frame and carry the dirty region across frames, so a
big undercut cascades over a few frames (which also looks better) rather than
hitching one. The scratch needed is one integrity float per voxel in the dirty
region, not over the whole world.

### Known limitation: width does not decide stability yet

The max-over-neighbors rule rewards hardness and two-sided support but ignores
cross-section: a one-voxel-thick hard bridge and a thick hard slab of the same
length and material score alike, and a wide SOFT bridge does not outlast a
narrow one as it should. Capturing width means letting parallel paths add up (a
flow or resistance network) instead of taking the max, a real jump in
complexity. Out of scope for the first cut, where hardness is the requested
discriminator; noted so the model is not mistaken for more than it is.

## Object index sizing

The material grid is a single `uint16` per voxel, read as one tagged number
line split at a threshold `OBJ_BASE`:

- `0 .. OBJ_BASE-1` is conventional material, and the value IS the hardness
  tier (higher is harder). It indexes a color palette for shading and is the
  right-hand side of the dig gate's `tool_strength >= value` comparison.
- `OBJ_BASE .. 65535` is an object slot: `value - OBJ_BASE` indexes the object
  table of the hit voxel's own chunk. Because every object slot sits above the
  whole conventional-hardness range, an object voxel is undiggable and
  impassable for free, by that same comparison (no tool reaches that high).

Objects are not globally numbered: the same slot value names different objects
in different chunks, so decode is chunk-from-position, then the chunk's object
table, then the object. Partition the world into chunks (boxes), each with a
local object/SDF table. An object is not limited to one chunk (a large object
spans several, and each overlapping chunk lists it), so the chunks can be small
and a small chunk overlaps few objects. A moving object updates the tables of
the chunks it enters and leaves.

`uint16` is chosen over `uint32` for memory: this is one integer for every
voxel in every loaded chunk, sitting beside the 4-byte density float, so the
narrower type halves the material grid. With the object table local to a chunk
the index is small (a chunk holds few objects), so 16 bits is already ample. An
`OBJ_BASE` of 256 leaves 256 hardness tiers (far more than will get distinct
colors) and 65280 object slots per chunk (far more than a chunk holds). The
width of the object-slot range is what caps how many objects a chunk can hold,
hence render. `uint32` would only be needed to drop the per-chunk table for a
single global object id, or for chunks large enough that their object count
approaches the slot range; neither is planned.

## Rejected: one grid (the density band)

An earlier plan packed the object marker and index into a reserved high band of
the single density value. It is rejected. Detectability and smoothness conflict
in one channel: to be recognizable the band must sit far from soil values, but a
far value is exactly what skews the gradient (the normal) of the adjacent soil,
so every object wears a wrongly lit one-voxel rim of dirt; trilinear filtering
smears it wider and scrambles the packed index. The two-grid split is what lets
the geometry channel stay smooth while the material channel carries sharp, exact
data, and it is also the only thing the texture hardware allows (one filter mode,
one channel format kind per texture).

## Still open

- Box size, and the exact `OBJ_BASE` threshold and hardness-tier count. The
  encoding itself is settled (a tagged `uint16` range; see Object index
  sizing), so only the boundary value and the chunk size are left to tune.
- The exact form of an object's stamped geometry (its true signed distance, or a
  cheaper smooth solid).
- The collapse continuum's tuning, now that hardness (as cohesion) drives it:
  the per-hardness repose angle, the cohesion threshold that switches a region
  from block-fall to sand-flow, and the impact energy that triggers fracture.
  Open too is whether the two ends run as two cooperating mechanisms (block
  shift plus sand relaxation, bridged by fracture) or one sand kernel whose
  parameters scale with hardness.
- What anchors dirt beyond the bottom bedrock stratum: whether a resting object
  anchors dirt (and what happens when that object is then picked up), and
  whether the finite volume's lateral walls anchor it or let edge dirt run off.
- The integrity cost constants (vertical versus horizontal step cost, the
  hardness divisor, the base maximum), tuned so plausible spans stand and
  implausible ones fall.
- Whether width should ever decide stability (see the limitation above), which
  would replace the max rule with an additive flow or resistance network.
