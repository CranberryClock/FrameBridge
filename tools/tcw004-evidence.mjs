import {readFile,readdir,mkdir,cp,writeFile} from "node:fs/promises";
import {createHash} from "node:crypto";
import {execFileSync} from "node:child_process";
import path from "node:path";
const root=process.cwd(),base=path.join(root,"artifacts/tcw-004");
const read=async p=>JSON.parse(await readFile(p,"utf8"));
const runs=[];
for(const name of await readdir(path.join(base,"runs"))) {
 const file=path.join(base,"runs",name,"result.json");
 try{runs.push({dir:name,data:await read(file)});}catch{/* incomplete run remains excluded */}
}
const acceptance=runs.filter(r=>r.data.mode==="acceptance").sort((a,b)=>a.data.end_time.localeCompare(b.data.end_time)).at(-1);
if(!acceptance)throw new Error("no acceptance record");
const sha=acceptance.data.implementation_commit;
const diagnostics=runs.filter(r=>r.data.mode==="diagnostic"&&r.data.implementation_commit===sha&&!r.data.implementation_dirty).sort((a,b)=>a.data.end_time.localeCompare(b.data.end_time)).slice(-2);
if(diagnostics.length!==2)throw new Error("two exact-commit diagnostics required");
const clean=path.join(root,"out/tcw004-clean-final/artifacts/tcw-004/validation");
const dirs=await readdir(clean);const validationDir=dirs.at(-1);if(!validationDir)throw new Error("clean validation missing");
const validation=await read(path.join(clean,validationDir,"result.json"));
if(validation.implementation_commit!==sha||!validation.clean_install)throw new Error("not a clean validation of implementation commit");
const target=path.join(base,"validation","clean-"+sha.slice(0,12));await mkdir(target,{recursive:true});await cp(path.join(clean,validationDir),target,{recursive:true});
const log=await readFile(path.join(target,"typescript-tests.txt"),"utf8");
const testResults=log.split(/\r?\n/).map(line=>line.slice(line.indexOf("{"))).filter(line=>line.startsWith("{")).flatMap(line=>{try{return [JSON.parse(line)];}catch{return [];}});
const ci=await read(path.join(base,"ci.json")).catch(()=>({status:"UNVERIFIED"}));
const changed=execFileSync("git",["diff","--name-only","97a5367155d5f580e863561322eab712bd2ddb32",sha],{encoding:"utf8"}).trim().split(/\r?\n/);
const gates={"TCW-BUILD-001":validation.status,"TCW-PROTO-001":validation.status==="PASS"?"PASS":"FAIL","TCW-PROTO-002":validation.status==="PASS"?"PASS":"FAIL","TCW-PROTO-003":"HUMAN_REQUIRED","TCW-PROTO-004":acceptance.data.status==="PASS"&&acceptance.data.elapsed_seconds>=1800&&diagnostics.every(r=>r.data.status==="PASS")?"PASS":"FAIL","TCW-CONT-001":"HUMAN_REQUIRED"};
const manifest={task:"TCW-004",status:"SUPERVISOR_REVIEW_HUMAN_REQUIRED",implementation_commit:sha,gates,scope_boundary:"Node loopback test/developer infrastructure; browser WebGPU renderer; no native socket or renderer claim",stale_evidence:"Prior real-soak.json invalidated; retained in Git history only",fixture_counts:{valid_binary:12,malformed_binary:11,json:2},test_results:testResults,cpp_output:await readFile(path.join(target,"native-execute.txt"),"utf8"),validation:{path:path.relative(base,target),...validation},diagnostics:diagnostics.map(r=>({path:"runs/"+r.dir+"/result.json",status:r.data.status,process_exit_code:r.data.process_exit_code,elapsed_seconds:r.data.elapsed_seconds,average_attempted_hz:r.data.average_attempted_hz})),acceptance:{path:"runs/"+acceptance.dir+"/result.json",...acceptance.data},ci,changed_files:changed,human_driver_checklist:"human-browser-steps.md",limitations:["Real Chrome interaction and visual acceptance remain HUMAN_REQUIRED.","Memory sampling uses explicit GC; both pre-GC and retained heap are recorded.","No native networking or rendering is implemented in TCW-004."]};
await writeFile(path.join(base,"manifest.json"),JSON.stringify(manifest,null,2)+"\n");
await writeFile(path.join(base,"commands.txt"),validation.commands.map(c=>JSON.stringify(c)).join("\n")+"\n"+diagnostics.concat(acceptance).map(r=>JSON.stringify({command:r.data.command,cwd:r.data.working_directory,environment:{TCW004_DURATION_SECONDS:r.data.duration_environment},exit_code:r.data.process_exit_code,evidence:"runs/"+r.dir+"/result.json"})).join("\n")+"\n");
await writeFile(path.join(base,"real-soak.json"),JSON.stringify({status:acceptance.data.status,implementation_commit:sha,evidence:"runs/"+acceptance.dir+"/result.json",note:"Pointer only; raw measurements reside in the immutable generated run record."},null,2)+"\n");
const hashes={};
async function walk(dir){for(const ent of await readdir(dir,{withFileTypes:true})){const p=path.join(dir,ent.name);if(ent.isDirectory())await walk(p);else if(ent.name!=="artifact-hashes.json")hashes[path.relative(base,p).replaceAll("\\","/")]=createHash("sha256").update(await readFile(p)).digest("hex");}}
await walk(base);await writeFile(path.join(base,"artifact-hashes.json"),JSON.stringify(hashes,null,2)+"\n");
console.log(JSON.stringify({implementation_commit:sha,gates,diagnostics:manifest.diagnostics,acceptance:manifest.acceptance.path,ci:manifest.ci}));
