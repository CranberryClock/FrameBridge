import WebSocket from "ws";
import {MirrorClient} from "./client.js";
import {FEATURE, type FrameState, type FrameAccepted} from "./index.js";
export async function until(predicate:()=>boolean, timeoutMs=3000):Promise<void> {
  const deadline=performance.now()+timeoutMs;
  while(!predicate()){if(performance.now()>deadline)throw new Error("condition timeout");await new Promise(r=>setTimeout(r,2));}
}
export class HarnessClient {
  readonly socket:WebSocket; readonly mirror:MirrorClient;
  sent=0; acknowledged=0; invalidAcks=0; protocolErrors=0; unexpectedCloses=0; drops=0;
  lastAck:FrameAccepted | undefined; lastState:FrameState | undefined;
  private plannedClose=false;
  private constructor(port:number,origin:string,token:string,onAck?:(ack:FrameAccepted,state:FrameState)=>void) {
    this.socket=new WebSocket("ws://127.0.0.1:"+port,{headers:{Origin:origin}});
    this.mirror=new MirrorClient(bytes=>this.socket.send(bytes));
    this.socket.on("open",()=>this.socket.send(JSON.stringify({kind:"hello",version:0,token,origin,three:{version:"0.185.0",commit:"2431a09"},buildId:"tcw004-harness-client",requestedCapabilities:[FEATURE],byteOrder:"little"})));
    this.socket.on("message",(data,binary)=>{
      try {
        if(!binary){this.mirror.authenticate(data.toString());return;}
        const {ack,state}=this.mirror.acknowledge(new Uint8Array(data as Buffer));
        this.acknowledged++;this.lastAck=ack;this.lastState=state;this.drops=ack.droppedFrames;onAck?.(ack,state);
      }catch{this.invalidAcks++;this.protocolErrors++;this.socket.close(1002,"client validation");}
    });
    this.socket.on("error",()=>{this.protocolErrors++;});
    this.socket.on("close",()=>{if(!this.plannedClose)this.unexpectedCloses++;this.mirror.disconnect(false);});
  }
  static async connect(port:number,origin:string,token:string,onAck?:(ack:FrameAccepted,state:FrameState)=>void):Promise<HarnessClient> {
    const c=new HarnessClient(port,origin,token,onAck);
    try{await until(()=>c.mirror.authenticated||c.socket.readyState===WebSocket.CLOSED);if(!c.mirror.authenticated)throw new Error("authentication failed");return c;}catch(e){c.socket.terminate();throw e;}
  }
  frame(state:FrameState):bigint|undefined {const seq=this.mirror.frame(state);if(seq!==undefined)this.sent++;return seq;}
  async close(clean=true):Promise<void>{this.plannedClose=true;if(this.socket.readyState===WebSocket.OPEN){this.mirror.disconnect(clean);this.socket.close(1000);}await until(()=>this.socket.readyState===WebSocket.CLOSED);}
}
