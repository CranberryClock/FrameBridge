import fs from 'node:fs';
import path from 'node:path';
const root=path.resolve(process.argv[2]??'artifacts/tcw-006r');
const textFiles=[];
const walk=dir=>{for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=path.join(dir,e.name);if(e.isDirectory())walk(p);else textFiles.push(p);}};
walk(root);
const findings=[];
for(const file of textFiles){const data=fs.readFileSync(file);if(file.toLowerCase().endsWith('.png'))continue;const s=data.toString('utf8');
  if(/[A-Za-z]:[\\/][^\r\n"']+/.test(s)) findings.push({file:path.relative(root,file),kind:'absolute_windows_path'});
  if(/(?:C:\\Users|C:\\/Users|D:\\Users|D:\\/Users|CranberryClock|Mark)/i.test(s)) findings.push({file:path.relative(root,file),kind:'user_or_home_name'});
  if(/(?:FRAMEBRIDGE_NATIVE_MIRROR_READY|token=[0-9a-f]{48}|BEGIN (?:RSA|OPENSSH|EC) PRIVATE KEY|(?:api[_-]?key|password|authorization)\s*[:=])/i.test(s)) findings.push({file:path.relative(root,file),kind:'secret_or_token'});
}
const result={status:findings.length?'FAIL':'PASS',root:'artifacts/tcw-006r',files_scanned:textFiles.length,findings};
console.log(JSON.stringify(result));
if(findings.length)process.exitCode=1;
