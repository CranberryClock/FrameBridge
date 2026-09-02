import assert from "node:assert/strict";
import {readFileSync,readdirSync} from "node:fs";
import * as P from "./index.js";
import {MirrorClient,logicalFrameAt} from "./client.js";
let tests=0; const categories:Record<string,number>={};
function test(category:string,name:string,fn:()=>void):void {try{fn();tests++;categories[category]=(categories[category]??0)+1;}catch(e){throw new Error(name,{cause:e});}}
const fixtureDir=new URL("../fixtures/",import.meta.url);
const hex=(s:string)=>new Uint8Array(Buffer.from(s.trim()==="-"?"":s.trim(),"hex"));
const specs=readFileSync(new URL("canonical.tsv",fixtureDir),"utf8").trim().split(/\r?\n/);
for(const [name,m] of Object.entries({unknown:{type:99,sequence:1n,payload:new Uint8Array()},flags:{type:3,flags:1,sequence:1n,payload:new Uint8Array()},zero:{type:3,sequence:0n,payload:new Uint8Array()},object:{type:16,sequence:1n,objectId:0n,payload:new Uint8Array(8)},buffer:{type:16,sequence:1n,objectId:1n,payload:new Uint8Array(7)},maximum:{type:3,sequence:1n,payload:new Uint8Array(P.MAX_PAYLOAD_BYTES+1)}}))test("encoder-rejections",name,()=>assert.throws(()=>P.encode(m)));
test("fixtures","all supported types represented",()=>assert.equal(specs.length,P.PAYLOAD_SIZES.size));
for(const row of specs) {
 const [name,t,f,s,id,p]=row.split(/\s+/);
 test("fixtures",name!,()=>{
   const payload=hex(p!),fixture=hex(readFileSync(new URL(name!,fixtureDir),"utf8"));
   const fields={type:Number(t) as P.MessageType,flags:Number(f),sequence:BigInt(s!),objectId:BigInt(id!),payload};
   assert.deepEqual(P.encode(fields),fixture);
   const actual=P.decode(fixture);assert.deepEqual(actual,fields);
 });
}
for(const name of readdirSync(fixtureDir).filter(n=>n.startsWith("malformed-")))test("malformed",name,()=>assert.throws(()=>P.decode(hex(readFileSync(new URL(name,fixtureDir),"utf8")))));
const M=P.MessageType,empty=new Uint8Array(),vp={width:640,height:360,resizeGeneration:1n};
const frame=(f=1n)=>P.canonicalState(f,vp);
const buffer=(n:bigint)=>{const p=new Uint8Array(8);new DataView(p.buffer).setBigUint64(0,n,true);return p;};
function session(active=true) {
 const value=new P.MirrorSession("test");let seq=0n;
 const send=(type:P.MessageType,payload:Uint8Array=empty,id=0n)=>value.accept({type,sequence:++seq,objectId:id,payload});
 if(active){send(M.BeginSession);send(M.Resize,P.encodeResize(vp));}
 return {value,send};
}
function rejects(name:string,fn:(s:ReturnType<typeof session>)=>void,active=true):void {test("session",name,()=>fn(session(active)));}
rejects("duplicate BeginSession",s=>assert.throws(()=>s.send(M.BeginSession)));
rejects("data before BeginSession",s=>assert.throws(()=>s.send(M.BeginFrame,P.encodeFrameState(frame()))),false);
rejects("duplicate/out-of-order sequence",s=>{assert.throws(()=>s.value.accept({type:M.Ping,sequence:2n,payload:empty}));assert.throws(()=>s.value.accept({type:M.Ping,sequence:1n,payload:empty}));});
rejects("nested BeginFrame",s=>{s.send(M.BeginFrame,P.encodeFrameState(frame()));assert.throws(()=>s.send(M.BeginFrame,P.encodeFrameState(frame(2n))));});
rejects("EndFrame without BeginFrame",s=>assert.throws(()=>s.send(M.EndFrame)));
rejects("Draw outside frame",s=>assert.throws(()=>s.send(M.Draw,new Uint8Array(16))));
rejects("draw quota",s=>{s.send(M.BeginFrame,P.encodeFrameState(frame()));for(let i=0;i<P.MAX_DRAWS_PER_FRAME;i++)s.send(M.Draw,new Uint8Array(16));assert.throws(()=>s.send(M.Draw,new Uint8Array(16)),/draw quota/);});
rejects("message quota",s=>{s.send(M.BeginFrame,P.encodeFrameState(frame()));for(let i=1;i<P.MAX_MESSAGES_PER_FRAME;i++)s.send(M.Ping);assert.throws(()=>s.send(M.Ping),/message quota/);});
rejects("duplicate resource",s=>{s.send(M.CreateBuffer,buffer(16n),1n);assert.throws(()=>s.send(M.CreateBuffer,buffer(16n),1n));});
rejects("destroy unknown resource",s=>assert.throws(()=>s.send(M.DestroyResource,empty,7n)));
rejects("live-resource quota",s=>{for(let i=1;i<=P.MAX_LIVE_RESOURCES;i++)s.send(M.CreateBuffer,buffer(1n),BigInt(i));assert.throws(()=>s.send(M.CreateBuffer,buffer(1n),999n),/live-resource quota/);});
rejects("declared byte quota",s=>{s.send(M.CreateBuffer,buffer(BigInt(P.MAX_DECLARED_RESOURCE_BYTES)),1n);assert.throws(()=>s.send(M.CreateBuffer,buffer(1n),2n));});
rejects("destroy subtracts bytes",s=>{s.send(M.CreateBuffer,buffer(4096n),1n);assert.equal(s.value.declaredResourceBytes,4096);s.send(M.DestroyResource,empty,1n);assert.equal(s.value.declaredResourceBytes,0);s.send(M.CreateBuffer,buffer(BigInt(P.MAX_DECLARED_RESOURCE_BYTES)),2n);});
rejects("invalid CreateBuffer payload",s=>assert.throws(()=>s.send(M.CreateBuffer,new Uint8Array(7),1n)));
rejects("invalid resize dimensions",s=>{const p=P.encodeResize({...vp,resizeGeneration:2n});new DataView(p.buffer).setUint32(0,0,true);assert.throws(()=>s.send(M.Resize,p));});
rejects("duplicate/out-of-order resize",s=>{assert.throws(()=>s.send(M.Resize,P.encodeResize(vp)));s.send(M.Resize,P.encodeResize({...vp,resizeGeneration:3n}));assert.throws(()=>s.send(M.Resize,P.encodeResize({...vp,resizeGeneration:2n})));});
rejects("invalid RTX mode",s=>assert.throws(()=>s.send(M.SetRtxMode,new Uint8Array([1]))));
rejects("EndSession while open",s=>{s.send(M.BeginFrame,P.encodeFrameState(frame()));assert.throws(()=>s.send(M.EndSession));});
rejects("messages after EndSession",s=>{s.send(M.EndSession);assert.throws(()=>s.send(M.Ping));assert.throws(()=>s.send(M.BeginSession));});
rejects("client FrameAccepted and Error",s=>{assert.throws(()=>s.send(M.FrameAccepted,new Uint8Array(40)));assert.throws(()=>s.send(M.Error,new Uint8Array(4)));});
rejects("complete queue oldest drop",s=>{for(let i=1n;i<=4n;i++){s.send(M.BeginFrame,P.encodeFrameState(frame(i)));s.send(M.EndFrame);}assert.equal(s.value.queuedFrames,2);assert.equal(s.value.droppedFrames,2);assert.equal(s.value.processOne()?.state.frame,3n);assert.equal(s.value.processOne()?.state.frame,4n);});
rejects("reject before mutation",s=>{const old=s.value.lastAcceptedSequence;assert.throws(()=>s.send(M.CreateBuffer,buffer(0n),1n));assert.equal(s.value.lastAcceptedSequence,old);assert.equal(s.value.declaredResourceBytes,0);});
const caps=readFileSync(new URL("capabilities.json",fixtureDir),"utf8");
test("json","shared hello and capabilities",()=>{const hello=JSON.parse(readFileSync(new URL("hello.json",fixtureDir),"utf8"));P.authenticate(hello,hello.token,hello.origin,hello.origin);assert.equal(P.validateCapabilities(JSON.parse(caps)).sessionGeneration,"2");});
test("capabilities","native Dawn backend accepted",()=>{const value=JSON.parse(caps);value.backend="native-dawn";assert.equal(P.validateCapabilities(value).backend,"native-dawn");});
test("capabilities","unknown backend rejected",()=>{const value=JSON.parse(caps);value.backend="untrusted-backend";assert.throws(()=>P.validateCapabilities(value));});
for(const hz of [60,120,144])test("scheduling",String(hz)+" Hz",()=>{
 const sends:Uint8Array[]=[];const c=new MirrorClient(b=>sends.push(b));c.authenticate(caps);
 const session=new P.MirrorSession();
 // Process callback-generated frames and ACK each one so outstanding memory stays bounded.
 let seen=0;const transmitted:bigint[]=[];
 for(let i=0;i<=hz*2;i++){const f=logicalFrameAt(i*1000/hz);c.frame(frame(f));while(seen<sends.length){session.accept(P.decode(sends[seen++]!));const item=session.processOne();if(item){transmitted.push(item.state.frame);c.acknowledge(P.encodeFrameAccepted(2n,item.state,item.sequence,0));}}}
 assert.equal(new Set(transmitted).size,transmitted.length);assert.equal(transmitted.at(-1),120n);
});
test("scheduling","irregular skipped callbacks and same-frame reconnect",()=>{
 const sends:Uint8Array[]=[];const c=new MirrorClient(b=>sends.push(b));c.authenticate(caps);
 for(const ms of [0,1,7,42,43,250])c.frame(frame(logicalFrameAt(ms)));
 assert.deepEqual(sends.map(P.decode).filter(x=>x.type===M.BeginFrame).map(x=>P.decodeFrameState(x.payload).frame),[1n,2n,15n]);
 c.disconnect(false);sends.length=0;c.authenticate(caps.replace('"2"','"3"'));c.frame(frame(15n));
 assert.equal(P.decode(sends[0]!).sequence,1n);assert.equal(P.decodeFrameState(P.decode(sends[2]!).payload).frame,15n);
 assert.equal(c.outstanding,1);
});
function client(){const c=new MirrorClient(()=>{});c.authenticate(caps);const sequence=c.frame(frame(4n))!;return {c,sequence};}
for(const kind of ["wrong generation","unknown sequence","wrong frame","resize mismatch","status","replay","repeat capabilities","future frame","inflated cumulative drop"])test("acknowledgements",kind,()=>{
 const {c,sequence}=client();const p=P.encodeFrameAccepted(kind==="wrong generation"?3n:2n,kind==="wrong frame"||kind==="future frame"?frame(99n):kind==="resize mismatch"?{...frame(4n),resizeGeneration:2n}:frame(4n),kind==="unknown sequence"?sequence+100n:sequence,0);
 if(kind==="inflated cumulative drop"){assert.throws(()=>c.acknowledge(P.encodeFrameAccepted(2n,frame(4n),sequence,1)));return;}
 if(kind==="repeat capabilities"){assert.throws(()=>c.authenticate(caps));return;}
 if(kind==="replay"){c.acknowledge(p);assert.throws(()=>c.acknowledge(p));return;}
 if(kind==="status"){const m=P.decode(p);new DataView(m.payload.buffer).setUint32(28,1,true);assert.throws(()=>c.acknowledge(P.encode(m)));return;}
 assert.throws(()=>c.acknowledge(p));
});
for(const key of ["kind","version","sessionId","buildId","backend","features","byteOrder","sessionGeneration"])test("capabilities",key,()=>{const c=JSON.parse(caps);c[key]=null;assert.throws(()=>P.validateCapabilities(c));});
test("resize","encoding and acknowledgement",()=>{const sent:Uint8Array[]=[];const c=new MirrorClient(b=>sent.push(b));c.authenticate(caps);const s={...frame(10n),width:800,height:450,resizeGeneration:5n};const seq=c.frame(s)!;assert.deepEqual(P.decodeResize(P.decode(sent[1]!).payload),{width:800,height:450,resizeGeneration:5n});assert.equal(c.acknowledge(P.encodeFrameAccepted(2n,s,seq,0)).ack.resizeGeneration,5n);});
console.log(JSON.stringify({suite:"protocol",status:"PASS",tests,categories,valid_binary:12,malformed_binary:11}));
