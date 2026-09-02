import { authenticate, decode, encode, MessageType, MirrorState } from "./index.js";
const fixture = encode({ type: MessageType.BeginFrame, sequence: 1n, objectId: 7n, payload: new Uint8Array([1, 2, 3, 4]) });
if (fixture.byteLength !== 40 || decode(fixture).sequence !== 1n) throw new Error("golden fixture failed");
for (const bad of [fixture.slice(0, 10), new Uint8Array(fixture).fill(0, 0, 4)]) { try { decode(bad); throw new Error("malformed input accepted"); } catch (error) { if ((error as Error).message === "malformed input accepted") throw error; } }
const caps = authenticate({ kind: "hello", version: 0, token: "t", origin: "https://cube.local", three: { version: "0.185.0", commit: "2431a09" }, buildId: "test", requestedCapabilities: [], byteOrder: "little" }, "t", "https://cube.local");
if (caps.kind !== "capabilities") throw new Error("auth failed");
const state = new MirrorState(); const payload = new Uint8Array(8); new DataView(payload.buffer).setBigUint64(0, 1n, true); state.accept(decode(encode({ type: MessageType.BeginFrame, sequence: 1n, payload }))); state.accept(decode(encode({ type: MessageType.EndFrame, sequence: 2n, payload: new Uint8Array() }))); if (state.acceptedFrame !== 1n) throw new Error("continuity failed");
console.log("TCW-PROTOCOL_FIXTURE_PASS");
