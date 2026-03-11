# CLAUDE.md

**Entity lifecycle**: Entities are created in the `entity_registry<>` (staging, `store_id = 0`), then activated into a storage. The registry tracks each entity's location as `{store_id, index}` for O(1) lookup.

**Storage hierarchy** (CRTP-based):
- `archetype_storage_base<CHILD, REG, TUPLE>` — common plumbing for all packed storages (entity ID tracking, registry pointer, `store_id`, remove/erase/clear) plus row wrappers and multi-component iterators
  - `archetype_storage<>` — concrete SoA multi-component storage
  - `chunked_archetype_storage<>` — AoSoA variant for locality tuning
  - `mono_archetype_storage<>` — single-element-tuple archetype; contiguous iterator and `component_t&` direct access

**Archetype scene** (`archetype_scene.h`): Aggregates a registry + a heterogeneous tuple of storages into a single interface. This is the primary entry point for ECS usage.

**Generation counters**: Entity handles carry an optional generation counter to detect ID reuse. Even with `generation_scheme::unversioned`, handles still provide more safety than raw IDs.

**`stable_ids.h` is legacy** — a standalone indexed vector predating the full ECS hierarchy. It is largely obsoleted by `entity_registry` and `mono_archetype_storage` and is not part of the registry/storage/archetype_scene design. Prefer the full ECS stack for new code.

**Roadmap**: `corvid/ecs/roadmap.md` documents missing features (query/view system, change detection, sparse storage, etc.) and prioritized next steps for building out the ECS further.
