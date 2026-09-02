import {startNative,connect,check} from './tcw005r-client.mjs';
const exe=process.argv[2];let n;
try {
  n=await startNative(exe,['--test-init-failure'],0,{expectFailure:true});check(n.exit===4&&!n.ready,'failed initialization advertised READY');
  n=await startNative(exe,['--test-protocol-only']);check(n.ready.backend==='test-harness','harness READY identity');
  const c=await connect(n);check(c.caps.backend==='test-harness','harness capabilities');await c.frame(60);await c.close();await n.stop();
  console.log(JSON.stringify({suite:'tcw005r-protocol-only',status:'PASS',backend:'test-harness',tests:3}));
}catch(e){if(n?.child.exitCode===null)n.child.kill();console.error('FAIL '+e.message);process.exitCode=1;}
