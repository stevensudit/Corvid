Architecture Evolution: Component Model
========================================

Goal: Add an EnTT-style component model alongside the existing archetype model,
sharing the entity ID infrastructure and O(1) flat-array lookup discipline. No
hash maps or tree-based containers anywhere in the hot path.

The two models serve different use cases:
- Archetype model: entities are typed by their fixed component set.
  Good for a stable entity taxonomy (Player, Enemy, Projectile). Efficient SoA
  iteration; migration cost on component-set change.
- Component model: entities are untyped; components are added/removed
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
  limit_. No FIFO/LIFO, no embedded free list, no defined sentinel
  (callers interpret slot content).

entity_registry
----------------
OWN_COUNT template parameter selects registry mode:
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
  - component_scene::for_each<Cs...>() intersects bitmaps to find candidates.

Callers never access a component_storage_base's id_container slot without
first confirming presence via the bitmap, so no slot sentinel is needed.

Storage / Scene Compatibility
------------------------------
The two storage families are not interchangeable. Each is coupled to its
scene type through the registry specialization they share:

  archetype_scene  <->  archetype-mode registry (ownership::unique)
                   <->  archetype_storage_base-derived storages
                         (archetype_storage, chunked_archetype_storage,
                          mono_archetype_storage)

  component_scene  <->  component-mode registry (ownership::shared)
                   <->  component_storage_base-derived storages
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
  Lookup/insert/update: O(1). Memory: O(max_entity_id x sizeof(SIZE_T)).
  Best for: components held by a large fraction of entities (Position,
  Velocity), or scenes whose population is bounded and known in advance.
  Worst for: components held by very few entities -- wastes one SIZE_T slot
  per entity in the scene, regardless of whether they have the component.

sorted_pair_index<ID_T, SIZE_T>
  A sorted vector<pair<ID_T, SIZE_T>>. Lookup: O(log K) binary search.
  Insert/remove: O(K) due to shifting. Memory: O(K x (sizeof(ID_T) +
  sizeof(SIZE_T))), no wasted slots.
  Best for: components held by a small, slowly-changing set of entities
  (a handful of Combatants) where the log-N lookup and linear insert cost
  are acceptable. Not suitable for high-churn or large-K use cases.

paged_sparse_index<ID_T, SIZE_T, PAGE_SIZE = 256>
  Divides the entity ID space into fixed-size pages. Maintains an outer
  vector<SIZE_T*> of page pointers (null = page not allocated). Pages are
  allocated from a private bump allocator when first needed; freed all at
  once on clear(). Lookup: O(1), two array accesses. Memory: only allocated
  pages consume space -- O(K_pages x PAGE_SIZE x sizeof(SIZE_T)) where
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

Component Model Classes
------------------------
component_storage_base<CHILD, REG, IDX = flat_sparse_index<...>>
  Mirrors archetype_storage_base structurally but uses a policy-based
  reverse index instead of registry location. Members:
    store_id_t store_id_
    registry_t* registry_        -- used for ID validity and bitmap updates only
    vector<id_t> ids_             -- dense-to-ID (ndx -> entity_id)
    IDX reverse_index_            -- entity_id -> ndx (policy type from above)
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
    add_component<S>(id, ...) -- add component to a specific store; entity
      need not be "moved"; multiple stores can hold data for the same entity.
    remove_component<S>(id) -- remove from one store, others unaffected.
    erase(id) -- clears the bitmap, removes from all occupied stores (O(set
      bits)), then destroys the entity in the registry.
    for_each<Cs...>(fn) -- iterates the primary component's storage,
      pre-builds a store_id_set_t target mask, and skips entities with a
      single is_subset_of check.
  No staging concept: entity_registry is a pure ID pool in this model;
  store_id_t{0} is not reserved.


Status Assessment (2026-03-13)
==============================

Feature status
---------------
1. Query / View System — RESOLVED. for_each<Cs...>(fn) on both archetype_scene
   and component_scene covers all practical use cases. A separate range-based
   view<> object is not needed; the callback form is sufficient.

2. Multi-storage iteration — RESOLVED, as part of item 1.

3. System scheduling — Intentionally out of scope.

4. Change detection — DEFERRED. All viable mechanisms hit the same wall:
   there is no tick/frame counter because Corvid owns no outer loop.
   - Storage-level version counter: cheap, but only tells you "something
     changed" and forces a full re-scan. Very limited utility.
   - Per-component tick (Bevy style): needs a parallel array per storage plus
     a global tick counter. Adds overhead to every storage even when unused.
     Still requires a linear scan or separate dirty set to collect changed IDs.
   - Dirty set / change list: needs a frame-boundary clear, which implies a
     tick counter and scheduler.
   Users who need change detection can embed a dirty flag or version field in
   their own component structs. Revisit if a concrete use case arises.

5. Entity relationships / hierarchy — NEXT (see "Next Steps" below).

6. Prefabs / templates — Deferred; no concrete driver.

7. Serialization — Out of scope.

8. Scene constructor flexibility — Open; second priority after scene graph.

Next Steps
-----------
1. Scene graph — A scene_graph class owning a tree of scene_node objects.
   Each node holds an entity handle (with generation counter). Design:
   - Nodes carry an entity handle; is_valid(handle) is checked on access.
   - Stale nodes (entity destroyed outside the tree) are pruned JIT on
     traversal; deletion cascades downward to children automatically.
   - If the tree is authoritative over entity lifetime, removing a node
     destroys the entity; no external deletion is possible.
   - Property propagation (e.g., position inheritance) is done by the caller
     traversing the tree and reading/writing components via the scene;
     scene_graph itself is traversal infrastructure only.
   - No on_destroy callbacks needed: handle validity replaces them.
   This is a separate data structure alongside the ECS, not embedded in it.

2. Scene constructor flexibility — Resolve the store_id alignment TODO
   (noted in archetype_scene.h).

3. Reactive callbacks and change detection — Deferred until a concrete use
   case pulls them in. Without a frame boundary they introduce ordering and
   re-entrancy complexity that outweighs the benefit.
