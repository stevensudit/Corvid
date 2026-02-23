# CLAUDE.md

**Entity lifecycle**: Entities are created in the `entity_registry<>` (staging, `store_id = 0`), then activated into a storage. The registry tracks each entity's location as `{store_id, index}` for O(1) lookup.

**Storage hierarchy** (CRTP-based):
- `storage_base<CHILD, REG>` — common plumbing for all packed storages (entity ID tracking, registry pointer, `store_id`, remove/erase/clear)
  - `archetype_storage_base<CHILD, REG, TUPLE>` — adds row access and SoA component management; shared by the two archetype variants
    - `archetype_storage<>` — concrete SoA multi-component storage
    - `chunked_archetype_storage<>` — AoSoA variant for locality tuning
  - `component_storage<>` — single-component per-entity; inherits `storage_base` directly, not `archetype_storage_base`

**Scene** (`scene.h`): Aggregates a registry + a heterogeneous tuple of storages into a single interface. This is the primary entry point for ECS usage.

**Generation counters**: Entity handles carry an optional generation counter to detect ID reuse. Even with `generation_scheme::unversioned`, handles still provide more safety than raw IDs.

**`stable_ids.h` is legacy** — a standalone indexed vector predating the full ECS hierarchy. It is largely obsoleted by `entity_registry` and `component_storage` and is not part of the registry/storage/scene design. Prefer the full ECS stack for new code.
