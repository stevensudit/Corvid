import type { ServerMsg, ClientMsg } from './types.js'

const statusEl = document.getElementById('status')!
const tickEl = document.getElementById('tick')!
const logEl = document.getElementById('log')!

function log(line: string): void {
  logEl.textContent += line + '\n'
  logEl.scrollTop = logEl.scrollHeight
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
  let msg: ServerMsg
  try {
    msg = JSON.parse(event.data) as ServerMsg
  } catch {
    log('(unparseable message)')
    return
  }
  if (msg.type === 'hello_ack') {
    log(`Server says: ${msg.message}`)
  } else if (msg.type === 'tick') {
    tickEl.textContent = String(msg.tick)
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
