ECS Overview
============

The ECS system is a header-only C++23 library organized into two parallel
families: the archetype model and the component model. Both share the same
entity ID infrastructure but differ in how they relate entities to storage.


Two Models
----------
Archetype model: an entity belongs to exactly one storage at a time. Its
component set is fixed by which storage it lives in. Suited for entity
taxonomies with a stable structure (Player, Enemy, Projectile).

Component model: an entity can occupy any number of storages simultaneously.
Components are added and removed freely at runtime. Suited for composable,
dynamic component sets (status effects, equipment, AI state).

The two models are not interchangeable and cannot be mixed in the same scene.


entity_registry
---------------
The registry is the foundation of both models. It is a flat array of records,
one per entity ID. Each record holds:
- A location (how the entity is tracked in storage -- see below).
- An optional generation counter for detecting ID reuse via handles.
- An optional user-defined metadata field.

Entity IDs are small integers; the registry pre-allocates slots and recycles
dead IDs through an intrusive free list (FIFO or LIFO, selectable at compile
time). The "invalid" sentinel is the maximum representable value of the
underlying enum.

Location tracking differs by mode:
- Archetype mode: the record holds a {store_id, index} pair. When an entity
  moves between storages, the registry is updated so that any holder of the
  entity's ID can find it in O(1).
- Component mode: the record holds a fixed_bitset presence bitmap. Each bit
  corresponds to a store_id; a set bit means the entity currently occupies
  that storage. The per-storage index is kept in the storage's own reverse
  index (see below), not in the registry.

Entities begin life in "staging" (store_id 0, bitmap all-zero). Staging is
not a real storage; it represents an entity that has been allocated but not
yet placed in any storage.

Handles vs. IDs: a raw `id_t` is a plain integer with no generation context.
A `handle_t` carries an id and a generation snapshot, allowing `is_valid()`
to detect stale handles after the entity has been erased and its ID reused.
Unversioned mode omits the generation field but still performs alive checks.


Archetype Storage Family
------------------------
All archetype storages derive from `archetype_storage_base` via CRTP. The
base class manages the entity ID vector, the registry pointer, and the
store_id assignment. It also provides the row wrapper types (`row_lens` for
mutable access, `row_view` for const), bidirectional iterators, and bulk
operations (`remove_if`, `erase_if`). The required CRTP customization points
are component-data operations: adding, accessing, swapping-and-popping, and
clearing.

Removal in all archetype storages is swap-and-pop: the entity at the target
index is swapped with the last, both are popped, and the registry location of
the moved entity is updated. This keeps the storage dense.

The concrete archetype storage types are:

archetype_storage<REG, TUPLE>
  Structure-of-Arrays layout. Holds one std::vector per component type in a
  tuple. Iterating over a single component type is a contiguous array walk.
  Multi-component access at a given index reads from parallel vectors.

chunked_archetype_storage<REG, TUPLE, CHUNKSZ>
  Array-of-Structures-of-Arrays layout. Component data is stored in chunks of
  K slots each (default K = 16, must be a power of two). Within a chunk, each
  component type has its own K-element array. This improves cache locality
  when systems access multiple components per entity. The public interface is
  identical to `archetype_storage`; CHUNKSZ is a tuning knob only.

mono_archetype_storage<REG, C>
  Single-component specialization. Holds one contiguous std::vector<C>. Its
  iterator is a contiguous iterator (not bidirectional) that dereferences
  directly to `C&`. Useful for components that are logically standalone and
  iterated independently.


Component Storage Family
------------------------
All component storages derive from `component_storage_base` via CRTP. The
base manages the entity ID vector, the registry pointer, the store_id, and
the reverse index. CRTP customization points are the same component-data
operations as the archetype base.

The key structural difference is the reverse index: because the component-mode
registry bitmap does not store per-storage indices, each component storage
maintains its own `entity_id -> packed_ndx` mapping. Three index policies are
available, selected per storage at compile time:

