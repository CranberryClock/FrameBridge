import {chromium} from 'playwright-core';
import {spawn} from 'node:child_process';
import {mkdir} from 'node:fs/promises';
import {startNative,until,check,sleep,origin} from './tcw005r-client.mjs';
const exe=process.argv[2]??'build/tcw005r/framebridge-native-mirror.exe';
const chrome=process.env.FRAMEBRIDGE_CHROME;
check(chrome,'Set FRAMEBRIDGE_CHROME to the local Chrome executable');
const out=process.argv[3]??'build/tcw005r-browser';await mkdir(out,{recursive:true});
const vite=spawn(process.execPath,['apps/demo-web/node_modules/vite/bin/vite.js','apps/demo-web','--host','127.0.0.1','--port','5173','--strictPort'],{stdio:'ignore'});
let native,browser;
try {
  for(let i=0;i<100;i++){try{if((await fetch(origin)).ok)break;}catch{}await sleep(50);}
  native=await startNative(exe,['--trace','--capture-dir',out]);
  browser=await chromium.launch({executablePath:chrome,headless:false,args:['--no-first-run','--no-default-browser-check']});
  const page=await browser.newPage({viewport:{width:1120,height:1100},deviceScaleFactor:1});const errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  async function attach(){await page.locator('#port').fill(String(native.ready.port));await page.locator('#token').fill(native.ready.token);await page.locator('#mirror').click();await page.waitForFunction(()=>document.querySelector('#connection').textContent==='connected');check(await page.locator('#token').inputValue()==='','token not cleared');check(await page.locator('#backend').textContent()==='native-dawn','wrong browser backend');}
  const parity=[];
  for(const frame of [60,120,180]) {
    await page.goto(origin+'/?parity='+frame);await page.waitForFunction(()=>document.querySelector('#renderer').textContent==='browser WebGPU fallback');await attach();
    await page.waitForFunction(f=>document.querySelector('#accepted').textContent===String(f),frame);await sleep(150);
    await page.locator('#cube').screenshot({path:out+'/browser-'+frame+'.png'});
    await page.screenshot({path:out+'/connected-'+frame+'.png'});
    parity.push({frame,accepted:Number(await page.locator('#accepted').textContent()),token_cleared:true});
    await page.locator('#mirror').click();await sleep(30);
  }
  await page.goto(origin);await page.waitForFunction(()=>document.querySelector('#renderer').textContent==='browser WebGPU fallback');await attach();await sleep(250);
  const pre={frame:Number(await page.locator('#browser-frame').textContent()),generation:Number(await page.locator('#generation').textContent())};
  await page.locator('#mirror').click();await sleep(350);
  const disconnectedFrame=Number(await page.locator('#browser-frame').textContent());check(disconnectedFrame>pre.frame,'browser paused while disconnected');
  await attach();await sleep(250);
  const post={frame:Number(await page.locator('#browser-frame').textContent()),accepted:Number(await page.locator('#accepted').textContent()),generation:Number(await page.locator('#generation').textContent())};
  check(post.frame>disconnectedFrame&&post.accepted>pre.frame&&post.generation>pre.generation,'reconnect continuity');
  const resizes=[];
  for(const id of ['size800','size640','size800','size640']) {
    await page.locator('#'+id).click();await sleep(100);
    await page.waitForFunction(()=>document.querySelector('#resize').textContent===document.querySelector('#accepted-resize').textContent);
    resizes.push({dimensions:await page.locator('#dimensions').textContent(),generation:await page.locator('#accepted-resize').textContent()});
  }
  check(errors.length===0,'browser page errors');check((await page.locator('#error').textContent()).trim()==='','browser protocol error');
  const chromeVersion=browser.version();
  await page.screenshot({path:out+'/connected-live.png'});await page.locator('#mirror').click();await browser.close();browser=undefined;
  await native.stop();
  console.log(JSON.stringify({suite:'tcw006r-real-browser',status:'PASS',chrome_version:chromeVersion,parity,pre_disconnect:pre,disconnected_frame:disconnectedFrame,post_reconnect:post,resizes,errors,node_frame_proxy:false}));
}catch(e){console.error('FAIL browser: '+e.message);process.exitCode=1;}
finally{if(browser)await browser.close();if(native?.child.exitCode===null)native.child.kill();vite.kill();}
