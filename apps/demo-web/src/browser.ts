import * as THREE from "three";
import {WebGPURenderer} from "three/webgpu";
import {MirrorClient,logicalFrameAt,canonicalState,decode,type ResizeState,MAX_DIMENSION,MessageType} from "@framebridge/protocol";
function element<T extends HTMLElement>(id:string):T {const value=document.getElementById(id);if(!value)throw new Error("missing "+id);return value as T;}
const canvas=element<HTMLCanvasElement>("cube"),nativeCanvas=element<HTMLCanvasElement>("native-return"),button=element<HTMLButtonElement>("mirror"),tokenInput=element<HTMLInputElement>("token"),portInput=element<HTMLInputElement>("port");
const nativeContext=nativeCanvas.getContext("2d");
const put=(id:string,value:unknown)=>{element(id).textContent=String(value);};
let socket:WebSocket|undefined,mirror:MirrorClient|undefined;
let viewport:ResizeState={width:640,height:360,resizeGeneration:1n};
const start=performance.now();let frame=1n;let protocolError="";
// Explicit deterministic capture mode; normal browser-authoritative simulation is unchanged.
const parityValue=new URLSearchParams(location.search).get("parity");
const parityFrame=parityValue&&["60","120","180"].includes(parityValue)?BigInt(parityValue):undefined;
if(parityFrame)put("mode","MIRROR SPIKE — NOT THREE BACKEND | PARITY CAPTURE: frame "+parityFrame);
let renderer:WebGPURenderer|undefined,camera:THREE.PerspectiveCamera|undefined;
function current(){return canonicalState(frame,viewport);}
function fail(message:string){protocolError=message;put("error",message);put("connection","protocol-error");socket?.close(1002,"protocol validation");}
function clearProtocolError(){protocolError="";put("error","");}
function viewportUI(){put("dimensions",viewport.width+" × "+viewport.height);put("resize",viewport.resizeGeneration);}
function sendFrame(){if(socket?.readyState!==WebSocket.OPEN||!mirror?.authenticated)return;
 try{const seq=mirror.frame(current());if(seq!==undefined){put("sent",frame);put("outstanding",mirror.outstanding);}}catch{fail("outbound protocol or backpressure failure");}}
function disconnect(){
 const ws=socket;try{mirror?.disconnect(ws?.readyState===WebSocket.OPEN);}catch{/* teardown must preserve the simulation */}
 ws?.close(1000,"developer disconnect");socket=undefined;mirror=undefined;
 put("connection","disconnected");put("authentication","not authenticated");put("outstanding",0);button.textContent="Connect mirror";
 nativeCanvas.style.display="none";canvas.style.visibility="visible";put("native-status","OFF — browser fallback");
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
   if(typeof event.data==="string"){const caps=client.authenticate(event.data);clearProtocolError();tokenInput.value="";put("generation",caps.sessionGeneration);put("backend",caps.backend);put("authentication","authenticated");put("connection","connected");sendFrame();return;}
   const bytes=new Uint8Array(event.data as ArrayBuffer); const type=decode(bytes).type;
   if(type===MessageType.NativeImage){const image=client.image(bytes); if(!nativeContext)throw new Error("native canvas unavailable"); nativeCanvas.width=image.width;nativeCanvas.height=image.height;nativeContext.putImageData(new ImageData(new Uint8ClampedArray(image.pixels),image.width,image.height),0,0);nativeCanvas.style.display="block";canvas.style.visibility="hidden";put("native-status","ON — browser displaying native pixels");put("native-frame",image.nativeFrame);put("native-fps","live");}
   else {const {ack}=client.acknowledge(bytes);put("accepted",ack.frame);put("accepted-resize",ack.resizeGeneration);put("dropped",ack.droppedFrames);put("outstanding",client.outstanding);}
  }catch{fail("invalid capabilities or uncorrelated acknowledgement");}
 };
 ws.onerror=()=>{if(socket===ws)fail("loopback connection error");};
 ws.onclose=()=>{client.disconnect(false);if(socket!==ws)return;socket=undefined;mirror=undefined;nativeCanvas.style.display="none";canvas.style.visibility="visible";put("native-status","OFF — browser fallback");put("authentication","not authenticated");put("connection",protocolError?"protocol-error":"disconnected");put("outstanding",0);button.textContent="Connect mirror";};
}
button.onclick=()=>socket?disconnect():connect();
for(const [id,width,height] of [["size640",640,360],["size800",800,450]] as const)element(id).onclick=()=>{canvas.style.width=width+"px";canvas.style.height=height+"px";};
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
 const cube=new THREE.Mesh(new THREE.BoxGeometry(1,1,1),new THREE.MeshBasicMaterial({color:0x36d6ff}));scene.add(cube);
 put("renderer","browser WebGPU fallback");
 function animate(){frame=parityFrame??logicalFrameAt(performance.now()-start);const s=current();cube.rotation.set(s.rotationX,s.rotationY,0);camera!.position.z=s.cameraZ;
  put("browser-frame",frame);sendFrame();renderer!.render(scene,camera!);requestAnimationFrame(animate);}
 requestAnimationFrame(animate);
}
void main().catch(()=>{put("renderer","WebGPU initialization failed");});
