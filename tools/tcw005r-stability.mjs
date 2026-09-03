import {execFileSync} from 'node:child_process';
import {startNative,connect,until,check,canonicalState,sleep} from './tcw005r-client.mjs';
const exe=process.argv[2]??'build/tcw005r/framebridge-native-mirror.exe';
const duration=Number(process.argv[3]??600);check(Number.isFinite(duration)&&duration>=2,'invalid duration');
const sourceCommit=execFileSync('git',['rev-parse','HEAD'],{encoding:'utf8'}).trim();
const dirty=execFileSync('git',['status','--porcelain','--untracked-files=no'],{encoding:'utf8'}).trim();
check(dirty==='','stability source checkout must be clean');
let native;const clients=[];
try {
  native=await startNative(exe);check(native.ready.backend==='native-dawn','stability requires native rendering');
  let client=await connect(native);clients.push(client);
  const startTime=new Date().toISOString(),start=performance.now();
  let lastFrame=0,rg=1n,cycle=0,reconnects=0,resizeCount=0,preReconnect,postReconnect;
  const elapsed=()=>(performance.now()-start)/1000;
  while(elapsed()<duration) {
    if(reconnects<3 && elapsed()>duration*(reconnects+1)/4) {
      await until(()=>client.client.outstanding===0||client.error);if(client.error)throw client.error;
      preReconnect=lastFrame;await client.close();await sleep(100);
      client=await connect(native);clients.push(client);reconnects++;
      postReconnect=Math.max(lastFrame+1,Math.floor(elapsed()*60)+1);check(postReconnect>preReconnect,'simulation reset');
    }
    const frame=Math.floor(elapsed()*60)+1;
    if(frame>lastFrame) {
      const newCycle=Math.floor(elapsed()/Math.min(5,duration/8));
      if(cycle!==newCycle){cycle=newCycle;rg++;resizeCount++;}
      const wide=cycle%2===1;
      client.client.frame(canonicalState(BigInt(frame),{width:wide?800:640,height:wide?450:360,resizeGeneration:rg}));lastFrame=frame;
    }
    if(client.error)throw client.error;check(!client.closed,'unexpected close');await sleep(1);
  }
  await until(()=>client.client.outstanding===0||client.error);if(client.error)throw client.error;
  check(client.last.frame===BigInt(lastFrame)&&client.last.resizeGeneration===rg,'final convergence');
  const elapsedSeconds=elapsed(),endTime=new Date().toISOString();await client.close();const shutdown=await native.stop();
  const memory=native.events.filter(x=>x.event==='memory');const warm=memory.filter(x=>x.elapsed_seconds>=Math.min(60,duration/2));
  const peakWorking=Math.max(...memory.map(x=>x.working_set_bytes)),peakPrivate=Math.max(...memory.map(x=>x.private_bytes));
  const growth=warm.length>1?warm.at(-1).private_bytes-warm[0].private_bytes:0;
  const gpu=native.events.filter(x=>x.luid_verified);
  const acknowledged=clients.reduce((n,c)=>n+c.count,0),dropped=clients.reduce((n,c)=>n+c.drops,0);
  const checks={memory:peakWorking<512*1024**2&&peakPrivate<512*1024**2&&growth<64*1024**2,
    gpu:gpu.length>=2&&gpu.every(x=>x.dawn_validation_errors===0&&x.d3d12_messages===0&&(x.dxgi_messages??0)===0&&!x.device_lost),
    correlation:shutdown.submitted===acknowledged&&shutdown.acknowledged===acknowledged,
    render_target_pool:gpu.at(-1)?.target_cache_capacity===2&&gpu.at(-1)?.target_allocations===2&&gpu.at(-1)?.swapchain_allocations===2,
    queue:shutdown.max_queued_complete_frames<=2,duration:elapsedSeconds>=duration,cadence:acknowledged/elapsedSeconds>=55,
    temporal_upscaler:gpu.length>=2&&gpu.every(x=>x.render_scale===0.5&&x.upscaler==='reference-upscale NOT DLSS'&&x.input_width>0&&x.input_height>0&&x.output_width>0&&x.output_height>0&&x.output_allocations<=2),
    jitter_schedule:gpu.some(x=>x.jittered_frames>0&&x.non_jittered_frames>0)};
  const passed=Object.values(checks).every(Boolean);
  console.log(JSON.stringify({suite:'tcw006r-native-stability',status:passed?'PASS':'FAIL',checks,acceptance:duration>=600&&passed,source_commit:sourceCommit,source_dirty:false,
    start_time:startTime,end_time:endTime,elapsed_seconds:elapsedSeconds,required_seconds:duration,target_hz:60,submitted_frames:shutdown.submitted,
    acknowledged_frames:acknowledged,dropped_frames:dropped,reconnect_count:reconnects,resize_count:resizeCount,final_frame:lastFrame,final_resize_generation:String(rg),
    protocol_errors:0,peak_working_set_bytes:peakWorking,peak_private_bytes:peakPrivate,private_growth_after_warmup_bytes:growth,
    memory_limits:{peak_bytes:512*1024**2,warm_private_growth_bytes:64*1024**2},memory_samples:memory,gpu,shutdown,node_role:'test_client_only'}));
  if(!passed)process.exitCode=1;
}catch(e){if(native?.child.exitCode===null)native.child.kill();console.log(JSON.stringify({suite:'tcw006r-native-stability',status:'FAIL',source_commit:sourceCommit,reason:e.message,telemetry:native?.events??[]}));console.error('FAIL stability: '+e.message);process.exitCode=1;}
