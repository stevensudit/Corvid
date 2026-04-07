// Client -> server
export interface HelloMsg {
  type: 'hello';
  client: string;
}

// Server -> client
export interface HelloAckMsg {
  type: 'hello_ack';
  message: string;
}

export interface TickMsg {
  type: 'tick';
  tick: number;
}

export interface EntityPosition {
  id: number
  x: number
  y: number
}

export interface EntityAppearance {
  glyph: number
  scale: number
  fg: number
  bg: number
  glow: number
}

export interface EntityUpsert {
  pos: EntityPosition
  app: EntityAppearance
}

export interface EntityRender {
  glyph: string
  scale: number
  fg: string // CSS rgba(...), unpacked from a packed RGBA number
  bg: string // CSS rgba(...), unpacked from a packed RGBA number
  glow: string // CSS rgba(...), unpacked from a packed RGBA number
}

interface PathPoint {
  x: number
  y: number
}

export interface SpawnMsg {
  type: 'spawn'
  x: number
  y: number
}

export interface WorldDelta {
  type: 'world_delta'
  tick: number
  upserts: EntityUpsert[]
  erased: number[]
  lives: number
  resources: number
  phase: string
}

export interface WorldSnapshot {
  type: 'world_snapshot'
  paths: PathPoint[]
  delta: WorldDelta
}

export type ServerMsg = HelloAckMsg | TickMsg | WorldDelta | WorldSnapshot;
export type ClientMsg = HelloMsg | SpawnMsg;
