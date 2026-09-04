import * as THREE from "three";
import {WebGPURenderer} from "three/webgpu";
import {MirrorClient,logicalFrameAt,canonicalState,decode,type ResizeState,MAX_DIMENSION,MessageType} from "@framebridge/protocol";
function element<T extends HTMLElement>(id:string):T {const value=document.getElementById(id);if(!value)throw new Error("missing "+id);return value as T;}
const canvas=element<HTMLCanvasElement>("cube"),nativeCanvas=element<HTMLCanvasElement>("native-return"),wrapper=element<HTMLDivElement>("cube-wrap"),button=element<HTMLButtonElement>("mirror"),textureButton=element<HTMLButtonElement>("texture-change"),tokenInput=element<HTMLInputElement>("token"),portInput=element<HTMLInputElement>("port"),cameraInput=element<HTMLInputElement>("camera");
const nativeContext=nativeCanvas.getContext("2d");
const put=(id:string,value:unknown)=>{element(id).textContent=String(value);};
let socket:WebSocket|undefined,mirror:MirrorClient|undefined;let cameraDistance=3;let received=0,displayed=0;let textureRevision=1n;const imageTimes:number[]=[];const sentAt=new Map<bigint,number>();let lastImageAt=0;let nativeMode:string|undefined,renderScale:number|undefined;
let updateBrowserTexture:(pixels:Uint8Array)=>void=()=>{};
function textureBytes(revision:bigint):Uint8Array { const p=new Uint8Array(256*256*4);for(let y=0;y<256;y++)for(let x=0;x<256;x++){const i=(y*256+x)*4;const checker=((x>>5)^(y>>5))&1;p[i]=checker?236:24;p[i+1]=checker?92:190;p[i+2]=checker?44:220;p[i+3]=255;if(Math.abs(x-y)<3||Math.abs(x-(255-y))<3){p[i]=255;p[i+1]=255;p[i+2]=255;}}const mark=Number(revision%200n)+28;for(let y=16;y<48;y++)for(let x=mark;x<mark+12;x++){const i=(y*256+x)*4;p[i]=20;p[i+1]=230;p[i+2]=180;}return p;}
let viewport:ResizeState={width:640,height:360,resizeGeneration:1n};
const start=performance.now();let frame=1n;let protocolError="";
// Explicit deterministic capture mode; normal browser-authoritative simulation is unchanged.
const parityValue=new URLSearchParams(location.search).get("parity");
const parityFrame=parityValue&&["60","120","180"].includes(parityValue)?BigInt(parityValue):undefined;
if(parityFrame)put("mode","MIRROR SPIKE — NOT THREE BACKEND | PARITY CAPTURE: frame "+parityFrame);
let renderer:WebGPURenderer|undefined,camera:THREE.PerspectiveCamera|undefined;
function current(){return {...canonicalState(frame,viewport),cameraZ:cameraDistance};}
function fail(message:string){protocolError=message;put("error",message);put("connection","protocol-error");socket?.close(1002,"protocol validation");}
function clearProtocolError(){protocolError="";put("error","");}
function viewportUI(){put("dimensions",viewport.width+" × "+viewport.height);put("resize",viewport.resizeGeneration);if(renderScale===undefined){put("native-input","unknown");put("native-output","unknown");put("native-scale","runtime mode unknown");return;}put("native-input",Math.max(1,Math.round(viewport.width*renderScale))+" × "+Math.max(1,Math.round(viewport.height*renderScale)));put("native-output",viewport.width+" × "+viewport.height);put("native-scale",(1/renderScale).toFixed(1)+"× input → output · "+nativeMode);}
function sendFrame(){if(socket?.readyState!==WebSocket.OPEN||!mirror?.authenticated)return;
 try{const seq=mirror.frame(current());if(seq!==undefined){sentAt.set(frame,performance.now());while(sentAt.size>128)sentAt.delete(sentAt.keys().next().value!);put("sent",frame);put("outstanding",mirror.outstanding);}}catch{fail("outbound protocol or backpressure failure");}}
