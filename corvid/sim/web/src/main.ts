import type {
  ClientMsg,
  DefenderMenuItem,
  EntityAppearance,
  EntityPosition,
  EntityUpsert,
  EntityVisualEffects,
  Position,
  ServerMsg,
  UiCanvasMsg,
  UiState,
  WorldDelta,
} from './types.js'

// Canvas-friendly color representation derived from packed RGBA wire values.
interface RenderColor {
  css: string
  alpha: number
}

// Cached offscreen sprite bitmap plus the origin offset needed to center it.
interface SpriteCacheEntry {
  canvas: HTMLCanvasElement
  offset: number
}

// Client-side appearance data derived from `EntityAppearance` and normalized
// for rendering entities and menu items.
interface RenderAppearance {
  glyph: string
  radius: number // Radius of entity
  fg: RenderColor
  bg: RenderColor
  attackRadius: number // Radius of attack
}

// Local placement-preview state for a defender being dragged or staged to spawn.
interface GhostState {
  worldX: number
  worldY: number
  entityName: string
  displayName: string
  appearance: RenderAppearance
  attackRadius: number  // world units, from Appearance.attackRadius
  pending: boolean      // true = dropped on map, awaiting confirmation click
  placeable: boolean    // true = can be placed at the current position
  spawnPending: boolean // true = dropped and clicked, awaiting server confirmation
}

// Client-side visual effects derived from `EntityVisualEffects`.
interface RenderVisualEffects {
  selection: RenderColor
  rangeRadius: number
  range: RenderColor
  flash: RenderColor
  flashExpiry: number
  flashStartedAt: number
}

// Fully materialized render snapshot for one entity id, derived from
// `EntityUpsert`.
interface RenderEntityUpsert {
  pos: EntityPosition
  app: RenderAppearance
  fx: RenderVisualEffects
}

// Pointer bookkeeping for an in-progress foreground-canvas interaction.
interface CanvasPointerState {
  button: number
  buttons: number
  dragging: boolean
}

// Mouse event sample expressed in both canvas-space and world-space coordinates.
interface CanvasPointerSample {
  button: number
  buttons: number
  canvasX: number
  canvasY: number
  worldX: number
  worldY: number
  shift: boolean
  ctrl: boolean
  alt: boolean
  meta: boolean
}

type PanelSide = 'left' | 'right'

// Textual content and attachment side for the overlay panel renderer.
interface SidePanelModel {
  side: PanelSide
  title: string
  lines: string[]
}

const DEFAULT_RENDER_APPEARANCE: RenderAppearance = {
  glyph: '?',
  radius: 5,
  fg: { css: 'rgba(255, 255, 255, 1)', alpha: 1 },
  bg: { css: 'rgba(0, 0, 0, 1)', alpha: 1 },
  attackRadius: 0,
}

const TRANSPARENT_RENDER_COLOR: RenderColor = {
  css: 'rgba(0, 0, 0, 0)',
  alpha: 0,
}

const DEFAULT_RENDER_VISUAL_EFFECTS: RenderVisualEffects = {
  selection: TRANSPARENT_RENDER_COLOR,
  rangeRadius: 0,
  range: TRANSPARENT_RENDER_COLOR,
  flash: TRANSPARENT_RENDER_COLOR,
  flashExpiry: 0,
  flashStartedAt: 0,
}

// --- DOM setup ---

// Look up a required DOM node and fail fast if the page shape does not match
// what this client expects.
function requireEl<T extends HTMLElement>(
  id: string,
  type: abstract new (...args: any[]) => T,
): T {
  const el = document.getElementById(id)
  if (!(el instanceof type)) throw new Error(`Missing #${id}`)
  return el
}

const statusEl = requireEl('status', HTMLElement)
const tickEl = requireEl('tick', HTMLElement)
const logEl = requireEl('log', HTMLElement)
const viewportShell = requireEl('viewport-shell', HTMLElement)
const viewportHost = requireEl('viewport-host', HTMLElement)
const viewportFrame = requireEl('viewport-frame', HTMLElement)
const fullscreenToggle = requireEl('fullscreen-toggle', HTMLButtonElement)
const backgroundCanvas = requireEl('background-canvas', HTMLCanvasElement)
const foregroundCanvas = requireEl('foreground-canvas', HTMLCanvasElement)
const overlayCanvas = requireEl('overlay-canvas', HTMLCanvasElement)
const livesEl = document.getElementById('lives')
const resourcesEl = document.getElementById('resources')
const phaseEl = document.getElementById('phase')

const maybeBgCtx = backgroundCanvas.getContext('2d')
if (!maybeBgCtx) throw new Error('Could not get 2D background canvas context')
const bgCtx: CanvasRenderingContext2D = maybeBgCtx

const maybeFgCtx = foregroundCanvas.getContext('2d')
if (!maybeFgCtx) throw new Error('Could not get 2D foreground canvas context')
const fgCtx: CanvasRenderingContext2D = maybeFgCtx
const maybeOverlayCtx = overlayCanvas.getContext('2d')
if (!maybeOverlayCtx) throw new Error('Could not get 2D overlay canvas context')
const overlayCtx: CanvasRenderingContext2D = maybeOverlayCtx
// HUD and FPS are composited from offscreen canvases so they can redraw at
// their own cadence without forcing the world layer to rebuild extra state.
const hudCanvas = document.createElement('canvas')
hudCanvas.width = foregroundCanvas.width
hudCanvas.height = foregroundCanvas.height
const maybeHudCtx = hudCanvas.getContext('2d')
if (!maybeHudCtx) throw new Error('Could not get 2D HUD canvas context')
const hudCtx: CanvasRenderingContext2D = maybeHudCtx
const fpsCanvas = document.createElement('canvas')
fpsCanvas.width = foregroundCanvas.width
fpsCanvas.height = foregroundCanvas.height
const maybeFpsCtx = fpsCanvas.getContext('2d')
if (!maybeFpsCtx) throw new Error('Could not get 2D FPS canvas context')
const fpsCtx: CanvasRenderingContext2D = maybeFpsCtx
// --- World / canvas coordinate mapping ---
//
// The server simulation runs in a 1920x1080 world space. The canvas is sized
// to the same 16:9 aspect ratio so the mapping is a uniform scale with no
// letterboxing needed.
const WORLD_W = 1920
const WORLD_H = 1080
const DEFAULT_CANVAS_W = 960
const DEFAULT_CANVAS_H = 540
const DEFAULT_OVERLAY_W = 280
const OVERLAY_WIDTH_RATIO = DEFAULT_OVERLAY_W / DEFAULT_CANVAS_W

// Keep a canvas element's CSS box and backing pixel buffer in sync.
function setCanvasSize(
  canvas: HTMLCanvasElement,
  cssWidth: number,
  cssHeight: number,
  pixelWidth: number,
  pixelHeight: number,
): void {
  canvas.style.width = `${cssWidth}px`
  canvas.style.height = `${cssHeight}px`
  if (canvas.width !== pixelWidth) canvas.width = pixelWidth
  if (canvas.height !== pixelHeight) canvas.height = pixelHeight
}

// Fit the fixed-aspect simulation viewport inside the available host area.
function fitCanvasRect(availableWidth: number, availableHeight: number): [number, number] {
  const aspect = WORLD_W / WORLD_H
  let width = Math.max(1, Math.floor(availableWidth))
  let height = Math.max(1, Math.floor(width / aspect))

  if (height > availableHeight) {
    height = Math.max(1, Math.floor(availableHeight))
    width = Math.max(1, Math.floor(height * aspect))
  }

  return [width, height]
}

// Reflect the browser fullscreen state in the button label.
function updateFullscreenButtonLabel(): void {
  fullscreenToggle.textContent = document.fullscreenElement === viewportShell
    ? 'Exit Fullscreen'
    : 'Expand View'
}

let currentPathPoints: Array<{ x: number; y: number }> = []

// Recompute every canvas size when the viewport host or fullscreen state
// changes, then invalidate cached overlay/HUD rendering.
function resizeViewport(): void {
  const isFullscreen = document.fullscreenElement === viewportShell
  const availableWidth = isFullscreen
    ? viewportHost.clientWidth
    : Math.min(viewportHost.clientWidth, DEFAULT_CANVAS_W)
  const availableHeight = isFullscreen
    ? viewportHost.clientHeight
    : DEFAULT_CANVAS_H
  const [cssWidth, cssHeight] = fitCanvasRect(availableWidth, availableHeight)
  const deviceScale = window.devicePixelRatio || 1
  const pixelWidth = Math.max(1, Math.round(cssWidth * deviceScale))
  const pixelHeight = Math.max(1, Math.round(cssHeight * deviceScale))
  const overlayCssWidth = Math.max(
    1,
    Math.min(
      cssWidth,
      Math.max(
        Math.min(220, cssWidth),
        Math.min(Math.round(cssWidth * 0.4), Math.round(cssWidth * OVERLAY_WIDTH_RATIO)),
      ),
    ),
  )
  const overlayPixelWidth = Math.max(1, Math.round(overlayCssWidth * deviceScale))
  const canvasSizeChanged =
    backgroundCanvas.width !== pixelWidth ||
    backgroundCanvas.height !== pixelHeight

  viewportFrame.style.width = `${cssWidth}px`
  viewportFrame.style.height = `${cssHeight}px`

  setCanvasSize(backgroundCanvas, cssWidth, cssHeight, pixelWidth, pixelHeight)
  setCanvasSize(foregroundCanvas, cssWidth, cssHeight, pixelWidth, pixelHeight)
  setCanvasSize(overlayCanvas, overlayCssWidth, cssHeight, overlayPixelWidth, pixelHeight)
  setCanvasSize(hudCanvas, cssWidth, cssHeight, pixelWidth, pixelHeight)
  setCanvasSize(fpsCanvas, cssWidth, cssHeight, pixelWidth, pixelHeight)

  if (canvasSizeChanged) {
    glyphFontSizeCache.clear()
    entitySpriteCache.clear()
  }
  if (currentPathPoints.length > 1) drawBackground(currentPathPoints)
  invalidateHud()
  invalidateSidePanel()
}

