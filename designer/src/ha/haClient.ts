// A small Home Assistant WebSocket client: auth, commands, subscriptions.
// The same protocol the panel speaks, so the designer exercises it too.

type Pending = { resolve: (v: any) => void; reject: (e: Error) => void };

export class HaClient {
  private nextId = 1;
  private pending = new Map<number, Pending>();
  private subs = new Map<number, (event: any) => void>();

  // A plain field rather than a parameter property, so this file also
  // runs under Node's type stripping (the live test uses it directly).
  private ws: WebSocket;

  private constructor(ws: WebSocket) {
    this.ws = ws;
    ws.onmessage = (m) => {
      const msg = JSON.parse(m.data);
      if (msg.type === "result") {
        const p = this.pending.get(msg.id);
        if (!p) return;
        this.pending.delete(msg.id);
        if (msg.success) p.resolve(msg.result);
        else p.reject(new Error(msg.error?.message ?? "command failed"));
      } else if (msg.type === "event") {
        this.subs.get(msg.id)?.(msg.event);
      }
    };
  }

  static connect(url: string, token: string, onClose: () => void): Promise<HaClient> {
    const wsUrl = url.replace(/^https:/, "wss:").replace(/^http:/, "ws:").replace(/\/$/, "") + "/api/websocket";
    return new Promise((resolve, reject) => {
      let ws: WebSocket;
      try { ws = new WebSocket(wsUrl); } catch (e) { reject(e as Error); return; }
      ws.onerror = () => reject(new Error(`cannot reach ${wsUrl}`));
      ws.onmessage = (m) => {
        const msg = JSON.parse(m.data);
        if (msg.type === "auth_required") ws.send(JSON.stringify({ type: "auth", access_token: token }));
        else if (msg.type === "auth_ok") { ws.onclose = onClose; resolve(new HaClient(ws)); }
        else if (msg.type === "auth_invalid") { reject(new Error("token rejected by Home Assistant")); ws.close(); }
      };
    });
  }

  cmd<T = any>(type: string, extra: Record<string, unknown> = {}): Promise<T> {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.ws.send(JSON.stringify({ id, type, ...extra }));
    });
  }

  async subscribe(type: string, extra: Record<string, unknown>, onEvent: (e: any) => void): Promise<() => void> {
    const id = this.nextId; // cmd() takes this id
    this.subs.set(id, onEvent);
    await this.cmd(type, extra);
    return () => {
      this.subs.delete(id);
      this.cmd("unsubscribe_events", { subscription: id }).catch(() => {});
    };
  }

  close() { this.ws.onclose = null; this.ws.close(); }
}
