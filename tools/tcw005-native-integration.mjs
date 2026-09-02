import {spawn} from "node:child_process";
import WebSocket from "../packages/protocol/node_modules/ws/index.js";
import {MirrorClient,canonicalState,decodeFrameAccepted} from "../packages/protocol/dist/index.js";

const exe = process.argv[2] ?? "build/tcw005/Release/framebridge-native-mirror.exe";
const origin = "http://127.0.0.1:5173";
const child = spawn(exe, [], {stdio:["pipe","pipe","pipe"]});
let output = "", error = "";
child.stdout.setEncoding("utf8"); child.stderr.setEncoding("utf8");
child.stdout.on("data", chunk => { output += chunk; }); child.stderr.on("data", chunk => { error += chunk; });
function waitForReady() {
  return new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error("native receiver did not start")),5000);
    const tick=()=>{const line=output.split(/\r?\n/).find(x=>x.includes("FRAMEBRIDGE_NATIVE_MIRROR_READY")); if(!line)return setTimeout(tick,10); clearTimeout(timer); const match=/port=(\d+) token=([0-9a-f]{48})/.exec(line); if(!match)reject(new Error("invalid native ready line")); else resolve({port:Number(match[1]),token:match[2]});}; tick();
  });
}
const closeChild=()=>{if(!child.killed)child.kill();};
try {
  const {port,token}=await waitForReady();
  const socket=new WebSocket(`ws://127.0.0.1:${port}`,{headers:{Origin:origin}}); socket.binaryType="arraybuffer";
  const sent=[]; const client=new MirrorClient(bytes=>{sent.push(bytes);socket.send(bytes);});
  const caps=await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error("native hello timeout")),5000);
    socket.on("open",()=>socket.send(JSON.stringify({kind:"hello",version:0,token,origin,three:{version:"0.185.0",commit:"2431a09"},buildId:"tcw005-test",requestedCapabilities:["explicit-mirror"],byteOrder:"little"})));
    socket.on("message",data=>{try{const text=typeof data === "string" ? data : data.toString(); if(text.startsWith("{")){clearTimeout(timer);resolve(text);}}catch(e){reject(e);}});
    socket.on("error",reject);
  });
  const parsed=JSON.parse(caps); if(parsed.backend!=="native-dawn")throw new Error("native backend not advertised");
  client.authenticate(caps);
  const state=canonicalState(60n,{width:640,height:360,resizeGeneration:1n});
  const sequence=client.frame(state); if(sequence===undefined)throw new Error("frame was not sent");
  const ackBytes=await new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(new Error("native ack timeout")),5000);socket.on("message",data=>{if(typeof data === "string")return; const bytes=new Uint8Array(data); if(bytes[0]===0x46){clearTimeout(timer);resolve(bytes);}});});
  const ack=decodeFrameAccepted(ackBytes);
  if(ack.frame!==60n || ack.sequence!==sequence || ack.sessionGeneration!==1n)throw new Error("ack correlation mismatch");
  client.acknowledge(ackBytes);
  socket.close(); closeChild();
  console.log(JSON.stringify({suite:"tcw005-native-integration",status:"PASS",backend:parsed.backend,frame:String(ack.frame),sessionGeneration:String(ack.sessionGeneration),ackSequence:String(ack.sequence),nodeFrameProxy:false}));
} catch (errorValue) {
  closeChild(); console.error(`FAIL ${errorValue instanceof Error ? errorValue.message : String(errorValue)}${error ? ` stderr=${error.trim()}` : ""}`); process.exitCode=1;
}