// Convert world-space coordinates from the simulation into canvas pixels.
function worldToCanvas(wx: number, wy: number): [number, number] {
  return [
    (wx + WORLD_W / 2) * foregroundCanvas.width / WORLD_W,
    (wy + WORLD_H / 2) * foregroundCanvas.height / WORLD_H,
  ]
}

// Convert canvas pixels back into simulation world coordinates.
function canvasToWorld(cx: number, cy: number): [number, number] {
  return [
    (cx - foregroundCanvas.width / 2) * WORLD_W / foregroundCanvas.width,
    (cy - foregroundCanvas.height / 2) * WORLD_H / foregroundCanvas.height,
  ]
}

// Convert a world-space distance into the current canvas scale.
function worldLengthToCanvas(length: number): number {
  return length * foregroundCanvas.width / WORLD_W
}

// Sample a mouse event once and package both canvas-space and world-space
// coordinates for downstream input handling and server messages.
function eventToCanvasSample(event: MouseEvent): CanvasPointerSample {
  const rect = foregroundCanvas.getBoundingClientRect()
  const canvasX = (event.clientX - rect.left) * (foregroundCanvas.width / rect.width)
  const canvasY = (event.clientY - rect.top) * (foregroundCanvas.height / rect.height)
  const [worldX, worldY] = canvasToWorld(canvasX, canvasY)
  return {
    button: event.button,
    buttons: event.buttons,
    canvasX: Math.round(canvasX),
    canvasY: Math.round(canvasY),
    worldX: Math.round(worldX),
    worldY: Math.round(worldY),
    shift: event.shiftKey,
    ctrl: event.ctrlKey,
    alt: event.altKey,
    meta: event.metaKey,
  }
}

// Reuse an existing pointer sample while overriding button bookkeeping for
// synthesized drag events.
function withPointerButton(
  sample: CanvasPointerSample,
  button: number,
  buttons = sample.buttons,
): CanvasPointerSample {
  return {
    ...sample,
    button,
    buttons,
  }
}

// Build a pointer sample from the ghost's current world position instead of the
// literal mouse location.
function ghostStateToSample(
  ghost: GhostState,
  event: MouseEvent,
  button = event.button,
  buttons = event.buttons,
): CanvasPointerSample {
  const [canvasX, canvasY] = worldToCanvas(ghost.worldX, ghost.worldY)
  return {
    button,
    buttons,
    canvasX: Math.round(canvasX),
    canvasY: Math.round(canvasY),
    worldX: Math.round(ghost.worldX),
    worldY: Math.round(ghost.worldY),
    shift: event.shiftKey,
    ctrl: event.ctrlKey,
    alt: event.altKey,
    meta: event.metaKey,
  }
}

// --- FPS tracking ---

let fps = 0
let lastFrameTime = 0

// --- Interpolation state ---

// The server streams discrete snapshots. We retain both the last committed
// render state and the newest one so each animation frame can blend between
// them instead of stepping entity motion every network tick.
// `currEntitiesById` holds the latest authoritative render state for every
// entity we know about. `prevRenderStateById` only keeps the immediately prior
// render state for entities touched by the most recent delta, which is enough
// to interpolate from the old snapshot toward the new one.
const currEntitiesById = new Map<number, RenderEntityUpsert>()
const prevRenderStateById = new Map<number, RenderEntityUpsert>()
let prevSnapshotTime = 0
let currSnapshotTime = 0
let snapshotIntervalMs = 50
let lives = 0
let resources = 0
let lastFpsOverlayUpdateTime = 0
let hudDirty = true
let currentPathWidth = 40
const glyphFontSizeCache = new Map<string, number>()
// Entity appearances come from a small, mostly fixed set of defender/enemy
// visuals, so we memoize prerendered sprites across frames and world updates.
// The cache is cleared when a resize changes the render scale, rather than
// paying per-frame draw costs or maintaining an eviction policy.
const entitySpriteCache = new Map<string, SpriteCacheEntry>()
const FPS_OVERLAY_INTERVAL_MS = 100
const FLASH_FLICKER_INTERVAL_MS = 62.5
let nextClientSeq = 1
let canvasPointerState: CanvasPointerState | null = null
let pendingDragMove: CanvasPointerSample | null = null
let dragMoveFrameRequested = false
let sidePanelDirty = true
let edgeHoverSide: PanelSide | null = null
let defenderMenuItems: DefenderMenuItem[] = []
let selectedMenuIndex: number | null = null
let menuScrollOffset = 0
let ghostState: GhostState | null = null
let menuDragActive = false
let currentPhase = 'build'
let mouseOverOverlay = false
let overlayStayOpen = false
let currentPanelSide: PanelSide = 'right'
let overlaySideSwapFrame = 0
let selectedDefenderSummary: DefenderMenuItem | null = null
let selectedDefenderPosition: Position | null = null

const SIDE_PANEL_TRIGGER_RATIO = 0.25
const SIDE_PANEL_TRIGGER_MIN_PX = 72
const SIDE_PANEL_TRIGGER_MAX_PX = 180
const MENU_COLS = 2
const MENU_VISIBLE_ROWS = 4
const SIDE_PANEL_RADIUS = 18
const SIDE_PANEL_LINE_HEIGHT = 22
const SIDE_PANEL_TEXT_MARGIN = 28

// Interpolate linearly between two numeric values.
function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t
}

// Draw the static path layer onto the background canvas.
function drawBackground(
  pathPoints: Array<{ x: number; y: number }>,
  pathWidth = currentPathWidth,
): void {
  currentPathPoints = pathPoints
  currentPathWidth = pathWidth
  bgCtx.clearRect(0, 0, backgroundCanvas.width, backgroundCanvas.height)
  if (pathPoints.length <= 1) return

  bgCtx.save()
  bgCtx.strokeStyle = '#3b82f6'
  bgCtx.lineWidth = Math.max(2, worldLengthToCanvas(pathWidth))
  bgCtx.lineCap = 'round'
  bgCtx.lineJoin = 'round'
  bgCtx.beginPath()
  const [startX, startY] = worldToCanvas(pathPoints[0].x, pathPoints[0].y)
  bgCtx.moveTo(startX, startY)
  for (const point of pathPoints.slice(1)) {
    const [x, y] = worldToCanvas(point.x, point.y)
    bgCtx.lineTo(x, y)
  }
  bgCtx.stroke()
  bgCtx.restore()
}

// Render a frame by interpolating entity positions between the previous and
// current snapshots, then drawing any active placement ghost on top.
function renderInterpolated(now: number): void {
  const interval = Math.max(snapshotIntervalMs, 1)
  const t = Math.min((now - currSnapshotTime) / interval, 1)

  fgCtx.clearRect(0, 0, foregroundCanvas.width, foregroundCanvas.height)
  for (const e of currEntitiesById.values()) {
    const prevState = prevRenderStateById.get(e.pos.id)
    const prevPos = prevState?.pos
    const prevApp = prevState?.app ?? e.app
    const wx = prevPos ? lerp(prevPos.x, e.pos.x, t) : e.pos.x
    const wy = prevPos ? lerp(prevPos.y, e.pos.y, t) : e.pos.y
    const [x, y] = worldToCanvas(wx, wy)
    const radius = worldLengthToCanvas(lerp(prevApp.radius, e.app.radius, t))
    drawEntity(x, y, radius, e.app, e.fx, now)
  }
  drawGhost()
}

// Rebuild the cached HUD overlay only when marked dirty.
function updateHudOverlay(): void {
  if (!hudDirty) return

  hudCtx.clearRect(0, 0, hudCanvas.width, hudCanvas.height)
  drawHud(hudCtx)
  hudDirty = false
}

// Refresh the FPS overlay at a throttled cadence instead of every frame.
function updateFpsOverlay(now: number): void {
  if (now - lastFpsOverlayUpdateTime < FPS_OVERLAY_INTERVAL_MS) return

  fpsCtx.clearRect(0, 0, fpsCanvas.width, fpsCanvas.height)
  drawFps(fpsCtx)
  lastFpsOverlayUpdateTime = now
}

// How far the panel peeks out from the edge (CSS pixels).
const PANEL_PEEK_PX = 18

type OverlayLevel = 'hidden' | 'peek' | 'open'

// Report whether overlay content should remain open even without direct hover.
function hasPinnedOpenPanel(): boolean {
  return ghostState?.pending === true ||
    selectedDefenderPosition !== null ||
    overlayStayOpen
}

// Choose which screen edge the overlay should currently attach to.
function getTargetSide(): PanelSide {
  // A pinned panel (like pending placement) owns the side. Otherwise follow
  // the current edge hover so the peek/open transition stays on one edge.
  return getSidePanelModel()?.side ?? edgeHoverSide ?? currentPanelSide
}

// Decide whether the overlay is hidden, peeking, or fully open.
function getOverlayLevel(): OverlayLevel {
  // The overlay behaves like a tiny state machine:
  // hidden when irrelevant, peek when edge-hovering, and open when hovered or
  // when a pinned interaction (selected defender / pending placement) owns it.
  if (menuDragActive) return 'hidden'
  const hasPanelContent = getSidePanelModel() !== null
  const hasBuildMenu = currentPhase === 'build' && defenderMenuItems.length > 0
  if (!hasPanelContent && !hasBuildMenu) return 'hidden'
  if (hasPinnedOpenPanel() || mouseOverOverlay) return 'open'
  if (edgeHoverSide !== null) return 'peek'
  return 'hidden'
}

// Return the narrow strip that can turn a peeking panel into a fully open one.
function getOverlayPeekActivationBounds(): DOMRect | null {
  if (getOverlayLevel() !== 'peek') return null
  const rect = overlayCanvas.getBoundingClientRect()
  if (currentPanelSide === 'left') {
    return new DOMRect(rect.right - PANEL_PEEK_PX, rect.top, PANEL_PEEK_PX, rect.height)
  }
  return new DOMRect(rect.left, rect.top, PANEL_PEEK_PX, rect.height)
}

// Test whether a screen-space point lies inside a rectangle.
function isPointInRect(x: number, y: number, rect: DOMRect): boolean {
  return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom
}

