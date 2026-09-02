import assert from "node:assert/strict";
import WebSocket from "ws";
import {BridgeServer} from "./server.js";
import {HarnessClient,until} from "./transport-client.js";
import * as P from "./index.js";
const origin="http://127.0.0.1:5173";let tests=0;const categories:Record<string,number>={};
async function test(category:string,name:string,fn:()=>Promise<void>){try{await fn();tests++;categories[category]=(categories[category]??0)+1;}catch(e){throw new Error(name,{cause:e});}}
const server=new BridgeServer(origin);const port=await server.start();
const hello=()=>({kind:"hello",version:0,token:server.token,origin,three:{version:"0.185.0",commit:"test"},buildId:"test",requestedCapabilities:["explicit-mirror"],byteOrder:"little"});
async function rejectHello(body:unknown,header=origin,binary=false):Promise<void>{
 const ws=new WebSocket("ws://127.0.0.1:"+port,{headers:{Origin:header}});let closeCode=0;
 ws.on("error",()=>{});ws.on("close",code=>{closeCode=code;});ws.on("open",()=>ws.send(binary?Buffer.from(JSON.stringify(body)):typeof body==="string"?body:JSON.stringify(body)));
 try{await until(()=>ws.readyState===WebSocket.CLOSED);assert.equal(closeCode,1008);}finally{ws.terminate();}
}
try {
 for(const name of ["wrong token","wrong HTTP Origin","body/header mismatch","missing capability","oversized hello","binary hello"])await test("authentication",name,async()=>{
  const h=hello();if(name==="wrong token")h.token="bad";if(name==="body/header mismatch")h.origin="http://localhost:5173";if(name==="missing capability")h.requestedCapabilities=[];
  await rejectHello(name==="oversized hello"?" ".repeat(P.MAX_HELLO_BYTES+1):h,name==="wrong HTTP Origin"?"http://localhost:5173":origin,name==="binary hello");
 });
 await test("authentication","competing controller",async()=>{const c=await HarnessClient.connect(port,origin,server.token);try{await rejectHello(hello());}finally{await c.close();}});
 for(const name of ["post-auth text","unknown binary type","unsupported flags","zero sequence","duplicate sequence","out-of-order sequence"])await test("transport-rejection",name,async()=>{
  const c=await HarnessClient.connect(port,origin,server.token);let code=0;c.socket.on("close",x=>code=x);
  const p=P.encode({type:P.MessageType.Ping,sequence:2n,payload:new Uint8Array()});
  if(name==="unknown binary type")new DataView(p.buffer).setUint16(6,99,true);
  if(name==="unsupported flags")new DataView(p.buffer).setUint32(8,1,true);
  if(name==="zero sequence")new DataView(p.buffer).setBigUint64(16,0n,true);
  if(name==="duplicate sequence")new DataView(p.buffer).setBigUint64(16,1n,true);
  if(name==="out-of-order sequence"){c.socket.send(P.encode({type:P.MessageType.Ping,sequence:9n,payload:new Uint8Array()}));}
  c.socket.send(name==="post-auth text"?"{}":p);
  await until(()=>c.socket.readyState===WebSocket.CLOSED);assert.equal(code,1002);
 });
 await test("continuity","full reconnect and resize acknowledgement",async()=>{
  const acked:bigint[]=[];const first=await HarnessClient.connect(port,origin,server.token,a=>acked.push(a.frame));
  const firstGeneration=first.mirror.generation;
  for(let i=1n;i<=3n;i++){first.frame(P.canonicalState(i,{width:640,height:360,resizeGeneration:1n}));await until(()=>first.acknowledged===Number(i));}
  assert.deepEqual(acked,[1n,2n,3n]);await first.close();assert.equal(first.socket.readyState,WebSocket.CLOSED);
  const second=await HarnessClient.connect(port,origin,server.token);
  assert.notEqual(second.mirror.generation,firstGeneration);
  const authoritative=P.canonicalState(4n,{width:800,height:450,resizeGeneration:2n});
  const endSequence=second.frame(authoritative);assert.equal(endSequence,4n);
  await until(()=>second.acknowledged===1);
  assert.equal(second.lastAck?.sessionGeneration,BigInt(second.mirror.generation!));
  assert.equal(second.lastAck?.sequence,endSequence);assert.equal(second.lastAck?.frame,4n);assert.equal(second.lastAck?.resizeGeneration,2n);
  assert.deepEqual(second.lastState,authoritative);assert.equal(second.mirror.outstanding,0);await second.close();
 });
}finally{await server.close();}
await test("backpressure","real delayed receiver drops oldest complete frames",async()=>{
 const slow=new BridgeServer(origin,100);const p=await slow.start();const accepted:bigint[]=[];
 try{const c=await HarnessClient.connect(p,origin,slow.token,a=>accepted.push(a.frame));
 for(let f=1n;f<=6n;f++)c.frame(P.canonicalState(f,{width:640,height:360,resizeGeneration:1n}));
 await until(()=>c.acknowledged===2);
 assert.deepEqual(accepted,[5n,6n]);assert.equal(c.drops,4);assert.equal(c.invalidAcks,0);assert.equal(c.mirror.outstanding,0);assert.equal(c.lastAck?.sequence,14n);
 await c.close();}finally{await slow.close();}
});
console.log(JSON.stringify({suite:"loopback",status:"PASS",tests,categories,browser_gates:"HUMAN_REQUIRED"}));
