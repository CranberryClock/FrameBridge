import {fork,execFileSync} from "node:child_process";
import {createHash} from "node:crypto";
import {readFile,mkdir,writeFile,readdir,mkdtemp} from "node:fs/promises";
import {fileURLToPath} from "node:url";
import path from "node:path";
import {setTimeout as sleep} from "node:timers/promises";
const script=fileURLToPath(import.meta.url);
const root=path.resolve(path.dirname(script),"..");
const duration=Number(process.env.TCW004_DURATION_SECONDS??1800);
if(!Number.isFinite(duration)||duration<2)throw new Error("invalid duration");
const inputs={duration_seconds:duration,target_hz:60,minimum_attempted_hz:59,minimum_ack_ratio:.99,max_catch_up:4,reconnect_at_seconds:duration/2,reconnect_pause_ms:100,resize_interval_seconds:Math.min(5,duration/4),sample_interval_seconds:Math.min(10,duration/4),heap_limit_bytes:128*1024**2,rss_limit_bytes:256*1024**2,heap_growth_limit_bytes:32*1024**2,rss_growth_limit_bytes:64*1024**2,max_trend_bytes_per_minute:1024**2};
inputs.memory_warmup_seconds=Math.min(60,duration/2);
inputs.force_gc_at_samples=true;
// One-minute runs include allocator/JIT startup; longer runs must prove a tighter RSS trend.
inputs.rss_max_trend_bytes_per_minute=(duration<120?4:1)*1024**2;
function evaluate(r) {
 const m=r.memory;
 return {
  duration:r.elapsed_seconds>=inputs.duration_seconds,
  cadence:r.attempted_frames/r.elapsed_seconds>=inputs.minimum_attempted_hz,
  acknowledgements:r.eligible_sent_frames>0&&r.acknowledged_frames/r.eligible_sent_frames>=inputs.minimum_ack_ratio,
  no_invalid_ack:r.invalid_acknowledgements===0,
  one_reconnect:r.reconnects===1,
  resynchronized:r.first_post_reconnect_sent_frame===r.first_post_reconnect_acknowledged_frame&&r.first_post_reconnect_sent_frame!==null&&r.first_post_reconnect_ack_generation===r.reconnect_generation,
  resized:r.resize_generations>1,
  no_protocol_errors:r.protocol_errors===0,
  no_unexpected_closes:r.unexpected_closes===0,
  convergence:r.final_authoritative_frame===r.final_acknowledged_frame&&r.final_authoritative_resize===r.final_acknowledged_resize&&r.outstanding_frames===0,
  memory:m.heap.max<=inputs.heap_limit_bytes&&m.rss.max<=inputs.rss_limit_bytes&&m.heap.growth<=inputs.heap_growth_limit_bytes&&m.rss.growth<=inputs.rss_growth_limit_bytes&&m.heap.trend_bytes_per_minute<=inputs.max_trend_bytes_per_minute&&m.rss.trend_bytes_per_minute<=inputs.rss_max_trend_bytes_per_minute
 };
}
function memorySummary(samples,key) {
 const values=samples.map(s=>s[key]);const warm=samples.filter(s=>s.elapsed_seconds>=inputs.memory_warmup_seconds);
 const selected=warm.length>=3?warm:samples;
 const xs=selected.map(s=>s.elapsed_seconds/60),ys=selected.map(s=>s[key]);
 const xm=xs.reduce((a,b)=>a+b,0)/xs.length,ym=ys.reduce((a,b)=>a+b,0)/ys.length;
 const den=xs.reduce((sum,x)=>sum+(x-xm)**2,0);
 const slope=den?xs.reduce((sum,x,i)=>sum+(x-xm)*(ys[i]-ym),0)/den:0;
 return {start:values[0],end:values.at(-1),min:Math.min(...values),max:Math.max(...values),growth:values.at(-1)-values[0],trend_bytes_per_minute:slope};
}
if(process.argv.includes("--worker")) {
 const {BridgeServer}=await import("../packages/protocol/dist/server.js");
 const {HarnessClient,until}=await import("../packages/protocol/dist/transport-client.js");
 const {canonicalState}=await import("../packages/protocol/dist/index.js");
 const origin="http://127.0.0.1:5173",server=new BridgeServer(origin),clients=[];
 const startTime=new Date().toISOString();const started=performance.now();
 const now=()=>(performance.now()-started)/1000;
 let client,attempted=0,reconnects=0,nextFrame=1,lastFrame=0,skipped=0,resizeGeneration=1n,resizeCycle=0,nextSample=0,reconnectDowntime=0,reconnectGeneration=null,firstSent=null,firstAck=null,firstAckGeneration=null;
 const samples=[];
 const sample=()=>{const before=process.memoryUsage();global.gc();const m=process.memoryUsage();samples.push({elapsed_seconds:now(),pre_gc_heap_bytes:before.heapUsed,heap_used_bytes:m.heapUsed,rss_bytes:m.rss,gc_forced:true,attempted_frames:attempted,sent_frames:clients.reduce((n,c)=>n+c.sent,0),acknowledged_frames:clients.reduce((n,c)=>n+c.acknowledged,0)});};
 const port=await server.start();
 async function connect(){const c=await HarnessClient.connect(port,origin,server.token,(ack)=>{if(reconnectGeneration===String(ack.sessionGeneration)&&firstAck===null){firstAck=String(ack.frame);firstAckGeneration=String(ack.sessionGeneration);}});clients.push(c);return c;}
 const transmit=(logical)=>{
  const cycle=Math.floor((logical/60)/inputs.resize_interval_seconds);
  if(cycle!==resizeCycle){resizeCycle=cycle;resizeGeneration++;}
  const wide=cycle%2===1;const state=canonicalState(BigInt(logical),{width:wide?800:640,height:wide?450:360,resizeGeneration});
  attempted++;const sequence=client.frame(state);
  if(sequence!==undefined&&reconnects===1&&firstSent===null)firstSent=String(logical);
  lastFrame=logical;
 };
 try {
  client=await connect();sample();nextSample=inputs.sample_interval_seconds;
  while(now()<duration) {
   if(reconnects===0&&now()>=inputs.reconnect_at_seconds){
    await until(()=>client.mirror.outstanding===0);
    const down=performance.now();await client.close();await sleep(inputs.reconnect_pause_ms);
    client=await connect();reconnects++;reconnectGeneration=client.mirror.generation;reconnectDowntime=performance.now()-down;
   }
   const target=Math.min(Math.floor(duration*60),Math.max(1,Math.floor(now()*60)+1));
   if(target-nextFrame+1>inputs.max_catch_up){const skip=target-nextFrame+1-inputs.max_catch_up;skipped+=skip;nextFrame+=skip;}
   while(nextFrame<=target){transmit(nextFrame);nextFrame++;}
   if(now()>=nextSample){sample();nextSample+=inputs.sample_interval_seconds;console.log("SOAK_PROGRESS "+JSON.stringify(samples.at(-1)));}
   await sleep(4);
  }
  const finalFrame=Math.floor(duration*60);
  if(lastFrame<finalFrame)transmit(finalFrame);
  await until(()=>client.mirror.outstanding===0,5000);
  sample();const finalAck=client.lastAck;const outstanding=clients.reduce((n,c)=>n+c.mirror.outstanding,0);
  await client.close();await server.close();
  const result={start_time:startTime,end_time:new Date().toISOString(),elapsed_seconds:now(),target_logical_frames:finalFrame,attempted_frames:attempted,sent_frames:clients.reduce((n,c)=>n+c.sent,0),eligible_sent_frames:clients.reduce((n,c)=>n+c.sent,0),acknowledged_frames:clients.reduce((n,c)=>n+c.acknowledged,0),dropped_frames:clients.reduce((n,c)=>n+c.drops,0),outstanding_frames:outstanding,skipped_logical_frames:skipped,invalid_acknowledgements:clients.reduce((n,c)=>n+c.invalidAcks,0),reconnects,reconnect_downtime_ms:reconnectDowntime,reconnect_generation:reconnectGeneration,first_post_reconnect_sent_frame:firstSent,first_post_reconnect_acknowledged_frame:firstAck,first_post_reconnect_ack_generation:firstAckGeneration,resize_generations:Number(resizeGeneration),protocol_errors:clients.reduce((n,c)=>n+c.protocolErrors,0),unexpected_closes:clients.reduce((n,c)=>n+c.unexpectedCloses,0),final_authoritative_frame:String(finalFrame),final_acknowledged_frame:String(finalAck?.frame),final_authoritative_resize:String(resizeGeneration),final_acknowledged_resize:String(finalAck?.resizeGeneration),memory_samples:samples,memory:{heap:memorySummary(samples,"heap_used_bytes"),rss:memorySummary(samples,"rss_bytes")}};
  result.acceptance=evaluate(result);result.average_attempted_hz=attempted/result.elapsed_seconds;result.status=Object.values(result.acceptance).every(Boolean)?"PASS":"FAIL";
  if(process.send)await new Promise(resolve=>process.send(result,resolve));
  process.exitCode=result.status==="PASS"?0:1;process.disconnect?.();
 }catch(e){
  for(const c of clients){c.socket.terminate();}await server.close();
  if(process.send)await new Promise(resolve=>process.send({status:"FAIL",error:String(e),start_time:startTime,end_time:new Date().toISOString(),elapsed_seconds:now(),memory_samples:samples},resolve));
  process.exitCode=1;process.disconnect?.();
 }
} else {
 const git=(...args)=>execFileSync("git",args,{cwd:root,encoding:"utf8"}).trim();
 const sha=git("rev-parse","HEAD");
 const sourceFiles=["tools/tcw004-soak.mjs",...(await readdir(path.join(root,"packages/protocol/src"))).filter(f=>f.endsWith(".ts")).map(f=>"packages/protocol/src/"+f),"pnpm-lock.yaml","package.json","packages/protocol/package.json"];
 const dirty=git("status","--porcelain","--",...sourceFiles);
 if(duration>=1800&&dirty)throw new Error("acceptance requires committed implementation; source changes detected");
 const hash=async f=>createHash("sha256").update(await readFile(path.join(root,f))).digest("hex");
 const hashFiles=[...sourceFiles,...(await readdir(path.join(root,"packages/protocol/dist"))).filter(f=>f.endsWith(".js")).map(f=>"packages/protocol/dist/"+f)];
 const hashes=Object.fromEntries(await Promise.all(hashFiles.map(async f=>[f,await hash(f)])));
 const pnpm=execFileSync("cmd.exe",["/d","/c","corepack pnpm --version"],{cwd:root,encoding:"utf8"}).trim();
 const parent=path.join(root,"artifacts/tcw-004/runs");await mkdir(parent,{recursive:true});
 const dir=await mkdtemp(path.join(parent,(duration>=1800?"acceptance-":"diagnostic-")+new Date().toISOString().replace(/[:.]/g,"-")+"-"));
 const child=fork(script,["--worker"],{cwd:root,execArgv:["--expose-gc"],stdio:["ignore","pipe","pipe","ipc"]});
 let result;let output="";child.on("message",m=>{result=m;});
 for(const stream of [child.stdout,child.stderr])stream.on("data",chunk=>{const value=String(chunk);output+=value;process.stdout.write(value);});
 const code=await new Promise(resolve=>child.on("close",(code,signal)=>resolve(code??(signal?1:1))));
 const hashesUnchanged=(await Promise.all(hashFiles.map(async f=>(await hash(f))===hashes[f]))).every(Boolean);
 const evidence={task:"TCW-004",mode:duration>=1800?"acceptance":"diagnostic",implementation_commit:sha,implementation_dirty:!!dirty,source_and_runtime_sha256:hashes,hashes_unchanged:hashesUnchanged,node:process.version,pnpm,platform:process.platform,architecture:process.arch,inputs,command:"node tools/tcw004-soak.mjs",working_directory:root,duration_environment:String(duration),process_exit_code:code,...result};
 evidence.status=code===0&&hashesUnchanged&&result?.status==="PASS"?"PASS":"FAIL";
 await writeFile(path.join(dir,"result.json"),JSON.stringify(evidence,null,2)+"\n");
 await writeFile(path.join(dir,"output.txt"),output);
 console.log("SOAK_RESULT "+JSON.stringify({status:evidence.status,mode:evidence.mode,exit_code:code,elapsed_seconds:evidence.elapsed_seconds,average_attempted_hz:evidence.average_attempted_hz,evidence:path.relative(root,dir)}));
 process.exitCode=evidence.status==="PASS"?0:1;
}