// Check whether the pointer is inside the visible "peek" activation strip.
function isPointerOverPeekActivation(event: MouseEvent): boolean {
  const bounds = getOverlayPeekActivationBounds()
  return bounds !== null && isPointInRect(event.clientX, event.clientY, bounds)
}

// Detect whether the cursor left the foreground canvas through the edge that
// currently owns the overlay.
function didExitCanvasViaPanelEdge(event: MouseEvent): boolean {
  const rect = foregroundCanvas.getBoundingClientRect()
  return (
    (edgeHoverSide === 'left' && event.clientX <= rect.left) ||
    (edgeHoverSide === 'right' && event.clientX >= rect.right)
  )
}

// Compute the CSS transform that positions the overlay for a given side/state.
function getOverlayTransform(
  side: PanelSide,
  level: OverlayLevel,
): string {
  if (side === 'right') {
    if (level === 'hidden') return 'translateX(100%)'
    if (level === 'peek') return `translateX(calc(100% - ${PANEL_PEEK_PX}px))`
    return 'translateX(0)'
  }

  if (level === 'hidden') return 'translateX(-100%)'
  if (level === 'peek') return `translateX(calc(-100% + ${PANEL_PEEK_PX}px))`
  return 'translateX(0)'
}

// Apply the current overlay state to the DOM, including side swaps and
// transition timing.
function applyOverlayPosition(): void {
  const level = getOverlayLevel()
  const targetSide = getTargetSide()
  const sideChanged = targetSide !== currentPanelSide
  const transitionMs = menuDragActive ? 100 : 220

  // Update the side whenever the panel is not fully open, so hovering the left
  // edge correctly switches to the left panel. When fully open, keep the current
  // side so the panel never jumps mid-flight.
  if (level !== 'open' || hasPinnedOpenPanel()) currentPanelSide = targetSide

  const foregroundCssWidth = foregroundCanvas.clientWidth
  const overlayCssWidth = overlayCanvas.clientWidth
  const leftPx = currentPanelSide === 'left'
    ? 0
    : Math.max(foregroundCssWidth - overlayCssWidth, 0)
  overlayCanvas.style.left = `${leftPx}px`
  overlayCanvas.style.right = ''
  if (overlaySideSwapFrame !== 0) {
    cancelAnimationFrame(overlaySideSwapFrame)
    overlaySideSwapFrame = 0
  }

  if (sideChanged && level !== 'hidden') {
    overlayCanvas.style.transitionDuration = '0ms'
    overlayCanvas.style.transform = getOverlayTransform(currentPanelSide, 'hidden')
    // Wait one frame so the browser commits the off-screen position before
    // animating the overlay back in on its new side.
    overlaySideSwapFrame = requestAnimationFrame(() => {
      overlaySideSwapFrame = 0
      overlayCanvas.style.transitionDuration = `${transitionMs}ms`
      overlayCanvas.style.transform = getOverlayTransform(currentPanelSide, level)
    })
  } else {
    overlayCanvas.style.transitionDuration = `${transitionMs}ms`
    overlayCanvas.style.transform = getOverlayTransform(currentPanelSide, level)
  }

  // pointer-events: none when hidden so the off-screen canvas doesn't eat clicks.
  overlayCanvas.style.pointerEvents = level === 'hidden' ? 'none' : 'auto'
  overlayCanvas.style.visibility = 'visible'
}

// Mark the overlay canvas dirty and immediately re-evaluate its DOM position.
function invalidateSidePanel(): void {
  sidePanelDirty = true
  applyOverlayPosition()
}

// Update which screen edge is currently hover-armed for the side panel.
function setEdgeHoverSide(nextSide: PanelSide | null): void {
  if (edgeHoverSide === nextSide) return
  edgeHoverSide = nextSide
  invalidateSidePanel()
}

// Clear the currently highlighted build-menu cell.
function clearMenuSelection(): void {
  selectedMenuIndex = null
}

// Convert packed RGBA integers from the wire format into canvas-friendly color
// objects.
function packedRgbaToRenderColor(color: number): RenderColor {
  const r = (color >>> 24) & 0xff
  const g = (color >>> 16) & 0xff
  const b = (color >>> 8) & 0xff
  const a = (color & 0xff) / 255
  return {
    css: `rgba(${r}, ${g}, ${b}, ${a})`,
    alpha: a,
  }
}

// Report whether a render color is fully transparent.
function isTransparent(color: RenderColor): boolean {
  return color.alpha === 0
}

// Draw a filled circle if its radius and alpha make it visible.
function drawFilledCircleOnContext(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  radius: number,
  color: RenderColor,
): void {
  if (radius <= 0 || isTransparent(color)) return

  ctx.fillStyle = color.css
  ctx.beginPath()
  ctx.arc(x, y, radius, 0, Math.PI * 2)
  ctx.fill()
}

// Draw a stroked circle if its radius, width, and alpha make it visible.
function drawStrokedCircleOnContext(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  radius: number,
  color: RenderColor,
  lineWidth: number,
): void {
  if (radius <= 0 || lineWidth <= 0 || isTransparent(color)) return

  ctx.strokeStyle = color.css
  ctx.lineWidth = lineWidth
  ctx.beginPath()
  ctx.arc(x, y, radius, 0, Math.PI * 2)
  ctx.stroke()
}

// Pick a glyph font size that fits inside a circular defender/enemy body.
function getGlyphFontSize(glyph: string, radius: number): number {
  const roundedRadius = Math.max(1, Math.round(radius))
  const cacheKey = `${glyph}:${roundedRadius}`
  const cachedFontSize = glyphFontSizeCache.get(cacheKey)
  if (cachedFontSize !== undefined) return cachedFontSize

  const maxBox = roundedRadius * Math.sqrt(2)
  let fontSize = maxBox
  fgCtx.font = `${fontSize}px monospace`

  const metrics = fgCtx.measureText(glyph)
  const width = metrics.width || 1
  const height = (metrics.actualBoundingBoxAscent + metrics.actualBoundingBoxDescent) || fontSize
  const scale = Math.min(maxBox / width, maxBox / height, 1) * 0.9
  fontSize *= scale

  glyphFontSizeCache.set(cacheKey, fontSize)
  return fontSize
}

// Choose a border width proportional to sprite size while remaining legible.
function getEntityBorderWidth(radius: number): number {
  return Math.max(1, radius * 0.18)
}

// Draw a centered glyph into a circular sprite or menu icon.
function drawGlyphOnContext(
  ctx: CanvasRenderingContext2D,
  glyph: string,
  x: number,
  y: number,
  radius: number,
  color: RenderColor,
): void {
  if (!glyph || radius <= 0 || isTransparent(color)) return

  const fontSize = getGlyphFontSize(glyph, radius)

  ctx.save()
  ctx.fillStyle = color.css
  ctx.font = `${fontSize}px monospace`
  ctx.textAlign = 'center'
  ctx.textBaseline = 'middle'
  ctx.fillText(glyph, x, y)
  ctx.restore()
}

// Create a stable cache key for prerendered sprite variants.
function getSpriteCacheKey(app: RenderAppearance, radius: number): string {
  const roundedRadius = Math.max(1, Math.round(radius))
  return [
    app.glyph,
    roundedRadius,
    app.fg.css,
    app.bg.css,
  ].join('|')
}

// Fetch or build an offscreen sprite canvas for a particular appearance/size.
function getEntitySprite(app: RenderAppearance, radius: number): SpriteCacheEntry | null {
  const roundedRadius = Math.max(1, Math.round(radius))
  if (roundedRadius <= 0) return null

  const cacheKey = getSpriteCacheKey(app, roundedRadius)
  const cachedSprite = entitySpriteCache.get(cacheKey)
  if (cachedSprite) return cachedSprite

  // Cache misses only happen the first time we see a visual variant for this
  // appearance/radius bucket. After that, renderInterpolated() can reuse the
  // offscreen canvas with a single drawImage() blit.
  const extent = roundedRadius
  const pad = 2
  const offset = Math.ceil(extent + pad)
  const size = offset * 2
  const canvas = document.createElement('canvas')
  canvas.width = size
  canvas.height = size

  const spriteCtx = canvas.getContext('2d')
  if (!spriteCtx) return null

  if (app.bg.alpha !== 0) drawFilledCircleOnContext(spriteCtx, offset, offset, roundedRadius, app.bg)
  if (app.fg.alpha !== 0) {
    drawStrokedCircleOnContext(
      spriteCtx,
      offset,
      offset,
      Math.max(roundedRadius - getEntityBorderWidth(roundedRadius) * 0.5, 0),
      app.fg,
      getEntityBorderWidth(roundedRadius),
    )
  }
  if (app.fg.alpha !== 0) drawGlyphOnContext(spriteCtx, app.glyph, offset, offset, roundedRadius, app.fg)

  const sprite = { canvas, offset }
  entitySpriteCache.set(cacheKey, sprite)
  return sprite
}

// Blit a prerendered entity sprite onto the foreground canvas.
function drawEntitySprite(x: number, y: number, radius: number, app: RenderAppearance): void {
  const sprite = getEntitySprite(app, radius)
  if (!sprite) return
  fgCtx.drawImage(sprite.canvas, x - sprite.offset, y - sprite.offset)
}

// Draw the semi-transparent attack range for a selected defender.
function drawRangeCircle(
  x: number,
  y: number,
  fx: RenderVisualEffects,
): void {
  if (fx.rangeRadius <= 0 || isTransparent(fx.range)) return

  const radius = worldLengthToCanvas(fx.rangeRadius)
  fgCtx.save()
  drawFilledCircleOnContext(fgCtx, x, y, radius, fx.range)
  fgCtx.restore()
}

// Draw the selection ring that highlights the active defender.
function drawSelectionOutline(
  x: number,
  y: number,
  radius: number,
  fx: RenderVisualEffects,
): void {
  if (radius <= 0 || isTransparent(fx.selection)) return

  const lineWidth = Math.max(3, radius * 0.35)
  fgCtx.save()
  drawStrokedCircleOnContext(
    fgCtx,
    x,
    y,
    radius + lineWidth * 0.5,
    fx.selection,
    lineWidth,
  )
  fgCtx.restore()
}

