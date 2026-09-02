import {startNative,connect,until,check} from './tcw005r-client.mjs';
const n=await startNative(process.argv[2]??'build/tcw005r/framebridge-native-mirror.exe');
try {
  const c=await connect(n);await c.frame(58);await c.frame(59,800,450,2n);await c.frame(60,640,360,3n);
  console.log('WAITING_FOR_WINDOW_CLOSE frame=60 backend=native-dawn');
  await until(()=>n.exit!==undefined,180000);
  check(n.exit===0&&n.stderr===''&&n.events.some(e=>e.event==='shutdown'&&e.clean_shutdown&&e.owned_threads_joined),'window close failed');
  console.log(JSON.stringify({suite:'native-window-close',status:'PASS',exit_code:n.exit,frame:60,telemetry:n.events}));
}catch(e){n.child.kill();console.error('FAIL '+e.message);process.exitCode=1;}