function disconnect(){
 const ws=socket;try{mirror?.disconnect(ws?.readyState===WebSocket.OPEN);}catch{/* teardown must preserve the simulation */}
 ws?.close(1000,"developer disconnect");socket=undefined;mirror=undefined;
 put("connection","disconnected");put("authentication","not authenticated");put("outstanding",0);button.textContent="Connect mirror";
 nativeCanvas.style.display="none";canvas.style.visibility="visible";put("native-status","OFF — browser fallback");put("native-stale","fallback");
}
function connect(){
 clearProtocolError();
 const port=Number(portInput.value),token=tokenInput.value;
 if(!Number.isInteger(port)||port<1||port>65535||!/^[0-9a-f]{48}$/i.test(token)){fail("invalid developer connection settings");return;}
 const ws=new WebSocket("ws://127.0.0.1:"+port);socket=ws;ws.binaryType="arraybuffer";
 const client=new MirrorClient(bytes=>ws.send(bytes));mirror=client;
 put("connection","connecting");put("authentication","awaiting hello");button.textContent="Disconnect mirror";
 ws.onopen=()=>{if(socket!==ws)return;put("authentication","authenticating");ws.send(JSON.stringify({kind:"hello",version:0,token,origin:location.origin,three:{version:"0.185.0",commit:"2431a09"},buildId:"demo-web",requestedCapabilities:["explicit-mirror"],byteOrder:"little"}));};
 ws.onmessage=event=>{if(socket!==ws)return;
  try{
   if(typeof event.data==="string"){const caps=client.authenticate(event.data);client.uploadTexture(textureBytes(textureRevision),textureRevision);clearProtocolError();tokenInput.value="";nativeMode=caps.nativeMode;renderScale=caps.renderScale??undefined;viewportUI();put("mode-detail",nativeMode+(nativeMode==="reference-upscale"?" — NOT DLSS":""));put("generation",caps.sessionGeneration);put("backend",caps.backend);put("authentication","authenticated");put("connection","connected");put("texture-revision",textureRevision+" / awaiting");sendFrame();return;}
   const bytes=new Uint8Array(event.data as ArrayBuffer); const type=decode(bytes).type;
   if(type===MessageType.TextureAccepted){const a=client.acknowledgeTexture(bytes);put("texture-revision",a.revision+" / applied");}
   else if(type===MessageType.NativeImage){const image=client.image(bytes);if(!nativeContext)throw new Error("native canvas unavailable");const now=performance.now();nativeCanvas.width=image.width;nativeCanvas.height=image.height;nativeContext.putImageData(new ImageData(new Uint8ClampedArray(image.pixels),image.width,image.height),0,0);client.consume(image);nativeCanvas.style.display="block";canvas.style.visibility="hidden";received++;displayed++;lastImageAt=now;imageTimes.push(now);while(imageTimes[0]!<now-1000)imageTimes.shift();put("native-status","ON — browser displaying native pixels");put("native-received",received);put("native-displayed",displayed);put("native-browser-frame",image.browserFrame);put("native-frame",image.nativeFrame);put("native-age",sentAt.has(image.browserFrame)?Math.max(0,now-sentAt.get(image.browserFrame)!).toFixed(1)+" ms":"unknown");put("native-fps",imageTimes.length.toFixed(1));put("native-stale","fresh");}
   else {const {ack}=client.acknowledge(bytes);put("accepted",ack.frame);put("accepted-resize",ack.resizeGeneration);put("dropped",ack.droppedFrames);put("outstanding",client.outstanding);}
  }catch(error){fail(error instanceof Error ? error.message : "invalid capabilities or uncorrelated acknowledgement");}
 };
 ws.onerror=()=>{if(socket===ws)fail("loopback connection error");};
 ws.onclose=()=>{client.disconnect(false);if(socket!==ws)return;socket=undefined;mirror=undefined;nativeCanvas.style.display="none";canvas.style.visibility="visible";put("native-status","OFF — browser fallback");put("native-stale","fallback");put("authentication","not authenticated");put("connection",protocolError?"protocol-error":"disconnected");put("outstanding",0);button.textContent="Connect mirror";};
}
button.onclick=()=>socket?disconnect():connect();
textureButton.onclick=()=>{textureRevision++;const pixels=textureBytes(textureRevision);updateBrowserTexture(pixels);put("texture-revision",textureRevision+" / uploading");if(mirror?.authenticated)try{mirror.uploadTexture(pixels,textureRevision);}catch(e){fail(e instanceof Error?e.message:"texture upload failed");}};
cameraInput.oninput=()=>{cameraDistance=Number(cameraInput.value);put("camera-value",cameraDistance.toFixed(1));};
for(const [id,width,height] of [["size640",640,360],["size800",800,450]] as const)element(id).onclick=()=>{wrapper.style.width=width+"px";wrapper.style.height=height+"px";canvas.style.width=width+"px";canvas.style.height=height+"px";nativeCanvas.style.width=width+"px";nativeCanvas.style.height=height+"px";};
new ResizeObserver(entries=>{
 const rect=entries[0]?.contentRect;if(!rect)return;
 const width=Math.round(rect.width),height=Math.round(rect.height);
 if(width<1||height<1||width>MAX_DIMENSION||height>MAX_DIMENSION)return;
 if(width===viewport.width&&height===viewport.height)return;
 viewport={width,height,resizeGeneration:viewport.resizeGeneration+1n};
 renderer?.setSize(width,height,false);if(camera){camera.aspect=width/height;camera.updateProjectionMatrix();}viewportUI();
 // MirrorClient sends Resize before the next complete frame, including after reconnect.
}).observe(canvas);
viewportUI();
async function main(){
 if(!(navigator as Navigator&{gpu?:unknown}).gpu){put("renderer","WebGPU unavailable");return;}
 const scene=new THREE.Scene();scene.background=new THREE.Color(0x080b12);
 camera=new THREE.PerspectiveCamera(60,viewport.width/viewport.height,.1,100);camera.position.z=3;
 renderer=new WebGPURenderer({canvas,antialias:false});await renderer.init();renderer.setPixelRatio(1);renderer.setSize(viewport.width,viewport.height,false);
 const pixels=textureBytes(textureRevision);const dataTexture=new THREE.DataTexture(pixels,256,256,THREE.RGBAFormat);dataTexture.needsUpdate=true;dataTexture.magFilter=THREE.NearestFilter;dataTexture.minFilter=THREE.NearestFilter;updateBrowserTexture=(next)=>{dataTexture.image.data=next;dataTexture.needsUpdate=true;};const cube=new THREE.Mesh(new THREE.BoxGeometry(1,1,1),new THREE.MeshBasicMaterial({map:dataTexture}));scene.add(cube);
 put("renderer","browser WebGPU fallback");
 function animate(){frame=parityFrame??logicalFrameAt(performance.now()-start);const s=current();cube.rotation.set(s.rotationX,s.rotationY,0);camera!.position.z=s.cameraZ;
  put("browser-frame",frame);sendFrame();renderer!.render(scene,camera!);requestAnimationFrame(animate);}
 requestAnimationFrame(animate);
}
setInterval(()=>{const now=performance.now();while(imageTimes[0]!<now-1000)imageTimes.shift();if(received)put("native-fps",imageTimes.length.toFixed(1));if(lastImageAt)put("native-stale",now-lastImageAt>2500?"stale":Math.round(now-lastImageAt)+" ms since receive");if(lastImageAt&&now-lastImageAt>2500&&nativeCanvas.style.display!=="none"){nativeCanvas.style.display="none";canvas.style.visibility="visible";put("native-status","STALE — browser fallback");}},500);
void main().catch(()=>{put("renderer","WebGPU initialization failed");});
