import {startNative,connect,openSocket,until,check,canonicalState,sleep} from './tcw005r-client.mjs';
import {encode,MessageType} from '../packages/protocol/dist/index.js';
const exe=process.argv[2]??'build/tcw005r/framebridge-native-mirror.exe';
const results=[];let native;
async function rejection(n,modify){const {ws,hello}=await openSocket(n);let closed=false,code;ws.on('close',c=>{closed=true;code=c;});modify(ws,hello);await until(()=>closed);check(code===1008||code===1002,'rejection code');}
try {
  native=await startNative(exe,['--test-init-failure'],0,{expectFailure:true});
  check(native.exit===4&&!native.ready,'native-dawn advertised before initialization');results.push('startup_failure_no_READY');
  native=await startNative(exe,['--test-protocol-only']);check(native.ready.backend==='test-harness','harness identity');
  let c=await connect(native);check(c.caps.backend==='test-harness','harness capabilities');await c.frame(60);await c.close();await native.stop();results.push('explicit_test_harness');
  native=await startNative(exe,['--trace','--capture-dir',process.argv[3]??'build/tcw005r-captures']);
  check(native.ready.backend==='native-dawn','native identity');
  await rejection(native,(ws,h)=>ws.send(JSON.stringify({...h,token:'0'.repeat(48)})));
  await rejection(native,(ws,h)=>ws.send(JSON.stringify({...h,origin:'http://localhost:5173'})));
  results.push('authentication_rejections_no_deadlock');
  {const {ws}=await openSocket(native);let closed=false;ws.on('close',code=>{check(code===1009,'hello quota close code');closed=true;});ws.send('x'.repeat(8193));await until(()=>closed);}
  results.push('hello_quota_at_transport');
  c=await connect(native);
  await rejection(native,(ws,h)=>ws.send(JSON.stringify(h)));results.push('competing_controller');
  for(const frame of [60,120,180])await c.frame(frame);
  for(const frame of [60,120,180]){const e=native.events.find(x=>x.event==='submitted'&&x.frame===frame);const s=canonicalState(BigInt(frame),{width:640,height:360,resizeGeneration:1n});check(e&&e.rotationX===s.rotationX&&e.rotationY===s.rotationY&&e.simulationTime===s.simulationTime&&e.cameraZ===s.cameraZ,'renderer state altered');}
  results.push('frames_60_120_180_unchanged_and_render_correlated');
  let frame=180,rg=1n;
  for(let i=0;i<6;i++){const wide=i%2===0;await c.frame(++frame,wide?800:640,wide?450:360,++rg);check(c.last.resizeGeneration===rg,'resize ack');}
  results.push('six_resize_cycles');
  for(let i=0;i<3;i++){const generation=BigInt(c.caps.sessionGeneration);await c.close();await sleep(30);frame+=60;c=await connect(native);check(BigInt(c.caps.sessionGeneration)===generation+1n,'new generation');await c.frame(frame,640,360,rg);}
  results.push('three_reconnects_continuity');
  c.ws.send('post-auth text');await until(()=>c.closed);results.push('post_auth_text_rejected');
  c=await connect(native);c.ws.send(encode({type:MessageType.Draw,sequence:2n,payload:new Uint8Array(16)}));await until(()=>c.closed);results.push('reserved_draw_rejected');
  c=await connect(native);c.ws.send(Buffer.alloc(600000),{binary:true,fin:false});c.ws.send(Buffer.alloc(600000),{binary:true,fin:true});await until(()=>c.closed);results.push('actual_WS_fragment_aggregate_quota');
  await native.stop();results.push('native_clean_shutdown_threads_joined');
  const telemetry=native.events.filter(x=>x.backend==='native-dawn');check(telemetry.every(x=>x.event==='shutdown'||x.dawn_validation_errors===0&&x.d3d12_messages===0&&!x.device_lost),'GPU validation');
  native=await startNative(exe,[],75);c=await connect(native);
  for(let f=1;f<=80;f++)c.client.frame(canonicalState(BigInt(f),{width:640,height:360,resizeGeneration:1n}));
  await until(()=>c.last?.frame===80n||c.error);if(c.error)throw c.error;
  check(c.drops>0&&c.client.outstanding===0,'delayed complete-frame queue');const drops=c.drops;
  await c.close();await native.stop();results.push('delayed_queue_drop_correlation');
  native=await startNative(exe,['--test-render-failure']);c=await connect(native);
  c.client.frame(canonicalState(60n,{width:640,height:360,resizeGeneration:1n}));await until(()=>native.exit!==undefined);
  check(native.exit===4&&c.count===0,'render failure sent successful ACK');results.push('render_failure_no_ACK');
  console.log(JSON.stringify({suite:'tcw005r-native-integration',status:'PASS',test_count:results.length,tests:results,delayed_dropped_frames:drops,node_role:'test_client_only',telemetry}));
}catch(e){if(native?.child.exitCode===null)native.child.kill();console.error('FAIL '+e.message);process.exitCode=1;}
