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
  upserts: EntityPosition[]
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
