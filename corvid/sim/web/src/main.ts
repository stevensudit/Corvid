import type {
  ClientMsg,
  DefenderMenuItem,
  EntityAppearance,
  EntityPosition,
  EntityUpsert,
  EntityVisualEffects,
  ServerMsg,
  UiCanvasMsg,
  WorldDelta,
} from './types.js'

interface RenderColor {
  css: string
  alpha: number
}

interface SpriteCacheEntry {
  canvas: HTMLCanvasElement
  offset: number
}

interface RenderAppearance {
  glyph: string
  radius: number
  fg: RenderColor
  bg: RenderColor
  attackRadius: number
}

interface GhostState {
  worldX: number
  worldY: number
  entityName: string
  appearance: RenderAppearance
  attackRadius: number  // world units, from Appearance.attackRadius
  pending: boolean      // true = dropped on map, awaiting confirmation click
}

interface RenderVisualEffects {
  selection: RenderColor
  rangeRadius: number
  range: RenderColor
  flash: RenderColor
  flashExpiry: number
  flashStartedAt: number
}

interface RenderEntityUpsert {
  pos: EntityPosition
  app: RenderAppearance
  fx: RenderVisualEffects
}

interface CanvasPointerState {
  button: number
  buttons: number
  dragging: boolean
}

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

function updateFullscreenButtonLabel(): void {
  fullscreenToggle.textContent = document.fullscreenElement === viewportShell
    ? 'Exit Fullscreen'
    : 'Expand View'
}

let currentPathPoints: Array<{ x: number; y: number }> = []

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

function worldToCanvas(wx: number, wy: number): [number, number] {
  return [
    (wx + WORLD_W / 2) * foregroundCanvas.width / WORLD_W,
    (wy + WORLD_H / 2) * foregroundCanvas.height / WORLD_H,
  ]
}

function canvasToWorld(cx: number, cy: number): [number, number] {
  return [
    (cx - foregroundCanvas.width / 2) * WORLD_W / foregroundCanvas.width,
    (cy - foregroundCanvas.height / 2) * WORLD_H / foregroundCanvas.height,
  ]
}

function worldLengthToCanvas(length: number): number {
  return length * foregroundCanvas.width / WORLD_W
}

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

// --- FPS tracking ---

let fps = 0
let lastFrameTime = 0

// --- Interpolation state ---

const currEntitiesById = new Map<number, RenderEntityUpsert>()
const prevRenderStateById = new Map<number, RenderEntityUpsert>()
let prevSnapshotTime = 0
let currSnapshotTime = 0
let snapshotIntervalMs = 50
let lives = 0
let resources = 0
let lastFpsOverlayUpdateTime = 0
let hudDirty = true
const glyphFontSizeCache = new Map<string, number>()
// Entity appearances come from a small, mostly fixed set of tower/enemy visuals,
// so we memoize prerendered sprites for the lifetime of the page instead of
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
let pendingMenuDragItem: DefenderMenuItem | null = null
let currentPhase = 'build'

const SIDE_PANEL_TRIGGER_PX = 48
const MENU_COLS = 2
const MENU_VISIBLE_ROWS = 4
const SIDE_PANEL_RADIUS = 18
const SIDE_PANEL_LINE_HEIGHT = 22
const SIDE_PANEL_TEXT_MARGIN = 28

// Linear interpolation calculator
function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t
}

