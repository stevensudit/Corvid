Missing Features
1. Query / View System — the biggest gap. There's no way to declare "give me all entities with components A and B" and get an iterator. Every storage must be iterated manually. There's even a TODO in scene.h noting for_each<C>() across all storages containing component type C is unimplemented.

2. Multi-storage iteration — related to the above. scene dispatches to a single storage at a time; iterating a component type that lives in multiple storages requires manual fan-out.

3. System scheduling — no registration of "systems" (functions/functors that run each frame), no dependency ordering, no phase graph. This is intentionally out of scope for a low-level library, but it means users must wire everything manually.

4. Change detection — no dirty flags or versioning on components. Systems can't ask "which entities had their Transform modified since last frame?" without writing that themselves.

5. Entity relationships / hierarchy — no parent-child, no tags as entities (Flecs style). No sparse/optional component storage for components that only a few entities have (everything is dense).

6. Prefabs / templates — no built-in way to define an archetype blueprint and stamp out instances.

7. Serialization — no reflection over component types, no save/load.

8. Scene constructor flexibility — can't pass in pre-constructed storages (noted as a TODO: "store_id alignment problem").


Natural Next Steps (in rough priority order)
Cross-storage for_each<C>() / view — the already-noted TODO. Even a simple compile-time filter over the storage tuple (using tuple_contains_v) would cover the most common use case. This is low-hanging fruit given the existing ecs_meta.h utilities.

Multi-component query / zip view — a scene.view<A, B>() that returns a range of {entity_id, A&, B&} tuples, iterating only storages that contain all requested types. This unblocks the "systems" pattern without needing formal system registration.

Reactive callbacks / event hooks — on_add<C>, on_remove<C>, on_modify<C> notification points. These are cheap to add to storage_base / archetype_storage_base as optional functors.

System runner scaffolding — even a thin system_scheduler that holds a list of callables and a dependency DAG would complete the "classic ECS loop" experience without adding heavy machinery.


Architecture Evolution: Component Model
========================================

Goal: Add an EnTT-style component model alongside the existing archetype model,
sharing the entity ID infrastructure and O(1) flat-array lookup discipline. No
hash maps or tree-based containers anywhere in the hot path.

The two models serve different use cases:
- Archetype model (current): entities are typed by their fixed component set.
  Good for a stable entity taxonomy (Player, Enemy, Projectile). Efficient SoA
  iteration; migration cost on component-set change.
- Component model (new): entities are untyped; components are added/removed
  freely at runtime. Good for composable, dynamic component sets (status
  effects, equipment, AI state). Per-component sparse reverse index enables
  O(1) add/remove; views require runtime intersection.

New Primitives (containers/)
-----------------------------
fixed_bitset<N_BITS = 64>
  Wraps std::array<uint64_t, N_BITS/64>. Provides set/clear/test by index,
  bitwise AND/OR (for view intersection), popcount, and iteration over set
  bits. Lives in containers/ because it is ECS-independent. Used as the
  presence bitmap in entity_registry's component-mode record_t.

id_container<T, id_t>
  Factored out of entity_registry. Wraps enum_vector<T, id_t> plus id_t
  limit_ . No FIFO/LIFO, no embedded free list, no defined sentinel
  (callers interpret slot content).

entity_registry evolution
--------------------------
[DONE] Added OWN_COUNT template parameter to select registry mode:
  OWN_COUNT == 1 (default, archetype mode) — record_t holds location_t
    {store_id, ndx}, as before.
  OWN_COUNT any multiple of 8 >= 8 (component mode) — record_t holds a
    fixed_bitset<OWN_COUNT> presence bitmap instead of location_t. Each bit
    corresponds to a store_id (bit i = store_id_t{i} is occupied). No ndx
    stored here; ndx lookup goes through component_storage_base's own
    id_container. OWN_COUNT also sets the bitmap size.

