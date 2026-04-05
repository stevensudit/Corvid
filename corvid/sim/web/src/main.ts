import type { ServerMsg, ClientMsg, SnapshotEntity } from './types.js'

const statusEl = document.getElementById('status')!
const tickEl = document.getElementById('tick')!
const logEl = document.getElementById('log')!
const canvas = document.getElementById('canvas') as HTMLCanvasElement
const ctx = canvas.getContext('2d')!

function render(entities: SnapshotEntity[]): void {
  ctx.clearRect(0, 0, canvas.width, canvas.height)

  for (const e of entities) {
    ctx.beginPath()
    ctx.arc(e.x, e.y, 5, 0, Math.PI * 2)
    ctx.fill()
  }
}

function log(line: string): void {
  logEl.textContent += line + '\n'
  logEl.scrollTop = logEl.scrollHeight
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
    case 'snapshot':
      return Array.isArray(v.entities) &&
         v.entities.every(e =>
           typeof e === 'object' &&
           e !== null &&
           typeof (e as any).id === 'number' &&
           typeof (e as any).x === 'number' &&
           typeof (e as any).y === 'number'
         )
    default:
      return false
  }
}

const protocol = location.protocol === 'https:' ? 'wss' : 'ws'
const ws = new WebSocket(`${protocol}://${location.host}/ws`)

ws.onopen = () => {
  statusEl.textContent = 'connected'
  log('Connected')
  const hello: ClientMsg = { type: 'hello', client: 'browser' }
  ws.send(JSON.stringify(hello))
}

ws.onmessage = (event: MessageEvent<string>) => {
  log(event.data)

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
      render(parsed.entities)
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
