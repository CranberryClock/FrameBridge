import {spawn} from 'node:child_process';
import {setTimeout as sleep} from 'node:timers/promises';
import WebSocket from '../packages/protocol/node_modules/ws/index.js';
import {MirrorClient,canonicalState} from '../packages/protocol/dist/index.js';
export {sleep,canonicalState};
export const origin='http://127.0.0.1:5173';
export function check(value,message){if(!value)throw new Error(message);}
export async function until(condition,ms=10000){const deadline=performance.now()+ms;while(!condition()){if(performance.now()>deadline)throw new Error('deadline exceeded');await sleep(5);}}
export async function startNative(exe,args=[],delay=0,{expectFailure=false}={}) {
  const child=spawn(exe,args,{stdio:['pipe','pipe','pipe'],env:{...process.env,FRAMEBRIDGE_NATIVE_PROCESSING_DELAY_MS:String(delay)}});
  let ready,exit,stderr='',buffer='';const events=[];
  child.on('error',e=>{stderr=e.message;exit=-1;});child.on('exit',code=>{exit=code;});
  child.stdout.on('data',chunk=>{
    buffer+=chunk.toString(); let end;
    while((end=buffer.indexOf('\n'))>=0) {
      const line=buffer.slice(0,end).trim();buffer=buffer.slice(end+1);
      if(line.startsWith('FRAMEBRIDGE_NATIVE_MIRROR_READY')) {
        const m=/port=(\d+) token=([0-9a-f]{48}) backend=(\S+)/.exec(line);
        if(m)ready={port:Number(m[1]),token:m[2],backend:m[3]};
      } else if(line.startsWith('{')) events.push(JSON.parse(line));
    }
  });
  child.stderr.on('data',chunk=>{stderr+=chunk.toString();});
  await until(()=>ready||exit!==undefined);
  if(!expectFailure)check(ready,'receiver initialization failed');
  return {child,ready,events,get stderr(){return stderr;},get exit(){return exit;},async stop(){
    if(exit===undefined)child.stdin.end('\n');
    try{await until(()=>exit!==undefined);}catch(e){child.kill();throw e;}
    check(exit===0,'unclean receiver exit');check(stderr==='','receiver validation stderr');
    check(events.some(x=>x.event==='shutdown'&&x.clean_shutdown&&x.owned_threads_joined),'missing joined shutdown');
    return events.at(-1);
  }};
}
export async function openSocket(native,token=native.ready.token,extraOrigin=origin){
  const ws=new WebSocket(`ws://127.0.0.1:${native.ready.port}`,{headers:{Origin:extraOrigin}});
  await new Promise((resolve,reject)=>{ws.once('open',resolve);ws.once('error',reject);});
  const hello={kind:'hello',version:0,token,origin:extraOrigin,three:{version:'0.185.0',commit:'2431a09'},buildId:'tcw005r-test',requestedCapabilities:['explicit-mirror'],byteOrder:'little'};
  return {ws,hello};
}
export async function connect(native) {
  const {ws,hello}=await openSocket(native);const client=new MirrorClient(bytes=>ws.send(bytes));
  let caps,last,error,closed=false,count=0,drops=0;
  const accepted=[];
  ws.on('message',(data,binary)=>{try {
    if(!binary){caps=client.authenticate(data.toString());return;}
    const result=client.acknowledge(new Uint8Array(data));last=result.ack;count++;drops=last.droppedFrames;
    accepted.push({frame:String(last.frame),resizeGeneration:String(last.resizeGeneration),sequence:String(last.sequence),generation:String(last.sessionGeneration)});
    if(accepted.length>32)accepted.shift();
  }catch(e){error=e;}});
  ws.on('error',e=>{error=e;}); ws.on('close',()=>{closed=true;});
  ws.send(JSON.stringify(hello));await until(()=>caps||error||closed);if(error)throw error;check(caps,'authentication failed');
  return {ws,client,caps,accepted,get last(){return last;},get count(){return count;},get drops(){return drops;},get error(){return error;},get closed(){return closed;},
    async frame(frame,width=640,height=360,resizeGeneration=1n){
      client.frame(canonicalState(BigInt(frame),{width,height,resizeGeneration}));
      await until(()=>last?.frame===BigInt(frame)||error||closed);if(error)throw error;check(last?.frame===BigInt(frame),'frame not accepted');
    },async close(){client.disconnect(true);await until(()=>closed);check(!error,'client protocol error');}
  };
}