Presence bitmap semantics:
  - "Does entity E have component in store S?" -> bitmap.test(*S), O(1)
  - "Which stores does entity E occupy?" -> iterate set bits, O(popcount)
  - component_scene::erase(id) fans out only to stores with set bits, not
    all stores.
  - component_scene::view<Cs...>() intersects bitmaps to find candidates.

Callers never access a component_storage_base's id_container slot without
first confirming presence via the bitmap, so no slot sentinel is needed.

Archetype Hierarchy Refactor (rename only, no behavior change)
---------------------------------------------------------------
- storage_base.h: absorbed into archetype_storage_base.h. The two-level
  CRTP chain (storage_base -> archetype_storage_base) collapses into a
  single archetype_storage_base, since the only reason storage_base was a
  separate, narrower class was to serve as a shared root for both
  component_storage and the archetype chain. That reason is gone.
- component_storage -> mono_archetype_storage. Naming rationale: it is an
  archetype with a single component type (mono = one), consistent with the
  archetype_storage / chunked_archetype_storage family.
- scene -> archetype_scene.

Storage / Scene Compatibility
------------------------------
The two storage families are not interchangeable. Each is coupled to its
scene type through the registry specialization they share:

  archetype_scene  ←->  archetype-mode registry (ownership::unique)
                   ←->  archetype_storage_base-derived storages
                         (archetype_storage, chunked_archetype_storage,
                          mono_archetype_storage)

  component_scene  ←->  component-mode registry (ownership::shared)
                   ←->  component_storage_base-derived storages
                         (component_storage, future grouped variants)

Mixing them is not possible: archetype storages require record_t.location
(absent in component-mode registries); component storages require
record_t.bitmap and their own sparse id_container (absent in archetype-mode
registries). Each scene type enforces the correct family via static_assert
on the shared registry_t.

Reverse-Index Policy
---------------------
component_storage_base requires an entity_id -> packed_ndx reverse index for
O(1) remove and direct access. Three strategies are supported, selected per
component at compile time. A single component_scene may mix them freely.

flat_sparse_index<ID_T, SIZE_T> (default)
  Wraps id_container<SIZE_T, ID_T>. The vector is sized to the highest entity
  ID ever inserted; all slots (present or absent) consume SIZE_T bytes.
  Lookup/insert/update: O(1). Memory: O(max_entity_id × sizeof(SIZE_T)).
  Best for: components held by a large fraction of entities (Position,
  Velocity), or scenes whose population is bounded and known in advance.
  Worst for: components held by very few entities — wastes one SIZE_T slot
  per entity in the scene, regardless of whether they have the component.

sorted_pair_index<ID_T, SIZE_T>
  A sorted vector<pair<ID_T, SIZE_T>>. Lookup: O(log K) binary search.
  Insert/remove: O(K) due to shifting. Memory: O(K × (sizeof(ID_T) +
  sizeof(SIZE_T))), no wasted slots.
  Best for: components held by a small, slowly-changing set of entities
  (a handful of Combatants) where the log-N lookup and linear insert cost
  are acceptable. Not suitable for high-churn or large-K use cases.

paged_sparse_index<ID_T, SIZE_T, PAGE_SIZE = 256>
  Divides the entity ID space into fixed-size pages. Maintains an outer
  vector<SIZE_T*> of page pointers (null = page not allocated). Pages are
  allocated from a private bump allocator when first needed; freed all at
  once on clear(). Lookup: O(1), two array accesses. Memory: only allocated
  pages consume space — O(K_pages × PAGE_SIZE × sizeof(SIZE_T)) where
  K_pages is the number of pages that contain at least one entity with this
  component. In practice, entities are created in bursts and have sequential
  IDs, so K_pages is close to ceil(K / PAGE_SIZE).
  Best for: components with moderate or variable entity counts where neither
  flat (too wasteful) nor sorted (too slow at higher K) fits well.
  Note: the page allocator is a simple fixed-block bump allocator internal
  to this class, not the general-purpose arena_allocator from containers/.

