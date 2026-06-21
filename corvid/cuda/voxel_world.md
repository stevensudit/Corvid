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
- The MATERIAL grid is new: an `int32` per voxel, point-sampled (exact, never
  filtered). It packs the material hardness and, for an object voxel, the
  object's id. It is read at a render hit and at dig time, and never
  differentiated.

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
always exact, so it does not even need a texture: an `int32` buffer or surface
suffices.

## Objects live in both grids

Placing an object writes both grids: a solid region shaped to the object into
the geometry grid (its signed distance in the field's convention, so the march
stops on it for occlusion and its zero crossing lines up with its true surface),
and its id into the material grid (which object, plus maximum hardness so the dig
gate refuses it). The crisp surface itself comes from the object's analytic SDF
at the hit.

So an object is represented three ways, each for a distinct job: baked into the
geometry grid so the march stops on it without evaluating analytic SDFs per step;
tagged in the material grid so the hit knows which object and that it is
undiggable; and defined by its analytic SDF so the surface and normal are exact
(the mirror ball). Because the object's stamped geometry is its own smooth
signed distance, the soil beside it stays smooth too, so the adjacent soil
normals stay correct. There is no spike to skew them; that is the defect the
rejected one-grid scheme could not avoid (see below).

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
  soil, and an object voxel's maximum hardness makes objects impassable to
  digging and to each other. A moved object leaves air behind: no displaced soil
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
gate. For each voxel in the brush: read its hardness from the material grid; if
the tool is too weak, or the voxel is an object, skip it; otherwise lower its
geometry density toward air. Both grids are exact point access over a small
brush, so the layout there does not matter.

## Object index sizing

Partition the world into boxes (bricks), each with a local SDF table; the
material grid stores an index into the voxel's own box table. Objects are not
limited to one box (a large object spans several, and each overlapping box lists
it), so the boxes can be small, a small box overlaps few objects, and the
per-voxel index is only a few bits (decode is box-from-position, then the box
table, then the object). The `int32` material voxel is split between the hardness
tier and the object index; the exact split follows from the box size and the
object density. A moving object updates the tables of the boxes it enters and
leaves.

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

- Box size and the `int32` material-voxel split (hardness tier versus object
  index width).
- The exact form of an object's stamped geometry (its true signed distance, or a
  cheaper smooth solid).
