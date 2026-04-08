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

export type UiCanvasEvent =
  | 'click'
  | 'dblclick'
  | 'contextmenu'
  | 'dragstart'
  | 'dragmove'
  | 'dragend'

export type UiButton = 'left' | 'middle' | 'right' | 'other'

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
}

export interface EntityVisualEffects {
  selection: number
  rangeRadius: number
  range: number
  flash: number
}

interface PathPoint {
  x: number
  y: number
}

export interface EntityUpsert {
  pos: EntityPosition
  app?: EntityAppearance
  vfx?: EntityVisualEffects
}

export interface UiCanvasMsg {
  type: 'ui_canvas'
  seq: number
  event: UiCanvasEvent
  button: UiButton
  buttons: number
  x: number
  y: number
  canvasX: number
  canvasY: number
  shift: boolean
  ctrl: boolean
  alt: boolean
  meta: boolean
}

export interface UiActionMsg {
  type: 'ui_action'
  seq: number
  action: string
  fields?: Record<string, string>
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

export type ServerMsg = HelloAckMsg | WorldDelta | WorldSnapshot;
export type ClientMsg = HelloMsg | UiCanvasMsg | UiActionMsg;
