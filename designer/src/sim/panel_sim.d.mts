// Types for the Emscripten module built by sim/build.py.
export interface PanelSimModule {
  _sim_boot(): void;
  _sim_step(): number;
  _sim_framebuffer(): number;
  _sim_pointer(x: number, y: number, down: number): void;
  _sim_event(type: number, entity: number, value: number, fvalue: number): void;
  _sim_store_layout(json: number, err: number, errN: number): number;
  _sim_layout_from_nvs(): number;
  _sim_pool_free(): number;
  _sim_fit_label(text: number, width: number): number;
  _sim_text_width(text: number, px: number): number;
  _malloc(n: number): number;
  _free(p: number): void;
  HEAPU16: Uint16Array;
  UTF8ToString(p: number): string;
  stringToUTF8(s: string, p: number, n: number): void;
  lengthBytesUTF8(s: string): number;
}

export interface PanelSimOptions {
  print?: (s: string) => void;
  printErr?: (s: string) => void;
  onCommand?: (kind: number, index: number, value: number) => void;
}

declare function createPanelSim(opts?: PanelSimOptions): Promise<PanelSimModule>;
export default createPanelSim;
