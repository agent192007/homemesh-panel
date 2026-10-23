// EventType from src/panel_model.h - the vocabulary between the
// transport and the UI. The simulator and its JavaScript share it.
export const Ev = {
  Link: 0,
  DevicesAvailable: 1,
  Power: 2,
  Brightness: 3,
  Color: 4,
  Temperature: 5,
  Humidity: 6,
  AutomationRan: 7,
  Availability: 8,   // value 1 / 0: HA reports the device unavailable
  HeaderStatus: 9,   // value 1 bad, 0 good, -1 unknown: the status bar's block
} as const;

export const Link = { NoWifi: 0, NoBroker: 1, Online: 2 } as const;

export interface SimEvent { type: number; entity: number; value: number; fvalue?: number }