function drawBackground(pathPoints: Array<{ x: number; y: number }>): void {
  currentPathPoints = pathPoints
  bgCtx.clearRect(0, 0, backgroundCanvas.width, backgroundCanvas.height)
  if (pathPoints.length <= 1) return

  bgCtx.save()
  bgCtx.strokeStyle = '#3b82f6'
  bgCtx.lineWidth = 2
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

function updateHudOverlay(): void {
  if (!hudDirty) return

  hudCtx.clearRect(0, 0, hudCanvas.width, hudCanvas.height)
  drawHud(hudCtx)
  hudDirty = false
}

function updateFpsOverlay(now: number): void {
  if (now - lastFpsOverlayUpdateTime < FPS_OVERLAY_INTERVAL_MS) return

  fpsCtx.clearRect(0, 0, fpsCanvas.width, fpsCanvas.height)
  drawFps(fpsCtx)
  lastFpsOverlayUpdateTime = now
}

function invalidateSidePanel(): void {
  sidePanelDirty = true
}

function setEdgeHoverSide(nextSide: PanelSide | null): void {
  if (edgeHoverSide === nextSide) return
  edgeHoverSide = nextSide
  invalidateSidePanel()
}

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

function isTransparent(color: RenderColor): boolean {
  return color.alpha === 0
}

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

function getEntityBorderWidth(radius: number): number {
  return Math.max(1, radius * 0.18)
}

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

function getSpriteCacheKey(app: RenderAppearance, radius: number): string {
  const roundedRadius = Math.max(1, Math.round(radius))
  return [
    app.glyph,
    roundedRadius,
    app.fg.css,
    app.bg.css,
  ].join('|')
}

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

function drawEntitySprite(x: number, y: number, radius: number, app: RenderAppearance): void {
  const sprite = getEntitySprite(app, radius)
  if (!sprite) return
  fgCtx.drawImage(sprite.canvas, x - sprite.offset, y - sprite.offset)
}

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

function isFlashVisible(fx: RenderVisualEffects, now: number): boolean {
  const elapsed = Math.max(now - fx.flashStartedAt, 0)
  return Math.floor(elapsed / FLASH_FLICKER_INTERVAL_MS) % 2 === 0
}

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

function appearanceToRender(app: EntityAppearance): RenderAppearance {
  return {
    glyph: app.glyph === 0 ? '' : String.fromCodePoint(app.glyph),
    radius: app.radius,
    fg: packedRgbaToRenderColor(app.fg),
    bg: packedRgbaToRenderColor(app.bg),
    attackRadius: app.attackRadius,
  }
}

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
    flashStartedAt: flashExpiry > 0
      ? (keepExistingFlashPhase ? prevFx.flashStartedAt : now)
      : 0,
  }
}

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

function panelSideFromCanvasX(canvasX: number): PanelSide | null {
  const cssWidth = foregroundCanvas.clientWidth
  const triggerPx = cssWidth > 0
    ? SIDE_PANEL_TRIGGER_PX * (foregroundCanvas.width / cssWidth)
    : SIDE_PANEL_TRIGGER_PX
  if (canvasX <= triggerPx) return 'left'
  if (canvasX >= foregroundCanvas.width - triggerPx) return 'right'
  return null
}

function updateEdgeHover(sample: CanvasPointerSample): void {
  if (menuDragActive || ghostState || currentPhase !== 'build') {
    setEdgeHoverSide(null)
    return
  }
  setEdgeHoverSide(panelSideFromCanvasX(sample.canvasX))
}

function findSelectedTower(): RenderEntityUpsert | null {
  for (const entity of currEntitiesById.values()) {
    if (!isTransparent(entity.fx.selection) || entity.fx.rangeRadius > 0) {
      return entity
    }
  }
  return null
}

function formatPanelNumber(value: number): string {
  return Number.isInteger(value) ? String(value) : value.toFixed(1)
}

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

function getSelectedTowerPanelModel(): SidePanelModel | null {
  const selectedTower = findSelectedTower()
  if (!selectedTower) return null

  const defaultSide: PanelSide = selectedTower.pos.x <= 0 ? 'right' : 'left'
  const side: PanelSide = edgeHoverSide ?? defaultSide
  return {
    side,
    title: 'Tower Selected',
    lines: [
      `Entity #${selectedTower.pos.id}`,
      `World ${formatPanelNumber(selectedTower.pos.x)}, ${formatPanelNumber(selectedTower.pos.y)}`,
      `Glyph ${selectedTower.app.glyph || '?'}`,
      `Body radius ${formatPanelNumber(selectedTower.app.radius)}`,
      `Attack radius ${formatPanelNumber(selectedTower.fx.rangeRadius)}`,
      '',
      `Phase ${phaseEl?.textContent ?? '--'}`,
      `Resources $${resources}`,
      'Left click empty space to dismiss',
    ],
  }
}

function getPendingGhostPanelModel(): SidePanelModel | null {
  if (!ghostState?.pending) return null
  return {
    side: 'right',
    title: 'Place Tower',
    lines: [
      ghostState.entityName,
      '',
      'Double-click to place',
      'Left-click to move',
      'Right-click to cancel',
    ],
  }
}

