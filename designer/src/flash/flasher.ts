// Flashing a panel from the designer, with no toolchain on the PC: Web
// Serial (built into Electron) and esptool-js, Espressif's own flasher in
// JavaScript. The image is tools/flash_image.py's whole-board image, so it
// goes at 0x0 and brings up a display that has never run this project.
import { ESPLoader, Transport } from "esptool-js";

export interface FlashProgress {
  stage: "connecting" | "writing" | "verifying" | "restarting";
  fraction: number;   // of the write, 0..1
}

// Flash `image` and leave the board restarting into it. Resolves with the
// chip esptool identified; the port is closed again when it returns.
export async function flashPanel(port: SerialPort, image: Uint8Array, md5: string | undefined,
  onProgress: (p: FlashProgress) => void, onLog: (line: string) => void): Promise<string> {
  const transport = new Transport(port, false);
  const loader = new ESPLoader({
    transport, baudrate: 921600, romBaudrate: 115200,
    terminal: { clean() {}, writeLine: (s: string) => onLog(s), write: (s: string) => onLog(s) },
  });
  try {
    onProgress({ stage: "connecting", fraction: 0 });
    const chip = await loader.main();
    // The panel is a classic ESP32; an S2/S3/C3 board needs another build.
    if (loader.chip.CHIP_NAME !== "ESP32")
      throw new Error(`This board is an ${loader.chip.CHIP_NAME}; the panel firmware is built for the classic ESP32.`);
    await loader.writeFlash({
      fileArray: [{ data: image, address: 0 }],
      // The merged image already carries the board's flash settings.
      flashMode: "keep", flashFreq: "keep", flashSize: "keep",
      eraseAll: false, compress: true,
      reportProgress: (_i, written, total) =>
        onProgress({ stage: written >= total ? "verifying" : "writing", fraction: written / total }),
      // esptool-js checks the written flash against this when given one.
      calculateMD5Hash: md5 ? () => md5 : undefined,
    });
    onProgress({ stage: "restarting", fraction: 1 });
    await loader.after("hard_reset");
    return chip;
  } finally {
    await transport.disconnect().catch(() => {});
  }
}

// Reset the panel and hand each line of its serial log to `onLine` until
// it returns true or `ms` pass. The board's auto-reset circuit takes RTS
// to EN: pulsing it restarts the panel, so the log is read from the top.
export async function watchBoot(port: SerialPort, onLine: (line: string) => boolean | void, ms: number) {
  await port.open({ baudRate: 115200 });
  const reader = port.readable!.getReader();
  const timer = setTimeout(() => reader.cancel().catch(() => {}), ms);
  try {
    await port.setSignals({ dataTerminalReady: false, requestToSend: true });
    await new Promise((r) => setTimeout(r, 120));
    await port.setSignals({ requestToSend: false });
    const text = new TextDecoder();
    let pending = "";
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      pending += text.decode(value, { stream: true });
      const lines = pending.split(/\r?\n/);
      pending = lines.pop() ?? "";
      for (const raw of lines) {
        // Boot ROM noise before the app starts is not worth showing.
        const line = raw.replace(/[^\x20-\x7e\xb0]/g, "").trimEnd();
        if (line && onLine(line) === true) return;
      }
    }
  } finally {
    clearTimeout(timer);
    await reader.cancel().catch(() => {});
    reader.releaseLock();
    await port.close().catch(() => {});
  }
}
