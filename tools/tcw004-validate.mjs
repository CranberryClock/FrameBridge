import {spawnSync} from "node:child_process";
import {mkdir,mkdtemp,writeFile,access} from "node:fs/promises";
import path from "node:path";
const root=process.cwd();
const cleanInstall=await access(path.join(root,"node_modules")).then(()=>false,()=>true);
const parent=path.join(root,"artifacts/tcw-004/validation");await mkdir(parent,{recursive:true});
const dir=await mkdtemp(path.join(parent,"run-"));
const cmake=process.env.FRAMEBRIDGE_CMAKE??"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe";
const commands=[
 ["frozen-install","cmd.exe",["/d","/c","corepack pnpm install --frozen-lockfile"],0],
 ["typescript-build","cmd.exe",["/d","/c","corepack pnpm -r build"],0],
 ["typescript-tests","cmd.exe",["/d","/c","corepack pnpm -r test"],0],
 ["vite-build","cmd.exe",["/d","/c","corepack pnpm --filter @framebridge/demo-web exec vite build"],0],
 ["native-configure",cmake,["-S",".","-B","out/tcw004-validation","-G","Visual Studio 17 2022","-A","x64"],0],
 ["native-build",cmake,["--build","out/tcw004-validation","--config","Release","--target","framebridge-bridge-codec"],0],
 ["native-execute",path.join(root,"out/tcw004-validation/Release/framebridge-bridge-codec.exe"),[],0],
 ["native-missing-fixture",path.join(root,"out/tcw004-validation/Release/framebridge-bridge-codec.exe"),["packages/protocol/fixtures/does-not-exist"],1]
];
const entries=[];
for(const [name,executable,args,expected] of commands){
 const result=spawnSync(executable,args,{cwd:root,encoding:"utf8"});
 const output=(result.stdout??"")+(result.stderr??"")+(result.error?String(result.error):"");
 await writeFile(path.join(dir,name+".txt"),output);
 const entry={name,executable,args,working_directory:root,exit_code:result.status,expected_exit:expected,output:name+".txt"};entries.push(entry);
 console.log(JSON.stringify(entry));
 if(result.status!==expected){process.exitCode=1;break;}
}
const git=spawnSync("git",["rev-parse","HEAD"],{cwd:root,encoding:"utf8"});
await writeFile(path.join(dir,"result.json"),JSON.stringify({status:process.exitCode?"FAIL":"PASS",implementation_commit:git.stdout.trim(),clean_install:cleanInstall,node:process.version,commands:entries},null,2)+"\n");
console.log("VALIDATION_EVIDENCE "+dir);
