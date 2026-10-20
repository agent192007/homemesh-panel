// Start the designer: build if needed, then launch Electron with
// ELECTRON_RUN_AS_NODE removed. VS Code sets that variable in its
// terminals, and with it set Electron runs as plain Node and fails with
// "does not provide an export named 'BrowserWindow'".
import { spawn, spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const electron = join(root, "node_modules", "electron", "dist", process.platform === "win32" ? "electron.exe" : "electron");

if (!existsSync(electron)) {
  console.log("Electron's runtime is missing (npm 11 blocks its download step) - fetching it now.");
  spawnSync(process.execPath, [join(root, "node_modules", "electron", "install.js")], { stdio: "inherit" });
}
if (!existsSync(join(root, "dist", "index.html")) || process.argv.includes("--build")) {
  spawnSync(process.platform === "win32" ? "npx.cmd" : "npx", ["vite", "build"], { cwd: root, stdio: "inherit", shell: true });
}
const env = { ...process.env };
delete env.ELECTRON_RUN_AS_NODE;
spawn(electron, ["."], { cwd: root, env, detached: true, stdio: "ignore" }).unref();