// Determine whether a flashing effect should currently be visible.
function isFlashVisible(fx: RenderVisualEffects, now: number): boolean {
  const elapsed = Math.max(now - fx.flashStartedAt, 0)
  return Math.floor(elapsed / FLASH_FLICKER_INTERVAL_MS) % 2 === 0
}

// Draw a transient flash overlay and expire it locally once its timer ends.
function drawFlashOverlay(
  x: number,
  y: number,
  radius: number,
  fx: RenderVisualEffects,
  now: number,
): void {
  if (fx.flashExpiry == 0) return

  if (now >= fx.flashExpiry) {
    fx.flash = TRANSPARENT_RENDER_COLOR
    fx.flashExpiry = 0
    return
  }

  if (radius <= 0 || isTransparent(fx.flash) || !isFlashVisible(fx, now)) return

  fgCtx.save()
  drawFilledCircleOnContext(fgCtx, x, y, radius, fx.flash)
  fgCtx.restore()
}

// Draw one entity and all of its client-side visual effects.
function drawEntity(
  x: number,
  y: number,
  radius: number,
  app: RenderAppearance,
  fx: RenderVisualEffects,
  now: number,
): void {
  drawRangeCircle(x, y, fx)
  drawEntitySprite(x, y, radius, app)
  drawSelectionOutline(x, y, radius, fx)
  drawFlashOverlay(x, y, radius, fx, now)
}

// Convert wire-format appearance data into client render state.
function appearanceToRender(app: EntityAppearance): RenderAppearance {
  return {
    glyph: app.glyph === 0 ? '' : String.fromCodePoint(app.glyph),
    radius: app.radius,
    fg: packedRgbaToRenderColor(app.fg),
    bg: packedRgbaToRenderColor(app.bg),
    attackRadius: app.attackRadius,
  }
}

// Convert wire-format visual effects into client render state, preserving flash
// phase across repeated delta updates when appropriate.
function visualEffectsToRender(
  fx: EntityVisualEffects,
  now: number,
  prevFx?: RenderVisualEffects,
): RenderVisualEffects {
  const flash = packedRgbaToRenderColor(fx.flash)
  const flashExpiry = fx.flashExpiryMs <= 0 ? 0 : now + fx.flashExpiryMs
  const keepExistingFlashPhase =
    prevFx !== undefined &&
    prevFx.flashExpiry > now &&
    flashExpiry > 0

  return {
    selection: packedRgbaToRenderColor(fx.selection),
    rangeRadius: fx.rangeRadius,
    range: packedRgbaToRenderColor(fx.range),
    flash,
    flashExpiry,
    // Preserve the original flash phase while the server keeps extending the
    // effect, so flicker timing does not restart on every delta packet.
    flashStartedAt: flashExpiry > 0
      ? (keepExistingFlashPhase ? prevFx.flashStartedAt : now)
      : 0,
  }
}

// Merge a server upsert with previously known appearance/effect state so
// partial updates remain renderable.
function upsertToRenderEntity(
  upsert: EntityUpsert,
  now: number,
  prevApp?: RenderAppearance,
  prevFx?: RenderVisualEffects,
): RenderEntityUpsert {
  return {
    pos: upsert.pos,
    app: upsert.app ? appearanceToRender(upsert.app) : (prevApp ?? DEFAULT_RENDER_APPEARANCE),
    fx: upsert.vfx
      ? visualEffectsToRender(upsert.vfx, now, prevFx)
      : (prevFx ?? DEFAULT_RENDER_VISUAL_EFFECTS),
  }
}

// Draw the small FPS meter in the lower-right corner.
function drawFps(ctx: CanvasRenderingContext2D): void {
  ctx.save()
  const label = `${fps.toFixed(1)} FPS`
  ctx.font = '14px monospace'
  const pad = 6
  const w = ctx.measureText(label).width + pad * 2
  const h = 20
  const x = fpsCanvas.width - w - 4
  const y = fpsCanvas.height - 4 - h
  ctx.fillStyle = 'rgba(0,0,0,0.5)'
  ctx.fillRect(x, y, w, h)
  ctx.fillStyle = 'white'
  ctx.fillText(label, x + pad, y + 14)
  ctx.restore()
}

// Draw the top HUD bar that displays lives and resources.
function drawHud(ctx: CanvasRenderingContext2D): void {
  ctx.save()
  ctx.font = '20px monospace'
  ctx.textBaseline = 'top'

  const livesLabel = `${lives.toString().padStart(4, ' ')}❤️`;
  const resourcesLabel = `$${resources}`
  const pad = 4
  const barHeight = 24

  ctx.fillStyle = 'rgba(0,0,0,0.55)'
  ctx.fillRect(0, 0, hudCanvas.width, barHeight)

  ctx.fillStyle = 'white'
  ctx.fillText(`${livesLabel}   ${resourcesLabel}`, pad, pad)
  ctx.restore()
}

// Decide whether the pointer is close enough to the left or right edge to arm
// the build menu panel.
function panelSideFromCanvasX(canvasX: number): PanelSide | null {
  // Trigger zones scale with the viewport, but stay clamped so the edge-hover
  // affordance is still reachable on tiny windows and not overly eager on wide
  // monitors.
  const cssWidth = foregroundCanvas.clientWidth
  const triggerCssPx = cssWidth > 0
    ? Math.max(
      SIDE_PANEL_TRIGGER_MIN_PX,
      Math.min(SIDE_PANEL_TRIGGER_MAX_PX, cssWidth * SIDE_PANEL_TRIGGER_RATIO),
    )
    : SIDE_PANEL_TRIGGER_MIN_PX
  const triggerPx = cssWidth > 0
    ? triggerCssPx * (foregroundCanvas.width / cssWidth)
    : triggerCssPx
  if (canvasX <= triggerPx) return 'left'
  if (canvasX >= foregroundCanvas.width - triggerPx) return 'right'
  return null
}

// Update edge-hover state based on the current pointer sample.
function updateEdgeHover(sample: CanvasPointerSample): void {
  overlayStayOpen = false
  if (menuDragActive || ghostState || currentPhase !== 'build') {
    setEdgeHoverSide(null)
    return
  }
  setEdgeHoverSide(panelSideFromCanvasX(sample.canvasX))
}

// Hit-test defender bodies in world space using the current render snapshot.
function findDefenderAtWorld(worldX: number, worldY: number): RenderEntityUpsert | null {
  const hitPos = { x: worldX, y: worldY }
  for (const entity of currEntitiesById.values()) {
    if (entity.app.attackRadius <= 0) continue
    if (SimWorldDistanceSquared(entity.pos, hitPos) <= entity.app.radius * entity.app.radius) {
      return entity
    }
  }
  return null
}

// Compute squared distance without paying for a square root.
function SimWorldDistanceSquared(
  a: { x: number; y: number },
  b: { x: number; y: number },
): number {
  const dx = a.x - b.x
  const dy = a.y - b.y
  return dx * dx + dy * dy
}

// Format panel numbers without trailing decimals for integer values.
function formatPanelNumber(value: number): string {
  return Number.isInteger(value) ? String(value) : value.toFixed(1)
}

// Place side panels on the opposite half of the screen from the subject they
// describe.
function getOppositePanelSideForCanvasX(canvasX: number): PanelSide {
  return canvasX < foregroundCanvas.width / 2 ? 'right' : 'left'
}

// Wrap panel body text to the available width using the current canvas font.
function wrapPanelText(
  ctx: CanvasRenderingContext2D,
  text: string,
  maxWidth: number,
): string[] {
  if (text.length === 0) return ['']

  const words = text.split(/\s+/)
  const lines: string[] = []
  let currentLine = ''

  for (const word of words) {
    const candidate = currentLine.length === 0 ? word : `${currentLine} ${word}`
    if (ctx.measureText(candidate).width <= maxWidth || currentLine.length === 0) {
      currentLine = candidate
      continue
    }

    lines.push(currentLine)
    currentLine = word
  }

  if (currentLine.length > 0) lines.push(currentLine)
  return lines
}

// Build the overlay model for the currently selected defender, if any.
function getSelectedDefenderPanelModel(): SidePanelModel | null {
  if (!selectedDefenderPosition || !selectedDefenderSummary) return null

  // Put the panel opposite the selected unit so it reads like an inspector
  // without covering the thing the user just clicked.
  const side = getOppositePanelSideForCanvasX(
    worldToCanvas(selectedDefenderPosition.x, selectedDefenderPosition.y)[0],
  )

  return {
    side,
    title: 'Defender Selected',
    lines: [
      selectedDefenderSummary.displayName,
      selectedDefenderSummary.flavorText,
      '',
      `Glyph ${selectedDefenderSummary.appearance.glyph === 0 ? '?' : String.fromCodePoint(selectedDefenderSummary.appearance.glyph)}`,
      `Body radius ${formatPanelNumber(selectedDefenderSummary.appearance.radius)}`,
      `Attack radius ${formatPanelNumber(selectedDefenderSummary.appearance.attackRadius)}`,
      '',
      `Phase ${phaseEl?.textContent ?? '--'}`,
      `Resources $${resources}`,
      'Left click empty space to dismiss',
    ],
  }
}

// Build the overlay model for a dropped-but-not-yet-confirmed placement ghost.
function getPendingGhostPanelModel(): SidePanelModel | null {
  if (!ghostState?.pending) return null
  const [ghostCanvasX] = worldToCanvas(ghostState.worldX, ghostState.worldY)
  const side = getOppositePanelSideForCanvasX(ghostCanvasX)
  return {
    side,
    title: 'Place Defender',
    lines: [
      ghostState.displayName,
      '',
      'Right-click to place',
      'Left-click ghost to move',
      'Any other click cancels',
    ],
  }
}

// Choose the highest-priority pinned side panel to display, if any.
function getSidePanelModel(): SidePanelModel | null {
  return getPendingGhostPanelModel() ?? getSelectedDefenderPanelModel()
}