flat_sparse_index (default)
  A vector sized to the highest entity ID ever seen. Every entity slot
  consumes memory regardless of whether it holds this component. O(1)
  lookup. Best when the component is held by a large fraction of entities,
  or when the entity ID space is bounded and small.

sorted_pair_index
  A sorted vector of (entity_id, ndx) pairs. O(log K) binary search,
  O(K) insert/remove. Memory is proportional to the number of entities
  that actually hold the component. Best for components held by a small,
  slowly-changing set of entities.

paged_sparse_index
  Divides the entity ID space into fixed-size pages. Pages are allocated
  on demand from a private bump allocator and freed all at once on clear().
  O(1) lookup (two array accesses). Memory cost depends only on how many
  pages have been touched, not on the total entity ID range. Best for
  moderate or variable entity counts.

The only concrete component storage type today is:

component_storage<REG, C, TAG, IDX>
  Single-component storage. Dense std::vector<C> parallel to the ID vector.
  TAG disambiguates multiple storages with the same component type in one
  scene. IDX selects the reverse-index policy.


Scene Types
-----------
The scene aggregates a registry and a heterogeneous tuple of storages into a
single interface. It is the primary entry point for callers.

archetype_scene<REG, STORES...>
  Owns an archetype-mode registry and a tuple of archetype storages. Store
  IDs are assigned as sequential integers starting at 1; store_id 0 is
  reserved for staging (represented by std::monostate at tuple index 0).
  Provides entity creation, storage assignment, migration between storages,
  and bulk for_each iteration.

component_scene<REG, STORES...>
  Owns a component-mode registry and a tuple of component storages. No
  staging slot is reserved in the tuple; an entity with an all-zero bitmap
  is implicitly staged. Provides per-storage component add/remove,
  erase-from-all, and for_each iteration.

Both scene types support two clear strategies:
- clear(release): drops storage vectors wholesale and resets the registry.
  O(S) in the number of storages. Generation counters are reset, so
  previously issued handles become permanently invalid.
- clear(preserve): erases entities one by one, preserving generation counters.
  O(N) in the number of entities.


Iteration
---------
for_each<Cs...>(fn) is available on both scene types. It iterates every
entity that holds all of the requested component types, calling
fn(id, tuple<Cs&...>) for each. The callback returns bool; returning false
stops iteration early.

In archetype_scene, the iteration is a compile-time filter: only storages
whose tuple_t contains all Cs... are visited.

In component_scene, the primary component's storage is iterated; for each
entity, a bitmap subset check confirms the entity also occupies the other
requested storages before dispatching the callback.

Component types are resolved at compile time by ecs_meta utilities. A
selector type C matches a storage if C is the storage's component_t; if no
unique component_t match exists, C is tried as a tag_t. Ambiguous or absent
selectors are compile errors.


ecs_meta.h
----------
Compile-time metaprogramming utilities used internally by the scene types.
Key utilities: tuple_contains_v, has_all_components_v,
find_component_storage_index_v, storage_index_for_v. These drive the
for_each dispatch and selector resolution described above.


File Map
--------
entity_ids.h              -- default ID enum types (id_t, store_id_t, etc.)
entity_registry.h         -- registry; entity lifecycle, location, handles
archetype_storage_base.h  -- CRTP base for all archetype storages
archetype_storage.h       -- SoA multi-component archetype storage
chunked_archetype_storage.h -- AoSoA multi-component archetype storage
mono_archetype_storage.h  -- single-component archetype storage
archetype_scene.h         -- primary archetype-mode ECS entry point
component_index_policies.h -- flat/sorted/paged reverse-index strategies
component_storage_base.h  -- CRTP base for all component storages
component_storage.h       -- single-component storage for component model
component_scene.h         -- primary component-mode ECS entry point
ecs_meta.h                -- compile-time type analysis and dispatch utilities
stable_ids.h              -- legacy; predates the registry/storage design