All three satisfy the same concept (roughly: insert, update, erase, lookup).
The default template parameter on component_storage is flat_sparse_index.
Benchmarks should drive per-component choices.

New Component Model Classes
----------------------------
component_storage_base<CHILD, REG, IDX = flat_sparse_index<...>>
  Mirrors archetype_storage_base structurally but uses a policy-based
  reverse index instead of registry location. Members:
    store_id_t store_id_
    registry_t* registry_        — used for ID validity and bitmap updates only
    vector<id_t> ids_             — dense-to-ID (ndx -> entity_id)
    IDX reverse_index_            — entity_id -> ndx (policy type from above)
  When an entity is removed (swap-and-pop), the moved entity's ndx is
  updated in reverse_index_, and the registry bitmap bit for this store is
  cleared. No entity lifetime ownership: entities are not "staged" or
  uniquely owned. REG must be specialized with ownership::shared.

component_storage<REG, C, TAG, IDX = flat_sparse_index<...>>
  Derives from component_storage_base. Concrete single-component storage.
  Dense packed C[] array parallel to ids_. TAG allows multiple distinct
  component_storage<REG, C> instances in the same scene. IDX selects the
  reverse-index strategy for this component.

component_scene<REG, STORES...>
  Same tuple-of-storages pattern as archetype_scene. Each storage must be a
  component_storage_base-derived type using the same REG. Enforced by
  static_assert. Assigned sequential store_ids.
  Key behavioral differences from archetype_scene:
    add_component<S>(id, ...) — add component to a specific store; entity
      need not be "moved"; multiple stores can hold data for the same entity.
    remove_component<S>(id) — remove from one store, others unaffected.
    erase(id) — clears the bitmap, removes from all occupied stores (O(set
      bits)), then destroys the entity in the registry.
    view<Cs...>() — returns entity_view / entity_lens range. Uses bitmap
      intersection to find entities present in all requested stores, then
      yields rows with ndx-based O(1) component access.
  No staging concept: entity_registry is a pure ID pool in this model;
  store_id_t{0} is not reserved.

View System (both scenes)
--------------------------
view<Cs...>() is a factory method on each scene returning a lightweight
non-owning range object (following the std::string_view / range-adaptor
pattern). The range yields entity_view (const) or entity_lens (mutable)
rows. Mutable/const is controlled by whether the scene is accessed through
a const or non-const reference, mirroring the row_lens/row_view convention
already in archetype_storage_base.

  archetype_scene::view<Cs...>(): compile-time filter over storage tuple
    (tuple_contains_v for each Cs). Concatenating iterator across matching
    storages. No runtime cost beyond iteration itself.

  component_scene::view<Cs...>(): runtime bitmap intersection. Iterator
    walks entities in the smallest matching store, probes bitmap for the
    rest. O(min_store_size) iterations, O(1) per probe.

Implementation Steps (in order)
---------------------------------
1. Add fixed_bitset<N_BITS> to containers/ (COMPLETED)
2. Add id_container<T, id_t> to containers/, factored from entity_registry (COMPLETED)
3. Refactor entity_registry to use id_container internally; add
   ownership LOCATION and N_BITS template parameters; wire in fixed_bitset for
   component mode via maybe_t. As part of this, move the unit tests for this file out from ecs_test.cpp and into entity_registry_test.cpp (COMPLETED)
4. Collapse storage_base into archetype_storage_base (merge the two files,
   flatten the CRTP chain) (COMPLETED)
5. Rename component_storage -> mono_archetype_storage (COMPLETED)
6. Rename scene -> archetype_scene; update ecs.h umbrella (COMPLETED)
7. Implement component_storage_base and component_storage (COMPLETED)
8. Implement component_scene (COMPLETED)
9. Add view<Cs...>() to archetype_scene (entity_view / entity_lens)
10. Add view<Cs...>() to component_scene
11. Minor: Follow the TODO in fixed_bitset.