function getSidePanelModel(): SidePanelModel | null {
  return getPendingGhostPanelModel() ?? getSelectedTowerPanelModel()
}

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
  ctx.fillText('Build Menu', textMargin, titleTop, textWidth)

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

function drawGhost(): void {
  if (!ghostState) return
  const [x, y] = worldToCanvas(ghostState.worldX, ghostState.worldY)
  const radius = worldLengthToCanvas(ghostState.appearance.radius)
  const attackPx = worldLengthToCanvas(ghostState.attackRadius)

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
      css: ghostState.pending
        ? 'rgba(255, 0, 127, 0.6)'
        : 'rgba(255, 255, 100, 0.4)',
      alpha: ghostState.pending ? 0.6 : 0.4,
    }
    drawFilledCircleOnContext(fgCtx, x, y, attackPx, rangeColor)
  }

  if (ghostState.pending) {
    fgCtx.globalAlpha = 0.9
    const lineWidth = Math.max(3, radius * 0.35)
    drawStrokedCircleOnContext(
      fgCtx,
      x,
      y,
      radius + lineWidth * 0.5,
      { css: 'rgba(255, 242, 63, 0.9)', alpha: 0.9 },
      lineWidth,
    )
  }

  fgCtx.restore()
}

function updateSidePanelOverlay(): void {
  if (!sidePanelDirty) return

  // Hide everything while a drag is actively in progress.
  if (menuDragActive) {
    overlayCtx.clearRect(0, 0, overlayCanvas.width, overlayCanvas.height)
    overlayCanvas.style.display = 'none'
    sidePanelDirty = false
    return
  }

  const panel = getSidePanelModel()
  const showBuildMenu =
    edgeHoverSide !== null &&
    defenderMenuItems.length > 0 &&
    currentPhase === 'build'
  overlayCtx.clearRect(0, 0, overlayCanvas.width, overlayCanvas.height)

  if (!panel && !showBuildMenu) {
    overlayCanvas.style.display = 'none'
    sidePanelDirty = false
    return
  }

  overlayCanvas.style.display = 'block'
  const foregroundCssWidth = foregroundCanvas.clientWidth
  const overlayCssWidth = overlayCanvas.clientWidth
  const side: PanelSide = edgeHoverSide ?? (panel?.side ?? 'right')
  overlayCanvas.style.left = side === 'left'
    ? '0'
    : `${Math.max(foregroundCssWidth - overlayCssWidth, 0)}px`
  overlayCanvas.style.right = ''

  if (panel) {
    drawSidePanel(overlayCtx, panel)
  } else {
    drawBuildMenu(overlayCtx)
  }
  sidePanelDirty = false
}

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

function log(line: string): void {
  logEl.textContent += line + '\n'
  logEl.scrollTop = logEl.scrollHeight
}

function setTextIfElement(el: HTMLElement | null, value: string): void {
  if (el) el.textContent = value
}

function invalidateHud(): void {
  hudDirty = true
}

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

function sendClientMsg(msg: ClientMsg): void {
  if (ws.readyState !== WebSocket.OPEN) return
  ws.send(JSON.stringify(msg))
}

function sendCanvasMsg(
  eventName: 'click' | 'dblclick' | 'contextmenu' | 'dragstart' | 'dragmove' | 'dragend',
  sample: CanvasPointerSample,
  command?: string,
  parameters?: string[],
): void {
  const msg: UiCanvasMsg = {
    type: 'ui_canvas',
    seq: nextClientSeq++,
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
}

function flushPendingDragMove(): void {
  dragMoveFrameRequested = false
  if (!pendingDragMove) return
  sendCanvasMsg('dragmove', pendingDragMove)
  pendingDragMove = null
}

function scheduleDragMoveFlush(): void {
  if (dragMoveFrameRequested) return
  dragMoveFrameRequested = true
  requestAnimationFrame(() => {
    flushPendingDragMove()
  })
}

function formDataToFields(formData: FormData): Record<string, string> | undefined {
  const fields: Record<string, string> = {}
  for (const [key, value] of formData.entries()) {
    if (typeof value === 'string') fields[key] = value
  }
  return Object.keys(fields).length === 0 ? undefined : fields
}

function sendUiAction(action: string, fields?: Record<string, string>): void {
  const msg: ClientMsg = {
    type: 'ui_action',
    seq: nextClientSeq++,
    action,
  }
  if (fields && Object.keys(fields).length > 0) msg.fields = fields
  sendClientMsg(msg)
}

function isPoint(value: unknown): value is { x: number; y: number } {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as Record<string, unknown>).x === 'number' &&
    typeof (value as Record<string, unknown>).y === 'number'
  )
}

