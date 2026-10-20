import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("homemesh", {
  loadEnv: () => ipcRenderer.invoke("env:load"),
  projectRoot: () => ipcRenderer.invoke("project:root"),
  loadLayout: () => ipcRenderer.invoke("layout:load"),
  saveLayout: (json: string, header: string) => ipcRenderer.invoke("layout:save", json, header),
});
