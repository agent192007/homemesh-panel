// Electron main process: one window, and three narrow file operations
// exposed through the preload - read the HA credentials, read the saved
// layout, write the layout files. Nothing else touches the disk.
import { app, BrowserWindow, dialog, ipcMain } from "electron";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
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

app.whenReady().then(createWindow);
app.on("window-all-closed", () => app.quit());
