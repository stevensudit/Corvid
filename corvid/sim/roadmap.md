# What is CorvidSim?

CorvidSim, which is located in the "sim" subdirectory, is a project leveraging and
exercising the Corvid library. Using the WebSocket and ECS code, it hosts an
eponymous browser-based tower defense game.

# Steps

## Static HTTP server
- `corvid_sim_main.h` -- Entry point: walks up from the executable to find
  `corvid/sim/web/dist`, loads it into a `static_file_cache`, and serves it on
  `localhost:8080`. Accepts `-testonly` to exit cleanly in CI.
- `web/index.html` -- Game page with status, tick, lives/resources/phase HUD,
  "Start Wave" button, dual-canvas viewport, and a scrollable log pane.
---

## WebSocket transport and JSON protocol
- `sim_ws_handler.h` -- `SimWsHandler` (inherits `http_websocket_transaction`).
  Registers `on_message`/`on_close` callbacks. Enables 20s-ping / 5s-pong
  keepalive. Owns the 20 Hz tick timer via the `timer_fuse` double-check
  pattern. Drives one `SimGame::next()` call per 50 ms frame, then sends the
  game state.
- `sim_json_parse.h` -- Parses client messages: `hello`, `ui_canvas`,
  `ui_action`. Classifies by `"type"` field using enum reflection.
- `sim_json_wire.h` -- Builds server messages using `json_writer`:
  - `build_sim_hello_ack_json()` -- `{"type":"hello_ack",...}`
  - `build_sim_game_state_json()` -- on the first frame sends
    `{"type":"world_snapshot","paths":[...],"delta":{...}}`; subsequent frames
    send `{"type":"world_delta",...}`. Incremental mode only emits entities
    whose `Appearance` or `VisualEffects` changed on the current tick.
  - `sim_game_state_json` reusable buffer with high-watermark reserves.
- `web/src/main.ts` (TypeScript/Vite) -- WebSocket client:
  - Sends `hello` on open; receives `hello_ack` then ticks start.
  - Handles `world_snapshot` (resets state, draws path, applies delta) and
    `world_delta` (incremental update).
  - Validates every incoming message shape before processing.
  - Sends `ui_canvas` for click / dblclick / contextmenu / drag events.
  - Sends `ui_action` for button and form interactions.
---

## ECS world (`sim_world.h`)
- `SimWorld` owns the `archetype_scene` with five storages:
  - `sidPos` / `ArchP` -- static background entities (`Position + Appearance`).
  - `sidPosVel` / `ArchPV` -- velocity-driven movers
    (`Position + Velocity + Appearance`).
  - `sidEnemy` / `ArchEnemy` -- path-following invaders
    (`Position + Appearance + VisualEffects + PathFollower + Invader`).
  - `sidTower` / `ArchTower` -- placed towers
    (`Position + Appearance + VisualEffects + Tower`).
  - `sidBullet` / `ArchBullet` -- bullets
    (`Position + Velocity + Bullet`).
- Path geometry: `PathJoints` -> `SegmentedPath` with cumulative distances for
  O(log n) progress-to-position mapping; corner snapping for fast movers.
- Physics per frame (`next()`): velocity movers bounce at world boundary;
  `PathFollower` entities advance by `speed` and are collected as escapees when
  they exit the path.
- Dirty tracking via registry metadata (last-change tick) and an
  `updatedEntities_` list; `markAllDirty()` stamps `Appearance.modified` /
  `VisualEffects.modified` for full snapshot serialization.
- Tower attack range: `towersAttack()` detects circle-circle overlaps and
  triggers flash visual effects on both tower and enemy. Actual
  damage/kill/cooldown is stubbed out behind `#if 0`.
- `flashEntity()` sets `flash_color` and `flash_expiry` (in ticks) on any
  entity carrying `VisualEffects`.
---

## Game simulation (`sim_game.h`)
- `SimGame` owns `SimWorld` and the game rules: `GamePhase` (build / wave /
  game_over / victory), wave definitions, lives, and resources.
- `WaveDefinition` holds a list of `EnemySpawn` records (startTick, enemyType).
  `doLoadMap1()` builds a single hardcoded spiral path and one wave of 20
  enemies spaced 20 ticks apart.
- `next()` runs `SimWorld::next()`, advances `WaveTick`, spawns due enemies,
  resolves escapees (decrement lives), and transitions to `game_over` at 0
  lives.
- Input handling:
  - Left click -- selects tower (shows range circle via `VisualEffects`);
    deselects previous.
  - Double-click -- places a new tower at that world position.
  - Right click -- flashes the entity under the cursor.
  - `ui_action "start_wave"` -- transitions build -> wave phase.
- `extractDelta()` / `extractPaths()` / `markAllDirty()` provide the data the
  wire layer needs.
---

## Client rendering (`web/src/main.ts`)
- Dual-canvas layout: `background-canvas` for the static path; `foreground-
  canvas` for live entities + HUD compositing.
- World space 1920x1080 mapped to 960x540 canvas pixels (uniform scale, no
  letterboxing).
- `requestAnimationFrame` loop (runs at the display's native refresh rate) with
  linear interpolation between the last two 20 Hz snapshots for smooth motion.
- Per-entity sprite cache (offscreen `HTMLCanvasElement`): filled circle + glyph
  pre-rendered once per unique (glyph, radius, fg, bg) bucket; reused each frame
  via `drawImage`.
- Visual effects layered in draw order: range circle, entity sprite, selection
  outline, flash overlay (flickers at 62.5 ms intervals until expiry).
- Canvas HUD overlay (lives + resources) rendered to a separate offscreen
  canvas and composited onto the visible foreground canvas only when dirty;
  the same values are also mirrored in the DOM status text above the viewport.
- FPS counter rendered to a separate offscreen canvas, refreshed at 100 ms
  intervals, then composited onto the visible foreground canvas.
- Drag throttling: `mousemove` batches `dragmove` messages to one per
  `requestAnimationFrame`.
---

## Known gaps / next steps
- Tower damage is disabled (`#if 0` in `towersAttack`); needs cooldown, health
  reduction, bounty award, and tombstoning.
- Only one hardcoded map and one wave; need data-driven map/wave loading.
- `placeTower()` in `SimGame` is a stub; spending resources and enforcing build
  slots is not implemented.
- No victory condition when all waves clear with lives remaining.
- Enemy variety (`enemyType` field exists but all enemies share the same speed
  and appearance).
- `sidBullet` archetype exists but no projectile-firing tower type is wired up.
- Web asset pipeline (Vite/TypeScript) must be built separately (`npm run
  build` in `web/`) before the C++ server can serve the dist files.
