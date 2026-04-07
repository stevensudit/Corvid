import type {
  ClientMsg,
  EntityAppearance,
  EntityPosition,
  EntityUpsert,
  ServerMsg,
  WorldDelta,
} from './types.js'

interface RenderColor {
  css: string
  alpha: number
}

interface RenderAppearance {
  glyph: string
  scale: number
  fg: RenderColor
  bg: RenderColor
  glow: RenderColor
}

interface RenderEntityUpsert {
  pos: EntityPosition
  app: RenderAppearance
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
const glyphFontSizeCache = new Map<string, number>()

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

function renderInterpolated(): void {
  const interval = Math.max(snapshotIntervalMs, 1)
  const t = Math.min((performance.now() - currSnapshotTime) / interval, 1)

  fgCtx.clearRect(0, 0, foregroundCanvas.width, foregroundCanvas.height)
  for (const e of currEntitiesById.values()) {
    const prevState = prevRenderStateById.get(e.pos.id)
    const prevPos = prevState?.pos
    const prevApp = prevState?.app ?? e.app
    const wx = prevPos ? lerp(prevPos.x, e.pos.x, t) : e.pos.x
    const wy = prevPos ? lerp(prevPos.y, e.pos.y, t) : e.pos.y
    const [x, y] = worldToCanvas(wx, wy)
    const radius = 5 * lerp(prevApp.scale, e.app.scale, t)

    if (e.app.glow.alpha !== 0) drawFilledCircle(x, y, radius * 1.5, e.app.glow)
    if (e.app.bg.alpha !== 0) drawFilledCircle(x, y, radius, e.app.bg)
    if (e.app.fg.alpha !== 0) drawGlyphInCircle(e.app.glyph, x, y, radius, e.app.fg)
  }
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

function drawFilledCircle(x: number, y: number, radius: number, color: RenderColor): void {
  if (radius <= 0 || isTransparent(color)) return

  fgCtx.fillStyle = color.css
  fgCtx.beginPath()
  fgCtx.arc(x, y, radius, 0, Math.PI * 2)
  fgCtx.fill()
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

function drawGlyphInCircle(glyph: string, x: number, y: number, radius: number, color: RenderColor): void {
  if (!glyph || radius <= 0 || isTransparent(color)) return

  const fontSize = getGlyphFontSize(glyph, radius)

  fgCtx.save()
  fgCtx.fillStyle = color.css
  fgCtx.font = `${fontSize}px monospace`
  fgCtx.textAlign = 'center'
  fgCtx.textBaseline = 'middle'
  fgCtx.fillText(glyph, x, y)
  fgCtx.restore()
}

function appearanceToRender(app: EntityAppearance): RenderAppearance {
  return {
    glyph: String.fromCodePoint(app.glyph),
    scale: app.scale,
    fg: packedRgbaToRenderColor(app.fg),
    bg: packedRgbaToRenderColor(app.bg),
    glow: packedRgbaToRenderColor(app.glow),
  }
}

function upsertToRenderEntity(upsert: EntityUpsert): RenderEntityUpsert {
  return {
    pos: upsert.pos,
    app: appearanceToRender(upsert.app),
  }
}

function drawFps(): void {
  fgCtx.save()
  const label = `${fps.toFixed(1)} FPS`
  fgCtx.font = '14px monospace'
  const pad = 6
  const w = fgCtx.measureText(label).width + pad * 2
  const h = 20
  const x = foregroundCanvas.width - w - 4
  const y = foregroundCanvas.height - 4 - h
  fgCtx.fillStyle = 'rgba(0,0,0,0.5)'
  fgCtx.fillRect(x, y, w, h)
  fgCtx.fillStyle = 'white'
  fgCtx.fillText(label, x + pad, y + 14)
  fgCtx.restore()
}

function drawHud(): void {
  fgCtx.save()
  fgCtx.font = '20px monospace'
  fgCtx.textBaseline = 'top'

  const livesLabel = `${lives}❤️`
  const resourcesLabel = `$${resources}`
  const pad = 4
  const barHeight = 24

  fgCtx.fillStyle = 'rgba(0,0,0,0.55)'
  fgCtx.fillRect(0, 0, foregroundCanvas.width, barHeight)

  fgCtx.fillStyle = 'white'
  fgCtx.fillText(`${livesLabel}   ${resourcesLabel}`, pad, pad)
  fgCtx.restore()
}

function frame(now: number): void {
  if (lastFrameTime !== 0) {
    const dt = now - lastFrameTime
    fps = fps * 0.9 + (1000 / dt) * 0.1
  }
  lastFrameTime = now

  renderInterpolated()
  drawHud()
  drawFps()
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
    typeof (value as Record<string, unknown>).scale === 'number' &&
    typeof (value as Record<string, unknown>).fg === 'number' &&
    typeof (value as Record<string, unknown>).bg === 'number' &&
    typeof (value as Record<string, unknown>).glow === 'number'
  )
}

function isEntityUpsert(value: unknown): value is EntityUpsert {
  return (
    typeof value === 'object' &&
    value !== null &&
    isEntityPosition((value as Record<string, unknown>).pos) &&
    isEntityAppearance((value as Record<string, unknown>).app)
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
  prevRenderStateById.clear()

  for (const entity of delta.upserts) {
    const nextEntity = upsertToRenderEntity(entity)
    const prevEntity = currEntitiesById.get(entity.pos.id)
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
    case 'tick':
      return typeof v.tick === 'number'
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
  ws.send(JSON.stringify(hello))
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
    case 'tick':
      tickEl.textContent = String(parsed.tick)
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

foregroundCanvas.addEventListener('click', (e: MouseEvent) => {
  if (ws.readyState !== WebSocket.OPEN) return
  const rect = foregroundCanvas.getBoundingClientRect()
  const cssX = (e.clientX - rect.left) * (foregroundCanvas.width / rect.width)
  const cssY = (e.clientY - rect.top) * (foregroundCanvas.height / rect.height)
  const [x, y] = canvasToWorld(cssX, cssY).map(Math.round) as [number, number]
  const msg: ClientMsg = { type: 'spawn', x, y }
  ws.send(JSON.stringify(msg))
})

requestAnimationFrame(frame)
