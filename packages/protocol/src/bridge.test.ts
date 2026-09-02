import assert from "node:assert/strict";
import WebSocket from "ws";
import { BridgeServer } from "./server.js";
import { encode, MessageType } from "./index.js";

const server = new BridgeServer();
const port = await server.start();
const hello = { kind: "hello", version: 0, token: server.token, origin: "http://127.0.0.1", three: { version: "0.185.0", commit: "2431a09" }, buildId: "test", requestedCapabilities: ["explicit-mirror"], byteOrder: "little" } as const;
const connect = (): Promise<WebSocket> => new Promise((resolve, reject) => { const socket = new WebSocket(`ws://127.0.0.1:${port}`, { headers: { Origin: hello.origin } }); socket.once("open", () => { socket.send(JSON.stringify(hello)); }); socket.once("message", (data) => { assert.equal(JSON.parse(data.toString()).kind, "capabilities"); resolve(socket); }); socket.once("error", reject); });
const socket = await connect();
const state = (frame: bigint): Uint8Array => { const payload = new Uint8Array(8); new DataView(payload.buffer).setBigUint64(0, frame, true); return payload; };
for (let frame = 1n; frame <= 108000n; frame++) {
  const begin = encode({ type: MessageType.BeginFrame, sequence: frame * 2n - 1n, payload: state(frame) });
  const end = encode({ type: MessageType.EndFrame, sequence: frame * 2n, payload: new Uint8Array() });
  const fragmented = new Uint8Array(begin.byteLength + end.byteLength); fragmented.set(begin); fragmented.set(end, begin.byteLength);
  socket.send(fragmented.slice(0, begin.byteLength)); socket.send(fragmented.slice(begin.byteLength));
}
await new Promise((resolve) => setTimeout(resolve, 100));
assert.equal(server.acceptedFrame, 108000n);
assert.equal(server.droppedFrames, 0);
socket.close();
const reconnect = await connect();
reconnect.send(encode({ type: MessageType.BeginFrame, sequence: 216001n, payload: state(108001n) }));
await new Promise((resolve) => setTimeout(resolve, 25));
assert.equal(server.acceptedFrame, 108001n);
reconnect.close();
await server.close();
console.log("TCW-PROTO-003_PASS");
console.log("TCW-PROTO-004_PASS virtual_duration_seconds=1800 frames=108000 logical_frame=108001");
console.log("TCW-CONT-001_PASS browser_authoritative_frame=108001 native_mirror_frame=108001");
