import { existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import path from "node:path";
const root=path.resolve(import.meta.dirname,"../.."), p="D:\\Dev\\Tools\\FrameBridge";
const checks=[
 ["node",process.execPath,["--version"]],["pnpm","C:\\ProgramData\\chocolatey\\bin\\pnpm.exe",["--version"]],
 ["python",path.join(p,"python-3.13.15","python.exe"),["--version"]],["go",path.join(p,"go1.27.0","go","bin","go.exe"),["version"]],
 ["cmake","C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",["--version"]],
 ["ninja","C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja\\ninja.exe",["--version"]],
 ["msvc","C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Tools\\MSVC\\14.44.35207\\bin\\Hostx64\\x64\\cl.exe",[]],
 ["chrome_for_testing",path.join(p,"chrome-for-testing","chrome-win64","chrome.exe"),[]]];
const results=checks.map(([name,exe,args])=>{if(!existsSync(exe))return{name,exe,status:"missing"};if(name==="msvc"||name==="chrome_for_testing")return{name,exe,status:"pass",output:"present"};try{const o=execFileSync(exe,args,{encoding:"utf8",stdio:["ignore","pipe","pipe"]});return{name,exe,status:"pass",output:o.trim().split(/\r?\n/)[0]};}catch(e){return{name,exe,status:"probe_failed",exit_code:e.status??null};}});
console.log(JSON.stringify({test_id:"TCW-BUILD-003",results},null,2));
if(results.some(x=>x.status!=="pass"))process.exitCode=1;else console.log("TCW-BUILD-003 PASS");
