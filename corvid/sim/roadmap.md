# What is CorvidSim?

CorvidSim, which is located in the "sim" subdirectory, is a project leveraging
and exercising the Corvid library. Using the WebSocket and ECS code, it hosts
an eponymous browser-based tower defense game.

# Steps

## Static HTTP server
- `corvid_sim_main.h`: Entry point, included exactly once from
  "tests/linux/notest_corvid_sim.cpp". Walks up from the executable until it
  finds `corvid/sim/web/dist` (or takes the web root as `argv[1]`), loads it
  into an `epoll_static_file_cache`, and serves it on `localhost:8080`. The
  server and sim share a 1200-slot, 50ms timing wheel, so the world loop can
  hit its 20 Hz cadence while the HTTP server's 30s request timeout stays
  within the wheel's span. Blocks in `sigwait` until SIGINT or SIGTERM.
- `web/index.html`: Game page with status, tick, lives/resources/phase HUD,
  "Start Wave" button, dual-canvas viewport, and a scrollable log pane.
---

## WebSocket transport and JSON protocol
- `sim_ws_handler.h`: `SimWsHandler` (inherits
  `epoll_http_websocket_transaction`) serves the `/ws` route, and each
  connection owns its own `SimGame`. The factory returns null when the sim map
  fails to load, which the server reports as a 503. Registers
  `on_message`/`on_close` callbacks and enables 20s-ping / 5s-pong keepalive.
  Owns the 20 Hz tick timer via the `timer_fuse` double-check pattern: each
  50ms frame runs `SimGame::next()`, streams the state, and advances the tick.
- `sim_json_parse.h`: Parses client messages: `hello`, `ui_canvas`,
  `ui_action`. Classifies by `"type"` field using enum reflection. A
  `ui_canvas` message requires the full field set (seq, buttons, coordinates,
  modifiers, event, button) and passes through the optional
  `command`/`parameters`.
