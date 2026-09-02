import { WebSocketServer, WebSocket } from "ws";
import { randomBytes } from "node:crypto";
import { authenticate, decode, MirrorState, type Hello } from "./index.js";

export class BridgeServer {
  readonly token = randomBytes(24).toString("hex");
  private readonly state = new MirrorState();
  private readonly server = new WebSocketServer({ host: "127.0.0.1", port: 0 });
  private port = 0;

  async start(): Promise<number> {
    await new Promise<void>((resolve, reject) => { this.server.once("listening", resolve); this.server.once("error", reject); });
    const address = this.server.address();
    if (!address || typeof address === "string") throw new Error("bridge did not receive an ephemeral port");
    this.port = address.port;
    this.server.on("connection", (socket, request) => {
      let authenticated = false;
      socket.once("message", (data, isBinary) => {
        try {
          if (isBinary) throw new Error("hello must be JSON");
          const hello = JSON.parse(data.toString()) as Hello;
          const caps = authenticate(hello, this.token, request.headers.origin ?? "");
          authenticated = true;
          socket.send(JSON.stringify(caps));
        } catch { socket.close(1008, "authentication rejected"); }
      });
      socket.on("message", (data, isBinary) => {
        if (!authenticated || !isBinary || socket.readyState !== WebSocket.OPEN) return;
        try { this.state.accept(decode(new Uint8Array(data as Buffer))); }
        catch { socket.close(1002, "protocol violation"); }
      });
    });
    return this.port;
  }

  async close(): Promise<void> { for (const client of this.server.clients) client.terminate(); await new Promise<void>((resolve) => this.server.close(() => resolve())); }
  get acceptedFrame(): bigint { return this.state.acceptedFrame; }
  get droppedFrames(): number { return this.state.droppedFrames; }
}
