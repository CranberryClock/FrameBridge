export const MAGIC = 0x30574246;
export { MirrorClient, logicalFrameAt } from "./client.js";
export const VERSION = 0;
export const HEADER_BYTES = 36;
export const MAX_HELLO_BYTES = 8192;
export const MAX_PAYLOAD_BYTES = 1024 * 1024;
export const MAX_WEBSOCKET_BYTES = HEADER_BYTES + MAX_PAYLOAD_BYTES;
export const MAX_LIVE_RESOURCES = 256;
export const MAX_DECLARED_RESOURCE_BYTES = 64 * 1024 * 1024;
export const MAX_DRAWS_PER_FRAME = 4096;
export const MAX_MESSAGES_PER_FRAME = 8192;
export const MAX_QUEUED_FRAMES = 2;
export const MAX_DIMENSION = 8192;
export const SUPPORTED_FLAGS = 0;
export const FEATURE = "explicit-mirror";
const U64_MAX = (1n << 64n) - 1n;
export enum MessageType { BeginSession = 1, EndSession, Ping, Error, SetRtxMode, CreateBuffer = 16, DestroyResource, BeginFrame = 32, Draw, EndFrame, Resize, FrameAccepted = 48 }
export const PAYLOAD_SIZES: ReadonlyMap<number, number> = new Map([[1,0],[2,0],[3,0],[4,4],[5,1],[16,8],[17,0],[32,48],[33,16],[34,0],[35,16],[48,40]]);
export type BinaryMessage = Readonly<{ type: MessageType; flags?: number; sequence: bigint; objectId?: bigint; payload: Uint8Array }>;
export function requireValid(ok: unknown, reason: string): asserts ok { if (!ok) throw new Error(reason); }
export function checksum(bytes: Uint8Array): number { let h = 0x811c9dc5; for (const b of bytes) h = Math.imul(h ^ b, 0x01000193) >>> 0; return h; }
export function validateMessage(m: BinaryMessage): void {
  requireValid(PAYLOAD_SIZES.has(m.type), "unknown type");
  requireValid((m.flags ?? 0) === 0, "unsupported flags");
  requireValid(m.sequence > 0n && m.sequence <= U64_MAX, "invalid sequence");
  const id = m.objectId ?? 0n;
  requireValid(id >= 0n && id <= U64_MAX && (![16,17].includes(m.type) || id > 0n), "illegal object id");
  requireValid(m.payload.byteLength <= MAX_PAYLOAD_BYTES, "maximum payload");
  requireValid(m.payload.byteLength === PAYLOAD_SIZES.get(m.type), "invalid fixed payload");
}
export function encode(m: BinaryMessage): Uint8Array {
  validateMessage(m);
  const bytes = new Uint8Array(HEADER_BYTES + m.payload.length), v = new DataView(bytes.buffer);
  v.setUint32(0,MAGIC,true); v.setUint16(4,VERSION,true); v.setUint16(6,m.type,true);
  v.setUint32(8,m.flags ?? 0,true); v.setUint32(12,m.payload.length,true);
  v.setBigUint64(16,m.sequence,true); v.setBigUint64(24,m.objectId ?? 0n,true);
  v.setUint32(32,checksum(m.payload),true); bytes.set(m.payload,HEADER_BYTES); return bytes;
}
export function decode(bytes: Uint8Array): BinaryMessage {
  requireValid(bytes.length >= HEADER_BYTES,"truncated header");
  const v = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength), n = v.getUint32(12,true);
  requireValid(v.getUint32(0,true) === MAGIC,"wrong magic"); requireValid(v.getUint16(4,true) === VERSION,"wrong version");
  requireValid(n <= MAX_PAYLOAD_BYTES && n + HEADER_BYTES === bytes.length,"invalid payload length");
  const m = { type: v.getUint16(6,true) as MessageType, flags:v.getUint32(8,true), sequence:v.getBigUint64(16,true), objectId:v.getBigUint64(24,true), payload:bytes.slice(HEADER_BYTES) };
  validateMessage(m); requireValid(checksum(m.payload) === v.getUint32(32,true),"checksum mismatch"); return m;
}
export type Hello = { kind:"hello"; version:0; token:string; origin:string; three:{version:string;commit:string}; buildId:string; requestedCapabilities:string[]; byteOrder:"little" };
export type Capabilities = { kind:"capabilities"; version:0; sessionId:string; sessionGeneration:string; buildId:string; backend:"test-harness"|"native-dawn"; features:string[]; byteOrder:"little" };
function record(x: unknown): Record<string, unknown> { requireValid(typeof x === "object" && x !== null && !Array.isArray(x),"invalid object"); return x as Record<string,unknown>; }
function nonempty(x: unknown): x is string { return typeof x === "string" && x.length > 0 && x.length <= 256; }
export function validateCapabilities(value: unknown): Capabilities {
  const c = record(value);
  requireValid(c.kind === "capabilities" && c.version === VERSION && nonempty(c.sessionId) && nonempty(c.buildId),"invalid capabilities");
  requireValid((c.backend === "test-harness" || c.backend === "native-dawn") && c.byteOrder === "little" && Array.isArray(c.features) && c.features.every(x => typeof x === "string") && c.features.includes(FEATURE),"unsupported capabilities");
  requireValid(typeof c.sessionGeneration === "string" && /^[1-9][0-9]{0,19}$/.test(c.sessionGeneration) && BigInt(c.sessionGeneration) <= U64_MAX,"invalid session generation");
  return c as Capabilities;
}
export function authenticate(value: unknown, token: string, origin: string, headerOrigin: string): void {
  const h = record(value), three = record(h.three);
  requireValid(new TextEncoder().encode(JSON.stringify(h)).length <= MAX_HELLO_BYTES,"oversized hello");
  requireValid(h.kind === "hello" && h.version === VERSION && h.byteOrder === "little","invalid hello");
  requireValid(h.token === token && h.origin === origin && headerOrigin === origin,"authentication rejected");
  requireValid(nonempty(h.buildId) && nonempty(three.version) && nonempty(three.commit),"invalid client identity");
  requireValid(Array.isArray(h.requestedCapabilities) && h.requestedCapabilities.length > 0 && h.requestedCapabilities.every(x => x === FEATURE),"missing/unsupported requested capability");
}
export type ResizeState = Readonly<{ width:number; height:number; resizeGeneration:bigint }>;
export type FrameState = ResizeState & Readonly<{ frame:bigint; simulationTime:number; rotationX:number; rotationY:number; cameraZ:number }>;
function validateDimensions(s: ResizeState): void {
  requireValid(Number.isInteger(s.width) && Number.isInteger(s.height) && s.width > 0 && s.height > 0 && s.width <= MAX_DIMENSION && s.height <= MAX_DIMENSION,"invalid resize dimensions");
  requireValid(s.resizeGeneration > 0n && s.resizeGeneration <= U64_MAX,"invalid resize generation");
}
export function encodeResize(s: ResizeState): Uint8Array { validateDimensions(s); const p = new Uint8Array(16), v = new DataView(p.buffer); v.setUint32(0,s.width,true); v.setUint32(4,s.height,true); v.setBigUint64(8,s.resizeGeneration,true); return p; }
export function decodeResize(p: Uint8Array): ResizeState { requireValid(p.length === 16,"invalid resize payload"); const v = new DataView(p.buffer,p.byteOffset,p.byteLength); const s = {width:v.getUint32(0,true),height:v.getUint32(4,true),resizeGeneration:v.getBigUint64(8,true)}; validateDimensions(s); return s; }
export function canonicalState(frame:bigint, viewport:ResizeState): FrameState { return {...viewport, frame, simulationTime:Number(frame)/60, rotationX:Math.fround(Number(frame)*.01), rotationY:Math.fround(Number(frame)*.013), cameraZ:3}; }
export function encodeFrameState(s: FrameState): Uint8Array {
  validateDimensions(s); requireValid(s.frame > 0n && s.frame <= U64_MAX && [s.simulationTime,s.rotationX,s.rotationY,s.cameraZ].every(Number.isFinite) && s.simulationTime >= 0,"invalid frame state");
  const p = new Uint8Array(48), v = new DataView(p.buffer);
  v.setBigUint64(0,s.frame,true); v.setFloat64(8,s.simulationTime,true); v.setFloat32(16,s.rotationX,true); v.setFloat32(20,s.rotationY,true); v.setFloat32(24,s.cameraZ,true); v.setUint32(28,s.width,true); v.setUint32(32,s.height,true); v.setBigUint64(36,s.resizeGeneration,true); return p;
}
export function decodeFrameState(p: Uint8Array): FrameState {
  requireValid(p.length === 48,"invalid frame payload"); const v = new DataView(p.buffer,p.byteOffset,p.length);
  const s = {frame:v.getBigUint64(0,true),simulationTime:v.getFloat64(8,true),rotationX:v.getFloat32(16,true),rotationY:v.getFloat32(20,true),cameraZ:v.getFloat32(24,true),width:v.getUint32(28,true),height:v.getUint32(32,true),resizeGeneration:v.getBigUint64(36,true)};
  encodeFrameState(s); requireValid(v.getUint32(44,true) === 0,"frame reserved bytes"); return s;
}
export type CompleteFrame = Readonly<{ state:FrameState; sequence:bigint }>;
export class MirrorSession {
  private phase: "awaiting-begin"|"active"|"closed" = "awaiting-begin";
  private sequence = 0n; private lastFrame = 0n; private accepted = 0n;
  private open: FrameState | undefined; private viewport: ResizeState | undefined;
  private queue: CompleteFrame[] = []; private live = new Map<bigint,bigint>();
  private bytes = 0n; private draws = 0; private messages = 0; private drops = 0;
  constructor(readonly sessionId:string = crypto.randomUUID()) {}
  accept(m: BinaryMessage): void {
    validateMessage(m);
    requireValid(this.phase !== "closed","session ended"); requireValid(m.sequence > this.sequence,"out of order sequence");
    requireValid(m.type !== MessageType.FrameAccepted && m.type !== MessageType.Error,"server-only message");
    if (m.type === MessageType.BeginSession) { requireValid(this.phase === "awaiting-begin","duplicate BeginSession"); this.phase = "active"; this.sequence = m.sequence; return; }
    requireValid(this.phase === "active","data before BeginSession");
    if (this.open) requireValid(this.messages + 1 <= MAX_MESSAGES_PER_FRAME,"message quota");
    const id = m.objectId ?? 0n;
    switch (m.type) {
      case MessageType.CreateBuffer: {
        requireValid(!this.open,"resource inside frame");
        const size = new DataView(m.payload.buffer,m.payload.byteOffset).getBigUint64(0,true);
        requireValid(!this.live.has(id),"duplicate resource"); requireValid(this.live.size < MAX_LIVE_RESOURCES,"live-resource quota");
        requireValid(size > 0n && size <= BigInt(MAX_DECLARED_RESOURCE_BYTES)-this.bytes,"declared-resource-byte quota");
        this.live.set(id,size); this.bytes += size; break;
      }
      case MessageType.DestroyResource: {
        requireValid(!this.open,"resource inside frame"); const size = this.live.get(id); requireValid(size !== undefined,"destroy unknown resource"); this.live.delete(id); this.bytes -= size; break;
      }
      case MessageType.Resize: {
        requireValid(!this.open,"resize inside frame"); const s = decodeResize(m.payload);
        requireValid(!this.viewport || s.resizeGeneration > this.viewport.resizeGeneration,"out of order resize generation"); this.viewport = s; break;
      }
      case MessageType.BeginFrame: {
        requireValid(!this.open,"nested BeginFrame"); const s = decodeFrameState(m.payload);
        requireValid(s.frame > this.lastFrame,"out of order frame");
        requireValid(this.viewport && s.width === this.viewport.width && s.height === this.viewport.height && s.resizeGeneration === this.viewport.resizeGeneration,"frame viewport mismatch");
        this.open = s; this.draws = 0; this.messages = 0; break;
      }
      case MessageType.Draw: requireValid(this.open,"Draw outside frame"); requireValid(this.draws < MAX_DRAWS_PER_FRAME,"draw quota"); this.draws++; break;
      case MessageType.EndFrame:
        requireValid(this.open,"EndFrame without BeginFrame");
        if (this.queue.length === MAX_QUEUED_FRAMES) { this.queue.shift(); this.drops++; }
        this.queue.push({state:this.open,sequence:m.sequence}); this.lastFrame = this.open.frame; this.open = undefined; this.messages = 0; break;
      case MessageType.EndSession: requireValid(!this.open,"EndSession while frame open"); this.phase = "closed"; this.live.clear(); this.bytes = 0n; this.queue = []; break;
      case MessageType.SetRtxMode: requireValid(m.payload[0] === 0,"invalid RTX-mode value; renderer unavailable"); break;
      case MessageType.Ping: break;
      default: throw new Error("illegal message");
    }
    this.sequence = m.sequence; if (this.open) this.messages++;
  }
  processOne(): CompleteFrame | undefined { const item = this.queue.shift(); if (item) this.accepted = item.state.frame; return item; }
  get acceptedFrame():bigint { return this.accepted; }
  get lastAcceptedSequence():bigint { return this.sequence; }
  get droppedFrames():number { return this.drops; }
  get queuedFrames():number { return this.queue.length; }
  get declaredResourceBytes():number { return Number(this.bytes); }
}
export type FrameAccepted = Readonly<{sessionGeneration:bigint; frame:bigint; sequence:bigint; droppedFrames:number; status:number; resizeGeneration:bigint}>;
export function encodeFrameAccepted(generation:bigint, state:FrameState, sequence:bigint, dropped:number): Uint8Array {
  const p = new Uint8Array(40), v = new DataView(p.buffer); v.setBigUint64(0,generation,true); v.setBigUint64(8,state.frame,true); v.setBigUint64(16,sequence,true); v.setUint32(24,dropped,true); v.setUint32(28,0,true); v.setBigUint64(32,state.resizeGeneration,true); return encode({type:MessageType.FrameAccepted,sequence,payload:p});
}
export function decodeFrameAccepted(bytes:Uint8Array): FrameAccepted {
  const m = decode(bytes); requireValid(m.type === MessageType.FrameAccepted,"unexpected acknowledgement type");
  const v = new DataView(m.payload.buffer,m.payload.byteOffset,m.payload.length);
  const a = {sessionGeneration:v.getBigUint64(0,true),frame:v.getBigUint64(8,true),sequence:v.getBigUint64(16,true),droppedFrames:v.getUint32(24,true),status:v.getUint32(28,true),resizeGeneration:v.getBigUint64(32,true)};
  requireValid(a.sequence === m.sequence && a.status === 0 && a.sessionGeneration > 0n && a.frame > 0n && a.resizeGeneration > 0n,"invalid acknowledgement fields"); return a;
}