function isEntityPosition(value: unknown): value is EntityPosition {
  return (
    typeof value === 'object' &&
    value !== null &&
    typeof (value as Record<string, unknown>).id === 'number' &&
    typeof (value as Record<string, unknown>).x === 'number' &&
    typeof (value as Record<string, unknown>).y === 'number'
  )
}

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

function getDeltaPhase(delta: WorldDelta): string {
  const maybePhase = (delta as WorldDelta & { phase?: unknown }).phase
  if (typeof maybePhase === 'string') return maybePhase
  return delta.phase
}

function finishSnapshotUpdate(): void {
  prevSnapshotTime = currSnapshotTime
  currSnapshotTime = performance.now()

  if (prevSnapshotTime !== 0) {
    snapshotIntervalMs = currSnapshotTime - prevSnapshotTime
  }
}

function resetClientWorldState(): void {
  currentPathPoints = []
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
  pendingMenuDragItem = null
  currentPhase = 'build'

  bgCtx.clearRect(0, 0, backgroundCanvas.width, backgroundCanvas.height)
  fgCtx.clearRect(0, 0, foregroundCanvas.width, foregroundCanvas.height)
  overlayCtx.clearRect(0, 0, overlayCanvas.width, overlayCanvas.height)
  overlayCanvas.style.display = 'none'
  hudCtx.clearRect(0, 0, hudCanvas.width, hudCanvas.height)
  fpsCtx.clearRect(0, 0, fpsCanvas.width, fpsCanvas.height)
}

function applyWorldDelta(delta: WorldDelta): void {
  const now = performance.now()
  prevRenderStateById.clear()

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
  invalidateHud()
  invalidateSidePanel()
  setTextIfElement(livesEl, String(delta.lives))
  setTextIfElement(resourcesEl, String(delta.resources))
  currentPhase = getDeltaPhase(delta)
  setTextIfElement(phaseEl, currentPhase)
}

// --- Message validation ---

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
    v.upserts.every(isEntityUpsert) &&
    v.erased.every((id) => typeof id === 'number')
  )
}

function isServerMsg(value: unknown): value is ServerMsg {
  if (!value || typeof value !== 'object') return false
  const v = value as Record<string, unknown>
  if (typeof v.type !== 'string') return false

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
        Array.isArray(md.paths) &&
        md.paths.every(isPoint) &&
        Array.isArray(v.defenderMenu) &&
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

ws.onopen = () => {
  statusEl.textContent = 'connected'
  log('Connected')
  const hello: ClientMsg = { type: 'hello', client: 'browser' }
  sendClientMsg(hello)
}

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
      drawBackground(parsed.mapDesign.paths)
      applyWorldDelta(parsed.delta)
      break
  }
}

ws.onclose = () => {
  statusEl.textContent = 'disconnected'
  log('Disconnected')
}

ws.onerror = () => {
  statusEl.textContent = 'error'
  log('WebSocket error')
}

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

document.addEventListener('fullscreenchange', () => {
  updateFullscreenButtonLabel()
  resizeViewport()
})

window.addEventListener('resize', () => {
  resizeViewport()
})

if (typeof ResizeObserver !== 'undefined') {
  const resizeObserver = new ResizeObserver(() => {
    resizeViewport()
  })
  resizeObserver.observe(viewportHost)
}

