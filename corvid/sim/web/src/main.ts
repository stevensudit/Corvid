import type {
  ClientMsg,
  EntityAppearance,
  EntityPosition,
  EntityUpsert,
  EntityVisualEffects,
  ServerMsg,
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
}

interface RenderVisualEffects {
  selection: RenderColor
  rangeRadius: number
  range: RenderColor
  flash: RenderColor
  flashExpiry: number
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

const DEFAULT_RENDER_APPEARANCE: RenderAppearance = {
  glyph: '?',
  radius: 5,
  fg: { css: 'rgba(255, 255, 255, 1)', alpha: 1 },
  bg: { css: 'rgba(0, 0, 0, 1)', alpha: 1 },
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
const backgroundCanvas = requireEl('background-canvas', HTMLCanvasElement)
const foregroundCanvas = requireEl('foreground-canvas', HTMLCanvasElement)
const livesEl = document.getElementById('lives')
const resourcesEl = document.getElementById('resources')
const phaseEl = document.getElementById('phase')

const maybeBgCtx = backgroundCanvas.getContext('2d')
if (!maybeBgCtx) throw new Error('Could not get 2D background canvas context')
const bgCtx: CanvasRenderingContext2D = maybeBgCtx

const maybeFgCtx = foregroundCanvas.getContext('2d')
if (!maybeFgCtx) throw new Error('Could not get 2D foreground canvas context')
const fgCtx: CanvasRenderingContext2D = maybeFgCtx
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
let nextClientSeq = 1
let canvasPointerState: CanvasPointerState | null = null
let pendingDragMove: CanvasPointerSample | null = null
let dragMoveFrameRequested = false

// Linear interpolation calculator
function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t
}

function drawBackground(pathPoints: Array<{ x: number; y: number }>): void {
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

function isFlashVisible(now: number): boolean {
  return Math.floor(now / 125) % 2 === 0
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

  if (radius <= 0 || isTransparent(fx.flash) || !isFlashVisible(now)) return

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
  }
}

function visualEffectsToRender(
  fx: EntityVisualEffects,
  now: number,
): RenderVisualEffects {
  return {
    selection: packedRgbaToRenderColor(fx.selection),
    rangeRadius: fx.rangeRadius,
    range: packedRgbaToRenderColor(fx.range),
    flash: packedRgbaToRenderColor(fx.flash),
    flashExpiry: fx.flashExpiryMs <= 0 ? 0 : now + fx.flashExpiryMs,
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
    fx: upsert.vfx ? visualEffectsToRender(upsert.vfx, now) : (prevFx ?? DEFAULT_RENDER_VISUAL_EFFECTS),
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

function frame(now: number): void {
  if (lastFrameTime !== 0) {
    const dt = now - lastFrameTime
    fps = fps * 0.9 + (1000 / dt) * 0.1
  }
  lastFrameTime = now

  renderInterpolated(now)
  updateHudOverlay()
  updateFpsOverlay(now)
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

function sendCanvasMsg(eventName: 'click' | 'dblclick' | 'contextmenu' | 'dragstart' | 'dragmove' | 'dragend', sample: CanvasPointerSample): void {
  sendClientMsg({
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
  })
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
  setTextIfElement(livesEl, String(delta.lives))
  setTextIfElement(resourcesEl, String(delta.resources))
  setTextIfElement(phaseEl, getDeltaPhase(delta))
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
    case 'world_snapshot':
      return (
        Array.isArray(v.paths) &&
        v.paths.every(isPoint) &&
        isWorldDelta(v.delta)
      )
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
      drawBackground(parsed.paths)
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

foregroundCanvas.addEventListener('mousedown', (event: MouseEvent) => {
  const sample = eventToCanvasSample(event)
  canvasPointerState = {
    button: sample.button,
    buttons: sample.buttons,
    dragging: false,
  }

  if (sample.button !== 2) sendCanvasMsg('click', sample)
})

foregroundCanvas.addEventListener('mousemove', (event: MouseEvent) => {
  if (!canvasPointerState) return
  const sample = eventToCanvasSample(event)

  if (!canvasPointerState.dragging) {
    canvasPointerState.dragging = true
    canvasPointerState.buttons = sample.buttons
    sendCanvasMsg('dragstart', sample)
    return
  }

  canvasPointerState.buttons = sample.buttons
  pendingDragMove = sample
  scheduleDragMoveFlush()
})

window.addEventListener('mouseup', (event: MouseEvent) => {
  if (!canvasPointerState) return
  const sample = eventToCanvasSample(event)
  const wasDragging = canvasPointerState.dragging
  canvasPointerState = null
  flushPendingDragMove()
  if (wasDragging) sendCanvasMsg('dragend', sample)
})

foregroundCanvas.addEventListener('contextmenu', (event: MouseEvent) => {
  event.preventDefault()
  sendCanvasMsg('contextmenu', eventToCanvasSample(event))
})

foregroundCanvas.addEventListener('dblclick', (event: MouseEvent) => {
  sendCanvasMsg('dblclick', eventToCanvasSample(event))
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

requestAnimationFrame(frame)
