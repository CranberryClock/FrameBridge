import {spawn} from 'node:child_process';
import WebSocket from '../packages/protocol/node_modules/ws/index.js';
import * as P from '../packages/protocol/dist/index.js';
const exe=process.argv[2]??'build/tcw007/framebridge-native-mirror.exe';
const mode=process.argv[3]??'full';
const dimension=Number(process.argv[4]??640);
const child=spawn(exe,[...(dimension<640?['--test-protocol-only']:[]),'--return-mode',mode,'--trace'],{stdio:['pipe','pipe','pipe'],env:process.env});
let buffer='',ready,lines=[],stderr='';child.stdout.on('data',d=>{buffer+=d;for(;;){const i=buffer.indexOf('\n');if(i<0)break;const line=buffer.slice(0,i).trim();buffer=buffer.slice(i+1);lines.push(line);const m=/port=(\d+) token=([0-9a-f]{48})/.exec(line);if(m)ready={port:+m[1],token:m[2]};}});child.stderr.on('data',d=>stderr+=d);
const wait=ms=>new Promise(r=>setTimeout(r,ms));for(let i=0;i<100&&!ready;i++)await wait(50);if(!ready)throw new Error('receiver not ready');
const ws=new WebSocket(`ws://127.0.0.1:${ready.port}`,{headers:{Origin:'http://127.0.0.1:5173'}});const client=new P.MirrorClient(b=>ws.send(b));let caps,acks=0,images=0,consumed=0,error='';
ws.on('message',(data,isBinary)=>{try{if(!isBinary){caps=client.authenticate(data.toString());return;}const bytes=new Uint8Array(data);const m=P.decode(bytes);if(m.type===P.MessageType.NativeImage){const image=P.decodeNativeImage(bytes);images++;client.consume(image);consumed++;}else{client.acknowledge(bytes);acks++;}}catch(e){error=e.message;}});
await new Promise((resolve,reject)=>{ws.once('open',resolve);ws.once('error',reject);});ws.send(JSON.stringify({kind:'hello',version:0,token:ready.token,origin:'http://127.0.0.1:5173',three:{version:'0.185.0',commit:'2431a09'},buildId:'tcw007b-isolation',requestedCapabilities:['explicit-mirror'],byteOrder:'little'}));
for(let i=1;i<31;i++){await wait(100);if(!caps){try{const text=lines.find(x=>x.startsWith('{"backend"')||x.includes('"kind":"capabilities"'));}catch{}}if(!client.authenticated)continue;const s=P.canonicalState(BigInt(i),{width:dimension,height:Math.round(dimension*9/16),resizeGeneration:1n});client.frame(s);}
await wait(7000);ws.close();child.stdin.end('\n');await wait(500);if(!child.killed&&child.exitCode===null)child.kill();
const stages=lines.filter(x=>x.startsWith('{')).slice(-20);const result={mode,port:ready.port,acks,images,consumed,client_error:error,stderr,process_exit:child.exitCode,stages};
const failures=[];if(error)failures.push(`client_error=${error}`);if(acks<29)failures.push(`acks=${acks}`);if(mode==='full'&&images<29)failures.push(`images=${images}`);if(consumed!==images)failures.push(`consumed=${consumed} images=${images}`);if(failures.length){console.error(JSON.stringify({...result,failures}));process.exitCode=1;}else console.log(JSON.stringify(result));