foregroundCanvas.addEventListener('mousedown', (event: MouseEvent) => {
  const sample = eventToCanvasSample(event)
  updateEdgeHover(sample)

  // Left-click with a pending ghost: pick it up and resume dragging.
  if (event.button === 0 && ghostState?.pending) {
    ghostState.pending = false
    menuDragActive = true
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

foregroundCanvas.addEventListener('mouseleave', (event: MouseEvent) => {
  if (event.relatedTarget === overlayCanvas) return
  setEdgeHoverSide(null)
})

overlayCanvas.addEventListener('mouseenter', () => {
  if (menuDragActive || ghostState || currentPhase !== 'build') return
  if (edgeHoverSide === null) setEdgeHoverSide('right')
})

overlayCanvas.addEventListener('mouseleave', () => {
  if (findSelectedTower()) return
  setEdgeHoverSide(null)
})

window.addEventListener('mouseup', (event: MouseEvent) => {
  // Handle menu drag commit/cancel.
  if (menuDragActive) {
    menuDragActive = false
    pendingMenuDragItem = null
    if (ghostState) {
      const fgRect = foregroundCanvas.getBoundingClientRect()
      const ovRect = overlayCanvas.getBoundingClientRect()
      const overOverlay =
        event.clientX >= ovRect.left && event.clientX <= ovRect.right &&
        event.clientY >= ovRect.top  && event.clientY <= ovRect.bottom
      const overFg =
        event.clientX >= fgRect.left && event.clientX <= fgRect.right &&
        event.clientY >= fgRect.top  && event.clientY <= fgRect.bottom
      if (overFg && !overOverlay) {
        ghostState.pending = true
      } else {
        ghostState = null
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

foregroundCanvas.addEventListener('dblclick', (event: MouseEvent) => {
  if (!ghostState?.pending) return
  const sample = eventToCanvasSample(event)
  const entityName = ghostState.entityName
  ghostState = null
  menuDragActive = false
  invalidateSidePanel()
  sendCanvasMsg('click', sample, 'spawn', [entityName])
})

foregroundCanvas.addEventListener('contextmenu', (event: MouseEvent) => {
  event.preventDefault()
  if (ghostState) {
    ghostState = null
    menuDragActive = false
    invalidateSidePanel()
    return
  }
  const sample = eventToCanvasSample(event)
  updateEdgeHover(sample)
  sendCanvasMsg('click', withPointerButton(sample, 2, 2))
})


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
  if (ghostState && event.target !== foregroundCanvas) {
    event.preventDefault()
    ghostState = null
    menuDragActive = false
    pendingMenuDragItem = null
    invalidateSidePanel()
  }
})

// Track ghost position globally during a menu drag. The ghost is created here
// on the first move so it never appears at the canvas origin.
window.addEventListener('mousemove', (event: MouseEvent) => {
  if (!menuDragActive) return
  const rect = foregroundCanvas.getBoundingClientRect()
  const canvasX = (event.clientX - rect.left) * (foregroundCanvas.width / rect.width)
  const canvasY = (event.clientY - rect.top) * (foregroundCanvas.height / rect.height)
  const [worldX, worldY] = canvasToWorld(canvasX, canvasY)
  if (!ghostState && pendingMenuDragItem) {
    const item = pendingMenuDragItem
    ghostState = {
      worldX,
      worldY,
      entityName: item.entityName,
      appearance: appearanceToRender(item.appearance),
      attackRadius: item.appearance.attackRadius,
      pending: false,
    }
  } else if (ghostState) {
    ghostState.worldX = worldX
    ghostState.worldY = worldY
  }
})

// Overlay mousedown: select a build menu cell and arm a drag (ghost appears on
// first mousemove so it never flashes at the canvas origin).
overlayCanvas.addEventListener('mousedown', (event: MouseEvent) => {
  if (event.button !== 0 || defenderMenuItems.length === 0) return
  if (currentPhase !== 'build') return
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

  if (localY < bodyTop) return
  const col = Math.floor(localX / cellSize)
  const row = Math.floor((localY - bodyTop) / cellSize)
  if (col < 0 || col >= MENU_COLS) return

  const index = menuScrollOffset * MENU_COLS + row * MENU_COLS + col
  if (index >= defenderMenuItems.length) return

  selectedMenuIndex = index
  pendingMenuDragItem = defenderMenuItems[index]
  menuDragActive = true
  ghostState = null  // created on first mousemove
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

updateFullscreenButtonLabel()
resizeViewport()
requestAnimationFrame(frame)