- `sim_json_wire.h`: Builds server messages using `json_writer`:
  - `buildSimHelloAckJson()`: `{"type":"hello_ack",...}`
  - `buildSimGameStateJson()`: on the first frame sends
    `{"type":"world_snapshot",...}` carrying the map design (sprite filenames
    plus path joints), the defender build menu, the category definitions, and
    an embedded full delta; subsequent frames send `{"type":"world_delta",...}`.
    A delta carries upserts (position always; appearance, visual effects, and
    health only when modified on the current tick), erased entity IDs,
    transient explosions and beams, game state (wave, lives, resources,
    phase), and one-shot UI state (placement/spawn verdicts, selection, and
    the selected defender's summary).
  - `SimGameStateJson`: reusable extraction state. The vectors keep their
    allocations across frames; the body string is handed off to the socket
    each send.
- `web/src/main.ts` (TypeScript/Vite) WebSocket client:
  - Sends `hello` on open; receives `hello_ack` then ticks start.
  - Handles `world_snapshot` (resets state, draws path, applies delta) and
    `world_delta` (incremental update).
  - Validates every incoming message shape before processing.
  - Sends `ui_canvas` for click / dblclick / contextmenu / drag events and
    `ui_action` for button and form interactions.
---

## ECS world (`sim_world.h`)
- `SimWorld` owns the `archetype_scene` with staging (`sidStaging`, used as
  the tombstone state) plus five storages:
  - `sidInvaderAlpha` / `ArchInvaderAlpha`: path-following invaders
    (Position + Appearance + VisualEffects + Pathing + Invader + Health).
  - `sidDefenderAoe` / `ArchDefenderAoe`: area-of-effect defenders
    (Position + Appearance + VisualEffects + Defender + DefenderStats +
    Health + DefenderAoe).
  - `sidBullet` / `ArchBullet`: spawned projectiles
    (Position + Velocity + Appearance + DefenderBullet).
  - `sidDefenderShooter` / `ArchDefenderShooter`: projectile-firing defenders
    (Position + Appearance + VisualEffects + Defender + DefenderStats +
    Health + DefenderShooter).
  - `sidDefenderHitscan` / `ArchDefenderHitscan`: instant-hit single-target
    defenders (Position + Appearance + VisualEffects + Defender +
    DefenderStats + Health + DefenderHitscan).
- Entity templates: `EntityTemplateStore` maps labels to component megatuples;
  `spawnEntity(label)` instantiates one, stamping `modified` fields with the
  current tick and marking the entity dirty.
- Path geometry: `PathJoints` -> `SegmentedPath` with cumulative distances for
  O(log n) progress-to-position mapping; corner snapping for fast movers.
- Physics per frame (`next()`): velocity movers advance and bounce at the
  world boundary; `Pathing` entities advance by `speed` and are collected as
  escapees when they exit the path; projectiles detonate on expiry or on the
  first invader struck along their swept path, with splash damage around the
  detonation point; defenders off cooldown collect in-range candidates
  (skipping invaders already killed this tick), pick a target by `TargetMode`,
  and attack by archetype: an AoE pulse hits every candidate, a shooter spawns
  a bullet aimed at the intercept-solved lead position (with an optional
  muzzle-flash beam), and a hitscan applies instant damage with a transient
  beam. All damage flows through `applyAttackDamage`, which clamps health at
  zero, credits `DefenderStats`, and queues kills.
- Deferred resolution: `resolveEscapees` and `resolveKills` destructively
  visit their queues, and erasure tombstones entities to staging so they show
  up as deletions in the next delta. Kills emit a death explosion.
- Dirty tracking via registry metadata (`WorldTick` last-change tick) and an
  `updatedEntities_` list; `markAllDirty(full)` also stamps `modified` fields
  for full snapshot serialization.
- Visual effect helpers: `flashEntity` (color plus expiry) and `setCooldown`
  (color, absolute expiry, and duration).
---

## Game simulation (`sim_game.h`)
- `SimGame` owns `SimWorld` and the game rules: `GamePhase` (build / wave /
  game_over / victory), lives (20), and resources (1000).
- Maps load from JSON files in `corvid/sim/maps` (found by walking up from the
  executable), keyed by stem filename and sorted; `loadMap()` activates the
  first. `loadMapFromJson` fills a `MapDesign` (categories, entities, paths,
  waves); `finalizeMapDesign` registers entity templates and derives the
  purchasable, `menuOrder`-sorted defender menu. Each map load prints a CSV
  report of its invader and defender definitions; set
  `CORVID_SUPPRESS_MAP_ENTITY_CSV` to suppress it.
- `next()` applies pending UI intents, runs world physics in the build and
  wave phases, and in the wave phase advances `WaveTick`, spawns due enemies,
  resolves escapees (decrementing lives, with 0 lives ending the game),
  resolves kills (crediting bounties), and ends the wave when all spawns are
  out and no invaders remain (next wave, or victory after the last).
- UI intents are recorded last-wins and applied at the start of the next
  frame:
  - `"placing"` ghost drag: validates placement and reports
    `placementAllowed`.
  - `"spawn"` click: checks affordability and placement, spawns the defender,
    and deducts its cost.
  - Left click: selects the defender under the cursor (range circle plus
    summary); deselects otherwise.
  - `"moving"` drag: relocates the selected defender for free, excluding its
    own footprint from the overlap test.
  - `ui_action "start_wave"`: transitions build -> wave and credits the
    wave's resource influx.
- `extractDelta()` / `extractFull()` / `markAllDirty()` provide the data the
  wire layer needs.
---

## Client rendering (`web/src/main.ts`)
- Dual-canvas layout: `background-canvas` for the static path;
  `foreground-canvas` for live entities plus HUD compositing.
- World space 1920x1080 mapped to 960x540 canvas pixels (uniform scale, no
  letterboxing).
- `requestAnimationFrame` loop (runs at the display's native refresh rate)
  with linear interpolation between the last two 20 Hz snapshots for smooth
  motion.
- Per-entity sprite cache (offscreen `HTMLCanvasElement`): filled circle plus
  glyph pre-rendered once per unique (glyph, radius, fg, bg) bucket; reused
  each frame via `drawImage`.
- Visual effects layered in draw order: range circle, motion trail, entity
  sprite, selection outline, flash overlay (flickers at 62.5ms intervals
  until expiry), cooldown wedge draining until its expiry, health bars from
  the streamed current/max, and transient explosions and beams.
- Build menu rendered from the snapshot's categories and defender menu: the
  top level shows categories, and clicking one shows its defenders, with
  drag-to-place ghosts.
- Canvas HUD overlay (lives plus resources) and FPS counter rendered to
  separate offscreen canvases and composited onto the visible foreground
  canvas when dirty; the same values are also mirrored in the DOM status text
  above the viewport.
- Drag throttling: `mousemove` batches `dragmove` messages to one per
  `requestAnimationFrame`.
---

## Known gaps / next steps
- Web asset pipeline (Vite/TypeScript) must be built separately (`npm run
  build` in `web/`) before the C++ server can serve the dist files.
- Map loader parity: `loadMapFromJson` never parses `targetMode` (every
  defender targets `first`), `Appearance.trailColor`, bullet `splashRadius`,
  `damageOverTime`, the damage-type fields, shooter `spread`, hitscan
  `halfAngleDeg`/`coneRadius`, or muzzle-flash `lineWidth`/`secondaryColor`.
  In particular, explosive (splash) bullets are not definable from map data.
- Multi-path wire format: the snapshot flattens all paths' joints into one
  array with a single `pathWidth`, so a map with more than one path would
  render as a single connected polyline.
- Health regeneration is parsed and reported but not applied (`SimWorld::next`
  TODO).
- No support for the idea of rotation speed limits.
- No support for defenders taking damage.
- No support for DoT and for various damage types, much less
  armor/resistance.
