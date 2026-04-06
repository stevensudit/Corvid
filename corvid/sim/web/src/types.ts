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

export interface SnapshotEntity {
  id: number
  x: number
  y: number
}

interface PathPoint {
  x: number
  y: number
}

export interface SnapshotMsg {
  type: 'snapshot'
  entities: SnapshotEntity[]
  path: PathPoint[]
}

export interface SpawnMsg {
  type: 'spawn'
  x: number
  y: number
}

export type ServerMsg = HelloAckMsg | TickMsg | SnapshotMsg;
export type ClientMsg = HelloMsg | SpawnMsg;
