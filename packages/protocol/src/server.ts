import { WebSocketServer, WebSocket, type RawData } from "ws";
import { randomBytes, randomUUID } from "node:crypto";
import { authenticate, decode, encodeFrameAccepted, MAX_HELLO_BYTES, MAX_WEBSOCKET_BYTES, MirrorSession, MessageType, VERSION, FEATURE } from "./index.js";
function rawBytes(data:RawData):Buffer { return Array.isArray(data) ? Buffer.concat(data) : Buffer.from(data as ArrayBuffer); }
export class BridgeServer {
  readonly token = randomBytes(24).toString("hex");
  private server:WebSocketServer | undefined; private controller:WebSocket | undefined; private generation = 0n;
  constructor(private readonly allowedOrigin = "http://127.0.0.1:5173", private readonly delayMs = 0) {
    const url = new URL(allowedOrigin);
    if (url.origin !== allowedOrigin || !["127.0.0.1","localhost"].includes(url.hostname) || !["http:","https:"].includes(url.protocol)) throw new Error("invalid allowed origin");
    if (!Number.isFinite(delayMs) || delayMs < 0) throw new Error("invalid processing delay");
  }
  async start():Promise<number> {
    const server = new WebSocketServer({host:"127.0.0.1",port:0,maxPayload:MAX_WEBSOCKET_BYTES}); this.server = server;
    server.on("connection",(socket,request)=>{
      let session:MirrorSession | undefined; let generation = 0n; let timer:ReturnType<typeof setTimeout> | undefined;
      const timeout = setTimeout(()=>socket.close(1008,"hello timeout"),5000);
      const process = ():void => {
        timer = undefined;
        if (!session || socket.readyState !== WebSocket.OPEN) return;
        const item = session.processOne();
        if (item) socket.send(encodeFrameAccepted(generation,item.state,item.sequence,session.droppedFrames));
        if (session.queuedFrames) timer = setTimeout(process,this.delayMs);
      };
      socket.on("error",()=>{ socket.terminate(); });
      socket.on("message",(data,binary)=>{
        if (socket.readyState !== WebSocket.OPEN) return;
        try {
          const bytes = rawBytes(data);
          if (!session) {
            if (binary || bytes.length > MAX_HELLO_BYTES || this.controller) throw new Error("hello rejected");
            authenticate(JSON.parse(bytes.toString()),this.token,this.allowedOrigin,request.headers.origin ?? "");
            clearTimeout(timeout); this.controller = socket; generation = ++this.generation; session = new MirrorSession(randomUUID());
            socket.send(JSON.stringify({kind:"capabilities",version:VERSION,sessionId:session.sessionId,sessionGeneration:String(generation),buildId:"framebridge-tcw004",backend:"test-harness",features:[FEATURE],byteOrder:"little"})); return;
          }
          if (!binary) throw new Error("post-auth text");
          const m = decode(new Uint8Array(bytes)); session.accept(m);
          if (m.type === MessageType.EndFrame && !timer) {
            if (this.delayMs === 0) process(); else timer = setTimeout(process,this.delayMs);
          }
          if (m.type === MessageType.EndSession) { if (timer) clearTimeout(timer); socket.close(1000,"session ended"); }
        } catch { socket.close(session ? 1002 : 1008,"protocol rejected"); }
      });
      socket.once("close",()=>{clearTimeout(timeout);if(timer)clearTimeout(timer);if(this.controller === socket)this.controller=undefined;});
    });
    await new Promise<void>((resolve,reject)=>{server.once("listening",resolve);server.once("error",reject);});
    const address = server.address(); if (!address || typeof address === "string") throw new Error("missing address"); return address.port;
  }
  async close():Promise<void> { const s=this.server;if(!s)return;for(const client of s.clients)client.terminate();await new Promise<void>(resolve=>s.close(()=>resolve())); }
}
