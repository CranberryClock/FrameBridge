import { encode, decodeFrameAccepted, decodeNativeImage, decodeTextureAccepted, encodeFrameState, encodeResize, encodeTextureUpload, MessageType, validateCapabilities, requireValid, type Capabilities, type FrameState, type FrameAccepted, type NativeImage, type TextureAccepted } from "./index.js";

// Latest-only browser scheduling. Reconnect resets session-local transmission state only.
export class MirrorClient {
  private caps:Capabilities | undefined;
  private sequence = 0n; private lastSent = 0n; private lastResize = 0n;
  private acceptedSequence = 0n; private acceptedFrame = 0n; private dropped = 0;
  private acceptedState:FrameState | undefined;
  private textureRevision=0n; private textureAccepted=0n;
  private pending = new Map<bigint,FrameState>();
  constructor(private readonly send:(bytes:Uint8Array)=>void) {}
  authenticate(text:string): Capabilities {
    requireValid(!this.caps,"repeated capabilities");
    const caps = validateCapabilities(JSON.parse(text));
    this.caps = caps; this.sequence = 0n; this.lastSent = 0n; this.lastResize = 0n;
    this.acceptedSequence = 0n; this.acceptedFrame = 0n; this.dropped = 0; this.acceptedState=undefined; this.pending.clear();
    this.textureRevision = 0n; this.textureAccepted = 0n;
    this.message(MessageType.BeginSession,new Uint8Array());
    return caps;
  }
  uploadTexture(pixels:Uint8Array, revision:bigint):bigint { requireValid(this.caps,"texture before authentication"); requireValid(revision>this.textureRevision,"non-monotonic texture revision"); this.textureRevision=revision; const encoded=encodeTextureUpload({sessionGeneration:BigInt(this.caps.sessionGeneration),resourceId:1n,revision,width:256,height:256,format:1,pixels},this.sequence+1n); return this.message(MessageType.TextureUpload,encoded.slice(36),1n); }
  acknowledgeTexture(bytes:Uint8Array):TextureAccepted { requireValid(this.caps,"texture acknowledgement before authentication"); const a=decodeTextureAccepted(bytes); requireValid(a.sessionGeneration===BigInt(this.caps.sessionGeneration)&&a.resourceId===1n&&a.revision>=this.textureAccepted&&a.width===256&&a.height===256,"invalid texture acknowledgement"); this.textureAccepted=a.revision; return a; }
  private message(type:MessageType,payload:Uint8Array,objectId=0n):bigint {
    const sequence = ++this.sequence; this.send(encode({type,sequence,objectId,payload})); return sequence;
  }
  frame(state:FrameState):bigint | undefined {
    if (!this.caps || state.frame <= this.lastSent) return undefined;
    // The native renderer may be slower than the browser cadence. Keep a
    // bounded client window and skip this sample; the next animation tick
    // carries the current browser-authoritative state.
    if (this.pending.size >= 64) return undefined;
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
    requireValid(skipped.length === ack.droppedFrames-this.dropped,"cumulative drop count does not match skipped frames");
    for (const seq of skipped) this.pending.delete(seq);
    this.pending.delete(ack.sequence); this.acceptedSequence = ack.sequence; this.acceptedFrame = ack.frame; this.dropped = ack.droppedFrames; this.acceptedState=state;
    return {ack,state};
  }
  image(bytes:Uint8Array):NativeImage {
    requireValid(this.caps,"image before authentication"); const image=decodeNativeImage(bytes);
    requireValid(image.sessionGeneration===BigInt(this.caps.sessionGeneration),"wrong image session");
    const state=this.acceptedState?.frame===image.browserFrame?this.acceptedState:[...this.pending.values()].find(s=>s.frame===image.browserFrame);
    requireValid(state && state.resizeGeneration===image.resizeGeneration && state.width===image.width && state.height===image.height && image.textureRevision===this.textureAccepted,"stale or mismatched native image");
    return image;
  }
  consume(image:NativeImage):void {
    requireValid(this.caps && image.sessionGeneration===BigInt(this.caps.sessionGeneration),"consume wrong session");
    const p=new Uint8Array(24),v=new DataView(p.buffer);v.setBigUint64(0,image.sessionGeneration,true);v.setBigUint64(8,image.nativeFrame,true);v.setBigUint64(16,image.resizeGeneration,true);this.message(MessageType.ImageConsumed,p);
  }
  disconnect(clean = true):void {
    if (clean && this.caps) this.message(MessageType.EndSession,new Uint8Array());
    this.caps = undefined; this.pending.clear(); this.acceptedState=undefined; this.lastSent = 0n; this.sequence = 0n; this.lastResize = 0n; this.textureRevision=0n; this.textureAccepted=0n;
  }
  get authenticated():boolean { return !!this.caps; }
  get outstanding():number { return this.pending.size; }
  get generation():string | undefined { return this.caps?.sessionGeneration; }
  get acceptedTextureRevision():bigint { return this.textureAccepted; }
}
export function logicalFrameAt(elapsedMs:number):bigint { return BigInt(Math.max(1,Math.floor(elapsedMs*60/1000))); }
