// The preload's API, with a fallback so the UI also runs in a plain
// browser (npm run dev without Electron): credentials typed in by hand,
// no reading or writing project files.
export interface Bridge {
  loadEnv(): Promise<{ url?: string; token?: string }>;
  projectRoot(): Promise<string | null>;
  loadLayout(): Promise<string | null>;
  saveLayout(json: string, header: string): Promise<string>;
}

declare global {
  interface Window { homemesh?: Bridge }
}

export const bridge: Bridge = window.homemesh ?? {
  loadEnv: async () => ({}),
  projectRoot: async () => null,
  loadLayout: async () => null,
  saveLayout: async () => { throw new Error("saving needs the desktop app"); },
};