// Trace a rounded-rectangle path used by both panel and build-menu backdrops.
function drawRoundedPanelPath(
  ctx: CanvasRenderingContext2D,
  x: number,
  y: number,
  width: number,
  height: number,
  radius: number,
): void {
  const r = Math.min(radius, width / 2, height / 2)
  ctx.beginPath()
  ctx.moveTo(x + r, y)
  ctx.lineTo(x + width - r, y)
  ctx.quadraticCurveTo(x + width, y, x + width, y + r)
  ctx.lineTo(x + width, y + height - r)
  ctx.quadraticCurveTo(x + width, y + height, x + width - r, y + height)
  ctx.lineTo(x + r, y + height)
  ctx.quadraticCurveTo(x, y + height, x, y + height - r)
  ctx.lineTo(x, y + r)
  ctx.quadraticCurveTo(x, y, x + r, y)
  ctx.closePath()
}

// Draw a textual side panel such as defender inspection or pending placement.
function drawSidePanel(ctx: CanvasRenderingContext2D, panel: SidePanelModel): void {
  const x = 0
  const y = 0
  const width = overlayCanvas.width
  const height = overlayCanvas.height
  const panelScale = Math.min(
    foregroundCanvas.width / DEFAULT_CANVAS_W,
    foregroundCanvas.height / DEFAULT_CANVAS_H,
  )
  const cornerRadius = SIDE_PANEL_RADIUS * panelScale
  const textMargin = SIDE_PANEL_TEXT_MARGIN * panelScale
  const titleFontSize = 24 * panelScale
  const bodyFontSize = 16 * panelScale
  const lineHeight = SIDE_PANEL_LINE_HEIGHT * panelScale
  const titleTop = 20 * panelScale
  const dividerY = 56 * panelScale
  const bodyTop = 74 * panelScale
  const textX = x + textMargin
  const textWidth = width - textMargin * 2

  ctx.save()
  drawRoundedPanelPath(ctx, x, y, width, height, cornerRadius)
  const gradient = ctx.createLinearGradient(x, y, x + width, y + height)
  gradient.addColorStop(0, 'rgba(12, 18, 26, 0.92)')
  gradient.addColorStop(1, 'rgba(18, 27, 39, 0.84)')
  ctx.fillStyle = gradient
  ctx.fill()
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.16)'
  ctx.lineWidth = Math.max(1, 1.5 * panelScale)
  ctx.stroke()

  ctx.fillStyle = 'rgba(255, 255, 255, 0.96)'
  ctx.font = `bold ${titleFontSize}px monospace`
  ctx.textBaseline = 'top'
  ctx.fillText(panel.title, textX, y + titleTop, textWidth)

  ctx.strokeStyle = 'rgba(255, 255, 255, 0.12)'
  ctx.beginPath()
  ctx.moveTo(textX, y + dividerY)
  ctx.lineTo(textX + textWidth, y + dividerY)
  ctx.stroke()

  ctx.font = `${bodyFontSize}px monospace`
  let lineY = y + bodyTop
  for (const line of panel.lines) {
    if (line.length === 0) {
      lineY += lineHeight * 0.65
      continue
    }
    ctx.fillStyle = line.startsWith('Left click') || line.startsWith('Double click') ||
      line.startsWith('Right click') || line.startsWith('Use Start Wave')
      ? 'rgba(181, 201, 224, 0.92)'
      : 'rgba(255, 255, 255, 0.92)'
    const wrappedLines = wrapPanelText(ctx, line, textWidth)
    for (const wrappedLine of wrappedLines) {
      ctx.fillText(wrappedLine, textX, lineY, textWidth)
      lineY += lineHeight
    }
  }
  ctx.restore()
}

// Draw the build-menu overlay grid containing available defender types.
function drawBuildMenu(ctx: CanvasRenderingContext2D): void {
  const panelScale = Math.min(
    foregroundCanvas.width / DEFAULT_CANVAS_W,
    foregroundCanvas.height / DEFAULT_CANVAS_H,
  )
  const width = overlayCanvas.width
  const height = overlayCanvas.height
  const cornerRadius = SIDE_PANEL_RADIUS * panelScale
  const titleFontSize = 24 * panelScale
  const titleTop = 20 * panelScale
  const dividerY = 56 * panelScale
  const bodyTop = 70 * panelScale
  const textMargin = SIDE_PANEL_TEXT_MARGIN * panelScale
  const textWidth = width - textMargin * 2

  ctx.save()
  drawRoundedPanelPath(ctx, 0, 0, width, height, cornerRadius)
  const gradient = ctx.createLinearGradient(0, 0, width, height)
  gradient.addColorStop(0, 'rgba(12, 18, 26, 0.92)')
  gradient.addColorStop(1, 'rgba(18, 27, 39, 0.84)')
  ctx.fillStyle = gradient
  ctx.fill()
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.16)'
  ctx.lineWidth = Math.max(1, 1.5 * panelScale)
  ctx.stroke()

  ctx.fillStyle = 'rgba(255, 255, 255, 0.96)'
  ctx.font = `bold ${titleFontSize}px monospace`
  ctx.textBaseline = 'top'
  ctx.fillText('Select Defender', textMargin, titleTop, textWidth)

  ctx.strokeStyle = 'rgba(255, 255, 255, 0.12)'
  ctx.beginPath()
  ctx.moveTo(textMargin, dividerY)
  ctx.lineTo(textMargin + textWidth, dividerY)
  ctx.stroke()

  const cellSize = Math.floor((width - 2) / MENU_COLS)
  const startIndex = menuScrollOffset * MENU_COLS
  const visibleItems = defenderMenuItems.slice(
    startIndex,
    startIndex + MENU_VISIBLE_ROWS * MENU_COLS,
  )

  for (let i = 0; i < visibleItems.length; i++) {
    const item = visibleItems[i]
    const col = i % MENU_COLS
    const row = Math.floor(i / MENU_COLS)
    const cellX = 1 + col * cellSize
    const cellY = bodyTop + row * cellSize
    const isSelected = (startIndex + i) === selectedMenuIndex

    ctx.fillStyle = isSelected
      ? 'rgba(255, 242, 182, 0.18)'
      : 'rgba(255, 255, 255, 0.06)'
    ctx.fillRect(cellX, cellY, cellSize, cellSize)

    if (isSelected) {
      ctx.strokeStyle = 'rgba(255, 242, 63, 0.85)'
      ctx.lineWidth = 2 * panelScale
      ctx.strokeRect(cellX + 1, cellY + 1, cellSize - 2, cellSize - 2)
    }

    const app = appearanceToRender(item.appearance)
    const circleX = cellX + cellSize / 2
    const circleY = cellY + cellSize * 0.40
    const circleR = Math.min(
      worldLengthToCanvas(app.radius) * 2,
      cellSize * 0.30,
    )

    if (app.bg.alpha !== 0) drawFilledCircleOnContext(ctx, circleX, circleY, circleR, app.bg)
    if (app.fg.alpha !== 0) {
      drawStrokedCircleOnContext(
        ctx,
        circleX,
        circleY,
        Math.max(circleR - getEntityBorderWidth(circleR) * 0.5, 0),
        app.fg,
        getEntityBorderWidth(circleR),
      )
    }
    if (app.glyph && app.fg.alpha !== 0) {
      drawGlyphOnContext(ctx, app.glyph, circleX, circleY, circleR, app.fg)
    }

    const nameFontSize = Math.max(9, 11 * panelScale)
    ctx.font = `${nameFontSize}px monospace`
    ctx.textAlign = 'center'
    ctx.textBaseline = 'alphabetic'
    ctx.fillStyle = 'rgba(255, 255, 255, 0.85)'
    ctx.fillText(item.displayName, circleX, cellY + cellSize * 0.76, cellSize - 4)
    ctx.fillStyle = 'rgba(181, 201, 224, 0.85)'
    ctx.fillText(`$${item.resourceCost}`, circleX, cellY + cellSize * 0.92, cellSize - 4)
  }

  ctx.restore()
}

// Draw the draggable or pending placement ghost on top of the world layer.
function drawGhost(): void {
  if (!ghostState) return
  const [x, y] = worldToCanvas(ghostState.worldX, ghostState.worldY)
  const radius = worldLengthToCanvas(ghostState.appearance.radius)
  const attackPx = worldLengthToCanvas(ghostState.attackRadius)
  const isPlaceable = ghostState.placeable

  fgCtx.save()
  fgCtx.globalAlpha = ghostState.pending ? 0.85 : 0.55

  if (ghostState.appearance.bg.alpha !== 0) {
    drawFilledCircleOnContext(fgCtx, x, y, radius, ghostState.appearance.bg)
  }
  if (ghostState.appearance.fg.alpha !== 0) {
    drawStrokedCircleOnContext(
      fgCtx,
      x,
      y,
      Math.max(radius - getEntityBorderWidth(radius) * 0.5, 0),
      ghostState.appearance.fg,
      getEntityBorderWidth(radius),
    )
    if (ghostState.appearance.glyph) {
      drawGlyphOnContext(
        fgCtx,
        ghostState.appearance.glyph,
        x,
        y,
        radius,
        ghostState.appearance.fg,
      )
    }
  }

  if (attackPx > 0) {
    fgCtx.globalAlpha = ghostState.pending ? 0.55 : 0.35
    const rangeColor: RenderColor = {
      css: isPlaceable
        ? 'rgba(255, 242, 63, 0.28)'
        : 'rgba(255, 76, 76, 0.45)',
      alpha: 0.45,
    }
    drawFilledCircleOnContext(fgCtx, x, y, attackPx, rangeColor)
    drawStrokedCircleOnContext(
      fgCtx,
      x,
      y,
      attackPx,
      isPlaceable
        ? { css: 'rgba(255, 242, 63, 0.85)', alpha: 0.85 }
        : { css: 'rgba(255, 76, 76, 0.85)', alpha: 0.85 },
      Math.max(2, radius * 0.08),
    )
  }

  if (ghostState.pending) {
    fgCtx.globalAlpha = 0.9
    const lineWidth = Math.max(3, radius * 0.35)
    drawStrokedCircleOnContext(
      fgCtx,
      x,
      y,
      radius + lineWidth * 0.5,
      isPlaceable
        ? { css: 'rgba(255, 242, 63, 0.9)', alpha: 0.9 }
        : { css: 'rgba(255, 76, 76, 0.9)', alpha: 0.9 },
      lineWidth,
    )
  }

  fgCtx.restore()
}

