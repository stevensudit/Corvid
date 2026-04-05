import type { ServerMsg, ClientMsg, SnapshotEntity } from './types.js'

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

const maybeCtx = canvas.getContext('2d')
if (!maybeCtx) throw new Error('Could not get 2D canvas context')
const ctx: CanvasRenderingContext2D = maybeCtx

// --- Interpolation state ---

const SNAPSHOT_INTERVAL_MS = 50 // matches server 20 Hz rate

let prevEntities: SnapshotEntity[] = []
let currEntities: SnapshotEntity[] = []
let prevById = new Map<number, SnapshotEntity>()
let lastSnapshotTime = 0

// Linear interpolation calculator
function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * t
}

function renderInterpolated(): void {
  const t = Math.min((performance.now() - lastSnapshotTime) / SNAPSHOT_INTERVAL_MS, 1)

  ctx.clearRect(0, 0, canvas.width, canvas.height)
  for (const e of currEntities) {
    const prev = prevById.get(e.id)
    const x = prev ? lerp(prev.x, e.x, t) : e.x
    const y = prev ? lerp(prev.y, e.y, t) : e.y
    ctx.beginPath()
    ctx.arc(x, y, 5, 0, Math.PI * 2)
    ctx.fill()
  }
}

function frame(): void {
  renderInterpolated()
  requestAnimationFrame(frame)
}

// --- Logging ---

function log(line: string): void {
  logEl.textContent += line + '\n'
  logEl.scrollTop = logEl.scrollHeight
}

// --- Message validation ---

function isServerMsg(value: unknown): value is ServerMsg {
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
        v.entities.every(
          (e) =>
            typeof e === 'object' &&
            e !== null &&
            typeof (e as Record<string, unknown>).id === 'number' &&
            typeof (e as Record<string, unknown>).x === 'number' &&
            typeof (e as Record<string, unknown>).y === 'number',
        )
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
      lastSnapshotTime = performance.now()
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
  const scaleX = canvas.width / rect.width
  const scaleY = canvas.height / rect.height
  const x = Math.round((e.clientX - rect.left) * scaleX)
  const y = Math.round((e.clientY - rect.top) * scaleY)
  const msg: ClientMsg = { type: 'spawn', x, y }
  ws.send(JSON.stringify(msg))
})

requestAnimationFrame(frame)
