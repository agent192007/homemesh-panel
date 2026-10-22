import { contextBridge, ipcRenderer } from "electron";

contextBridge.exposeInMainWorld("homemesh", {
  loadEnv: () => ipcRenderer.invoke("env:load"),
  projectRoot: () => ipcRenderer.invoke("project:root"),
  chooseProject: () => ipcRenderer.invoke("project:choose"),
  loadLayout: () => ipcRenderer.invoke("layout:load"),
  saveLayout: (json: string, header: string) => ipcRenderer.invoke("layout:save", json, header),
  firmwareInfo: () => ipcRenderer.invoke("firmware:info"),
  loadFirmware: () => ipcRenderer.invoke("firmware:load"),
  lastSerialPort: () => ipcRenderer.invoke("serial:last"),
});
