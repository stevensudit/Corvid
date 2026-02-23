# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

CMakeLists.txt lives in `tests/` only — there is none at the project root.

```sh
# Clean build + run all tests
./cleanbuild.sh                        # default (libc++)
./cleanbuild.sh libstdcpp              # use libstdc++ instead
./cleanbuild.sh tidy                   # also run clang-tidy

# Build a single target
cd tests && cmake --build build --target <target>
cd tests/build && ninja <target>

# Format all source files (required before committing)
./format_all.sh
```

Build directory: `tests/build/`. Test binaries: `tests/build/release_bin/`.

When creating new test files (`*_test.cpp` in `tests/`), CMakeLists.txt picks them up automatically via glob.

## Code Style

- Run `./format_all.sh` after code changes and before `git commit`. Claude's Edit/Write tools bypass IDE save hooks, so this must be done manually.
- When referencing code locations, use markdown link format: `[filename.ext:line](filename.ext#Lline)`

## Git Workflow

- Use `git add .` when committing (to include any user-made changes alongside Claude's)
- Use `git switch` instead of `git checkout`
- PRs are squash-merged — respond to review with additional commits, not amends

## Testing

Framework: custom minitest (`tests/minitest.h`). Macros: `EXPECT_EQ`, `EXPECT_TRUE`, etc.

## TODO File

The root `TODO` file tracks enhancement requests and design decisions. When encountering TODO comments in code that represent enhancements (not immediate work), move them there. Rewrite positional references ("below", "above") to name the specific function or class.

## Architecture

Corvid is a **header-only C++23 library** (no external dependencies beyond libc++). All headers live under `corvid/`. Six major subsystems:

### Strings (`corvid/strings/`)
The most foundational subsystem. Provides `cstring_view` (null-terminated string view), `fixed_string<N>` (compile-time NTTP strings), and a suite of composable operations: locating, splitting, trimming, concat/join, conversion, and enum↔string via the enum registry. Operations use an append-target model — they write to `std::string` or `std::ostream`.

### Enums (`corvid/enums/`)
Three enum adapters built on a shared `enum_spec_v<>` specialization point:
- `bitmask_enum<>` — satisfies the BitmaskType named requirement; optional wrapping prevents invalid bit combinations
- `sequence_enum<>` — strongly-typed integer enum for indices/IDs
- `bool_enum<>` — type-safe boolean wrapper

Enum metadata (names, printing) is registered via `enum_spec_v<>` specialization. The strings subsystem uses this for enum↔string conversion.

### Containers (`corvid/containers/`)
- `strong_type<T, TAG>` — generic strongly-typed wrapper
- `stable_ids<>` — dense ID-based storage with FIFO ID reuse
- `intern_table<>` / `interned_value<>` — singleton value storage with stable addresses
- `extensible_arena` — thread-local arena allocator
- `circular_buffer<>`, `enum_vector<>`, `enum_variant<>`, `interval<>`

### ECS (`corvid/ecs/`)
A non-trivial subsystem — understanding it requires reading several files together.

**Entity lifecycle**: Entities are created in the `entity_registry<>` (staging, `store_id = 0`), then activated into a storage. The registry tracks each entity's location as `{store_id, index}` for O(1) lookup.

**Storage hierarchy** (CRTP-based, in `storage_base.h`):
- `storage_base<CHILD, REG>` — common plumbing
- `archetype_storage<>` — SoA multi-component storage; removal uses swap-and-pop for dense packing
- `chunked_archetype_storage<>` — AoSoA variant for locality tuning
- `component_storage<>` — single-component per-entity

**Scene** (`corvid/ecs/scene.h`): Aggregates a registry + a heterogeneous tuple of storages into a single interface. This is the primary entry point for ECS usage.

**Generation counters**: Entity handles carry an optional generation counter to detect ID reuse. Even with `generation_scheme::unversioned`, handles still provide more safety than raw IDs.

### Meta (`corvid/meta/`)
Metaprogramming foundations used throughout the library: concepts (`ScopedEnum`, `Integer`, `AppendTarget`, etc.), traits (`is_specialization_of_v<>`, `is_bool_v<>`), and the `naming_traits<>` extensible type-naming system.

### Controllers (`corvid/controllers/`)
`pid_controller<>` and `sopdt_plant<>` (Second-Order Plus Dead Time) for feedback control loops.

## Key Cross-Cutting Patterns

- **CRTP** used extensively to avoid virtual dispatch while still sharing implementation (e.g., `storage_base<CHILD, REG>`, `archetype_storage_base<>`)
- **Specialization points** enable open-ended extension without modifying core code (e.g., `enum_spec_v<>`, `intern_traits<>`)
- **Inline namespaces** allow selective imports (e.g., `using namespace corvid::strings::trimming`)
- **`npos` and `npos_choice`**: base string position/npos types are in `corvid/strings/string_base.h`; `npos_choice` is in `corvid/strings/locating.h`
