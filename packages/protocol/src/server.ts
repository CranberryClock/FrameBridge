import { WebSocketServer, WebSocket, type RawData } from "ws";
import { randomBytes } from "node:crypto";
import { authenticate, decode, encodeFrameAccepted, MAX_HELLO_BYTES, MAX_WEBSOCKET_BYTES, MirrorSession, MessageType, type Hello } from "./index.js";

function rawBytes(data: RawData): Buffer { return Buffer.concat((Array.isArray(data) ? data : [data]).map((part) => Buffer.from(part as any))); }

export class BridgeServer {
  readonly token = randomBytes(24).toString("hex");
  private readonly server = new WebSocketServer({ host: "127.0.0.1", port: 0, maxPayload: MAX_WEBSOCKET_BYTES });
  private controller: WebSocket | undefined;
  private readonly sessions = new Map<WebSocket, MirrorSession>();
  constructor(private readonly allowedOrigin = "http://127.0.0.1:5173") {}
  async start(): Promise<number> {
    await new Promise<void>((resolve, reject) => { this.server.once("listening", resolve); this.server.once("error", reject); });
    const address = this.server.address(); if (!address || typeof address === "string") throw new Error("ephemeral port unavailable");
    this.server.on("connection", (socket, request) => {
      let authenticated = false; let bootstrapping = true;
      socket.once("message", (data, isBinary) => { try { const raw = rawBytes(data); if (isBinary || raw.byteLength > MAX_HELLO_BYTES || this.controller) throw new Error("session unavailable"); const body = JSON.parse(raw.toString()) as Hello; if ((request.headers.origin ?? "") !== this.allowedOrigin) throw new Error("origin rejected"); const caps = authenticate(body, this.token, this.allowedOrigin); authenticated = true; this.controller = socket; this.sessions.set(socket, new MirrorSession(caps.sessionId)); socket.send(JSON.stringify(caps)); queueMicrotask(() => { bootstrapping = false; }); } catch { socket.close(1008, "authentication rejected"); } });
      socket.on("message", (data, isBinary) => { if (!authenticated || bootstrapping || socket.readyState !== WebSocket.OPEN) return; try { if (!isBinary) throw new Error("post-auth text rejected"); const message = decode(new Uint8Array(rawBytes(data))); const session = this.sessions.get(socket); if (!session) throw new Error("session missing"); session.accept(message); if (message.type === MessageType.EndFrame) { const state = session.processOne(); if (!state) throw new Error("frame queue empty"); socket.send(encodeFrameAccepted(1n, state, session.lastAcceptedSequence, session.droppedFrames)); } } catch (error) { console.error(`protocol rejection: ${(error as Error).message}`); socket.close(1002, "protocol violation"); } });
      socket.once("close", () => { this.sessions.delete(socket); if (this.controller === socket) this.controller = undefined; });
    }); const listening = this.server.address(); return listening && typeof listening !== "string" ? listening.port : 0;
  }
  async close(): Promise<void> { for (const client of this.server.clients) client.terminate(); await new Promise<void>((resolve) => this.server.close(() => resolve())); }
  get acceptedFrame(): bigint { return [...this.sessions.values()][0]?.acceptedFrame ?? 0n; }
  get droppedFrames(): number { return [...this.sessions.values()][0]?.droppedFrames ?? 0; }
}
