import { encode, decodeFrameAccepted, encodeFrameState, encodeResize, MessageType, validateCapabilities, requireValid, type Capabilities, type FrameState, type FrameAccepted } from "./index.js";

// Latest-only browser scheduling. Reconnect resets session-local transmission state only.
export class MirrorClient {
  private caps:Capabilities | undefined;
  private sequence = 0n; private lastSent = 0n; private lastResize = 0n;
  private acceptedSequence = 0n; private acceptedFrame = 0n; private dropped = 0;
  private pending = new Map<bigint,FrameState>();
  constructor(private readonly send:(bytes:Uint8Array)=>void) {}
  authenticate(text:string): Capabilities {
    requireValid(!this.caps,"repeated capabilities");
    const caps = validateCapabilities(JSON.parse(text));
    this.caps = caps; this.sequence = 0n; this.lastSent = 0n; this.lastResize = 0n;
    this.acceptedSequence = 0n; this.acceptedFrame = 0n; this.dropped = 0; this.pending.clear();
    this.message(MessageType.BeginSession,new Uint8Array());
    return caps;
  }
  private message(type:MessageType,payload:Uint8Array):bigint {
    const sequence = ++this.sequence; this.send(encode({type,sequence,payload})); return sequence;
  }
  frame(state:FrameState):bigint | undefined {
    if (!this.caps || state.frame <= this.lastSent) return undefined;
    requireValid(this.pending.size < 1024,"outstanding frame quota");
    if (state.resizeGeneration !== this.lastResize) {
      this.message(MessageType.Resize,encodeResize(state)); this.lastResize = state.resizeGeneration;
    }
    this.message(MessageType.BeginFrame,encodeFrameState(state));
    const endSequence = this.sequence + 1n;
    this.pending.set(endSequence,state);
    this.message(MessageType.EndFrame,new Uint8Array()); this.lastSent = state.frame;
    return endSequence;
  }
  acknowledge(bytes:Uint8Array):{ack:FrameAccepted;state:FrameState} {
    requireValid(this.caps,"ack before authentication");
    const ack = decodeFrameAccepted(bytes), state = this.pending.get(ack.sequence);
    requireValid(ack.sessionGeneration === BigInt(this.caps.sessionGeneration),"wrong acknowledgement session");
    requireValid(state && ack.frame === state.frame && ack.resizeGeneration === state.resizeGeneration,"unknown/mismatched acknowledgement");
    requireValid(ack.sequence > this.acceptedSequence && ack.frame > this.acceptedFrame,"replayed acknowledgement");
    requireValid(ack.droppedFrames >= this.dropped,"decreasing dropped count");
    // FIFO processing means unacknowledged predecessors were dropped as complete frames.
    const skipped = [...this.pending.keys()].filter(s=>s < ack.sequence);
    requireValid(skipped.length <= ack.droppedFrames-this.dropped,"unexplained missing acknowledgement");
    for (const seq of skipped) this.pending.delete(seq);
    this.pending.delete(ack.sequence); this.acceptedSequence = ack.sequence; this.acceptedFrame = ack.frame; this.dropped = ack.droppedFrames;
    return {ack,state};
  }
  disconnect(clean = true):void {
    if (clean && this.caps) this.message(MessageType.EndSession,new Uint8Array());
    this.caps = undefined; this.pending.clear(); this.lastSent = 0n; this.sequence = 0n; this.lastResize = 0n;
  }
  get authenticated():boolean { return !!this.caps; }
  get outstanding():number { return this.pending.size; }
  get generation():string | undefined { return this.caps?.sessionGeneration; }
}
export function logicalFrameAt(elapsedMs:number):bigint { return BigInt(Math.max(1,Math.floor(elapsedMs*60/1000))); }
