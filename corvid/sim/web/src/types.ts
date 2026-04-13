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

export interface Position {
  x: number
  y: number
}

export interface EntityPosition {
  id: number
  x: number
  y: number
}

export interface EntityAppearance {
  glyph: number
  radius: number
  fg: number
  bg: number
  attackRadius: number
}

export interface EntityVisualEffects {
  selection: number
  rangeRadius: number
  range: number
  flash: number
  flashExpiryMs: number
}

interface PathPoint {
  x: number
  y: number
}

interface MapDesign {
  backgroundSprite: string
  foregroundSprite: string
  pathWidth: number
  paths: PathPoint[]
}

export interface DefenderMenuItem {
  entityName: string
  displayName: string
  flavorText: string
  resourceCost: number
  appearance: EntityAppearance
}

export interface UiState {
  placementAllowed?: boolean
  spawnAllowed?: boolean
  selectedDefender?: Position
  defenderSummary?: DefenderMenuItem
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
  command?: string
  parameters?: string[]
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
  uiState: UiState
}

export interface WorldSnapshot {
  type: 'world_snapshot'
  mapDesign: MapDesign
  defenderMenu: DefenderMenuItem[]
  delta: WorldDelta
}

export type ServerMsg = HelloAckMsg | WorldDelta | WorldSnapshot;
export type ClientMsg = HelloMsg | UiCanvasMsg | UiActionMsg;