// Redraw the overlay canvas when its contents have changed.
function updateSidePanelOverlay(): void {
  if (!sidePanelDirty) return

  overlayCtx.clearRect(0, 0, overlayCanvas.width, overlayCanvas.height)

  const panel = getSidePanelModel()
  const showBuildMenu = shouldShowBuildMenu()

  // The overlay only ever shows one mode at a time: pinned side panel wins,
  // otherwise the build menu can occupy the same canvas.
  if (panel) {
    drawSidePanel(overlayCtx, panel)
  } else if (showBuildMenu) {
    drawBuildMenu(overlayCtx)
  }

  sidePanelDirty = false
}

// Report whether the build-menu overlay should be visible right now.
function shouldShowBuildMenu(): boolean {
  return (
    getSidePanelModel() === null &&
    defenderMenuItems.length > 0 &&
    currentPhase === 'build' &&
    !menuDragActive
  )
}

// Main animation loop: update time-derived overlays, render the world, and
// composite the cached layers.
function frame(now: number): void {
  if (lastFrameTime !== 0) {
    const dt = now - lastFrameTime
    fps = fps * 0.9 + (1000 / dt) * 0.1
  }
  lastFrameTime = now

  renderInterpolated(now)
  updateHudOverlay()
  updateFpsOverlay(now)
  updateSidePanelOverlay()
  fgCtx.drawImage(hudCanvas, 0, 0)
  fgCtx.drawImage(fpsCanvas, 0, 0)
  requestAnimationFrame(frame)
}

// --- Logging ---

// Append a line to the on-page debug log.
function log(line: string): void {
  logEl.textContent += line + '\n'
  logEl.scrollTop = logEl.scrollHeight
}

// Set text content on an element when it exists.
function setTextIfElement(el: HTMLElement | null, value: string): void {
  if (el) el.textContent = value
}

// Mark the HUD overlay for regeneration on the next frame.
function invalidateHud(): void {
  hudDirty = true
}

// Translate DOM mouse button codes into the wire-format button names.
function buttonNameFromNumber(button: number): 'left' | 'middle' | 'right' | 'other' {
  switch (button) {
    case 0:
      return 'left'
    case 1:
      return 'middle'
    case 2:
      return 'right'
    default:
      return 'other'
  }
}

// Send a client message if the websocket is currently connected.
function sendClientMsg(msg: ClientMsg): void {
  if (ws.readyState !== WebSocket.OPEN) return
  ws.send(JSON.stringify(msg))
}

// Serialize a canvas interaction into the websocket protocol and return its
// client sequence number.
function sendCanvasMsg(
  eventName: 'click' | 'dblclick' | 'contextmenu' | 'dragstart' | 'dragmove' | 'dragend',
  sample: CanvasPointerSample,
  command?: string,
  parameters?: string[],
): number {
  const seq = nextClientSeq++
  const msg: UiCanvasMsg = {
    type: 'ui_canvas',
    seq,
    event: eventName,
    button: buttonNameFromNumber(sample.button),
    buttons: sample.buttons,
    x: sample.worldX,
    y: sample.worldY,
    canvasX: sample.canvasX,
    canvasY: sample.canvasY,
    shift: sample.shift,
    ctrl: sample.ctrl,
    alt: sample.alt,
    meta: sample.meta,
  }
  if (command) msg.command = command
  if (parameters?.length) msg.parameters = parameters
  sendClientMsg(msg)
  return seq
}

// Send the most recent coalesced drag-move sample, if one is pending.
function flushPendingDragMove(): void {
  dragMoveFrameRequested = false
  if (!pendingDragMove) return
  sendCanvasMsg('dragmove', pendingDragMove)
  pendingDragMove = null
}

// Send a placement-preview event for the current ghost entity.
function sendPlacementPreview(
  eventName: 'dragstart' | 'dragmove' | 'dragend',
  sample: CanvasPointerSample,
  entityName: string,
): void {
  if (!ghostState) return
  sendCanvasMsg(eventName, sample, 'placing', [entityName])
}

// Coalesce drag-move websocket traffic to at most once per animation frame.
function scheduleDragMoveFlush(): void {
  if (dragMoveFrameRequested) return
  dragMoveFrameRequested = true
  // Push at most one drag update per animation frame even if the pointer is
  // moving faster than the display refresh rate.
  requestAnimationFrame(() => {
    flushPendingDragMove()
  })
}

// Convert submitted form data into the string-only field map used by UI actions.
function formDataToFields(formData: FormData): Record<string, string> | undefined {
  const fields: Record<string, string> = {}
  for (const [key, value] of formData.entries()) {
    if (typeof value === 'string') fields[key] = value
  }
  return Object.keys(fields).length === 0 ? undefined : fields
}

// Send a generic UI action such as pressing a control button.
function sendUiAction(action: string, fields?: Record<string, string>): void {
  const msg: ClientMsg = {
    type: 'ui_action',
    seq: nextClientSeq++,
    action,
  }
  if (fields && Object.keys(fields).length > 0) msg.fields = fields
  sendClientMsg(msg)
}

// Narrow an unknown value to a simple `{x, y}` point.
function isPoint(value: unknown): value is { x: number; y: number } {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as Record<string, unknown>).x === 'number' &&
    typeof (value as Record<string, unknown>).y === 'number'
  )
}

// Narrow an unknown value to the entity-position wire type.
function isEntityPosition(value: unknown): value is EntityPosition {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as Record<string, unknown>).id === 'number' &&
    typeof (value as Record<string, unknown>).x === 'number' &&
    typeof (value as Record<string, unknown>).y === 'number'
  )
}

// Narrow an unknown value to the entity-appearance wire type.
function isEntityAppearance(value: unknown): value is EntityAppearance {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as Record<string, unknown>).glyph === 'number' &&
    typeof (value as Record<string, unknown>).radius === 'number' &&
    typeof (value as Record<string, unknown>).fg === 'number' &&
    typeof (value as Record<string, unknown>).bg === 'number'
  )
}

// Narrow an unknown value to the entity-visual-effects wire type.
function isEntityVisualEffects(value: unknown): value is EntityVisualEffects {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as Record<string, unknown>).selection === 'number' &&
    typeof (value as Record<string, unknown>).rangeRadius === 'number' &&
    typeof (value as Record<string, unknown>).range === 'number' &&
    typeof (value as Record<string, unknown>).flash === 'number' &&
    typeof (value as Record<string, unknown>).flashExpiryMs === 'number'
  )
}

// Narrow an unknown value to an entity upsert payload.
function isEntityUpsert(value: unknown): value is EntityUpsert {
  const maybeApp = (value as Record<string, unknown>).app
  const maybeVfx = (value as Record<string, unknown>).vfx
  return (
    typeof value === 'object' &&
    value !== null &&
    isEntityPosition((value as Record<string, unknown>).pos) &&
    (maybeApp === undefined || isEntityAppearance(maybeApp)) &&
    (maybeVfx === undefined || isEntityVisualEffects(maybeVfx))
  )
}

// Narrow an unknown value to a defender menu entry.
function isDefenderMenuItem(value: unknown): value is DefenderMenuItem {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as Record<string, unknown>).entityName === 'string' &&
    typeof (value as Record<string, unknown>).displayName === 'string' &&
    typeof (value as Record<string, unknown>).flavorText === 'string' &&
    typeof (value as Record<string, unknown>).resourceCost === 'number' &&
    isEntityAppearance((value as Record<string, unknown>).appearance)
  )
}

// Narrow an unknown value to the UI-state payload carried inside world deltas.
function isUiState(value: unknown): value is UiState {
  const v = value as Record<string, unknown>
  return (
    typeof value === 'object' &&
    value !== null &&
    (v.selectedDefender === undefined || isPoint(v.selectedDefender)) &&
    (v.placementAllowed === undefined || typeof v.placementAllowed === 'boolean') &&
    (v.spawnAllowed === undefined || typeof v.spawnAllowed === 'boolean') &&
    (v.defenderSummary === undefined || isDefenderMenuItem(v.defenderSummary))
  )
}

// Read the phase string from a delta, tolerating older payload shapes.
function getDeltaPhase(delta: WorldDelta): string {
  const maybePhase = (delta as WorldDelta & { phase?: unknown }).phase
  if (typeof maybePhase === 'string') return maybePhase
  return delta.phase
}

// Apply server-provided UI state to client-side selection and placement state.
function applyUiState(uiState: UiState): void {
  if (uiState.placementAllowed !== undefined && ghostState) {
    ghostState.placeable = uiState.placementAllowed
  }

  if (uiState.spawnAllowed !== undefined && ghostState?.spawnPending) {
    // Spawn requests are optimistic on the client: we keep the ghost in a
    // temporary "awaiting verdict" state until the server confirms whether the
    // placement was legal.
    ghostState.spawnPending = false
    if (uiState.spawnAllowed) {
      ghostState = null
      clearMenuSelection()
      mouseOverOverlay = false
      overlayStayOpen = false
    }
  }

  selectedDefenderPosition = uiState.selectedDefender ?? null
  if (!selectedDefenderPosition) {
    selectedDefenderSummary = null
  } else if (uiState.defenderSummary) {
    selectedDefenderSummary = uiState.defenderSummary
  }
}

// Advance snapshot timing bookkeeping after consuming a world update.
function finishSnapshotUpdate(): void {
  prevSnapshotTime = currSnapshotTime
  currSnapshotTime = performance.now()

  if (prevSnapshotTime !== 0) {
    snapshotIntervalMs = currSnapshotTime - prevSnapshotTime
  }
}

