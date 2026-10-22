// The preload's API, with a fallback so the UI also runs in a plain
// browser (npm run dev without Electron): credentials typed in by hand,
// no reading or writing project files.
export interface Bridge {
  loadEnv(): Promise<{ url?: string; token?: string }>;
  projectRoot(): Promise<string | null>;
  chooseProject(): Promise<string | null>;
  loadLayout(): Promise<string | null>;
  saveLayout(json: string, header: string): Promise<string>;
  firmwareInfo(): Promise<FirmwareInfo>;
  loadFirmware(): Promise<Uint8Array>;
  lastSerialPort(): Promise<string>;
}

// tools/flash_image.py's description of firmware/homemesh_panel.bin.
export interface FirmwareInfo {
  error?: string;
  path?: string; bytes?: number; built?: string; commit?: string;
  sha256?: string; md5?: string; wifi?: string; ha_host?: string; offset?: number;
}

declare global {
  interface Window { homemesh?: Bridge }
}

export const bridge: Bridge = window.homemesh ?? {
  loadEnv: async () => ({}),
  projectRoot: async () => null,
  chooseProject: async () => null,
  loadLayout: async () => null,
  saveLayout: async () => { throw new Error("saving needs the desktop app"); },
  firmwareInfo: async () => ({ error: "Flashing needs the desktop app." }),
  loadFirmware: async () => { throw new Error("flashing needs the desktop app"); },
  lastSerialPort: async () => "",
};
