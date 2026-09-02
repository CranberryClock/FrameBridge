import { randomUUID } from "node:crypto";

export const MAGIC = 0x30574246;
export const VERSION = 0;
export const HEADER_BYTES = 36;
export const MAX_PAYLOAD_BYTES = 1024 * 1024;

export enum MessageType { BeginSession = 1, EndSession, Ping, Error, SetRtxMode, CreateBuffer = 16, DestroyResource, BeginFrame = 32, Draw, EndFrame, Resize, FrameAccepted = 48 }
export type BinaryMessage = Readonly<{ type: MessageType; flags?: number; sequence: bigint; objectId?: bigint; payload: Uint8Array }>;

function checksum(bytes: Uint8Array): number { let h = 0x811c9dc5; for (const byte of bytes) { h ^= byte; h = Math.imul(h, 0x01000193) >>> 0; } return h >>> 0; }
export function encode(message: BinaryMessage): Uint8Array {
  if (message.payload.byteLength > MAX_PAYLOAD_BYTES) throw new Error("payload exceeds quota");
  const out = new Uint8Array(HEADER_BYTES + message.payload.byteLength); const view = new DataView(out.buffer);
  view.setUint32(0, MAGIC, true); view.setUint16(4, VERSION, true); view.setUint16(6, message.type, true); view.setUint32(8, message.flags ?? 0, true); view.setUint32(12, message.payload.byteLength, true); view.setBigUint64(16, message.sequence, true); view.setBigUint64(24, message.objectId ?? 0n, true); view.setUint32(32, checksum(message.payload), true); out.set(message.payload, HEADER_BYTES); return out;
}
export function decode(bytes: Uint8Array): BinaryMessage {
  if (bytes.byteLength < HEADER_BYTES) throw new Error("truncated header"); const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.getUint32(0, true) !== MAGIC) throw new Error("wrong magic"); if (view.getUint16(4, true) !== VERSION) throw new Error("wrong version"); const payloadBytes = view.getUint32(12, true); if (payloadBytes > MAX_PAYLOAD_BYTES || HEADER_BYTES + payloadBytes !== bytes.byteLength) throw new Error("invalid payload length");
  const payload = bytes.slice(HEADER_BYTES); if (view.getUint32(32, true) !== checksum(payload)) throw new Error("checksum mismatch"); return { type: view.getUint16(6, true) as MessageType, flags: view.getUint32(8, true), sequence: view.getBigUint64(16, true), objectId: view.getBigUint64(24, true), payload };
}

export type Hello = Readonly<{ kind: "hello"; version: 0; token: string; origin: string; three: { version: string; commit: string }; buildId: string; requestedCapabilities: string[]; byteOrder: "little" }>;
export type Capabilities = Readonly<{ kind: "capabilities"; version: 0; sessionId: string; buildId: string; adapter: string; backend: "D3D12"; features: string[] }>;
export function authenticate(hello: Hello, expectedToken: string, allowedOrigin: string): Capabilities {
  if (hello.kind !== "hello" || hello.version !== VERSION || hello.byteOrder !== "little") throw new Error("invalid hello");
  if (hello.token !== expectedToken) throw new Error("invalid token"); if (hello.origin !== allowedOrigin) throw new Error("origin rejected");
  return { kind: "capabilities", version: VERSION, sessionId: randomUUID(), buildId: "framebridge-dev", adapter: "pending", backend: "D3D12", features: ["explicit-mirror"] };
}

export class MirrorState {
  private lastFrame = -1n; private liveIds = new Set<bigint>(); private queuedFrames = 0; droppedFrames = 0;
  accept(message: BinaryMessage): void { if (message.sequence < 1n) throw new Error("invalid sequence"); if (message.type === MessageType.BeginFrame && message.payload.byteLength < 8) throw new Error("frame state truncated"); if (message.type === MessageType.CreateBuffer) { if (this.liveIds.has(message.objectId ?? 0n)) throw new Error("duplicate id"); this.liveIds.add(message.objectId ?? 0n); } if (message.type === MessageType.DestroyResource) { if (!this.liveIds.delete(message.objectId ?? 0n)) throw new Error("use after destroy"); } if (message.type === MessageType.BeginFrame) { const frame = new DataView(message.payload.buffer, message.payload.byteOffset, message.payload.byteLength).getBigUint64(0, true); if (frame <= this.lastFrame) throw new Error("out of order frame"); if (this.queuedFrames >= 2) { this.droppedFrames++; return; } this.lastFrame = frame; this.queuedFrames++; } if (message.type === MessageType.EndFrame && this.queuedFrames > 0) this.queuedFrames--; }
  get acceptedFrame(): bigint { return this.lastFrame; }
}
