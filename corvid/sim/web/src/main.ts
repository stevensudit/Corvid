import type { ServerMsg, ClientMsg, EntityPosition, SnapshotMsg, WorldDelta } from './types.js'

type IncomingServerMsg = ServerMsg | SnapshotMsg

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
const canvas = requireEl('canvas', HTMLCanvasElement)
const livesEl = document.getElementById('lives')
const resourcesEl = document.getElementById('resources')
const phaseEl = document.getElementById('phase')

const maybeCtx = canvas.getContext('2d')
if (!maybeCtx) throw new Error('Could not get 2D canvas context')
const ctx: CanvasRenderingContext2D = maybeCtx

// --- World / canvas coordinate mapping ---
//
// The server simulation runs in a 1920x1080 world space. The canvas is sized
// to the same 16:9 aspect ratio so the mapping is a uniform scale with no
// letterboxing needed.
const WORLD_W = 1920
const WORLD_H = 1080

function worldToCanvas(wx: number, wy: number): [number, number] {
  return [
    (wx + WORLD_W / 2) * canvas.width / WORLD_W,
    (wy + WORLD_H / 2) * canvas.height / WORLD_H,
  ]
}

function canvasToWorld(cx: number, cy: number): [number, number] {
  return [
    (cx - canvas.width / 2) * WORLD_W / canvas.width,
    (cy - canvas.height / 2) * WORLD_H / canvas.height,
  ]
}

// --- FPS tracking ---

let fps = 0
let lastFrameTime = 0

// --- Interpolation state ---

const SNAPSHOT_INTERVAL_MS = 50 // matches server 20 Hz rate

let prevEntities: EntityPosition[] = []
let currEntities: EntityPosition[] = []
let prevById = new Map<number, EntityPosition>()
let lastSnapshotTime = 0
let pathPoints: Array<{ x: number; y: number }> = []

// Linear interpolation calculator
function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t
}

function renderInterpolated(): void {
  const t = Math.min((performance.now() - lastSnapshotTime) / SNAPSHOT_INTERVAL_MS, 1)

  ctx.clearRect(0, 0, canvas.width, canvas.height)
  if (pathPoints.length > 1) {
    ctx.save()
    ctx.strokeStyle = '#3b82f6'
    ctx.lineWidth = 2
    ctx.beginPath()
    const [startX, startY] = worldToCanvas(pathPoints[0].x, pathPoints[0].y)
    ctx.moveTo(startX, startY)
    for (const point of pathPoints.slice(1)) {
      const [x, y] = worldToCanvas(point.x, point.y)
      ctx.lineTo(x, y)
    }
    ctx.stroke()
    ctx.restore()
  }
  for (const e of currEntities) {
    const prev = prevById.get(e.id)
    const wx = prev ? lerp(prev.x, e.x, t) : e.x
    const wy = prev ? lerp(prev.y, e.y, t) : e.y
    const [x, y] = worldToCanvas(wx, wy)
    ctx.beginPath()
    ctx.arc(x, y, 5, 0, Math.PI * 2)
    ctx.fill()
  }
}

function drawFps(): void {
  ctx.save()
  const label = `${fps.toFixed(1)} FPS`
  ctx.font = '14px monospace'
  const pad = 6
  const w = ctx.measureText(label).width + pad * 2
  const h = 20
  const x = canvas.width - w - 4
  const y = canvas.height - 4 - h
  ctx.fillStyle = 'rgba(0,0,0,0.5)'
  ctx.fillRect(x, y, w, h)
  ctx.fillStyle = 'white'
  ctx.fillText(label, x + pad, y + 14)
  ctx.restore()
}

