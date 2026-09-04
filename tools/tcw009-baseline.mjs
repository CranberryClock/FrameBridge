import {spawn} from 'node:child_process';
import {chromium} from 'playwright-core';
import {startNative,sleep,check,origin} from './tcw005r-client.mjs';

const exe=process.argv[2]??'build/tcw008/framebridge-native-mirror.exe';
const chrome=process.env.FRAMEBRIDGE_CHROME;
check(chrome,'Set FRAMEBRIDGE_CHROME to the local Chrome executable');
const vite=spawn(process.execPath,['apps/demo-web/node_modules/vite/bin/vite.js','apps/demo-web','--host','127.0.0.1','--port','5173','--strictPort'],{stdio:'ignore'});
let native,browser;
const numberText=async(page,id)=>Number(await page.locator(id).textContent());
const summary=values=>{
  const sorted=[...values].sort((a,b)=>a-b);
  return {minimum:sorted[0],median:sorted[Math.floor(sorted.length/2)],maximum:sorted.at(-1)};
};
try {
  for(let i=0;i<100;i++){try{if((await fetch(origin)).ok)break;}catch{}await sleep(50);}
  native=await startNative(exe,['--return-mode','full']);
  browser=await chromium.launch({executablePath:chrome,headless:false,args:['--no-first-run','--no-default-browser-check']});
  const page=await browser.newPage({viewport:{width:1120,height:1100},deviceScaleFactor:1});
  const errors=[];page.on('pageerror',e=>errors.push(e.message));
  await page.goto(origin);await page.waitForFunction(()=>document.querySelector('#renderer')?.textContent==='browser WebGPU fallback');
  await page.locator('#port').fill(String(native.ready.port));await page.locator('#token').fill(native.ready.token);await page.locator('#mirror').click();
  await page.waitForFunction(()=>document.querySelector('#connection')?.textContent==='connected');
  await page.waitForFunction(()=>Number(document.querySelector('#native-displayed')?.textContent)>0,undefined,{timeout:15000});
  const samples=[];let priorImages=await numberText(page,'#native-received');let noImageIntervals=0;
  for(let i=0;i<20;i++){
    await sleep(500);
    const images=await numberText(page,'#native-received');if(images===priorImages)noImageIntervals++;priorImages=images;
    samples.push({fps:await numberText(page,'#native-fps'),age_ms:Number.parseFloat((await page.locator('#native-age').textContent())??''),images,status:await page.locator('#native-stale').textContent()});
  }
  const fps=samples.map(x=>x.fps).filter(Number.isFinite),ages=samples.map(x=>x.age_ms).filter(Number.isFinite);
  const result={suite:'tcw009-reference-baseline',status:'PASS',duration_seconds:10,chrome_version:browser.version(),backend:await page.locator('#backend').textContent(),native_mode:await page.locator('#mode-detail').textContent(),native_input:await page.locator('#native-input').textContent(),native_output:await page.locator('#native-output').textContent(),native_scale:await page.locator('#native-scale').textContent(),returned_fps:summary(fps),frame_age_ms:summary(ages),images_received:await numberText(page,'#native-received'),images_displayed:await numberText(page,'#native-displayed'),browser_frame:await numberText(page,'#browser-frame'),returned_browser_frame:await numberText(page,'#native-browser-frame'),dropped_complete_frames:await numberText(page,'#dropped'),outstanding_frames:await numberText(page,'#outstanding'),half_second_intervals_without_new_image:noImageIntervals,return_status:await page.locator('#native-stale').textContent(),protocol_error:(await page.locator('#error').textContent())?.trim()||null,page_errors:errors};
  check(result.backend==='native-dawn','wrong native backend');check(result.native_mode==='reference-upscale — NOT DLSS','wrong native mode');check(result.native_input==='320 × 180'&&result.native_output==='640 × 360','native-reported dimensions mismatch');check(result.images_received>0&&result.images_received===result.images_displayed,'native return did not display');check(!result.protocol_error&&errors.length===0,'browser error');
  console.log(JSON.stringify(result,null,2));
  await page.locator('#mirror').click();await browser.close();browser=undefined;await native.stop();native=undefined;
}catch(error){console.error('FAIL tcw009 baseline: '+error.message);process.exitCode=1;}
finally{if(browser)await browser.close();if(native?.child.exitCode===null)native.child.kill();vite.kill();}