// Reset all client-side world, overlay, and interpolation state after a fresh
// snapshot (as opposed to delta) replaces the current session state.
function resetClientWorldState(): void {
  currentPathPoints = []
  currentPathWidth = 40
  currEntitiesById.clear()
  prevRenderStateById.clear()
  glyphFontSizeCache.clear()
  entitySpriteCache.clear()
  prevSnapshotTime = 0
  currSnapshotTime = 0
  snapshotIntervalMs = 50
  lives = 0
  resources = 0
  lastFpsOverlayUpdateTime = 0
  hudDirty = true
  sidePanelDirty = true
  edgeHoverSide = null
  defenderMenuItems = []
  selectedMenuIndex = null
  menuScrollOffset = 0
  ghostState = null
  menuDragActive = false
  currentPhase = 'build'
  mouseOverOverlay = false
  overlayStayOpen = false
  currentPanelSide = 'right'
  selectedDefenderSummary = null
  selectedDefenderPosition = null
  if (overlaySideSwapFrame !== 0) {
    cancelAnimationFrame(overlaySideSwapFrame)
    overlaySideSwapFrame = 0
  }

  bgCtx.clearRect(0, 0, backgroundCanvas.width, backgroundCanvas.height)
  fgCtx.clearRect(0, 0, foregroundCanvas.width, foregroundCanvas.height)
  overlayCtx.clearRect(0, 0, overlayCanvas.width, overlayCanvas.height)
  hudCtx.clearRect(0, 0, hudCanvas.width, hudCanvas.height)
  fpsCtx.clearRect(0, 0, fpsCanvas.width, fpsCanvas.height)
  applyOverlayPosition()
}

// Apply an incremental world update from the server and invalidate derived
// overlays that depend on the new state.
function applyWorldDelta(delta: WorldDelta): void {
  const now = performance.now()
  prevRenderStateById.clear()

  // Delta updates rewrite the current authoritative render state in-place while
  // preserving the previous render copy for interpolation during the next
  // animation frames.
  for (const entity of delta.upserts) {
    const prevEntity = currEntitiesById.get(entity.pos.id)
    const nextEntity =
      upsertToRenderEntity(entity, now, prevEntity?.app, prevEntity?.fx)
    if (prevEntity) prevRenderStateById.set(entity.pos.id, prevEntity)
    currEntitiesById.set(entity.pos.id, nextEntity)
  }
  for (const entityId of delta.erased) {
    currEntitiesById.delete(entityId)
    prevRenderStateById.delete(entityId)
  }

  finishSnapshotUpdate()
  tickEl.textContent = String(delta.tick)
  lives = delta.lives
  resources = delta.resources
  applyUiState(delta.uiState)
  invalidateHud()
  invalidateSidePanel()
  setTextIfElement(livesEl, String(delta.lives))
  setTextIfElement(resourcesEl, String(delta.resources))
  currentPhase = getDeltaPhase(delta)
  setTextIfElement(phaseEl, currentPhase)
}

// --- Message validation ---

// Narrow an unknown payload to the `world_delta` message shape.
function isWorldDelta(value: unknown): value is WorldDelta {
  if (!value || typeof value !== 'object') return false
  const v = value as Record<string, unknown>

  return (
    typeof v.tick === 'number' &&
    Array.isArray(v.upserts) &&
    Array.isArray(v.erased) &&
    typeof v.lives === 'number' &&
    typeof v.resources === 'number' &&
    typeof v.phase === 'string' &&
    isUiState(v.uiState) &&
    v.upserts.every(isEntityUpsert) &&
    // Erase entries are just numeric entity ids.
    v.erased.every((id) => typeof id === 'number')
  )
}

// Narrow an unknown websocket payload to one of the supported server messages.
function isServerMsg(value: unknown): value is ServerMsg {
  if (!value || typeof value !== 'object') return false
  const v = value as Record<string, unknown>
  if (typeof v.type !== 'string') return false

  // Validate websocket payloads at the edge so the rest of the file can treat
  // incoming messages as strongly typed data instead of defensive blobs.
  switch (v.type) {
    case 'hello_ack':
      return typeof v.message === 'string'
    case 'world_delta':
      return isWorldDelta(v)
    case 'world_snapshot': {
      const md = v.mapDesign as Record<string, unknown> | undefined
      return (
        !!md &&
        typeof md === 'object' &&
        typeof md.backgroundSprite === 'string' &&
        typeof md.foregroundSprite === 'string' &&
        typeof md.pathWidth === 'number' &&
        Array.isArray(md.paths) &&
        md.paths.every(isPoint) &&
        Array.isArray(v.defenderMenu) &&
        v.defenderMenu.every(isDefenderMenuItem) &&
        isWorldDelta(v.delta)
      )
    }
    default:
      return false
  }
}

// --- WebSocket ---

const protocol = location.protocol === 'https:' ? 'wss' : 'ws'
const ws = new WebSocket(`${protocol}://${location.host}/ws`)

// Announce a fresh websocket connection and send the browser hello packet.
ws.onopen = () => {
  statusEl.textContent = 'connected'
  log('Connected')
  const hello: ClientMsg = { type: 'hello', client: 'browser' }
  sendClientMsg(hello)
}

// Parse, validate, and dispatch every inbound websocket message.
ws.onmessage = (event: MessageEvent<string>) => {
  let parsed: unknown
  try {
    parsed = JSON.parse(event.data)
  } catch {
    log('(unparseable message)')
    return
  }

  if (!isServerMsg(parsed)) {
    log('(invalid server message shape)')
    return
  }

  switch (parsed.type) {
    case 'hello_ack':
      log(`Server says: ${parsed.message}`)
      break
    case 'world_delta':
      applyWorldDelta(parsed)
      break
    case 'world_snapshot':
      resetClientWorldState()
      defenderMenuItems = parsed.defenderMenu
      drawBackground(parsed.mapDesign.paths, parsed.mapDesign.pathWidth)
      applyWorldDelta(parsed.delta)
      break
  }
}

// Reflect websocket shutdown in the visible connection status.
ws.onclose = () => {
  statusEl.textContent = 'disconnected'
  log('Disconnected')
}

// Surface websocket transport errors in the small on-page status UI.
ws.onerror = () => {
  statusEl.textContent = 'error'
  log('WebSocket error')
}

// Toggle fullscreen mode for the viewport shell.
fullscreenToggle.addEventListener('click', async () => {
  try {
    if (document.fullscreenElement === viewportShell) {
      await document.exitFullscreen()
      return
    }
    await viewportShell.requestFullscreen()
  } catch {
    log('Could not change fullscreen state')
  }
})

// Recompute labels and canvas sizing after fullscreen transitions.
document.addEventListener('fullscreenchange', () => {
  updateFullscreenButtonLabel()
  resizeViewport()
})

// Resize canvases when the browser window changes size.
window.addEventListener('resize', () => {
  resizeViewport()
})

if (typeof ResizeObserver !== 'undefined') {
  // Resize canvases when the host element changes size for reasons other than
  // the global window, such as layout changes.
  const resizeObserver = new ResizeObserver(() => {
    resizeViewport()
  })
  resizeObserver.observe(viewportHost)
}

// Start clicks, ghost pickup/cancel, and drag tracking on the foreground world.
foregroundCanvas.addEventListener('mousedown', (event: MouseEvent) => {
  const sample = eventToCanvasSample(event)
  updateEdgeHover(sample)

  // A transient build panel opened from the background should collapse when
  // the user clicks empty world space again.
  if (
    event.button === 0 &&
    !ghostState &&
    !menuDragActive &&
    selectedDefenderPosition === null &&
    findDefenderAtWorld(sample.worldX, sample.worldY) === null
  ) {
    mouseOverOverlay = false
    overlayStayOpen = false
    setEdgeHoverSide(null)
  }

  // While a ghost is pending, left-click on the ghost picks it back up and
  // any other non-right click cancels placement.
  if (event.button === 0 && ghostState?.pending) {
    const [gx, gy] = worldToCanvas(ghostState.worldX, ghostState.worldY)
    const hitRadius = worldLengthToCanvas(ghostState.appearance.radius) * 2
    const dx = sample.canvasX - gx
    const dy = sample.canvasY - gy
    if (dx * dx + dy * dy <= hitRadius * hitRadius) {
      ghostState.pending = false
      menuDragActive = true
      mouseOverOverlay = false
      overlayStayOpen = false
      sendPlacementPreview('dragstart', sample, ghostState.entityName)
    } else {
      ghostState = null
      clearMenuSelection()
      overlayStayOpen = false
    }
    invalidateSidePanel()
    return
  }

  canvasPointerState = {
    button: sample.button,
    buttons: sample.buttons,
    dragging: false,
  }

  if (sample.button !== 2) sendCanvasMsg('click', sample)
})

// Convert foreground pointer motion into either edge-hover updates or drag
// preview traffic.
foregroundCanvas.addEventListener('mousemove', (event: MouseEvent) => {
  const sample = eventToCanvasSample(event)
  updateEdgeHover(sample)
  if (!canvasPointerState) return
  canvasPointerState.buttons = sample.buttons

  if (canvasPointerState.button !== 0) return

  const dragSample = withPointerButton(sample, canvasPointerState.button)

  if (!canvasPointerState.dragging) {
    canvasPointerState.dragging = true
    sendCanvasMsg('dragstart', dragSample)
    return
  }

  pendingDragMove = dragSample
  scheduleDragMoveFlush()
})

// Open the build menu directly from empty build space on double-click, then
// still forward the gesture to the server.
foregroundCanvas.addEventListener('dblclick', (event: MouseEvent) => {
  const sample = eventToCanvasSample(event)
  if (
    event.button === 0 &&
    !ghostState &&
    !menuDragActive &&
    currentPhase === 'build' &&
    defenderMenuItems.length > 0 &&
    findDefenderAtWorld(sample.worldX, sample.worldY) === null
  ) {
    // Double-clicking empty build space is a shortcut to fully open the menu
    // on the current side without requiring an edge-hover first.
    edgeHoverSide = currentPanelSide
    mouseOverOverlay = true
    overlayStayOpen = false
    invalidateSidePanel()
  }
  sendCanvasMsg('dblclick', sample)
})

