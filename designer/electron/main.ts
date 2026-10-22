// Electron main process: one window, and a few narrow file operations
// exposed through the preload - read the HA credentials, read the saved
// layout, write the layout files, read the panel firmware image. Nothing
// else touches the disk. Also turns on Web Serial, for flashing a panel.
import { app, BrowserWindow, dialog, ipcMain, session } from "electron";
import { existsSync, readFileSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const settingsFile = () => join(app.getPath("userData"), "settings.json");

function settings(): { projectRoot?: string } {
  try { return JSON.parse(readFileSync(settingsFile(), "utf8")); } catch { return {}; }
}

const isProject = (dir?: string | null) => !!dir && existsSync(join(dir, "include", "ha_layout.json"));

// The HomeMesh project folder: tools/.env and include/ha_layout.*.
// HOMEMESH_ROOT, then the folder chosen last time, then wherever the app
// sits inside a checkout - designer/ when run from source, or
// designer/release/ for the portable exe.
function projectRoot(): string | null {
  // A portable exe runs from a temp folder; electron-builder passes the
  // folder the exe actually sits in.
  const exeDir = process.env.PORTABLE_EXECUTABLE_DIR || dirname(process.execPath);
  const candidates = [process.env.HOMEMESH_ROOT, settings().projectRoot,
    resolve(app.getAppPath(), ".."), resolve(process.cwd(), ".."), resolve(exeDir, "..", ".."), resolve(exeDir, "..")];
  return candidates.find(isProject) ?? null;
}

ipcMain.handle("project:choose", async () => {
  const r = await dialog.showOpenDialog({ title: "Choose the HomeMesh project folder", properties: ["openDirectory"] });
  const dir = r.filePaths[0];
  if (!dir) return null;
  if (!isProject(dir)) throw new Error("That folder has no include/ha_layout.json - choose the homemesh-panel folder.");
  writeFileSync(settingsFile(), JSON.stringify({ ...settings(), projectRoot: dir }, null, 1));
  return dir;
});

function readEnv(): { url?: string; token?: string } {
  const root = projectRoot();
  if (!root) return {};
  const f = join(root, "tools", ".env");
  if (!existsSync(f)) return {};
  const env: Record<string, string> = {};
  for (const raw of readFileSync(f, "utf8").split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || line.startsWith("#") || !line.includes("=")) continue;
    const [k, ...v] = line.split("=");
    env[k.trim()] = v.join("=").trim().replace(/^["']|["']$/g, "");
  }
  return { url: env.HA_URL, token: env.HA_TOKEN };
}

ipcMain.handle("env:load", () => readEnv());
ipcMain.handle("project:root", () => projectRoot());
ipcMain.handle("layout:load", () => {
  const root = projectRoot();
  return root ? readFileSync(join(root, "include", "ha_layout.json"), "utf8") : null;
});
ipcMain.handle("layout:save", (_e, json: string, header: string) => {
  const root = projectRoot();
  if (!root) throw new Error("HomeMesh project folder not found (set HOMEMESH_ROOT)");
  writeFileSync(join(root, "include", "ha_layout.json"), json + "\n", "utf8");
  writeFileSync(join(root, "include", "ha_layout.h"), header, "utf8");
  return join(root, "include");
});

// The whole-board image tools/flash_image.py writes after each env:ha
// build. Read from the project, never bundled: secrets.h is compiled in.
const FIRMWARE = ["firmware", "homemesh_panel.bin"];
ipcMain.handle("firmware:info", () => {
  const root = projectRoot();
  if (!root) return { error: "Project folder not found." };
  const bin = join(root, ...FIRMWARE);
  if (!existsSync(bin)) return { error: "No firmware image yet: build it once with  pio run -e ha  (it writes firmware/homemesh_panel.bin)." };
  let meta: Record<string, unknown> = {};
  try { meta = JSON.parse(readFileSync(bin.replace(/\.bin$/, ".json"), "utf8")); } catch { /* described below */ }
  return { path: bin, bytes: statSync(bin).size, ...meta };
});
ipcMain.handle("firmware:load", () => {
  const root = projectRoot();
  if (!root) throw new Error("Project folder not found.");
  return new Uint8Array(readFileSync(join(root, ...FIRMWARE)));
});

// Web Serial. Chromium normally asks the user to pick a port; with no
// browser chrome Electron hands the choice to us. Prefer the USB-serial
// chips ESP32 boards use (CH340 on this panel, CP210x, FTDI, Espressif's
// own), and remember which port was taken so the UI can say so.
const USB_SERIAL_VIDS = ["1a86", "10c4", "0403", "303a"];
let lastPort = "";
function enableSerial() {
  session.defaultSession.on("select-serial-port", (event, ports, _wc, callback) => {
    event.preventDefault();
    const vid = (p: Electron.SerialPort) => (p.vendorId ?? "").toLowerCase().padStart(4, "0");
    const pick = ports.find((p) => USB_SERIAL_VIDS.includes(vid(p))) ?? ports[0];
    lastPort = pick ? `${pick.portName ?? pick.displayName ?? "port"}${pick.displayName ? ` (${pick.displayName})` : ""}` : "";
    callback(pick?.portId ?? "");
  });
  // Electron grants permission checks by default; these keep that true
  // for serial once a handler exists.
  session.defaultSession.setPermissionCheckHandler(() => true);
  session.defaultSession.setDevicePermissionHandler((d) => d.deviceType === "serial");
}
ipcMain.handle("serial:last", () => lastPort);

function createWindow() {
  const win = new BrowserWindow({
    width: 1520,
    height: 960,
    minWidth: 1200,
    minHeight: 760,
    backgroundColor: "#15181c",
    title: "HomeMesh Designer",
    icon: join(here, "..", "build", "icon.png"),
    webPreferences: { preload: join(here, "preload.cjs"), contextIsolation: true, nodeIntegration: false },
  });
  win.setMenuBarVisibility(false);
  if (process.env.VITE_DEV_SERVER_URL) win.loadURL(process.env.VITE_DEV_SERVER_URL);
  else win.loadFile(join(here, "..", "dist", "index.html"));
}

app.whenReady().then(() => { enableSerial(); createWindow(); });
app.on("window-all-closed", () => app.quit());