function frame(now: number): void {
  if (lastFrameTime !== 0) {
    const dt = now - lastFrameTime
    fps = fps * 0.9 + (1000 / dt) * 0.1
  }
  lastFrameTime = now

  renderInterpolated()
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

function snapshotPathsToPoints(paths: Array<{ points: Array<{ x: number; y: number }> }>): Array<{ x: number; y: number }> {
  return paths[0]?.points ?? []
}

function getDeltaPhase(delta: WorldDelta): string {
  const maybePhase = (delta as WorldDelta & { phase?: unknown }).phase
  if (typeof maybePhase === 'string') return maybePhase
  return delta.phase
}

function beginSnapshotUpdate(): Map<number, EntityPosition> {
  prevEntities = currEntities
  prevById = new Map(prevEntities.map((e) => [e.id, e]))
  return new Map(currEntities.map((e) => [e.id, e]))
}

function finishSnapshotUpdate(nextEntities: Map<number, EntityPosition>): void {
  currEntities = [...nextEntities.values()]
  lastSnapshotTime = performance.now()
}

function applyWorldDelta(delta: WorldDelta): void {
  const nextEntities = beginSnapshotUpdate()

  for (const entity of delta.upserts) nextEntities.set(entity.id, entity)
  for (const entityId of delta.erased) nextEntities.delete(entityId)

  finishSnapshotUpdate(nextEntities)
  tickEl.textContent = String(delta.tick)
  setTextIfElement(livesEl, String(delta.lives))
  setTextIfElement(resourcesEl, String(delta.resources))
  setTextIfElement(phaseEl, getDeltaPhase(delta))
}

// --- Message validation ---

function isServerMsg(value: unknown): value is IncomingServerMsg {
  if (!value || typeof value !== 'object') return false
  const v = value as Record<string, unknown>
  if (typeof v.type !== 'string') return false

  switch (v.type) {
    case 'hello_ack':
      return typeof v.message === 'string'
    case 'tick':
      return typeof v.tick === 'number'
    case 'snapshot':
      return (
        Array.isArray(v.entities) &&
        Array.isArray(v.paths) &&
        v.entities.every(
          (e) =>
            typeof e === 'object' &&
            e !== null &&
            typeof (e as Record<string, unknown>).id === 'number' &&
            typeof (e as Record<string, unknown>).x === 'number' &&
            typeof (e as Record<string, unknown>).y === 'number',
        ) &&
        v.paths.every(
          (path) =>
            typeof path === 'object' &&
            path !== null &&
            (path as Record<string, unknown>).type === 'path' &&
            Array.isArray((path as Record<string, unknown>).points) &&
            ((path as Record<string, unknown>).points as unknown[]).every(isPoint),
        )
      )
    case 'world_delta':
      return (
        typeof v.tick === 'number' &&
        Array.isArray(v.upserts) &&
        Array.isArray(v.erased) &&
        typeof v.lives === 'number' &&
        typeof v.resources === 'number' &&
        typeof v.phase === 'string' &&
        v.upserts.every(
          (e) =>
            typeof e === 'object' &&
            e !== null &&
            typeof (e as Record<string, unknown>).id === 'number' &&
            typeof (e as Record<string, unknown>).x === 'number' &&
            typeof (e as Record<string, unknown>).y === 'number',
        ) &&
        v.erased.every((id) => typeof id === 'number')
      )
    case 'world_snapshot':
      return (
        Array.isArray(v.paths) &&
        v.paths.every(isPoint) &&
        isServerMsg(v.delta)
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
    case 'snapshot':
      prevEntities = currEntities
      prevById = new Map(prevEntities.map((e) => [e.id, e]))
      currEntities = parsed.entities
      pathPoints = snapshotPathsToPoints(parsed.paths)
      lastSnapshotTime = performance.now()
      break
    case 'world_delta':
      applyWorldDelta(parsed)
      break
    case 'world_snapshot':
      pathPoints = parsed.paths
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

canvas.addEventListener('click', (e: MouseEvent) => {
  if (ws.readyState !== WebSocket.OPEN) return
  const rect = canvas.getBoundingClientRect()
  const cssX = (e.clientX - rect.left) * (canvas.width / rect.width)
  const cssY = (e.clientY - rect.top) * (canvas.height / rect.height)
  const [x, y] = canvasToWorld(cssX, cssY).map(Math.round) as [number, number]
  const msg: ClientMsg = { type: 'spawn', x, y }
  ws.send(JSON.stringify(msg))
})

requestAnimationFrame(frame)