// Keep the overlay open when the pointer leaves through the armed panel edge.
foregroundCanvas.addEventListener('mouseleave', (event: MouseEvent) => {
  // Fast motion can skip the overlay's peek strip entirely. If the pointer
  // leaves through the active panel edge, open the panel as though the user
  // had crossed onto the peeking portion.
  if (didExitCanvasViaPanelEdge(event)) {
    mouseOverOverlay = true
    overlayStayOpen = true
    applyOverlayPosition()
  }
})

// Mark the overlay as hovered once the pointer enters its visible area.
overlayCanvas.addEventListener('mouseenter', () => {
  if (getOverlayLevel() !== 'peek') mouseOverOverlay = true
  overlayStayOpen = false
  applyOverlayPosition()
})

// Promote a peeking overlay to fully open only while the pointer is over the
// visible activation strip.
overlayCanvas.addEventListener('mousemove', (event: MouseEvent) => {
  const shouldOpen = getOverlayLevel() !== 'peek' || isPointerOverPeekActivation(event)
  if (mouseOverOverlay === shouldOpen) return
  mouseOverOverlay = shouldOpen
  if (shouldOpen) overlayStayOpen = false
  applyOverlayPosition()
})

// Decide whether leaving the overlay should keep it latched open or hand hover
// control back to the foreground canvas.
overlayCanvas.addEventListener('mouseleave', (event: MouseEvent) => {
  mouseOverOverlay = false
  const toFg = event.relatedTarget === foregroundCanvas
  if (toFg) {
    overlayStayOpen = false
    if (!ghostState?.pending) setEdgeHoverSide(currentPanelSide)
  } else {
    overlayStayOpen = true
    if (edgeHoverSide === null) edgeHoverSide = currentPanelSide
  }
  invalidateSidePanel()
})

// Finish menu drags and normal canvas drags when the mouse button is released.
window.addEventListener('mouseup', (event: MouseEvent) => {
  // Handle menu drag commit/cancel.
  if (menuDragActive) {
    menuDragActive = false
    if (ghostState) {
      const fgRect = foregroundCanvas.getBoundingClientRect()
      const ovRect = overlayCanvas.getBoundingClientRect()
      const overOverlay =
        event.clientX >= ovRect.left && event.clientX <= ovRect.right &&
        event.clientY >= ovRect.top && event.clientY <= ovRect.bottom
      const overFg =
        event.clientX >= fgRect.left && event.clientX <= fgRect.right &&
        event.clientY >= fgRect.top && event.clientY <= fgRect.bottom
      if (overFg && !overOverlay) {
        // Dropping onto the playfield creates a pending ghost first; a later
        // right-click turns that preview into an actual spawn command.
        sendPlacementPreview('dragend', ghostStateToSample(ghostState, event), ghostState.entityName)
        ghostState.pending = true
        mouseOverOverlay = false
        overlayStayOpen = false
      } else {
        ghostState = null
        clearMenuSelection()
        overlayStayOpen = false
      }
    }
    invalidateSidePanel()
    return
  }

  if (!canvasPointerState) return
  const sample = withPointerButton(
    eventToCanvasSample(event),
    canvasPointerState.button,
  )
  const wasDragging = canvasPointerState.dragging
  canvasPointerState = null
  flushPendingDragMove()
  if (wasDragging) sendCanvasMsg('dragend', sample)
})

// Use right-click on the foreground canvas for placement confirmation or to
// forward a normal context-style click to the server.
foregroundCanvas.addEventListener('contextmenu', (event: MouseEvent) => {
  event.preventDefault()
  const sample = eventToCanvasSample(event)
  if (ghostState?.pending) {
    if (!ghostState.placeable || ghostState.spawnPending) return
    // Right-click while ghost is pending: place the defender.
    const entityName = ghostState.entityName
    const ghostSample = ghostStateToSample(ghostState, event, 2, 2)
    ghostState.spawnPending = true
    sendCanvasMsg('click', ghostSample, 'spawn', [entityName])
    return
  }
  updateEdgeHover(sample)
  sendCanvasMsg('click', withPointerButton(sample, 2, 2))
})


// Convert DOM controls carrying `data-action` into protocol-level UI actions.
document.addEventListener('click', (event: MouseEvent) => {
  const target = event.target
  if (!(target instanceof Element)) return

  const actionEl = target.closest<HTMLElement>('[data-action]')
  if (!actionEl || actionEl instanceof HTMLFormElement) return

  const action = actionEl.dataset.action
  if (!action) return

  if (actionEl.contains(foregroundCanvas)) return

  const form = actionEl.closest('form')
  const fields = form ? formDataToFields(new FormData(form)) : undefined
  if (actionEl instanceof HTMLButtonElement || actionEl instanceof HTMLInputElement) {
    event.preventDefault()
  }
  sendUiAction(action, fields)
})

// Convert form submissions carrying `data-action` into protocol-level UI
// actions instead of letting the browser navigate away.
document.addEventListener('submit', (event: SubmitEvent) => {
  const target = event.target
  if (!(target instanceof HTMLFormElement)) return

  const action = target.dataset.action
  if (!action) return

  event.preventDefault()
  sendUiAction(action, formDataToFields(new FormData(target)))
})

// Cancel ghost on right-click outside the foreground canvas.
document.addEventListener('contextmenu', (event: MouseEvent) => {
  if (event.defaultPrevented || !ghostState) return

  const fgRect = foregroundCanvas.getBoundingClientRect()
  const insideFg = isPointInRect(event.clientX, event.clientY, fgRect)
  if (ghostState.pending && insideFg) {
    const sample = eventToCanvasSample(event)
    const entityName = ghostState.entityName
    event.preventDefault()
    ghostState.spawnPending = true
    sendCanvasMsg('click', sample, 'spawn', [entityName])
    return
  }

  if (event.target !== foregroundCanvas) {
    event.preventDefault()
    ghostState = null
    clearMenuSelection()
    menuDragActive = false
    mouseOverOverlay = false
    overlayStayOpen = false
    invalidateSidePanel()
  }
})

// Any non-right-click outside the canvas cancels pending placement.
document.addEventListener('mousedown', (event: MouseEvent) => {
  if (event.button === 2 || !ghostState?.pending) return
  if (event.target === foregroundCanvas) return
  ghostState = null
  clearMenuSelection()
  menuDragActive = false
  mouseOverOverlay = false
  overlayStayOpen = false
  invalidateSidePanel()
})

// Track ghost world position globally during a menu drag.
window.addEventListener('mousemove', (event: MouseEvent) => {
  if (!menuDragActive || !ghostState) return
  const rect = foregroundCanvas.getBoundingClientRect()
  const canvasX = (event.clientX - rect.left) * (foregroundCanvas.width / rect.width)
  const canvasY = (event.clientY - rect.top) * (foregroundCanvas.height / rect.height)
  const [worldX, worldY] = canvasToWorld(canvasX, canvasY)
  ghostState.worldX = worldX
  ghostState.worldY = worldY
  sendPlacementPreview('dragmove', eventToCanvasSample(event), ghostState.entityName)
})

// Overlay mousedown: select a build menu cell and arm a drag (ghost appears on
// first mousemove so it never flashes at the canvas origin).
overlayCanvas.addEventListener('mousedown', (event: MouseEvent) => {
  if (event.button !== 0) return
  if (!shouldShowBuildMenu()) return
  event.preventDefault()

  const panelScale = Math.min(
    foregroundCanvas.width / DEFAULT_CANVAS_W,
    foregroundCanvas.height / DEFAULT_CANVAS_H,
  )
  const cellSize = Math.floor((overlayCanvas.width - 2) / MENU_COLS)
  const bodyTop = 70 * panelScale
  const rect = overlayCanvas.getBoundingClientRect()
  const localX = (event.clientX - rect.left) * (overlayCanvas.width / rect.width)
  const localY = (event.clientY - rect.top) * (overlayCanvas.height / rect.height)

  if (localY < bodyTop) {
    // Click above grid area (title bar) — deselect.
    selectedMenuIndex = null
    invalidateSidePanel()
    return
  }
  const col = Math.floor(localX / cellSize)
  const row = Math.floor((localY - bodyTop) / cellSize)
  if (col < 0 || col >= MENU_COLS) return

  const index = menuScrollOffset * MENU_COLS + row * MENU_COLS + col
  if (index >= defenderMenuItems.length) {
    // Click below last populated cell — deselect.
    selectedMenuIndex = null
    invalidateSidePanel()
    return
  }

  selectedMenuIndex = index
  menuDragActive = true
  mouseOverOverlay = false
  overlayStayOpen = false

  // Create the ghost immediately at the current mouse position so it appears
  // as soon as the mouse enters the foreground canvas, with no lag.
  const item = defenderMenuItems[index]
  const fgRect = foregroundCanvas.getBoundingClientRect()
  const fgCanvasX = (event.clientX - fgRect.left) * (foregroundCanvas.width / fgRect.width)
  const fgCanvasY = (event.clientY - fgRect.top) * (foregroundCanvas.height / fgRect.height)
  const [worldX, worldY] = canvasToWorld(fgCanvasX, fgCanvasY)
  ghostState = {
    worldX,
    worldY,
    entityName: item.entityName,
    displayName: item.displayName,
    appearance: appearanceToRender(item.appearance),
    attackRadius: item.appearance.attackRadius,
    pending: false,
    placeable: true,
    spawnPending: false,
  }
  const sample = eventToCanvasSample(event)
  sendPlacementPreview('dragstart', sample, item.entityName)
  clearMenuSelection()
  invalidateSidePanel()
})

// Scroll the build menu with the mouse wheel.
overlayCanvas.addEventListener('wheel', (event: WheelEvent) => {
  event.preventDefault()
  if (defenderMenuItems.length === 0) return
  const totalRows = Math.ceil(defenderMenuItems.length / MENU_COLS)
  const maxScroll = Math.max(0, totalRows - MENU_VISIBLE_ROWS)
  menuScrollOffset = Math.max(0, Math.min(maxScroll,
    menuScrollOffset + (event.deltaY > 0 ? 1 : -1)))
  invalidateSidePanel()
}, { passive: false })

// Bring the UI into sync with the current DOM/layout state before the first
// animation frame.
updateFullscreenButtonLabel()
resizeViewport()
requestAnimationFrame(frame)
